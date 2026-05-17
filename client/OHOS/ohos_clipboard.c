/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS cliprdr <-> Pasteboard bridge
 */

#include <freerdp/config.h>

#include "ohos_clipboard.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <database/pasteboard/oh_pasteboard.h>
#include <database/pasteboard/oh_pasteboard_err_code.h>
#include <database/udmf/udmf.h>
#include <database/udmf/udmf_err_code.h>
#include <database/udmf/uds.h>

#include <freerdp/channels/cliprdr.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/event.h>
#include <winpr/clipboard.h>

#define OHOS_CLIPBOARD_ECHO_SUPPRESS_MS 1500ULL

struct freerdp_ohos_clipboard
{
	rdpContext* context;
	FREERDP_OHOS_CLIPBOARD_CONFIG config;
	CliprdrClientContext* cliprdr;
	OH_Pasteboard* pasteboard;
	OH_PasteboardObserver* observer;
	BOOL pasteboardSubscribed;
	BOOL channelConnectedSubscribed;
	BOOL channelDisconnectedSubscribed;
	BOOL lockInitialized;
	pthread_mutex_t lock;
	UINT32 requestedFormatId;
	UINT32 ignoreLocalChanges;
	UINT64 ignoreLocalChangesUntil;

	UINT64 registerCount;
	UINT64 unregisterCount;
	UINT64 channelConnectCount;
	UINT64 channelDisconnectCount;
	UINT64 monitorReadyCount;
	UINT64 localFormatListCount;
	UINT64 remoteFormatListCount;
	UINT64 localRequestCount;
	UINT64 remoteResponseCount;
	UINT64 pasteboardReadCount;
	UINT64 pasteboardWriteCount;
	UINT64 pasteboardChangeCount;
	UINT64 suppressedChangeCount;
	UINT64 errorCount;
	UINT32 lastError;
	UINT32 lastRequestedFormat;
	UINT32 lastRemoteFormatCount;
	UINT32 lastLocalFormatCount;
	UINT32 lastTextBytes;
	char diagnostics[1024];

	struct freerdp_ohos_clipboard* registryNext;
};

static pthread_mutex_t g_registryMutex = PTHREAD_MUTEX_INITIALIZER;
static freerdpOhosClipboard* g_registryHead = NULL;

static void ohos_clipboard_log(freerdpOhosClipboard* clipboard, const char* format, ...)
{
	if (!clipboard || !clipboard->config.Log)
		return;

	char message[512];
	va_list ap;
	va_start(ap, format);
	(void)vsnprintf(message, sizeof(message), format, ap);
	va_end(ap);
	clipboard->config.Log(clipboard->config.logUserData, message);
}

static void ohos_clipboard_set_error(freerdpOhosClipboard* clipboard, char* errorBuffer,
                                     size_t errorBufferSize, const char* format, ...)
{
	if (clipboard)
		++clipboard->errorCount;

	if (!errorBuffer || errorBufferSize == 0)
		return;

	va_list ap;
	va_start(ap, format);
	(void)vsnprintf(errorBuffer, errorBufferSize, format, ap);
	va_end(ap);
}

static void ohos_clipboard_registry_add(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	pthread_mutex_lock(&g_registryMutex);
	clipboard->registryNext = g_registryHead;
	g_registryHead = clipboard;
	pthread_mutex_unlock(&g_registryMutex);
}

static void ohos_clipboard_registry_remove(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	pthread_mutex_lock(&g_registryMutex);
	freerdpOhosClipboard** current = &g_registryHead;
	while (*current)
	{
		if (*current == clipboard)
		{
			*current = clipboard->registryNext;
			clipboard->registryNext = NULL;
			break;
		}
		current = &(*current)->registryNext;
	}
	pthread_mutex_unlock(&g_registryMutex);
}

static freerdpOhosClipboard* ohos_clipboard_from_context(void* context)
{
	rdpContext* rdp_context = (rdpContext*)context;
	freerdpOhosClipboard* found = NULL;

	pthread_mutex_lock(&g_registryMutex);
	for (freerdpOhosClipboard* current = g_registryHead; current; current = current->registryNext)
	{
		if (current->context == rdp_context)
		{
			found = current;
			break;
		}
	}
	pthread_mutex_unlock(&g_registryMutex);
	return found;
}

