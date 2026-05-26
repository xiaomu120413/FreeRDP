/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS cliprdr <-> Pasteboard bridge
 */

#include "ohos_clipboard_internal.h"

static BOOL ohos_clipboard_make_data_cross_app(freerdpOhosClipboard* clipboard,
                                               OH_UdmfData* data, char* errorBuffer,
                                               size_t errorBufferSize)
{
	if (!data)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "UDMF data is unavailable");
		return FALSE;
	}

	OH_UdmfProperty* property = OH_UdmfProperty_Create(data);
	if (!property)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "UDMF property allocation failed");
		return FALSE;
	}

	const int rc = OH_UdmfProperty_SetShareOption(property, SHARE_OPTIONS_CROSS_APP);
	OH_UdmfProperty_Destroy(property);
	if (rc != UDMF_E_OK)
	{
		if (clipboard)
			clipboard->lastError = (UINT32)rc;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "UDMF cross-app share setup failed: %d", rc);
		return FALSE;
	}
	return TRUE;
}

FREERDP_LOCAL BOOL ohos_clipboard_write_plain_text(freerdpOhosClipboard* clipboard, const char* text,
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
	if (!ohos_clipboard_make_data_cross_app(clipboard, data, errorBuffer, errorBufferSize))
		goto fail;

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
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard SetData plain text: status=%d(%s) bytes=%zu",
	                   rc, ohos_clipboard_pasteboard_status_name(rc),
	                   ohos_clipboard_safe_strlen(text));
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

FREERDP_LOCAL BOOL ohos_clipboard_write_html(freerdpOhosClipboard* clipboard, const char* html,
                                      const char* plain, char* errorBuffer,
                                      size_t errorBufferSize)
{
	int rc = UDMF_E_OK;
	OH_UdsHtml* htmlData = NULL;
	OH_UdsPlainText* plainText = NULL;
	OH_UdmfRecord* record = NULL;
	OH_UdmfData* data = NULL;

	if (!clipboard || !clipboard->pasteboard)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard unavailable");
		return FALSE;
	}

	htmlData = OH_UdsHtml_Create();
	plainText = OH_UdsPlainText_Create();
	record = OH_UdmfRecord_Create();
	data = OH_UdmfData_Create();
	if (!htmlData || !plainText || !record || !data)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "UDMF allocation failed");
		goto fail;
	}
	if (!ohos_clipboard_make_data_cross_app(clipboard, data, errorBuffer, errorBufferSize))
		goto fail;

	const char* htmlValue = html ? html : "";
	const char* plainValue = (plain && plain[0] != '\0') ? plain : htmlValue;
	rc = OH_UdsHtml_SetContent(htmlData, htmlValue);
	if (rc == UDMF_E_OK)
		rc = OH_UdsHtml_SetPlainContent(htmlData, plainValue);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfRecord_AddHtml(record, htmlData);
	if (rc == UDMF_E_OK)
		rc = OH_UdsPlainText_SetContent(plainText, plainValue);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfRecord_AddPlainText(record, plainText);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfData_AddRecord(data, record);
	if (rc != UDMF_E_OK)
	{
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "UDMF html setup failed: %d", rc);
		goto fail;
	}

	pthread_mutex_lock(&clipboard->lock);
	clipboard->ignoreLocalChanges++;
	clipboard->ignoreLocalChangesUntil = ohos_clipboard_now_ms() + OHOS_CLIPBOARD_ECHO_SUPPRESS_MS;
	pthread_mutex_unlock(&clipboard->lock);

	rc = OH_Pasteboard_SetData(clipboard->pasteboard, data);
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard SetData html: status=%d(%s) htmlBytes=%zu "
	                   "plainBytes=%zu",
	                   rc, ohos_clipboard_pasteboard_status_name(rc),
	                   ohos_clipboard_safe_strlen(htmlValue),
	                   ohos_clipboard_safe_strlen(plainValue));
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
	clipboard->lastHtmlBytes = htmlValue ? (UINT32)strlen(htmlValue) : 0;
	clipboard->lastTextBytes = plainValue ? (UINT32)strlen(plainValue) : 0;
	if (htmlData)
		OH_UdsHtml_Destroy(htmlData);
	if (plainText)
		OH_UdsPlainText_Destroy(plainText);
	if (record)
		OH_UdmfRecord_Destroy(record);
	if (data)
		OH_UdmfData_Destroy(data);
	return TRUE;

fail:
	if (htmlData)
		OH_UdsHtml_Destroy(htmlData);
	if (plainText)
		OH_UdsPlainText_Destroy(plainText);
	if (record)
		OH_UdmfRecord_Destroy(record);
	if (data)
		OH_UdmfData_Destroy(data);
	return FALSE;
}

