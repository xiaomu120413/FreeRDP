/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS cliprdr <-> Pasteboard bridge
 */

#include "ohos_clipboard_internal.h"

FREERDP_LOCAL UINT ohos_clipboard_send_client_capabilities(CliprdrClientContext* cliprdr)
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
	generalCapabilitySet.generalFlags =
	    CB_USE_LONG_FORMAT_NAMES | CB_STREAM_FILECLIP_ENABLED | CB_FILECLIP_NO_FILE_PATHS;
	return cliprdr->ClientCapabilities(cliprdr, &capabilities);
}

FREERDP_LOCAL UINT ohos_clipboard_send_format_list_response(CliprdrClientContext* cliprdr, BOOL accepted)
{
	CLIPRDR_FORMAT_LIST_RESPONSE response = { 0 };

	if (!cliprdr || !cliprdr->ClientFormatListResponse)
		return ERROR_INVALID_PARAMETER;

	response.common.msgType = CB_FORMAT_LIST_RESPONSE;
	response.common.msgFlags = accepted ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
	return cliprdr->ClientFormatListResponse(cliprdr, &response);
}

FREERDP_LOCAL UINT ohos_clipboard_send_local_format_list(freerdpOhosClipboard* clipboard,
                                                  const char* reason)
{
	char error[160] = { 0 };
	char* text = NULL;
	char* html = NULL;
	char* htmlPlain = NULL;
	char* uri = NULL;
	char* localImageFileName = NULL;
	CLIPRDR_FORMAT formats[10] = { 0 };
	CLIPRDR_FORMAT_LIST formatList = { 0 };
	const BOOL hasText = ohos_clipboard_read_plain_text(clipboard, &text, error, sizeof(error));
	const BOOL hasHtml = ohos_clipboard_read_html(clipboard, &html, &htmlPlain, NULL, 0);
	const BOOL hasUri = ohos_clipboard_read_uri(clipboard, &uri, NULL, 0);
	const BOOL hasUriImage = hasUri && ohos_clipboard_uri_may_reference_local_image(uri);
	const BOOL hasImage = hasUriImage;
	UINT32 uriImageFormatId = hasUriImage ? ohos_clipboard_image_format_id_from_uri(uri) : 0;
	UINT32 localImageFileFormatId = 0;
	UINT64 localImageFileSize = 0;
	const BOOL hasUriImageFile =
	    hasUriImage &&
	    ohos_clipboard_get_local_image_file_info_from_uri(
	        uri, NULL, &localImageFileName, &localImageFileSize, &localImageFileFormatId, NULL,
	        0);
	CliprdrClientContext* cliprdr = ohos_clipboard_snapshot_cliprdr(clipboard);
	UINT32 count = 0;

	if (hasUriImageFile && uriImageFormatId == 0U)
		uriImageFormatId = localImageFileFormatId;

	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard format probe: reason=%s text=%d textBytes=%zu "
	                   "html=%d htmlBytes=%zu htmlPlainBytes=%zu uri=%d uriBytes=%zu "
	                   "uriKind=%s uriImage=%d file=%d fileName=%s fileBytes=%" PRIu64
	                   " imageFormat=%s error=%s",
	                   reason ? reason : "unknown", hasText, ohos_clipboard_safe_strlen(text),
	                   hasHtml, ohos_clipboard_safe_strlen(html),
	                   ohos_clipboard_safe_strlen(htmlPlain), hasUri,
	                   ohos_clipboard_safe_strlen(uri), ohos_clipboard_uri_kind(uri),
	                   hasUriImage, hasUriImageFile,
	                   localImageFileName ? localImageFileName : "none", localImageFileSize,
	                   ohos_clipboard_image_format_name_from_id(uriImageFormatId)
	                       ? ohos_clipboard_image_format_name_from_id(uriImageFormatId)
	                       : "none",
	                   error[0] ? error : "none");

	if (!cliprdr || !cliprdr->ClientFormatList)
	{
		free(text);
		free(html);
		free(htmlPlain);
		free(uri);
		free(localImageFileName);
		return CHANNEL_RC_OK;
	}

	if (!hasText && !hasHtml && !hasUri && !hasImage && error[0] != '\0')
		ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard read warning: %s", error);

	if (hasUriImageFile)
	{
		formats[count].formatId = OHOS_CLIPBOARD_FORMAT_FILE_GROUP_DESCRIPTOR_W;
		formats[count++].formatName =
		    (char*)OHOS_CLIPBOARD_FILE_GROUP_DESCRIPTOR_W_FORMAT_NAME;
	}
	if (hasImage && uriImageFormatId != 0U)
	{
		formats[count].formatId = uriImageFormatId;
		formats[count++].formatName =
		    (char*)ohos_clipboard_image_format_name_from_id(uriImageFormatId);
	}
	if (hasImage && !hasUriImageFile)
		formats[count++].formatId = CF_DIBV5;
	if (hasImage && !hasUriImageFile)
		formats[count++].formatId = CF_DIB;
	if (!hasImage && (hasText || hasHtml || hasUri))
		formats[count++].formatId = CF_UNICODETEXT;
	if (!hasImage && hasHtml)
	{
		formats[count].formatId = OHOS_CLIPBOARD_FORMAT_HTML;
		formats[count++].formatName = (char*)OHOS_CLIPBOARD_HTML_FORMAT_NAME;
	}
	if (!hasImage && hasUri)
	{
		formats[count].formatId = OHOS_CLIPBOARD_FORMAT_URIW;
		formats[count++].formatName = (char*)OHOS_CLIPBOARD_URIW_FORMAT_NAME;
		formats[count].formatId = OHOS_CLIPBOARD_FORMAT_URI_LIST;
		formats[count++].formatName = (char*)OHOS_CLIPBOARD_URI_LIST_FORMAT_NAME;
	}

	formatList.common.msgType = CB_FORMAT_LIST;
	formatList.common.msgFlags = 0;
	formatList.numFormats = count;
	formatList.formats = (count > 0) ? formats : NULL;

	if (hasImage && !hasUriImageFile)
		(void)ohos_clipboard_schedule_image_export(clipboard, reason);
	else
		ohos_clipboard_note_local_clipboard_changed(clipboard);

	clipboard->lastLocalFormatCount = formatList.numFormats;
	UINT rc = cliprdr->ClientFormatList(cliprdr, &formatList);
	if (rc == CHANNEL_RC_OK)
	{
		++clipboard->localFormatListCount;
		ohos_clipboard_log(clipboard,
		                   "cliprdr local format list sent: text=%d html=%d uri=%d image=%d "
		                   "uriImage=%d file=%d fileBytes=%" PRIu64
		                   " dib=%d uriKind=%s encoded=%s count=%" PRIu32 " reason=%s",
		                   hasText || hasHtml || hasUri, hasHtml, hasUri, hasImage,
		                   hasUriImage, hasUriImageFile, localImageFileSize,
		                   hasImage && !hasUriImageFile,
		                   ohos_clipboard_uri_kind(uri),
		                   ohos_clipboard_image_format_name_from_id(uriImageFormatId)
		                       ? ohos_clipboard_image_format_name_from_id(uriImageFormatId)
		                       : "none",
		                   count,
		                   reason ? reason : "unknown");
	}
	free(text);
	free(html);
	free(htmlPlain);
	free(uri);
	free(localImageFileName);
	return rc;
}