static CliprdrClientContext* ohos_clipboard_snapshot_cliprdr(freerdpOhosClipboard* clipboard)
{
	CliprdrClientContext* cliprdr = NULL;

	if (!clipboard || !clipboard->lockInitialized)
		return NULL;

	pthread_mutex_lock(&clipboard->lock);
	cliprdr = clipboard->cliprdr;
	pthread_mutex_unlock(&clipboard->lock);
	return cliprdr;
}

static char* ohos_clipboard_strdup(const char* value)
{
	if (!value)
		return NULL;

	const size_t length = strlen(value);
	char* copy = (char*)calloc(length + 1U, sizeof(char));
	if (!copy)
		return NULL;
	memcpy(copy, value, length);
	return copy;
}

static UINT64 ohos_clipboard_now_ms(void)
{
	struct timespec ts = { 0 };
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return ((UINT64)ts.tv_sec * 1000ULL) + ((UINT64)ts.tv_nsec / 1000000ULL);
}

static BYTE* ohos_clipboard_utf8_to_utf16le(const char* text, UINT32* outSize)
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

static void ohos_clipboard_append_utf8(char** text, size_t* count, size_t* capacity, uint32_t cp)
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

static char* ohos_clipboard_utf16le_to_utf8(const BYTE* data, UINT32 size)
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

static BOOL ohos_clipboard_read_plain_text(freerdpOhosClipboard* clipboard, char** outText,
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

	data = OH_Pasteboard_GetData(clipboard->pasteboard, &status);
	if (status != ERR_OK || !data)
	{
		clipboard->lastError = (UINT32)status;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_GetData status=%d", status);
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

static BOOL ohos_clipboard_write_plain_text(freerdpOhosClipboard* clipboard, const char* text,
                                            char* errorBuffer, size_t errorBufferSize)
{
	int rc = UDMF_E_OK;
	OH_UdsPlainText* plainText = NULL;
	OH_UdmfRecord* record = NULL;
	OH_UdmfData* data = NULL;

	if (!clipboard || !clipboard->pasteboard)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard unavailable");
		return FALSE;
	}

	plainText = OH_UdsPlainText_Create();
	record = OH_UdmfRecord_Create();
	data = OH_UdmfData_Create();
	if (!plainText || !record || !data)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "UDMF allocation failed");
		goto fail;
	}

	rc = OH_UdsPlainText_SetContent(plainText, text ? text : "");
	if (rc == UDMF_E_OK)
		rc = OH_UdmfRecord_AddPlainText(record, plainText);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfData_AddRecord(data, record);
	if (rc != UDMF_E_OK)
	{
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "UDMF plain text setup failed: %d", rc);
		goto fail;
	}

	pthread_mutex_lock(&clipboard->lock);
	clipboard->ignoreLocalChanges++;
	clipboard->ignoreLocalChangesUntil = ohos_clipboard_now_ms() + OHOS_CLIPBOARD_ECHO_SUPPRESS_MS;
	pthread_mutex_unlock(&clipboard->lock);

	rc = OH_Pasteboard_SetData(clipboard->pasteboard, data);
	if (rc != ERR_OK)
	{
		pthread_mutex_lock(&clipboard->lock);
		if (clipboard->ignoreLocalChanges > 0)
			clipboard->ignoreLocalChanges--;
		if (clipboard->ignoreLocalChanges == 0)
			clipboard->ignoreLocalChangesUntil = 0;
		pthread_mutex_unlock(&clipboard->lock);
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_SetData status=%d", rc);
		goto fail;
	}

	++clipboard->pasteboardWriteCount;
	clipboard->lastTextBytes = text ? (UINT32)strlen(text) : 0;
	if (plainText)
		OH_UdsPlainText_Destroy(plainText);
	if (record)
		OH_UdmfRecord_Destroy(record);
	if (data)
		OH_UdmfData_Destroy(data);
	return TRUE;

