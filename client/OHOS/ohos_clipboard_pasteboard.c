/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS cliprdr <-> Pasteboard bridge
 */

#include "ohos_clipboard_internal.h"

FREERDP_LOCAL const char* ohos_clipboard_pasteboard_status_name(int status)
{
	switch (status)
	{
		case ERR_OK:
			return "ERR_OK";
		case ERR_PERMISSION_ERROR:
			return "ERR_PERMISSION_ERROR";
		case ERR_INVALID_PARAMETER:
			return "ERR_INVALID_PARAMETER";
		case ERR_DEVICE_NOT_SUPPORTED:
			return "ERR_DEVICE_NOT_SUPPORTED";
		case ERR_INNER_ERROR:
			return "ERR_INNER_ERROR";
		case ERR_BUSY:
			return "ERR_BUSY";
		case ERR_PASTEBOARD_COPY_FILE_ERROR:
			return "ERR_PASTEBOARD_COPY_FILE_ERROR";
		case ERR_PASTEBOARD_PROGRESS_START_ERROR:
			return "ERR_PASTEBOARD_PROGRESS_START_ERROR";
		case ERR_PASTEBOARD_PROGRESS_ABNORMAL:
			return "ERR_PASTEBOARD_PROGRESS_ABNORMAL";
		case ERR_PASTEBOARD_GET_DATA_FAILED:
			return "ERR_PASTEBOARD_GET_DATA_FAILED";
		default:
			return "unknown";
	}
}

FREERDP_LOCAL void ohos_clipboard_trace_pasteboard_state(freerdpOhosClipboard* clipboard,
                                                  const char* reason)
{
	if (!clipboard || !clipboard->pasteboard)
		return;

	char source[128] = { 0 };
	const BOOL hasData = OH_Pasteboard_HasData(clipboard->pasteboard) ? TRUE : FALSE;
	const BOOL remoteData = OH_Pasteboard_IsRemoteData(clipboard->pasteboard) ? TRUE : FALSE;
	const int sourceRc =
	    OH_Pasteboard_GetDataSource(clipboard->pasteboard, source, (unsigned int)sizeof(source));
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard state before %s: hasData=%d remoteData=%d "
	                   "sourceRc=%d source=%s",
	                   reason ? reason : "unknown", hasData, remoteData, sourceRc,
	                   source[0] ? source : "none");
}

FREERDP_LOCAL void ohos_clipboard_trace_udmf_data(freerdpOhosClipboard* clipboard, const char* reason,
                                           OH_UdmfData* data)
{
	if (!clipboard || !data)
		return;

	const int recordCount = OH_UdmfData_GetRecordCount(data);
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard data after %s: records=%d local=%d "
	                   "hasPlain=%d hasHtml=%d hasHyperlink=%d hasFileUri=%d hasPixelMap=%d "
	                   "hasImage=%d",
	                   reason ? reason : "unknown", recordCount, OH_UdmfData_IsLocal(data) ? 1 : 0,
	                   OH_UdmfData_HasType(data, UDMF_META_PLAIN_TEXT) ? 1 : 0,
	                   OH_UdmfData_HasType(data, UDMF_META_HTML) ? 1 : 0,
	                   OH_UdmfData_HasType(data, UDMF_META_HYPERLINK) ? 1 : 0,
	                   OH_UdmfData_HasType(data, UDMF_META_GENERAL_FILE_URI) ? 1 : 0,
	                   OH_UdmfData_HasType(data, UDMF_META_OPENHARMONY_PIXEL_MAP) ? 1 : 0,
	                   OH_UdmfData_HasType(data, UDMF_META_IMAGE) ? 1 : 0);
}

FREERDP_LOCAL BOOL ohos_clipboard_ensure_read_permission(freerdpOhosClipboard* clipboard,
                                                  const char* reason)
{
	if (!clipboard)
		return FALSE;
	if (clipboard->pasteboardReadPermissionGranted)
		return TRUE;
	if (!clipboard->config.RequestReadPermission)
		return TRUE;

	const BOOL granted = clipboard->config.RequestReadPermission(
	    clipboard->config.permissionUserData, OHOS_CLIPBOARD_PERMISSION_TIMEOUT_MS);
	if (granted)
	{
		clipboard->pasteboardReadPermissionGranted = TRUE;
		ohos_clipboard_log(clipboard,
		                   "HarmonyOS Pasteboard read permission granted before %s",
		                   reason ? reason : "clipboard read");
		return TRUE;
	}

	clipboard->lastError = ERR_PERMISSION_ERROR;
	++clipboard->errorCount;
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard read permission denied before %s",
	                   reason ? reason : "clipboard read");
	return FALSE;
}