FREERDP_LOCAL UINT ohos_clipboard_send_format_data_request(freerdpOhosClipboard* clipboard,
                                                    UINT32 formatId,
                                                    OHOS_CLIPBOARD_REQUEST_KIND kind)
{
	CLIPRDR_FORMAT_DATA_REQUEST request = { 0 };
	CliprdrClientContext* cliprdr = ohos_clipboard_snapshot_cliprdr(clipboard);

	if (!clipboard || !cliprdr || !cliprdr->ClientFormatDataRequest)
		return ERROR_INVALID_PARAMETER;

	request.common.msgType = CB_FORMAT_DATA_REQUEST;
	request.requestedFormatId = formatId;

	pthread_mutex_lock(&clipboard->lock);
	clipboard->requestedFormatId = formatId;
	clipboard->requestedFormatKind = kind;
	clipboard->lastRequestedFormat = formatId;
	pthread_mutex_unlock(&clipboard->lock);

	ohos_clipboard_log(clipboard, "cliprdr remote data request sent: format=%" PRIu32
	                              " kind=%d",
	                   formatId, kind);
	return cliprdr->ClientFormatDataRequest(cliprdr, &request);
}

FREERDP_LOCAL freerdpOhosClipboard* ohos_clipboard_from_cliprdr(CliprdrClientContext* cliprdr)
{
	return cliprdr ? (freerdpOhosClipboard*)cliprdr->custom : NULL;
}

FREERDP_LOCAL UINT ohos_clipboard_monitor_ready(CliprdrClientContext* cliprdr,
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

FREERDP_LOCAL UINT ohos_clipboard_server_capabilities(CliprdrClientContext* cliprdr,
                                               const CLIPRDR_CAPABILITIES* capabilities)
{
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !capabilities)
		return ERROR_INVALID_PARAMETER;

	ohos_clipboard_log(clipboard, "cliprdr server capabilities received");
	return CHANNEL_RC_OK;
}