fail:
	if (plainText)
		OH_UdsPlainText_Destroy(plainText);
	if (record)
		OH_UdmfRecord_Destroy(record);
	if (data)
		OH_UdmfData_Destroy(data);
	return FALSE;
}

static UINT ohos_clipboard_send_client_capabilities(CliprdrClientContext* cliprdr)
{
	CLIPRDR_CAPABILITIES capabilities = { 0 };
	CLIPRDR_GENERAL_CAPABILITY_SET generalCapabilitySet = { 0 };

	if (!cliprdr || !cliprdr->ClientCapabilities)
		return ERROR_INVALID_PARAMETER;

	capabilities.cCapabilitiesSets = 1;
	capabilities.capabilitySets = (CLIPRDR_CAPABILITY_SET*)&generalCapabilitySet;
	generalCapabilitySet.capabilitySetType = CB_CAPSTYPE_GENERAL;
	generalCapabilitySet.capabilitySetLength = 12;
	generalCapabilitySet.version = CB_CAPS_VERSION_2;
	generalCapabilitySet.generalFlags = CB_USE_LONG_FORMAT_NAMES;
	return cliprdr->ClientCapabilities(cliprdr, &capabilities);
}

static UINT ohos_clipboard_send_format_list_response(CliprdrClientContext* cliprdr, BOOL accepted)
{
	CLIPRDR_FORMAT_LIST_RESPONSE response = { 0 };

	if (!cliprdr || !cliprdr->ClientFormatListResponse)
		return ERROR_INVALID_PARAMETER;

	response.common.msgType = CB_FORMAT_LIST_RESPONSE;
	response.common.msgFlags = accepted ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
	return cliprdr->ClientFormatListResponse(cliprdr, &response);
}

static UINT ohos_clipboard_send_local_format_list(freerdpOhosClipboard* clipboard,
                                                  const char* reason)
{
	char error[160] = { 0 };
	char* text = NULL;
	CLIPRDR_FORMAT format = { 0 };
	CLIPRDR_FORMAT_LIST formatList = { 0 };
	const BOOL hasText = ohos_clipboard_read_plain_text(clipboard, &text, error, sizeof(error));
	CliprdrClientContext* cliprdr = ohos_clipboard_snapshot_cliprdr(clipboard);

	if (!cliprdr || !cliprdr->ClientFormatList)
	{
		free(text);
		return CHANNEL_RC_OK;
	}

	if (!hasText && error[0] != '\0')
		ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard read warning: %s", error);

	format.formatId = CF_UNICODETEXT;
	formatList.common.msgType = CB_FORMAT_LIST;
	formatList.common.msgFlags = 0;
	formatList.numFormats = hasText ? 1U : 0U;
	formatList.formats = hasText ? &format : NULL;

	clipboard->lastLocalFormatCount = formatList.numFormats;
	UINT rc = cliprdr->ClientFormatList(cliprdr, &formatList);
	if (rc == CHANNEL_RC_OK)
	{
		++clipboard->localFormatListCount;
		ohos_clipboard_log(clipboard, "cliprdr local format list sent: %s reason=%s",
		                   hasText ? "CF_UNICODETEXT" : "empty",
		                   reason ? reason : "unknown");
	}
	free(text);
	return rc;
}

static UINT ohos_clipboard_send_format_data_request(freerdpOhosClipboard* clipboard,
                                                    UINT32 formatId)
{
	CLIPRDR_FORMAT_DATA_REQUEST request = { 0 };
	CliprdrClientContext* cliprdr = ohos_clipboard_snapshot_cliprdr(clipboard);

	if (!clipboard || !cliprdr || !cliprdr->ClientFormatDataRequest)
		return ERROR_INVALID_PARAMETER;

	request.common.msgType = CB_FORMAT_DATA_REQUEST;
	request.requestedFormatId = formatId;

	pthread_mutex_lock(&clipboard->lock);
	clipboard->requestedFormatId = formatId;
	clipboard->lastRequestedFormat = formatId;
	pthread_mutex_unlock(&clipboard->lock);

	ohos_clipboard_log(clipboard, "cliprdr remote text request sent: format=%" PRIu32,
	                   formatId);
	return cliprdr->ClientFormatDataRequest(cliprdr, &request);
}