FREERDP_LOCAL OH_UdmfData* ohos_clipboard_get_pasteboard_data(freerdpOhosClipboard* clipboard,
                                                       const char* reason, int* outStatus)
{
	int status = ERR_OK;
	OH_UdmfData* data = NULL;

	if (outStatus)
		*outStatus = ERR_OK;
	if (!clipboard || !clipboard->pasteboard)
	{
		if (outStatus)
			*outStatus = ERR_INVALID_PARAMETER;
		return NULL;
	}

	if (!ohos_clipboard_ensure_read_permission(clipboard, reason))
	{
		if (outStatus)
			*outStatus = ERR_PERMISSION_ERROR;
		return NULL;
	}

	ohos_clipboard_trace_pasteboard_state(clipboard, reason);
	data = OH_Pasteboard_GetData(clipboard->pasteboard, &status);
	if (outStatus)
		*outStatus = status;
	if (status == ERR_PERMISSION_ERROR)
		clipboard->pasteboardReadPermissionGranted = FALSE;
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard GetData after %s: status=%d(%s) data=%p",
	                   reason ? reason : "unknown", status,
	                   ohos_clipboard_pasteboard_status_name(status), (void*)data);
	if (status == ERR_OK && data)
		ohos_clipboard_trace_udmf_data(clipboard, reason, data);
	return data;
}

FREERDP_LOCAL UINT ohos_clipboard_server_format_data_response(
    CliprdrClientContext* cliprdr, const CLIPRDR_FORMAT_DATA_RESPONSE* response)
{
	char error[160] = { 0 };
	char* text = NULL;
	char* html = NULL;
	char* uri = NULL;
	BYTE* bgra = NULL;
	UINT32 imageWidth = 0;
	UINT32 imageHeight = 0;
	UINT32 requested = 0;
	OHOS_CLIPBOARD_REQUEST_KIND requestedKind = OHOS_CLIPBOARD_REQUEST_NONE;
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
	requestedKind = clipboard->requestedFormatKind;
	clipboard->requestedFormatKind = OHOS_CLIPBOARD_REQUEST_NONE;
	pthread_mutex_unlock(&clipboard->lock);

	if (requestedKind == OHOS_CLIPBOARD_REQUEST_FILE_LIST)
		return ohos_clipboard_handle_remote_file_list(clipboard, response->requestedFormatData,
		                                              response->common.dataLen);

	if (requestedKind == OHOS_CLIPBOARD_REQUEST_DIB ||
	    requestedKind == OHOS_CLIPBOARD_REQUEST_DIBV5 ||
	    requestedKind == OHOS_CLIPBOARD_REQUEST_IMAGE_BMP ||
	    requestedKind == OHOS_CLIPBOARD_REQUEST_IMAGE_PNG ||
	    requestedKind == OHOS_CLIPBOARD_REQUEST_IMAGE_JPEG ||
	    requestedKind == OHOS_CLIPBOARD_REQUEST_IMAGE_WEBP)
	{
		const BOOL decoded =
		    (requestedKind == OHOS_CLIPBOARD_REQUEST_IMAGE_PNG ||
		     requestedKind == OHOS_CLIPBOARD_REQUEST_IMAGE_JPEG ||
		     requestedKind == OHOS_CLIPBOARD_REQUEST_IMAGE_WEBP)
		        ? ohos_clipboard_image_buffer_to_bgra(response->requestedFormatData,
		                                             response->common.dataLen, &bgra,
		                                             &imageWidth, &imageHeight, error,
		                                             sizeof(error))
		        : ohos_clipboard_bitmap_to_bgra(response->requestedFormatData,
		                                       response->common.dataLen, requestedKind, &bgra,
		                                       &imageWidth, &imageHeight, error, sizeof(error));
		if (!decoded)
		{
			ohos_clipboard_log(clipboard, "cliprdr remote image decode failed: %s", error);
			return CHANNEL_RC_OK;
		}
		if (!ohos_clipboard_write_pixelmap(clipboard, bgra, imageWidth, imageHeight,
		                                   response->common.dataLen, error, sizeof(error)))
		{
			ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard pixelmap write failed: %s",
			                   error);
			free(bgra);
			return ERROR_INTERNAL_ERROR;
		}
		++clipboard->remoteResponseCount;
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote image copied to HarmonyOS Pasteboard: %" PRIu32
		                   "x%" PRIu32 " sourceBytes=%" PRIu32,
		                   imageWidth, imageHeight, response->common.dataLen);
		free(bgra);
		return CHANNEL_RC_OK;
	}

	if (requestedKind == OHOS_CLIPBOARD_REQUEST_HTML)
	{
		html = ohos_clipboard_extract_ms_html(response->requestedFormatData,
		                                     response->common.dataLen);
		if (!html || html[0] == '\0')
		{
			free(html);
			ohos_clipboard_log(clipboard, "cliprdr remote html response was empty");
			return CHANNEL_RC_OK;
		}
		if (!ohos_clipboard_write_html(clipboard, html, NULL, error, sizeof(error)))
		{
			ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard html write failed: %s", error);
			free(html);
			return ERROR_INTERNAL_ERROR;
		}
		++clipboard->remoteResponseCount;
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote html copied to HarmonyOS Pasteboard: %zu bytes",
		                   strlen(html));
		free(html);
		return CHANNEL_RC_OK;
	}

	if (requestedKind == OHOS_CLIPBOARD_REQUEST_URIW)
	{
		uri = ohos_clipboard_utf16le_to_utf8(response->requestedFormatData,
		                                    response->common.dataLen);
	}
	else if (requestedKind == OHOS_CLIPBOARD_REQUEST_URI_LIST)
	{
		uri = ohos_clipboard_extract_uri_list_first(response->requestedFormatData,
		                                           response->common.dataLen);
		if (!uri)
			uri = ohos_clipboard_bytes_to_string(response->requestedFormatData,
			                                    response->common.dataLen);
	}
	if (requestedKind == OHOS_CLIPBOARD_REQUEST_URIW ||
	    requestedKind == OHOS_CLIPBOARD_REQUEST_URI_LIST)
	{
		if (!uri || uri[0] == '\0')
		{
			free(uri);
			ohos_clipboard_log(clipboard, "cliprdr remote uri response was empty");
			return CHANNEL_RC_OK;
		}
		if (!ohos_clipboard_write_uri(clipboard, uri, error, sizeof(error)))
		{
			ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard uri write failed: %s", error);
			free(uri);
			return ERROR_INTERNAL_ERROR;
		}
		++clipboard->remoteResponseCount;
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote uri copied to HarmonyOS Pasteboard: %zu bytes",
		                   strlen(uri));
		free(uri);
		return CHANNEL_RC_OK;
	}

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