FREERDP_LOCAL UINT ohos_clipboard_server_format_list(CliprdrClientContext* cliprdr,
                                              const CLIPRDR_FORMAT_LIST* formatList)
{
	UINT32 requested = 0;
	OHOS_CLIPBOARD_REQUEST_KIND requestedKind = OHOS_CLIPBOARD_REQUEST_NONE;
	UINT32 imageRequested = 0;
	OHOS_CLIPBOARD_REQUEST_KIND imageRequestedKind = OHOS_CLIPBOARD_REQUEST_NONE;
	UINT32 imagePriority = 0;
	UINT32 htmlRequested = 0;
	UINT32 uriWRequested = 0;
	UINT32 uriListRequested = 0;
	UINT32 textRequested = 0;
	OHOS_CLIPBOARD_REQUEST_KIND textRequestedKind = OHOS_CLIPBOARD_REQUEST_NONE;
	UINT32 fileGroupRequested = 0;
	BOOL sawFileClipboard = FALSE;
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !formatList)
		return ERROR_INVALID_PARAMETER;

	pthread_mutex_lock(&clipboard->lock);
	ohos_clipboard_clear_remote_file_transfer_locked(clipboard);
	pthread_mutex_unlock(&clipboard->lock);

	for (UINT32 index = 0; index < formatList->numFormats; index++)
	{
		const UINT32 formatId = formatList->formats[index].formatId;
		const char* formatName = formatList->formats[index].formatName;
		ohos_clipboard_log(clipboard,
		                   "cliprdr server format[%" PRIu32 "]: id=%" PRIu32
		                   " name=%s",
		                   index, formatId, formatName ? formatName : "");
		if (formatName &&
		    ohos_clipboard_equals_ignore_case(
		        formatName, OHOS_CLIPBOARD_FILE_GROUP_DESCRIPTOR_W_FORMAT_NAME))
		{
			sawFileClipboard = TRUE;
			fileGroupRequested = formatId;
			continue;
		}
		if (formatName &&
		    (ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_HTML_FORMAT_NAME) ||
		     ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_TEXT_HTML_FORMAT_NAME)))
		{
			htmlRequested = formatId;
			continue;
		}
		if (formatName &&
		    ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_URIW_FORMAT_NAME))
		{
			uriWRequested = formatId;
			continue;
		}
		if (formatName &&
		    (ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_URI_FORMAT_NAME) ||
		     ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_URI_LIST_FORMAT_NAME)))
		{
			uriListRequested = formatId;
			continue;
		}
		if (formatId == CF_DIB && imagePriority < 80U)
		{
			imageRequested = formatId;
			imageRequestedKind = OHOS_CLIPBOARD_REQUEST_DIB;
			imagePriority = 80U;
			continue;
		}
		if (formatId == CF_DIBV5 && imagePriority < 70U)
		{
			imageRequested = formatId;
			imageRequestedKind = OHOS_CLIPBOARD_REQUEST_DIBV5;
			imagePriority = 70U;
			continue;
		}
		if (formatName &&
		    ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_IMAGE_BMP_FORMAT_NAME))
		{
			if (imagePriority < 60U)
			{
				imageRequested = formatId;
				imageRequestedKind = OHOS_CLIPBOARD_REQUEST_IMAGE_BMP;
				imagePriority = 60U;
			}
			continue;
		}
		if (formatName &&
		    ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_IMAGE_PNG_FORMAT_NAME))
		{
			if (imagePriority < 100U)
			{
				imageRequested = formatId;
				imageRequestedKind = OHOS_CLIPBOARD_REQUEST_IMAGE_PNG;
				imagePriority = 100U;
			}
			continue;
		}
		if (formatName &&
		    ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_IMAGE_JPEG_FORMAT_NAME))
		{
			if (imagePriority < 100U)
			{
				imageRequested = formatId;
				imageRequestedKind = OHOS_CLIPBOARD_REQUEST_IMAGE_JPEG;
				imagePriority = 100U;
			}
			continue;
		}
		if (formatName &&
		    ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_IMAGE_WEBP_FORMAT_NAME))
		{
			if (imagePriority < 100U)
			{
				imageRequested = formatId;
				imageRequestedKind = OHOS_CLIPBOARD_REQUEST_IMAGE_WEBP;
				imagePriority = 100U;
			}
			continue;
		}
		if (formatId == CF_UNICODETEXT && textRequestedKind == OHOS_CLIPBOARD_REQUEST_NONE)
		{
			textRequested = CF_UNICODETEXT;
			textRequestedKind = OHOS_CLIPBOARD_REQUEST_TEXT;
		}
		else if (formatId == CF_TEXT && textRequestedKind == OHOS_CLIPBOARD_REQUEST_NONE)
		{
			textRequested = CF_TEXT;
			textRequestedKind = OHOS_CLIPBOARD_REQUEST_TEXT;
		}
		else if (formatId == CF_OEMTEXT && textRequestedKind == OHOS_CLIPBOARD_REQUEST_NONE)
		{
			textRequested = CF_OEMTEXT;
			textRequestedKind = OHOS_CLIPBOARD_REQUEST_TEXT;
		}
	}

	if (fileGroupRequested != 0U)
	{
		requested = fileGroupRequested;
		requestedKind = OHOS_CLIPBOARD_REQUEST_FILE_LIST;
	}
	else if (imageRequested != 0U)
	{
		requested = imageRequested;
		requestedKind = imageRequestedKind;
	}
	else if (htmlRequested != 0U)
	{
		requested = htmlRequested;
		requestedKind = OHOS_CLIPBOARD_REQUEST_HTML;
	}
	else if (uriWRequested != 0U)
	{
		requested = uriWRequested;
		requestedKind = OHOS_CLIPBOARD_REQUEST_URIW;
	}
	else if (uriListRequested != 0U)
	{
		requested = uriListRequested;
		requestedKind = OHOS_CLIPBOARD_REQUEST_URI_LIST;
	}
	else if (textRequested != 0U)
	{
		requested = textRequested;
		requestedKind = textRequestedKind;
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
		if (sawFileClipboard)
			ohos_clipboard_log(
			    clipboard,
			    "cliprdr server offered file clipboard but no usable FileGroupDescriptorW format id");
		ohos_clipboard_log(clipboard, "cliprdr server format list has no supported format");
		return CHANNEL_RC_OK;
	}
	ohos_clipboard_log(clipboard, "cliprdr server format selected: format=%" PRIu32
	                              " kind=%d",
	                   requested, requestedKind);
	return ohos_clipboard_send_format_data_request(clipboard, requested, requestedKind);
}