static freerdpOhosClipboard* ohos_clipboard_from_cliprdr(CliprdrClientContext* cliprdr)
{
	return cliprdr ? (freerdpOhosClipboard*)cliprdr->custom : NULL;
}

static UINT ohos_clipboard_monitor_ready(CliprdrClientContext* cliprdr,
                                         const CLIPRDR_MONITOR_READY* monitorReady)
{
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);
	UINT rc = CHANNEL_RC_OK;

	if (!clipboard || !monitorReady)
		return ERROR_INVALID_PARAMETER;

	rc = ohos_clipboard_send_client_capabilities(cliprdr);
	if (rc != CHANNEL_RC_OK)
		return rc;

	++clipboard->monitorReadyCount;
	ohos_clipboard_log(clipboard, "cliprdr monitor ready");
	return ohos_clipboard_send_local_format_list(clipboard, "monitor ready");
}

static UINT ohos_clipboard_server_capabilities(CliprdrClientContext* cliprdr,
                                               const CLIPRDR_CAPABILITIES* capabilities)
{
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !capabilities)
		return ERROR_INVALID_PARAMETER;

	ohos_clipboard_log(clipboard, "cliprdr server capabilities received");
	return CHANNEL_RC_OK;
}

static UINT ohos_clipboard_server_format_list(CliprdrClientContext* cliprdr,
                                              const CLIPRDR_FORMAT_LIST* formatList)
{
	UINT32 requested = 0;
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !formatList)
		return ERROR_INVALID_PARAMETER;

	for (UINT32 index = 0; index < formatList->numFormats; index++)
	{
		const UINT32 formatId = formatList->formats[index].formatId;
		if (formatId == CF_UNICODETEXT)
			requested = CF_UNICODETEXT;
		else if (requested == 0 && formatId == CF_TEXT)
			requested = CF_TEXT;
		else if (requested == 0 && formatId == CF_OEMTEXT)
			requested = CF_OEMTEXT;
	}

	clipboard->lastRemoteFormatCount = formatList->numFormats;
	++clipboard->remoteFormatListCount;
	ohos_clipboard_log(clipboard, "cliprdr server format list received: %" PRIu32,
	                   formatList->numFormats);

	UINT rc = ohos_clipboard_send_format_list_response(cliprdr, TRUE);
	if (rc != CHANNEL_RC_OK)
		return rc;

	if (requested == 0)
	{
		ohos_clipboard_log(clipboard, "cliprdr server format list has no supported text format");
		return CHANNEL_RC_OK;
	}
	return ohos_clipboard_send_format_data_request(clipboard, requested);
}

static UINT ohos_clipboard_server_format_list_response(
    CliprdrClientContext* cliprdr, const CLIPRDR_FORMAT_LIST_RESPONSE* response)
{
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !response)
		return ERROR_INVALID_PARAMETER;

	ohos_clipboard_log(clipboard, "cliprdr server accepted local format list");
	return CHANNEL_RC_OK;
}

static UINT ohos_clipboard_server_lock_clipboard_data(
    CliprdrClientContext* cliprdr, const CLIPRDR_LOCK_CLIPBOARD_DATA* lockClipboardData)
{
	return (!cliprdr || !lockClipboardData) ? ERROR_INVALID_PARAMETER : CHANNEL_RC_OK;
}

static UINT ohos_clipboard_server_unlock_clipboard_data(
    CliprdrClientContext* cliprdr, const CLIPRDR_UNLOCK_CLIPBOARD_DATA* unlockClipboardData)
{
	return (!cliprdr || !unlockClipboardData) ? ERROR_INVALID_PARAMETER : CHANNEL_RC_OK;
}

