/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS cliprdr <-> Pasteboard bridge
 */

#include "ohos_clipboard_internal.h"

FREERDP_LOCAL BYTE* ohos_clipboard_utf8_to_utf16le(const char* text, UINT32* outSize)
{
	BYTE* output = NULL;
	size_t capacity = 0;
	size_t count = 0;
	size_t offset = 0;
	const size_t length = text ? strlen(text) : 0;

	if (!outSize)
		return NULL;
	*outSize = 0;

	capacity = (length + 1U) * 4U;
	output = (BYTE*)calloc(capacity, sizeof(BYTE));
	if (!output)
		return NULL;

	while (offset < length)
	{
		uint32_t cp = 0;
		const uint8_t first = (uint8_t)text[offset++];
		if (first < 0x80U)
		{
			cp = first;
		}
		else
		{
			size_t trailing = 0;
			if ((first & 0xE0U) == 0xC0U)
			{
				cp = first & 0x1FU;
				trailing = 1;
			}
			else if ((first & 0xF0U) == 0xE0U)
			{
				cp = first & 0x0FU;
				trailing = 2;
			}
			else if ((first & 0xF8U) == 0xF0U)
			{
				cp = first & 0x07U;
				trailing = 3;
			}
			else
			{
				cp = 0xFFFDU;
				trailing = 0;
			}

			if (offset + trailing > length)
			{
				cp = 0xFFFDU;
				offset = length;
			}
			else
			{
				for (size_t x = 0; x < trailing; x++)
				{
					const uint8_t next = (uint8_t)text[offset++];
					if ((next & 0xC0U) != 0x80U)
					{
						cp = 0xFFFDU;
						break;
					}
					cp = (cp << 6U) | (next & 0x3FU);
				}
			}
		}

		if (cp > 0x10FFFFU)
			cp = 0xFFFDU;

		if (cp <= 0xFFFFU)
		{
			const uint16_t unit = (uint16_t)cp;
			output[count++] = (BYTE)(unit & 0xFFU);
			output[count++] = (BYTE)((unit >> 8U) & 0xFFU);
		}
		else
		{
			cp -= 0x10000U;
			const uint16_t high = (uint16_t)(0xD800U | (cp >> 10U));
			const uint16_t low = (uint16_t)(0xDC00U | (cp & 0x3FFU));
			output[count++] = (BYTE)(high & 0xFFU);
			output[count++] = (BYTE)((high >> 8U) & 0xFFU);
			output[count++] = (BYTE)(low & 0xFFU);
			output[count++] = (BYTE)((low >> 8U) & 0xFFU);
		}
	}

	output[count++] = 0;
	output[count++] = 0;
	if (count > UINT32_MAX)
	{
		free(output);
		return NULL;
	}
	*outSize = (UINT32)count;
	return output;
}

FREERDP_LOCAL void ohos_clipboard_append_utf8(char** text, size_t* count, size_t* capacity, uint32_t cp)
{
	BYTE encoded[4];
	size_t encodedCount = 0;

	if (cp <= 0x7FU)
	{
		encoded[encodedCount++] = (BYTE)cp;
	}
	else if (cp <= 0x7FFU)
	{
		encoded[encodedCount++] = (BYTE)(0xC0U | (cp >> 6U));
		encoded[encodedCount++] = (BYTE)(0x80U | (cp & 0x3FU));
	}
	else if (cp <= 0xFFFFU)
	{
		encoded[encodedCount++] = (BYTE)(0xE0U | (cp >> 12U));
		encoded[encodedCount++] = (BYTE)(0x80U | ((cp >> 6U) & 0x3FU));
		encoded[encodedCount++] = (BYTE)(0x80U | (cp & 0x3FU));
	}
	else
	{
		encoded[encodedCount++] = (BYTE)(0xF0U | (cp >> 18U));
		encoded[encodedCount++] = (BYTE)(0x80U | ((cp >> 12U) & 0x3FU));
		encoded[encodedCount++] = (BYTE)(0x80U | ((cp >> 6U) & 0x3FU));
		encoded[encodedCount++] = (BYTE)(0x80U | (cp & 0x3FU));
	}

	if (*count + encodedCount + 1U > *capacity)
	{
		const size_t nextCapacity = (*capacity + encodedCount + 64U) * 2U;
		char* grown = (char*)realloc(*text, nextCapacity);
		if (!grown)
			return;
		*text = grown;
		*capacity = nextCapacity;
	}
	memcpy(*text + *count, encoded, encodedCount);
	*count += encodedCount;
	(*text)[*count] = '\0';
}