FREERDP_LOCAL void ohos_clipboard_on_pasteboard_finalize(void* context)
{
	(void)context;
}

FREERDP_LOCAL void ohos_clipboard_handle_pasteboard_changed(freerdpOhosClipboard* clipboard,
                                                     Pasteboard_NotifyType type)
{
	BOOL suppress = FALSE;

	if (!clipboard || type != NOTIFY_LOCAL_DATA_CHANGE)
		return;

	++clipboard->pasteboardChangeCount;
	UINT32 ignoreLocalChanges = 0;
	UINT64 ignoreLocalChangesUntil = 0;
	pthread_mutex_lock(&clipboard->lock);
	ignoreLocalChanges = clipboard->ignoreLocalChanges;
	ignoreLocalChangesUntil = clipboard->ignoreLocalChangesUntil;
	pthread_mutex_unlock(&clipboard->lock);
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard change observed: type=%d changes=%" PRIu64
	                   " ignoreLocalChanges=%" PRIu32 " ignoreUntil=%" PRIu64,
	                   type, clipboard->pasteboardChangeCount, ignoreLocalChanges,
	                   ignoreLocalChangesUntil);
	ohos_clipboard_note_local_clipboard_changed(clipboard);

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

FREERDP_LOCAL void ohos_clipboard_on_pasteboard_changed(void* context, Pasteboard_NotifyType type)
{
	ohos_clipboard_handle_pasteboard_changed((freerdpOhosClipboard*)context, type);
}

FREERDP_LOCAL void ohos_clipboard_destroy_pasteboard(freerdpOhosClipboard* clipboard)
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

FREERDP_LOCAL void ohos_clipboard_create_pasteboard(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	clipboard->pasteboard = OH_Pasteboard_Create();
	if (!clipboard->pasteboard)
	{
		ohos_clipboard_log(clipboard,
		                   "HarmonyOS Pasteboard create failed; cliprdr will advertise no local formats");
		return;
	}
	ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard created for cliprdr backend");

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
		ohos_clipboard_log(clipboard,
		                   "HarmonyOS Pasteboard observer subscribed: rc=%d(%s)", rc,
		                   ohos_clipboard_pasteboard_status_name(rc));
	}
	else
	{
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard subscribe warning: %d(%s)", rc,
		                   ohos_clipboard_pasteboard_status_name(rc));
	}
}