static UINT ohos_clipboard_server_format_data_request(
    CliprdrClientContext* cliprdr, const CLIPRDR_FORMAT_DATA_REQUEST* request)
{
	char error[160] = { 0 };
	char* text = NULL;
	BYTE* data = NULL;
	UINT32 dataLen = 0;
	CLIPRDR_FORMAT_DATA_RESPONSE response = { 0 };
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !request || !cliprdr->ClientFormatDataResponse)
		return ERROR_INVALID_PARAMETER;

	const BOOL ok = ohos_clipboard_read_plain_text(clipboard, &text, error, sizeof(error));
	if (ok && request->requestedFormatId == CF_UNICODETEXT)
	{
		data = ohos_clipboard_utf8_to_utf16le(text, &dataLen);
	}
	else if (ok && (request->requestedFormatId == CF_TEXT || request->requestedFormatId == CF_OEMTEXT))
	{
		dataLen = (UINT32)strlen(text) + 1U;
		data = (BYTE*)calloc(dataLen, sizeof(BYTE));
		if (data)
			memcpy(data, text, dataLen - 1U);
	}

	response.common.msgType = CB_FORMAT_DATA_RESPONSE;
	response.common.msgFlags = data ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
	response.common.dataLen = data ? dataLen : 0;
	response.requestedFormatData = data;

	++clipboard->localRequestCount;
	if (data)
		ohos_clipboard_log(clipboard, "cliprdr local text response sent: %" PRIu32 " bytes",
		                   dataLen);
	else
		ohos_clipboard_log(clipboard, "cliprdr local text request failed: %s", error);

	UINT rc = cliprdr->ClientFormatDataResponse(cliprdr, &response);
	free(data);
	free(text);
	return rc;
}

static UINT ohos_clipboard_server_format_data_response(
    CliprdrClientContext* cliprdr, const CLIPRDR_FORMAT_DATA_RESPONSE* response)
{
	char error[160] = { 0 };
	char* text = NULL;
	UINT32 requested = 0;
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !response)
		return ERROR_INVALID_PARAMETER;

	if ((response->common.msgFlags & CB_RESPONSE_FAIL) != 0 || !response->requestedFormatData)
	{
		ohos_clipboard_log(clipboard, "cliprdr remote text response failed: flags=%" PRIu16
		                              " length=%" PRIu32,
		                   response->common.msgFlags, response->common.dataLen);
		return CHANNEL_RC_OK;
	}

	pthread_mutex_lock(&clipboard->lock);
	requested = clipboard->requestedFormatId;
	pthread_mutex_unlock(&clipboard->lock);

	if (requested == CF_UNICODETEXT)
	{
		text = ohos_clipboard_utf16le_to_utf8(response->requestedFormatData,
		                                     response->common.dataLen);
	}
	else if (requested == CF_TEXT || requested == CF_OEMTEXT)
	{
		const char* bytes = (const char*)response->requestedFormatData;
		const size_t length = strnlen(bytes, response->common.dataLen);
		text = (char*)calloc(length + 1U, sizeof(char));
		if (text)
			memcpy(text, bytes, length);
	}

	if (!text || text[0] == '\0')
	{
		free(text);
		ohos_clipboard_log(clipboard, "cliprdr remote text response was empty");
		return CHANNEL_RC_OK;
	}

	if (!ohos_clipboard_write_plain_text(clipboard, text, error, sizeof(error)))
	{
		ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard write failed: %s", error);
		free(text);
		return ERROR_INTERNAL_ERROR;
	}

	++clipboard->remoteResponseCount;
	ohos_clipboard_log(clipboard, "cliprdr remote text copied to HarmonyOS Pasteboard: %zu bytes",
	                   strlen(text));
	free(text);
	return CHANNEL_RC_OK;
}

static UINT ohos_clipboard_server_file_contents_request(
    CliprdrClientContext* cliprdr, const CLIPRDR_FILE_CONTENTS_REQUEST* request)
{
	return (!cliprdr || !request) ? ERROR_INVALID_PARAMETER : CHANNEL_RC_OK;
}

static UINT ohos_clipboard_server_file_contents_response(
    CliprdrClientContext* cliprdr, const CLIPRDR_FILE_CONTENTS_RESPONSE* response)
{
	return (!cliprdr || !response) ? ERROR_INVALID_PARAMETER : CHANNEL_RC_OK;
}