FREERDP_LOCAL char* ohos_clipboard_utf16le_to_utf8(const BYTE* data, UINT32 size)
{
	size_t count = 0;
	size_t capacity = size + 4U;
	char* text = NULL;
	const UINT32 units = size / 2U;

	if (!data || size < 2)
		return ohos_clipboard_strdup("");

	text = (char*)calloc(capacity, sizeof(char));
	if (!text)
		return NULL;

	for (UINT32 index = 0; index < units; index++)
	{
		uint32_t cp = (uint16_t)data[index * 2U] |
		              ((uint16_t)data[(index * 2U) + 1U] << 8U);
		if (cp == 0)
			break;

		if (cp >= 0xD800U && cp <= 0xDBFFU && index + 1U < units)
		{
			const uint16_t next = (uint16_t)data[(index + 1U) * 2U] |
			                      ((uint16_t)data[((index + 1U) * 2U) + 1U] << 8U);
			if (next >= 0xDC00U && next <= 0xDFFFU)
			{
				cp = 0x10000U + (((cp - 0xD800U) << 10U) | (next - 0xDC00U));
				index++;
			}
		}

		ohos_clipboard_append_utf8(&text, &count, &capacity, cp);
	}
	return text;
}

FREERDP_LOCAL char* ohos_clipboard_bytes_to_string(const BYTE* data, UINT32 size)
{
	if (!data)
		return NULL;

	size_t length = size;
	while (length > 0 && data[length - 1U] == '\0')
		length--;

	char* text = (char*)calloc(length + 1U, sizeof(char));
	if (!text)
		return NULL;
	if (length > 0)
		memcpy(text, data, length);
	return text;
}

FREERDP_LOCAL const char* ohos_clipboard_find_token(const char* text, const char* token)
{
	if (!text || !token || token[0] == '\0')
		return NULL;

	const size_t tokenLen = strlen(token);
	for (const char* cursor = text; *cursor; cursor++)
	{
		if (strncmp(cursor, token, tokenLen) == 0)
			return cursor;
	}
	return NULL;
}

FREERDP_LOCAL UINT32 ohos_clipboard_parse_html_offset(const char* text, const char* key)
{
	const char* cursor = ohos_clipboard_find_token(text, key);
	if (!cursor)
		return UINT32_MAX;

	cursor += strlen(key);
	while (*cursor == ' ' || *cursor == '\t')
		cursor++;

	UINT32 value = 0;
	BOOL found = FALSE;
	while (*cursor >= '0' && *cursor <= '9')
	{
		found = TRUE;
		value = (value * 10U) + (UINT32)(*cursor - '0');
		cursor++;
	}
	return found ? value : UINT32_MAX;
}

FREERDP_LOCAL char* ohos_clipboard_extract_ms_html(const BYTE* data, UINT32 size)
{
	char* source = ohos_clipboard_bytes_to_string(data, size);
	if (!source)
		return NULL;

	UINT32 start = ohos_clipboard_parse_html_offset(source, "StartFragment:");
	UINT32 end = ohos_clipboard_parse_html_offset(source, "EndFragment:");
	if (start == UINT32_MAX || end == UINT32_MAX || start >= end || end > size)
	{
		start = ohos_clipboard_parse_html_offset(source, "StartHTML:");
		end = ohos_clipboard_parse_html_offset(source, "EndHTML:");
	}

	if (start != UINT32_MAX && end != UINT32_MAX && start < end && end <= size)
	{
		const UINT32 length = end - start;
		char* html = (char*)calloc((size_t)length + 1U, sizeof(char));
		if (html)
			memcpy(html, source + start, length);
		free(source);
		return html;
	}

	return source;
}

FREERDP_LOCAL BYTE* ohos_clipboard_html_to_ms_html(const char* html, UINT32* outSize)
{
	static const char prefix[] =
	    "Version:0.9\r\n"
	    "StartHTML:%010u\r\n"
	    "EndHTML:%010u\r\n"
	    "StartFragment:%010u\r\n"
	    "EndFragment:%010u\r\n";
	static const char startFragment[] = "<html><body>\r\n<!--StartFragment-->";
	static const char endFragment[] = "<!--EndFragment-->\r\n</body></html>";

	if (!outSize)
		return NULL;
	*outSize = 0;

	const char* body = html ? html : "";
	const int headerLen = snprintf(NULL, 0, prefix, 0U, 0U, 0U, 0U);
	if (headerLen <= 0)
		return NULL;

	const UINT32 startHtml = (UINT32)headerLen;
	const UINT32 startFragmentOffset = startHtml + (UINT32)strlen(startFragment);
	const UINT32 endFragmentOffset = startFragmentOffset + (UINT32)strlen(body);
	const UINT32 endHtml = endFragmentOffset + (UINT32)strlen(endFragment);
	const size_t total = (size_t)endHtml + 1U;

	BYTE* output = (BYTE*)calloc(total, sizeof(BYTE));
	if (!output)
		return NULL;

	const int written = snprintf((char*)output, total, prefix, startHtml, endHtml,
	                             startFragmentOffset, endFragmentOffset);
	if (written != headerLen)
	{
		free(output);
		return NULL;
	}
	memcpy(output + startHtml, startFragment, strlen(startFragment));
	memcpy(output + startFragmentOffset, body, strlen(body));
	memcpy(output + endFragmentOffset, endFragment, strlen(endFragment));
	*outSize = (UINT32)total;
	return output;
}