FREERDP_LOCAL BOOL ohos_clipboard_write_uri(freerdpOhosClipboard* clipboard, const char* uri,
                                     char* errorBuffer, size_t errorBufferSize)
{
	int rc = UDMF_E_OK;
	OH_UdsHyperlink* hyperlink = NULL;
	OH_UdsFileUri* fileUri = NULL;
	OH_UdsPlainText* plainText = NULL;
	OH_UdmfRecord* record = NULL;
	OH_UdmfData* data = NULL;

	if (!clipboard || !clipboard->pasteboard)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard unavailable");
		return FALSE;
	}

	const char* uriValue = uri ? uri : "";
	record = OH_UdmfRecord_Create();
	data = OH_UdmfData_Create();
	plainText = OH_UdsPlainText_Create();
	if (!record || !data || !plainText)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "UDMF allocation failed");
		goto fail;
	}
	if (!ohos_clipboard_make_data_cross_app(clipboard, data, errorBuffer, errorBufferSize))
		goto fail;

	if (ohos_clipboard_starts_with_ignore_case(uriValue, "http://") ||
	    ohos_clipboard_starts_with_ignore_case(uriValue, "https://"))
	{
		hyperlink = OH_UdsHyperlink_Create();
		if (!hyperlink)
		{
			ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
			                         "UDMF hyperlink allocation failed");
			goto fail;
		}
		rc = OH_UdsHyperlink_SetUrl(hyperlink, uriValue);
		if (rc == UDMF_E_OK)
			rc = OH_UdsHyperlink_SetDescription(hyperlink, uriValue);
		if (rc == UDMF_E_OK)
			rc = OH_UdmfRecord_AddHyperlink(record, hyperlink);
	}
	else
	{
		fileUri = OH_UdsFileUri_Create();
		if (!fileUri)
		{
			ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
			                         "UDMF file uri allocation failed");
			goto fail;
		}
		rc = OH_UdsFileUri_SetFileUri(fileUri, uriValue);
		if (rc == UDMF_E_OK)
			rc = OH_UdmfRecord_AddFileUri(record, fileUri);
	}

	if (rc == UDMF_E_OK)
		rc = OH_UdsPlainText_SetContent(plainText, uriValue);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfRecord_AddPlainText(record, plainText);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfData_AddRecord(data, record);
	if (rc != UDMF_E_OK)
	{
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "UDMF uri setup failed: %d", rc);
		goto fail;
	}

	pthread_mutex_lock(&clipboard->lock);
	clipboard->ignoreLocalChanges++;
	clipboard->ignoreLocalChangesUntil = ohos_clipboard_now_ms() + OHOS_CLIPBOARD_ECHO_SUPPRESS_MS;
	pthread_mutex_unlock(&clipboard->lock);

	rc = OH_Pasteboard_SetData(clipboard->pasteboard, data);
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard SetData uri: status=%d(%s) bytes=%zu kind=%s", rc,
	                   ohos_clipboard_pasteboard_status_name(rc),
	                   ohos_clipboard_safe_strlen(uriValue), ohos_clipboard_uri_kind(uriValue));
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
	clipboard->lastUriBytes = (UINT32)strlen(uriValue);
	clipboard->lastTextBytes = (UINT32)strlen(uriValue);
	if (hyperlink)
		OH_UdsHyperlink_Destroy(hyperlink);
	if (fileUri)
		OH_UdsFileUri_Destroy(fileUri);
	if (plainText)
		OH_UdsPlainText_Destroy(plainText);
	if (record)
		OH_UdmfRecord_Destroy(record);
	if (data)
		OH_UdmfData_Destroy(data);
	return TRUE;

fail:
	if (hyperlink)
		OH_UdsHyperlink_Destroy(hyperlink);
	if (fileUri)
		OH_UdsFileUri_Destroy(fileUri);
	if (plainText)
		OH_UdsPlainText_Destroy(plainText);
	if (record)
		OH_UdmfRecord_Destroy(record);
	if (data)
		OH_UdmfData_Destroy(data);
	return FALSE;
}