static void ohos_clipboard_attach_cliprdr(freerdpOhosClipboard* clipboard,
                                          CliprdrClientContext* cliprdr)
{
	if (!clipboard || !cliprdr)
		return;

	pthread_mutex_lock(&clipboard->lock);
	clipboard->cliprdr = cliprdr;
	cliprdr->custom = clipboard;
	cliprdr->MonitorReady = ohos_clipboard_monitor_ready;
	cliprdr->ServerCapabilities = ohos_clipboard_server_capabilities;
	cliprdr->ServerFormatList = ohos_clipboard_server_format_list;
	cliprdr->ServerFormatListResponse = ohos_clipboard_server_format_list_response;
	cliprdr->ServerLockClipboardData = ohos_clipboard_server_lock_clipboard_data;
	cliprdr->ServerUnlockClipboardData = ohos_clipboard_server_unlock_clipboard_data;
	cliprdr->ServerFormatDataRequest = ohos_clipboard_server_format_data_request;
	cliprdr->ServerFormatDataResponse = ohos_clipboard_server_format_data_response;
	cliprdr->ServerFileContentsRequest = ohos_clipboard_server_file_contents_request;
	cliprdr->ServerFileContentsResponse = ohos_clipboard_server_file_contents_response;
	pthread_mutex_unlock(&clipboard->lock);

	++clipboard->channelConnectCount;
	ohos_clipboard_log(clipboard, "cliprdr connected to HarmonyOS Pasteboard text backend");
}

static void ohos_clipboard_detach_cliprdr(freerdpOhosClipboard* clipboard,
                                          CliprdrClientContext* cliprdr)
{
	if (!clipboard)
		return;

	pthread_mutex_lock(&clipboard->lock);
	if (clipboard->cliprdr == cliprdr)
	{
		if (clipboard->cliprdr)
			clipboard->cliprdr->custom = NULL;
		clipboard->cliprdr = NULL;
		clipboard->requestedFormatId = 0;
	}
	pthread_mutex_unlock(&clipboard->lock);

	++clipboard->channelDisconnectCount;
	ohos_clipboard_log(clipboard, "cliprdr disconnected from HarmonyOS Pasteboard text backend");
}

static void ohos_clipboard_channel_connected(void* context, const ChannelConnectedEventArgs* event)
{
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_context(context);

	if (!clipboard || !event || !event->name)
		return;
	if (strcmp(event->name, CLIPRDR_SVC_CHANNEL_NAME) == 0)
		ohos_clipboard_attach_cliprdr(clipboard, (CliprdrClientContext*)event->pInterface);
}

static void ohos_clipboard_channel_disconnected(void* context,
                                                const ChannelDisconnectedEventArgs* event)
{
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_context(context);

	if (!clipboard || !event || !event->name)
		return;
	if (strcmp(event->name, CLIPRDR_SVC_CHANNEL_NAME) == 0)
		ohos_clipboard_detach_cliprdr(clipboard, (CliprdrClientContext*)event->pInterface);
}

static void ohos_clipboard_on_pasteboard_finalize(void* context)
{
	(void)context;
}

static void ohos_clipboard_handle_pasteboard_changed(freerdpOhosClipboard* clipboard,
                                                     Pasteboard_NotifyType type)
{
	BOOL suppress = FALSE;

	if (!clipboard || type != NOTIFY_LOCAL_DATA_CHANGE)
		return;

	++clipboard->pasteboardChangeCount;
	pthread_mutex_lock(&clipboard->lock);
	if (clipboard->ignoreLocalChanges > 0 &&
	    ohos_clipboard_now_ms() <= clipboard->ignoreLocalChangesUntil)
	{
		clipboard->ignoreLocalChanges--;
		if (clipboard->ignoreLocalChanges == 0)
			clipboard->ignoreLocalChangesUntil = 0;
		suppress = TRUE;
	}
	else if (clipboard->ignoreLocalChanges > 0)
	{
		clipboard->ignoreLocalChanges = 0;
		clipboard->ignoreLocalChangesUntil = 0;
	}
	pthread_mutex_unlock(&clipboard->lock);

	if (suppress)
	{
		++clipboard->suppressedChangeCount;
		ohos_clipboard_log(clipboard,
		                   "HarmonyOS Pasteboard local change suppressed after remote clipboard write");
		return;
	}

	(void)ohos_clipboard_send_local_format_list(clipboard, "pasteboard changed");
}