FREERDP_LOCAL BOOL ohos_clipboard_read_plain_text(freerdpOhosClipboard* clipboard, char** outText,
                                           char* errorBuffer, size_t errorBufferSize)
{
	int status = ERR_OK;
	OH_UdmfData* data = NULL;
	char* text = NULL;

	if (!outText)
		return FALSE;
	*outText = NULL;

	if (!clipboard || !clipboard->pasteboard)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard unavailable");
		return FALSE;
	}

	data = ohos_clipboard_get_pasteboard_data(clipboard, "read plain text", &status);
	if (status != ERR_OK || !data)
	{
		clipboard->lastError = (UINT32)status;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_GetData status=%d(%s)", status,
		                         ohos_clipboard_pasteboard_status_name(status));
		return FALSE;
	}

	OH_UdsPlainText* primaryPlainText = OH_UdsPlainText_Create();
	if (primaryPlainText)
	{
		const int rc = OH_UdmfData_GetPrimaryPlainText(data, primaryPlainText);
		if (rc == UDMF_E_OK)
		{
			const char* content = OH_UdsPlainText_GetContent(primaryPlainText);
			text = ohos_clipboard_strdup(content);
		}
		OH_UdsPlainText_Destroy(primaryPlainText);
		if (text && text[0] != '\0')
			goto success;
		free(text);
		text = NULL;
	}

	const int recordCount = OH_UdmfData_GetRecordCount(data);
	for (int index = 0; index < recordCount; index++)
	{
		OH_UdmfRecord* record = OH_UdmfData_GetRecord(data, (unsigned int)index);
		if (!record)
			continue;
		OH_UdsPlainText* plainText = OH_UdsPlainText_Create();
		if (!plainText)
			continue;
		const int rc = OH_UdmfRecord_GetPlainText(record, plainText);
		if (rc == UDMF_E_OK)
		{
			const char* content = OH_UdsPlainText_GetContent(plainText);
			text = ohos_clipboard_strdup(content);
		}
		OH_UdsPlainText_Destroy(plainText);
		if (text && text[0] != '\0')
			goto success;
		free(text);
		text = NULL;
	}

	OH_UdmfData_Destroy(data);
	ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
	                         "pasteboard has no plain text record");
	return FALSE;

success:
	OH_UdmfData_Destroy(data);
	*outText = text;
	++clipboard->pasteboardReadCount;
	clipboard->lastTextBytes = (UINT32)strlen(text);
	return TRUE;
}

FREERDP_LOCAL BOOL ohos_clipboard_read_html(freerdpOhosClipboard* clipboard, char** outHtml,
                                     char** outPlain, char* errorBuffer,
                                     size_t errorBufferSize)
{
	int status = ERR_OK;
	OH_UdmfData* data = NULL;
	char* htmlText = NULL;
	char* plainText = NULL;

	if (!outHtml)
		return FALSE;
	*outHtml = NULL;
	if (outPlain)
		*outPlain = NULL;

	if (!clipboard || !clipboard->pasteboard)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard unavailable");
		return FALSE;
	}

	data = ohos_clipboard_get_pasteboard_data(clipboard, "read html", &status);
	if (status != ERR_OK || !data)
	{
		clipboard->lastError = (UINT32)status;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_GetData status=%d(%s)", status,
		                         ohos_clipboard_pasteboard_status_name(status));
		return FALSE;
	}

	OH_UdsHtml* primaryHtml = OH_UdsHtml_Create();
	if (primaryHtml)
	{
		const int rc = OH_UdmfData_GetPrimaryHtml(data, primaryHtml);
		if (rc == UDMF_E_OK)
		{
			htmlText = ohos_clipboard_strdup(OH_UdsHtml_GetContent(primaryHtml));
			plainText = ohos_clipboard_strdup(OH_UdsHtml_GetPlainContent(primaryHtml));
		}
		OH_UdsHtml_Destroy(primaryHtml);
		if (htmlText && htmlText[0] != '\0')
			goto success;
		free(htmlText);
		free(plainText);
		htmlText = NULL;
		plainText = NULL;
	}

	const int recordCount = OH_UdmfData_GetRecordCount(data);
	for (int index = 0; index < recordCount; index++)
	{
		OH_UdmfRecord* record = OH_UdmfData_GetRecord(data, (unsigned int)index);
		if (!record)
			continue;
		OH_UdsHtml* html = OH_UdsHtml_Create();
		if (!html)
			continue;
		const int rc = OH_UdmfRecord_GetHtml(record, html);
		if (rc == UDMF_E_OK)
		{
			htmlText = ohos_clipboard_strdup(OH_UdsHtml_GetContent(html));
			plainText = ohos_clipboard_strdup(OH_UdsHtml_GetPlainContent(html));
		}
		OH_UdsHtml_Destroy(html);
		if (htmlText && htmlText[0] != '\0')
			goto success;
		free(htmlText);
		free(plainText);
		htmlText = NULL;
		plainText = NULL;
	}

	OH_UdmfData_Destroy(data);
	ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard has no html record");
	return FALSE;