FREERDP_LOCAL BOOL ohos_clipboard_write_pixelmap_with_uri(freerdpOhosClipboard* clipboard,
                                                   const BYTE* bgra, UINT32 width,
                                                   UINT32 height, UINT32 sourceBytes,
                                                   const char* fileUriValue,
                                                   char* errorBuffer, size_t errorBufferSize)
{
	int rc = UDMF_E_OK;
	Image_ErrorCode imageRc = IMAGE_SUCCESS;
	OH_Pixelmap_InitializationOptions* options = NULL;
	OH_PixelmapNative* pixelmapNative = NULL;
	OH_UdsPixelMap* pixelMap = NULL;
	OH_UdsFileUri* fileUri = NULL;
	OH_UdmfRecord* record = NULL;
	OH_UdmfData* data = NULL;
	const BOOL includeFileUri = (fileUriValue && fileUriValue[0] != '\0');

	if (!clipboard || !clipboard->pasteboard)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard unavailable");
		return FALSE;
	}
	if (!bgra || width == 0U || height == 0U)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "invalid pixelmap input");
		return FALSE;
	}

	if ((size_t)width > SIZE_MAX / (size_t)height / 4U)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "pixelmap input is too large");
		return FALSE;
	}
	const size_t dataLength = (size_t)width * (size_t)height * 4U;

	imageRc = OH_PixelmapInitializationOptions_Create(&options);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_PixelmapInitializationOptions_SetWidth(options, width);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_PixelmapInitializationOptions_SetHeight(options, height);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_PixelmapInitializationOptions_SetPixelFormat(options, PIXEL_FORMAT_BGRA_8888);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_PixelmapInitializationOptions_SetAlphaType(
		    options, PIXELMAP_ALPHA_TYPE_UNPREMULTIPLIED);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_PixelmapNative_CreatePixelmap((uint8_t*)bgra, dataLength, options,
		                                           &pixelmapNative);
	if (imageRc != IMAGE_SUCCESS || !pixelmapNative)
	{
		clipboard->lastError = (UINT32)imageRc;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "PixelMap create failed: %d", imageRc);
		goto fail;
	}

	pixelMap = OH_UdsPixelMap_Create();
	if (includeFileUri)
		fileUri = OH_UdsFileUri_Create();
	record = OH_UdmfRecord_Create();
	data = OH_UdmfData_Create();
	if (!pixelMap || !record || !data || (includeFileUri && !fileUri))
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "UDMF allocation failed");
		goto fail;
	}
	if (!ohos_clipboard_make_data_cross_app(clipboard, data, errorBuffer, errorBufferSize))
		goto fail;

	rc = OH_UdsPixelMap_SetPixelMap(pixelMap, pixelmapNative);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfRecord_AddPixelMap(record, pixelMap);
	if (rc == UDMF_E_OK && includeFileUri)
		rc = OH_UdsFileUri_SetFileUri(fileUri, fileUriValue);
	if (rc == UDMF_E_OK && includeFileUri)
		rc = OH_UdmfRecord_AddFileUri(record, fileUri);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfData_AddRecord(data, record);
	if (rc != UDMF_E_OK)
	{
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "UDMF pixelmap setup failed: %d", rc);
		goto fail;
	}

	pthread_mutex_lock(&clipboard->lock);
	clipboard->ignoreLocalChanges++;
	clipboard->ignoreLocalChangesUntil = ohos_clipboard_now_ms() + OHOS_CLIPBOARD_ECHO_SUPPRESS_MS;
	pthread_mutex_unlock(&clipboard->lock);

	rc = OH_Pasteboard_SetData(clipboard->pasteboard, data);
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard SetData pixelmap: status=%d(%s) size=%" PRIu32
	                   "x%" PRIu32 " sourceBytes=%" PRIu32 " includeFileUri=%d uriBytes=%zu",
	                   rc, ohos_clipboard_pasteboard_status_name(rc), width, height, sourceBytes,
	                   includeFileUri,
	                   includeFileUri ? ohos_clipboard_safe_strlen(fileUriValue) : 0);
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
	clipboard->lastImageBytes = sourceBytes;
	if (includeFileUri)
		clipboard->lastUriBytes = (UINT32)strlen(fileUriValue);
	if (pixelMap)
		OH_UdsPixelMap_Destroy(pixelMap);
	if (fileUri)
		OH_UdsFileUri_Destroy(fileUri);
	if (record)
		OH_UdmfRecord_Destroy(record);
	if (data)
		OH_UdmfData_Destroy(data);
	if (pixelmapNative)
		OH_PixelmapNative_Release(pixelmapNative);
	if (options)
		OH_PixelmapInitializationOptions_Release(options);
	return TRUE;

fail:
	if (pixelMap)
		OH_UdsPixelMap_Destroy(pixelMap);
	if (fileUri)
		OH_UdsFileUri_Destroy(fileUri);
	if (record)
		OH_UdmfRecord_Destroy(record);
	if (data)
		OH_UdmfData_Destroy(data);
	if (pixelmapNative)
		OH_PixelmapNative_Release(pixelmapNative);
	if (options)
		OH_PixelmapInitializationOptions_Release(options);
	return FALSE;
}

FREERDP_LOCAL BOOL ohos_clipboard_write_pixelmap(freerdpOhosClipboard* clipboard, const BYTE* bgra,
                                          UINT32 width, UINT32 height, UINT32 sourceBytes,
                                          char* errorBuffer, size_t errorBufferSize)
{
	return ohos_clipboard_write_pixelmap_with_uri(clipboard, bgra, width, height, sourceBytes,
	                                             NULL, errorBuffer, errorBufferSize);
}