static void ohos_clipboard_on_pasteboard_changed(void* context, Pasteboard_NotifyType type)
{
	ohos_clipboard_handle_pasteboard_changed((freerdpOhosClipboard*)context, type);
}

static void ohos_clipboard_destroy_pasteboard(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	if (clipboard->pasteboard && clipboard->observer && clipboard->pasteboardSubscribed)
	{
		(void)OH_Pasteboard_Unsubscribe(clipboard->pasteboard, NOTIFY_LOCAL_DATA_CHANGE,
		                                clipboard->observer);
		clipboard->pasteboardSubscribed = FALSE;
	}
	if (clipboard->observer)
	{
		(void)OH_PasteboardObserver_Destroy(clipboard->observer);
		clipboard->observer = NULL;
	}
	if (clipboard->pasteboard)
	{
		OH_Pasteboard_Destroy(clipboard->pasteboard);
		clipboard->pasteboard = NULL;
	}
}

static void ohos_clipboard_create_pasteboard(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	clipboard->pasteboard = OH_Pasteboard_Create();
	if (!clipboard->pasteboard)
	{
		ohos_clipboard_log(clipboard,
		                   "HarmonyOS Pasteboard create failed; cliprdr will advertise no local text");
		return;
	}
	ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard created for cliprdr text backend");

	clipboard->observer = OH_PasteboardObserver_Create();
	if (!clipboard->observer)
	{
		ohos_clipboard_log(
		    clipboard, "HarmonyOS Pasteboard observer create failed; local clipboard changes require reconnect");
		return;
	}

	int rc = OH_PasteboardObserver_SetData(clipboard->observer, clipboard,
	                                       ohos_clipboard_on_pasteboard_changed,
	                                       ohos_clipboard_on_pasteboard_finalize);
	if (rc != ERR_OK)
	{
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard observer setup failed: %d", rc);
		return;
	}

	rc = OH_Pasteboard_Subscribe(clipboard->pasteboard, NOTIFY_LOCAL_DATA_CHANGE,
	                             clipboard->observer);
	if (rc == ERR_OK)
	{
		clipboard->pasteboardSubscribed = TRUE;
		ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard observer subscribed");
	}
	else
	{
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard subscribe warning: %d", rc);
	}
}

freerdpOhosClipboard* freerdp_ohos_clipboard_new(void)
{
	freerdpOhosClipboard* clipboard = (freerdpOhosClipboard*)calloc(1, sizeof(freerdpOhosClipboard));
	if (!clipboard)
		return NULL;

	if (pthread_mutex_init(&clipboard->lock, NULL) != 0)
	{
		free(clipboard);
		return NULL;
	}
	clipboard->lockInitialized = TRUE;
	return clipboard;
}

BOOL freerdp_ohos_clipboard_register(freerdpOhosClipboard* clipboard, rdpContext* context,
                                     const FREERDP_OHOS_CLIPBOARD_CONFIG* config,
                                     char* errorBuffer, size_t errorBufferSize)
{
	if (errorBuffer && errorBufferSize > 0)
		errorBuffer[0] = '\0';
	if (!clipboard || !context || !context->pubSub || !config || !config->PubSubSubscribe ||
	    !config->PubSubUnsubscribe)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "invalid OHOS clipboard registration arguments");
		return FALSE;
	}

	clipboard->context = context;
	clipboard->config = *config;

	ohos_clipboard_registry_add(clipboard);

	int rc = clipboard->config.PubSubSubscribe(context->pubSub, "ChannelConnected",
	                                           ohos_clipboard_channel_connected);
	if (rc < 0)
	{
		ohos_clipboard_registry_remove(clipboard);
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "subscribe ChannelConnected for clipboard failed: %d", rc);
		return FALSE;
	}
	clipboard->channelConnectedSubscribed = TRUE;

	rc = clipboard->config.PubSubSubscribe(context->pubSub, "ChannelDisconnected",
	                                       ohos_clipboard_channel_disconnected);
	if (rc < 0)
	{
		freerdp_ohos_clipboard_unregister(clipboard);
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "subscribe ChannelDisconnected for clipboard failed: %d", rc);
		return FALSE;
	}
	clipboard->channelDisconnectedSubscribed = TRUE;

	ohos_clipboard_create_pasteboard(clipboard);
	++clipboard->registerCount;
	ohos_clipboard_log(clipboard, "cliprdr bridge subscribed to FreeRDP channel events");
	return TRUE;
}