success:
	OH_UdmfData_Destroy(data);
	*outHtml = htmlText;
	if (outPlain)
		*outPlain = plainText;
	else
		free(plainText);
	++clipboard->pasteboardReadCount;
	clipboard->lastHtmlBytes = (UINT32)strlen(htmlText);
	return TRUE;
}

FREERDP_LOCAL BOOL ohos_clipboard_read_uri_from_data(OH_UdmfData* data, char** outUri)
{
	char* uri = NULL;

	if (!outUri)
		return FALSE;
	*outUri = NULL;
	if (!data)
		return FALSE;

	const int recordCount = OH_UdmfData_GetRecordCount(data);
	for (int index = 0; index < recordCount; index++)
	{
		OH_UdmfRecord* record = OH_UdmfData_GetRecord(data, (unsigned int)index);
		if (!record)
			continue;

		OH_UdsHyperlink* hyperlink = OH_UdsHyperlink_Create();
		if (hyperlink)
		{
			const int rc = OH_UdmfRecord_GetHyperlink(record, hyperlink);
			if (rc == UDMF_E_OK)
				uri = ohos_clipboard_strdup(OH_UdsHyperlink_GetUrl(hyperlink));
			OH_UdsHyperlink_Destroy(hyperlink);
			if (uri && uri[0] != '\0')
				goto success;
			free(uri);
			uri = NULL;
		}

		OH_UdsFileUri* fileUri = OH_UdsFileUri_Create();
		if (fileUri)
		{
			const int rc = OH_UdmfRecord_GetFileUri(record, fileUri);
			if (rc == UDMF_E_OK)
				uri = ohos_clipboard_strdup(OH_UdsFileUri_GetFileUri(fileUri));
			OH_UdsFileUri_Destroy(fileUri);
			if (uri && uri[0] != '\0')
				goto success;
			free(uri);
			uri = NULL;
		}
	}

	return FALSE;

success:
	*outUri = uri;
	return TRUE;
}

FREERDP_LOCAL BOOL ohos_clipboard_read_uri(freerdpOhosClipboard* clipboard, char** outUri,
                                    char* errorBuffer, size_t errorBufferSize)
{
	int status = ERR_OK;
	OH_UdmfData* data = NULL;
	char* uri = NULL;

	if (!outUri)
		return FALSE;
	*outUri = NULL;

	if (!clipboard || !clipboard->pasteboard)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard unavailable");
		return FALSE;
	}

	data = ohos_clipboard_get_pasteboard_data(clipboard, "read uri", &status);
	if (status != ERR_OK || !data)
	{
		clipboard->lastError = (UINT32)status;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_GetData status=%d(%s)", status,
		                         ohos_clipboard_pasteboard_status_name(status));
		return FALSE;
	}

	if (ohos_clipboard_read_uri_from_data(data, &uri))
		goto success;

	char textError[128] = { 0 };
	char* text = NULL;
	if (ohos_clipboard_read_plain_text(clipboard, &text, textError, sizeof(textError)) &&
	    ohos_clipboard_is_uri_text(text))
	{
		uri = text;
		goto success;
	}
	free(text);

	OH_UdmfData_Destroy(data);
	ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard has no uri record");
	return FALSE;

success:
	OH_UdmfData_Destroy(data);
	*outUri = uri;
	++clipboard->pasteboardReadCount;
	clipboard->lastUriBytes = (UINT32)strlen(uri);
	return TRUE;
}