FREERDP_LOCAL UINT ohos_clipboard_server_format_list_response(
    CliprdrClientContext* cliprdr, const CLIPRDR_FORMAT_LIST_RESPONSE* response)
{
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !response)
		return ERROR_INVALID_PARAMETER;

	ohos_clipboard_log(clipboard, "cliprdr server accepted local format list");
	return CHANNEL_RC_OK;
}

FREERDP_LOCAL UINT ohos_clipboard_server_lock_clipboard_data(
    CliprdrClientContext* cliprdr, const CLIPRDR_LOCK_CLIPBOARD_DATA* lockClipboardData)
{
	return (!cliprdr || !lockClipboardData) ? ERROR_INVALID_PARAMETER : CHANNEL_RC_OK;
}

FREERDP_LOCAL UINT ohos_clipboard_server_unlock_clipboard_data(
    CliprdrClientContext* cliprdr, const CLIPRDR_UNLOCK_CLIPBOARD_DATA* unlockClipboardData)
{
	return (!cliprdr || !unlockClipboardData) ? ERROR_INVALID_PARAMETER : CHANNEL_RC_OK;
}

FREERDP_LOCAL UINT ohos_clipboard_server_format_data_request(
    CliprdrClientContext* cliprdr, const CLIPRDR_FORMAT_DATA_REQUEST* request)
{
	char error[160] = { 0 };
	char* text = NULL;
	char* html = NULL;
	char* htmlPlain = NULL;
	char* uri = NULL;
	BYTE* data = NULL;
	UINT32 dataLen = 0;
	CLIPRDR_FORMAT_DATA_RESPONSE response = { 0 };
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !request || !cliprdr->ClientFormatDataResponse)
		return ERROR_INVALID_PARAMETER;

	if (request->requestedFormatId == OHOS_CLIPBOARD_FORMAT_HTML)
	{
		if (ohos_clipboard_read_html(clipboard, &html, &htmlPlain, error, sizeof(error)))
			data = ohos_clipboard_html_to_ms_html(html, &dataLen);
	}
	else if (request->requestedFormatId == OHOS_CLIPBOARD_FORMAT_URIW)
	{
		if (ohos_clipboard_read_uri(clipboard, &uri, error, sizeof(error)))
			data = ohos_clipboard_utf8_to_utf16le(uri, &dataLen);
	}
	else if (request->requestedFormatId == OHOS_CLIPBOARD_FORMAT_URI_LIST)
	{
		if (ohos_clipboard_read_uri(clipboard, &uri, error, sizeof(error)))
			data = ohos_clipboard_uri_to_uri_list(uri, &dataLen);
	}
	else if (request->requestedFormatId == OHOS_CLIPBOARD_FORMAT_FILE_GROUP_DESCRIPTOR_W)
	{
		char* fileName = NULL;
		UINT64 fileSize = 0;
		UINT32 fileFormatId = 0;
		if (ohos_clipboard_read_uri(clipboard, &uri, error, sizeof(error)) &&
		    ohos_clipboard_get_local_image_file_info_from_uri(uri, NULL, &fileName, &fileSize,
		                                                     &fileFormatId, error,
		                                                     sizeof(error)))
		{
			data = ohos_clipboard_make_local_file_descriptor_data(fileName, fileSize, &dataLen,
			                                                     error, sizeof(error));
			if (data)
				ohos_clipboard_log(clipboard,
				                   "cliprdr local file descriptor sent: name=%s bytes=%" PRIu64
				                   " format=%s",
				                   fileName, fileSize,
				                   ohos_clipboard_image_format_name_from_id(fileFormatId)
				                       ? ohos_clipboard_image_format_name_from_id(fileFormatId)
				                       : "none");
		}
		free(fileName);
	}
	else if (request->requestedFormatId == CF_DIB)
	{
		(void)ohos_clipboard_get_cached_image_data(clipboard, request->requestedFormatId,
		                                           &data, &dataLen, error, sizeof(error));
	}
	else if (request->requestedFormatId == CF_DIBV5)
	{
		(void)ohos_clipboard_get_cached_image_data(clipboard, request->requestedFormatId,
		                                           &data, &dataLen, error, sizeof(error));
	}
	else if (request->requestedFormatId == OHOS_CLIPBOARD_FORMAT_IMAGE_PNG ||
	         request->requestedFormatId == OHOS_CLIPBOARD_FORMAT_IMAGE_JPEG ||
	         request->requestedFormatId == OHOS_CLIPBOARD_FORMAT_IMAGE_WEBP)
	{
		if (ohos_clipboard_read_uri(clipboard, &uri, error, sizeof(error)))
		{
			data = ohos_clipboard_read_local_image_file_data_from_uri(
			    uri, request->requestedFormatId, &dataLen, error, sizeof(error));
			if (data)
				ohos_clipboard_log(clipboard,
				                   "cliprdr local encoded image file response prepared: "
				                   "format=%s bytes=%" PRIu32,
				                   ohos_clipboard_image_format_name_from_id(
				                       request->requestedFormatId),
				                   dataLen);
		}
		if (!data)
			(void)ohos_clipboard_get_cached_image_data(clipboard, request->requestedFormatId,
			                                           &data, &dataLen, error, sizeof(error));
	}
	else
	{
		BOOL ok = ohos_clipboard_read_plain_text(clipboard, &text, error, sizeof(error));
		if (!ok && ohos_clipboard_read_uri(clipboard, &uri, NULL, 0))
		{
			text = ohos_clipboard_strdup(uri);
			ok = (text != NULL);
		}
		if (!ok && ohos_clipboard_read_html(clipboard, &html, &htmlPlain, NULL, 0))
		{
			text = ohos_clipboard_strdup((htmlPlain && htmlPlain[0] != '\0') ? htmlPlain : html);
			ok = (text != NULL);
		}

		if (ok && request->requestedFormatId == CF_UNICODETEXT)
		{
			data = ohos_clipboard_utf8_to_utf16le(text, &dataLen);
		}
		else if (ok &&
		         (request->requestedFormatId == CF_TEXT || request->requestedFormatId == CF_OEMTEXT))
		{
			dataLen = (UINT32)strlen(text) + 1U;
			data = (BYTE*)calloc(dataLen, sizeof(BYTE));
			if (data)
				memcpy(data, text, dataLen - 1U);
		}
	}

	response.common.msgType = CB_FORMAT_DATA_RESPONSE;
	response.common.msgFlags = data ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
	response.common.dataLen = data ? dataLen : 0;
	response.requestedFormatData = data;

	++clipboard->localRequestCount;
	if (data)
		ohos_clipboard_log(clipboard, "cliprdr local response sent: format=%" PRIu32
		                              " bytes=%" PRIu32,
		                   request->requestedFormatId, dataLen);
	else
		ohos_clipboard_log(clipboard, "cliprdr local request failed: format=%" PRIu32
		                              " error=%s",
		                   request->requestedFormatId, error);

	UINT rc = cliprdr->ClientFormatDataResponse(cliprdr, &response);
	free(data);
	free(text);
	free(html);
	free(htmlPlain);
	free(uri);
	return rc;
}