void freerdp_ohos_clipboard_unregister(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	if (clipboard->cliprdr)
		ohos_clipboard_detach_cliprdr(clipboard, clipboard->cliprdr);

	ohos_clipboard_destroy_pasteboard(clipboard);

	if (clipboard->config.PubSubUnsubscribe && clipboard->context && clipboard->context->pubSub)
	{
		if (clipboard->channelConnectedSubscribed)
		{
			(void)clipboard->config.PubSubUnsubscribe(clipboard->context->pubSub,
			                                          "ChannelConnected",
			                                          ohos_clipboard_channel_connected);
			clipboard->channelConnectedSubscribed = FALSE;
		}
		if (clipboard->channelDisconnectedSubscribed)
		{
			(void)clipboard->config.PubSubUnsubscribe(clipboard->context->pubSub,
			                                          "ChannelDisconnected",
			                                          ohos_clipboard_channel_disconnected);
			clipboard->channelDisconnectedSubscribed = FALSE;
		}
	}

	ohos_clipboard_registry_remove(clipboard);
	clipboard->context = NULL;
	clipboard->requestedFormatId = 0;
	++clipboard->unregisterCount;
}

void freerdp_ohos_clipboard_free(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	freerdp_ohos_clipboard_unregister(clipboard);
	if (clipboard->lockInitialized)
	{
		pthread_mutex_destroy(&clipboard->lock);
		clipboard->lockInitialized = FALSE;
	}
	free(clipboard);
}

const char* freerdp_ohos_clipboard_get_diagnostics(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return "OHOS clipboard stats: unavailable";

	(void)snprintf(clipboard->diagnostics, sizeof(clipboard->diagnostics),
	               "OHOS clipboard stats: registered=%" PRIu64 " unregistered=%" PRIu64
	               " channelConnect=%" PRIu64 " channelDisconnect=%" PRIu64
	               " monitorReady=%" PRIu64 " localFormatList=%" PRIu64
	               " remoteFormatList=%" PRIu64 " localRequests=%" PRIu64
	               " remoteResponses=%" PRIu64 " pasteboardRead=%" PRIu64
	               " pasteboardWrite=%" PRIu64 " pasteboardChanges=%" PRIu64
	               " suppressedChanges=%" PRIu64 " errors=%" PRIu64
	               " lastError=%" PRIu32 " lastRequestedFormat=%" PRIu32
	               " lastRemoteFormats=%" PRIu32 " lastLocalFormats=%" PRIu32
	               " lastTextBytes=%" PRIu32,
	               clipboard->registerCount, clipboard->unregisterCount,
	               clipboard->channelConnectCount, clipboard->channelDisconnectCount,
	               clipboard->monitorReadyCount, clipboard->localFormatListCount,
	               clipboard->remoteFormatListCount, clipboard->localRequestCount,
	               clipboard->remoteResponseCount, clipboard->pasteboardReadCount,
	               clipboard->pasteboardWriteCount, clipboard->pasteboardChangeCount,
	               clipboard->suppressedChangeCount, clipboard->errorCount,
	               clipboard->lastError, clipboard->lastRequestedFormat,
	               clipboard->lastRemoteFormatCount, clipboard->lastLocalFormatCount,
	               clipboard->lastTextBytes);
	return clipboard->diagnostics;
}
