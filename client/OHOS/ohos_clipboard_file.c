/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS cliprdr <-> Pasteboard bridge
 */

#include "ohos_clipboard_internal.h"

FREERDP_LOCAL UINT64 ohos_clipboard_file_descriptor_size(const FILEDESCRIPTORW* file)
{
	if (!file)
		return 0;
	return (((UINT64)file->nFileSizeHigh) << 32U) | (UINT64)file->nFileSizeLow;
}

FREERDP_LOCAL BOOL ohos_clipboard_get_local_image_file_info_from_uri(
    const char* uri, char** outPath, char** outFileName, UINT64* outSize,
    UINT32* outFormatId, char* errorBuffer, size_t errorBufferSize)
{
	char* path = NULL;
	char* fileName = NULL;
	struct stat st = { 0 };
	BYTE signature[OHOS_CLIPBOARD_IMAGE_SIGNATURE_BYTES] = { 0 };
	UINT32 signatureSize = 0;
	UINT32 formatId = 0;

	if (outPath)
		*outPath = NULL;
	if (outFileName)
		*outFileName = NULL;
	if (outSize)
		*outSize = 0;
	if (outFormatId)
		*outFormatId = 0;
	if (!uri)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "empty local clipboard image uri");
		return FALSE;
	}

	path = ohos_clipboard_uri_to_local_path(uri);
	if (!path)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "clipboard image uri is not a local file: kind=%s",
		                         ohos_clipboard_uri_kind(uri));
		return FALSE;
	}

	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "clipboard image local file stat failed: %s",
		                         strerror(errno));
		goto fail;
	}

	const UINT64 fileSize = (UINT64)st.st_size;
	if (fileSize > OHOS_CLIPBOARD_MAX_URI_IMAGE_BYTES || fileSize > UINT32_MAX)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "clipboard image local file too large: %" PRIu64,
		                         fileSize);
		goto fail;
	}

	if (!ohos_clipboard_read_local_file_signature(path, signature, sizeof(signature),
	                                             &signatureSize))
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "clipboard image local file signature read failed");
		goto fail;
	}
	formatId = ohos_clipboard_image_format_id_from_signature(signature, signatureSize);
	if (formatId == 0U)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "clipboard local file is not supported png/jpg/webp");
		goto fail;
	}

	if (outFileName)
	{
		fileName = ohos_clipboard_local_image_file_name_from_path(path, formatId);
		if (!fileName)
		{
			ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
			                         "clipboard local file name allocation failed");
			goto fail;
		}
		*outFileName = fileName;
		fileName = NULL;
	}
	if (outPath)
	{
		*outPath = path;
		path = NULL;
	}
	if (outSize)
		*outSize = fileSize;
	if (outFormatId)
		*outFormatId = formatId;

	free(fileName);
	free(path);
	return TRUE;

fail:
	free(fileName);
	free(path);
	return FALSE;
}

FREERDP_LOCAL BOOL ohos_clipboard_set_file_descriptor_name(FILEDESCRIPTORW* descriptor,
                                                    const char* fileName)
{
	if (!descriptor || !fileName || fileName[0] == '\0')
		return FALSE;

	WCHAR* wideName = ConvertUtf8ToWCharAlloc(fileName, NULL);
	if (!wideName)
		return FALSE;

	size_t index = 0;
	for (; index + 1U < ARRAYSIZE(descriptor->cFileName) && wideName[index] != 0; index++)
		descriptor->cFileName[index] = wideName[index];
	descriptor->cFileName[index] = 0;
	free(wideName);
	return TRUE;
}

FREERDP_LOCAL BYTE* ohos_clipboard_make_local_file_descriptor_data(const char* fileName,
                                                           UINT64 fileSize,
                                                           UINT32* outSize,
                                                           char* errorBuffer,
                                                           size_t errorBufferSize)
{
	BYTE* data = NULL;
	FILEDESCRIPTORW descriptor = { 0 };

	if (!outSize)
		return NULL;
	*outSize = 0;
	if (!fileName || fileSize == 0U || fileSize > UINT32_MAX)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "invalid local file descriptor input");
		return NULL;
	}

	descriptor.dwFlags = FD_ATTRIBUTES | FD_FILESIZE;
	descriptor.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
	descriptor.nFileSizeHigh = (DWORD)((fileSize >> 32U) & 0xFFFFFFFFU);
	descriptor.nFileSizeLow = (DWORD)(fileSize & 0xFFFFFFFFU);
	if (!ohos_clipboard_set_file_descriptor_name(&descriptor, fileName))
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "local file descriptor name conversion failed");
		return NULL;
	}

	const UINT rc = cliprdr_serialize_file_list_ex(
	    CB_STREAM_FILECLIP_ENABLED | CB_FILECLIP_NO_FILE_PATHS, &descriptor, 1U, &data,
	    outSize);
	if (rc != NO_ERROR)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "local file descriptor serialize failed: rc=%" PRIu32, rc);
		free(data);
		return NULL;
	}
	return data;
}

FREERDP_LOCAL BYTE* ohos_clipboard_read_local_image_file_data_from_uri(
    const char* uri, UINT32 requestedFormatId, UINT32* outSize, char* errorBuffer,
    size_t errorBufferSize)
{
	char* path = NULL;
	UINT64 fileSize = 0;
	UINT32 fileFormatId = 0;
	BYTE* data = NULL;

	if (!outSize)
		return NULL;
	*outSize = 0;

	if (!ohos_clipboard_get_local_image_file_info_from_uri(uri, &path, NULL, &fileSize,
	                                                       &fileFormatId, errorBuffer,
	                                                       errorBufferSize))
		return NULL;
	if (fileFormatId != requestedFormatId)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "clipboard local image format mismatch: have=%s requested=%s",
		                         ohos_clipboard_image_format_name_from_id(fileFormatId)
		                             ? ohos_clipboard_image_format_name_from_id(fileFormatId)
		                             : "none",
		                         ohos_clipboard_image_format_name_from_id(requestedFormatId)
		                             ? ohos_clipboard_image_format_name_from_id(requestedFormatId)
		                             : "none");
		free(path);
		return NULL;
	}

	data = ohos_clipboard_read_local_file(path, OHOS_CLIPBOARD_MAX_URI_IMAGE_BYTES, outSize,
	                                      errorBuffer, errorBufferSize);
	free(path);
	(void)fileSize;
	return data;
}

FREERDP_LOCAL UINT ohos_clipboard_request_remote_file_range(freerdpOhosClipboard* clipboard,
                                                     UINT32 listIndex, UINT64 fileSize,
                                                     const char* fileName)
{
	if (!clipboard || fileSize == 0U || fileSize > OHOS_CLIPBOARD_MAX_URI_IMAGE_BYTES ||
	    fileSize > UINT32_MAX)
		return ERROR_INVALID_PARAMETER;

	CliprdrClientContext* cliprdr = ohos_clipboard_snapshot_cliprdr(clipboard);
	if (!cliprdr || !cliprdr->ClientFileContentsRequest)
		return ERROR_INVALID_PARAMETER;

	char* storedName = ohos_clipboard_strdup(fileName ? fileName : "");
	if (!storedName)
		return ERROR_NOT_ENOUGH_MEMORY;

	UINT32 streamId = 0;
	pthread_mutex_lock(&clipboard->lock);
	ohos_clipboard_clear_remote_file_transfer_locked(clipboard);
	if (clipboard->remoteFileNextStreamId == 0U)
		clipboard->remoteFileNextStreamId = 1U;
	streamId = clipboard->remoteFileNextStreamId++;
	if (clipboard->remoteFileNextStreamId == 0U)
		clipboard->remoteFileNextStreamId = 1U;
	clipboard->remoteFileTransferActive = TRUE;
	clipboard->remoteFileStreamId = streamId;
	clipboard->remoteFileListIndex = listIndex;
	clipboard->remoteFileExpectedBytes = (UINT32)fileSize;
	clipboard->remoteFileName = storedName;
	storedName = NULL;
	pthread_mutex_unlock(&clipboard->lock);

	CLIPRDR_FILE_CONTENTS_REQUEST request = { 0 };
	request.common.msgType = CB_FILECONTENTS_REQUEST;
	request.streamId = streamId;
	request.listIndex = listIndex;
	request.dwFlags = FILECONTENTS_RANGE;
	request.nPositionLow = 0;
	request.nPositionHigh = 0;
	request.cbRequested = (UINT32)fileSize;

	const UINT rc = cliprdr->ClientFileContentsRequest(cliprdr, &request);
	if (rc != CHANNEL_RC_OK)
	{
		pthread_mutex_lock(&clipboard->lock);
		if (clipboard->remoteFileStreamId == streamId)
			ohos_clipboard_clear_remote_file_transfer_locked(clipboard);
		pthread_mutex_unlock(&clipboard->lock);
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file content request failed: stream=%" PRIu32
		                   " index=%" PRIu32 " bytes=%" PRIu64 " rc=%" PRIu32,
		                   streamId, listIndex, fileSize, rc);
	}
	else
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file content request sent: stream=%" PRIu32
		                   " index=%" PRIu32 " bytes=%" PRIu64 " name=%s",
		                   streamId, listIndex, fileSize, fileName ? fileName : "");
	}

	free(storedName);
	return rc;
}

FREERDP_LOCAL UINT ohos_clipboard_handle_remote_file_list(freerdpOhosClipboard* clipboard,
                                                   const BYTE* data, UINT32 dataLen)
{
	if (!clipboard || !data || dataLen == 0U)
		return ERROR_INVALID_PARAMETER;

	FILEDESCRIPTORW* files = NULL;
	UINT32 fileCount = 0;
	const UINT parseRc = cliprdr_parse_file_list(data, dataLen, &files, &fileCount);
	if (parseRc != NO_ERROR)
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file list parse failed: rc=%" PRIu32
		                   " bytes=%" PRIu32,
		                   parseRc, dataLen);
		return CHANNEL_RC_OK;
	}

	UINT32 selectedIndex = UINT32_MAX;
	UINT64 selectedSize = 0;
	char* selectedName = NULL;
	for (UINT32 index = 0; index < fileCount; index++)
	{
		const FILEDESCRIPTORW* file = &files[index];
		char* name = ohos_clipboard_wchar_to_utf8(file->cFileName, ARRAYSIZE(file->cFileName));
		if (!name || name[0] == '\0')
		{
			free(name);
			continue;
		}

		if ((file->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U)
		{
			ohos_clipboard_log(clipboard,
			                   "cliprdr remote file list skipped directory: index=%" PRIu32
			                   " name=%s",
			                   index, name);
			free(name);
			continue;
		}

		if (!ohos_clipboard_uri_has_image_suffix(name))
		{
			free(name);
			continue;
		}

		const UINT64 fileSize = ohos_clipboard_file_descriptor_size(file);
		if ((file->dwFlags & FD_FILESIZE) == 0U || fileSize == 0U ||
		    fileSize > OHOS_CLIPBOARD_MAX_URI_IMAGE_BYTES || fileSize > UINT32_MAX)
		{
			ohos_clipboard_log(clipboard,
			                   "cliprdr remote image file skipped by size: index=%" PRIu32
			                   " bytes=%" PRIu64 " flags=0x%08" PRIx32 " name=%s",
			                   index, fileSize, (UINT32)file->dwFlags, name);
			free(name);
			continue;
		}

		selectedIndex = index;
		selectedSize = fileSize;
		selectedName = name;
		break;
	}

	ohos_clipboard_log(clipboard, "cliprdr remote file list received: files=%" PRIu32
	                              " selected=%s",
	                   fileCount, selectedName ? selectedName : "none");

	UINT rc = CHANNEL_RC_OK;
	if (selectedName)
		rc = ohos_clipboard_request_remote_file_range(clipboard, selectedIndex, selectedSize,
		                                             selectedName);
	else
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file list has no supported jpg/png/webp image");

	free(selectedName);
	free(files);
	return rc;
}

FREERDP_LOCAL UINT ohos_clipboard_send_local_file_contents_response(
    CliprdrClientContext* cliprdr, const CLIPRDR_FILE_CONTENTS_REQUEST* request,
    const BYTE* data, UINT32 dataSize, BOOL ok)
{
	if (!cliprdr || !request || !cliprdr->ClientFileContentsResponse)
		return ERROR_INVALID_PARAMETER;

	CLIPRDR_FILE_CONTENTS_RESPONSE response = { 0 };
	response.common.msgType = CB_FILECONTENTS_RESPONSE;
	response.common.msgFlags = ok ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
	response.common.dataLen = ok ? dataSize : 0;
	response.streamId = request->streamId;
	response.cbRequested = ok ? dataSize : 0;
	response.requestedData = ok ? data : NULL;
	return cliprdr->ClientFileContentsResponse(cliprdr, &response);
}

FREERDP_LOCAL UINT ohos_clipboard_server_file_contents_request(
    CliprdrClientContext* cliprdr, const CLIPRDR_FILE_CONTENTS_REQUEST* request)
{
	char error[160] = { 0 };
	char* uri = NULL;
	char* path = NULL;
	char* fileName = NULL;
	BYTE* data = NULL;
	UINT32 dataSize = 0;
	UINT64 fileSize = 0;
	UINT32 fileFormatId = 0;
	UINT rc = CHANNEL_RC_OK;
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !request || !cliprdr->ClientFileContentsResponse)
		return ERROR_INVALID_PARAMETER;

	const BOOL wantsSize = (request->dwFlags & FILECONTENTS_SIZE) != 0U;
	const BOOL wantsRange = (request->dwFlags & FILECONTENTS_RANGE) != 0U;
	if (wantsSize == wantsRange || request->listIndex != 0U)
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr local file contents request rejected: stream=%" PRIu32
		                   " index=%" PRIu32 " flags=0x%08" PRIx32,
		                   request->streamId, request->listIndex, request->dwFlags);
		return ohos_clipboard_send_local_file_contents_response(cliprdr, request, NULL, 0,
		                                                        FALSE);
	}

	if (!ohos_clipboard_read_uri(clipboard, &uri, error, sizeof(error)) ||
	    !ohos_clipboard_get_local_image_file_info_from_uri(uri, &path, &fileName, &fileSize,
	                                                       &fileFormatId, error, sizeof(error)))
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr local file contents source unavailable: stream=%" PRIu32
		                   " error=%s",
		                   request->streamId, error);
		rc = ohos_clipboard_send_local_file_contents_response(cliprdr, request, NULL, 0, FALSE);
		goto fail;
	}

	if (wantsSize)
	{
		BYTE sizeData[sizeof(UINT64)] = { 0 };
		for (size_t index = 0; index < sizeof(sizeData); index++)
			sizeData[index] = (BYTE)((fileSize >> (index * 8U)) & 0xFFU);
		rc = ohos_clipboard_send_local_file_contents_response(
		    cliprdr, request, sizeData, (UINT32)sizeof(sizeData), TRUE);
		if (rc == CHANNEL_RC_OK)
			ohos_clipboard_log(clipboard,
			                   "cliprdr local file size response sent: stream=%" PRIu32
			                   " bytes=%" PRIu64 " name=%s",
			                   request->streamId, fileSize, fileName);
		goto fail;
	}

	const UINT64 offset =
	    (((UINT64)request->nPositionHigh) << 32U) | (UINT64)request->nPositionLow;
	if (request->cbRequested == 0U || offset >= fileSize)
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr local file range rejected: stream=%" PRIu32
		                   " offset=%" PRIu64 " requested=%" PRIu32 " size=%" PRIu64
		                   " name=%s",
		                   request->streamId, offset, request->cbRequested, fileSize, fileName);
		rc = ohos_clipboard_send_local_file_contents_response(cliprdr, request, NULL, 0, FALSE);
		goto fail;
	}

	const UINT64 remaining = fileSize - offset;
	UINT32 bytesToRead = request->cbRequested;
	if ((UINT64)bytesToRead > remaining)
		bytesToRead = (UINT32)remaining;

	data = ohos_clipboard_read_local_file_range(path, offset, bytesToRead, &dataSize, error,
	                                            sizeof(error));
	if (!data)
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr local file range read failed: stream=%" PRIu32
		                   " offset=%" PRIu64 " requested=%" PRIu32 " error=%s name=%s",
		                   request->streamId, offset, bytesToRead, error, fileName);
		rc = ohos_clipboard_send_local_file_contents_response(cliprdr, request, NULL, 0, FALSE);
		goto fail;
	}

	rc = ohos_clipboard_send_local_file_contents_response(cliprdr, request, data, dataSize, TRUE);
	if (rc == CHANNEL_RC_OK)
		ohos_clipboard_log(clipboard,
		                   "cliprdr local file contents response sent: stream=%" PRIu32
		                   " offset=%" PRIu64 " bytes=%" PRIu32 " fileBytes=%" PRIu64
		                   " name=%s format=%s",
		                   request->streamId, offset, dataSize, fileSize, fileName,
		                   ohos_clipboard_image_format_name_from_id(fileFormatId)
		                       ? ohos_clipboard_image_format_name_from_id(fileFormatId)
		                       : "none");

fail:
	free(data);
	free(fileName);
	free(path);
	free(uri);
	return rc;
}

FREERDP_LOCAL UINT ohos_clipboard_server_file_contents_response(
    CliprdrClientContext* cliprdr, const CLIPRDR_FILE_CONTENTS_RESPONSE* response)
{
	char error[160] = { 0 };
	BYTE* bgra = NULL;
	UINT32 imageWidth = 0;
	UINT32 imageHeight = 0;
	UINT32 expectedBytes = 0;
	UINT32 listIndex = 0;
	char* fileName = NULL;
	char* cachedFileUri = NULL;
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !response)
		return ERROR_INVALID_PARAMETER;

	pthread_mutex_lock(&clipboard->lock);
	if (!clipboard->remoteFileTransferActive ||
	    clipboard->remoteFileStreamId != response->streamId)
	{
		pthread_mutex_unlock(&clipboard->lock);
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file content response ignored: stream=%" PRIu32,
		                   response->streamId);
		return CHANNEL_RC_OK;
	}
	expectedBytes = clipboard->remoteFileExpectedBytes;
	listIndex = clipboard->remoteFileListIndex;
	fileName = clipboard->remoteFileName;
	clipboard->remoteFileName = NULL;
	ohos_clipboard_clear_remote_file_transfer_locked(clipboard);
	pthread_mutex_unlock(&clipboard->lock);

	if ((response->common.msgFlags & CB_RESPONSE_FAIL) != 0 || !response->requestedData)
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file content response failed: stream=%" PRIu32
		                   " flags=0x%04" PRIx16 " name=%s",
		                   response->streamId, response->common.msgFlags,
		                   fileName ? fileName : "");
		free(fileName);
		return CHANNEL_RC_OK;
	}

	if (response->cbRequested == 0U ||
	    response->cbRequested > OHOS_CLIPBOARD_MAX_URI_IMAGE_BYTES)
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file content size rejected: stream=%" PRIu32
		                   " bytes=%" PRIu32 " name=%s",
		                   response->streamId, response->cbRequested, fileName ? fileName : "");
		free(fileName);
		return CHANNEL_RC_OK;
	}

	if (expectedBytes != 0U && response->cbRequested != expectedBytes)
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file content size mismatch: stream=%" PRIu32
		                   " expected=%" PRIu32 " got=%" PRIu32 " name=%s",
		                   response->streamId, expectedBytes, response->cbRequested,
		                   fileName ? fileName : "");

	if (!ohos_clipboard_image_buffer_to_bgra(response->requestedData, response->cbRequested,
	                                         &bgra, &imageWidth, &imageHeight, error,
	                                         sizeof(error)) ||
	    !ohos_clipboard_validate_local_image_size(imageWidth, imageHeight, error, sizeof(error)))
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file image decode failed: index=%" PRIu32
		                   " bytes=%" PRIu32 " name=%s error=%s",
		                   listIndex, response->cbRequested, fileName ? fileName : "",
		                   error[0] ? error : "unknown");
		free(bgra);
		free(fileName);
		return CHANNEL_RC_OK;
	}

	error[0] = '\0';
	cachedFileUri = ohos_clipboard_cache_remote_file(clipboard, fileName, response->streamId,
	                                                 response->requestedData,
	                                                 response->cbRequested, error,
	                                                 sizeof(error));
	if (!cachedFileUri)
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file image sandbox cache failed: name=%s error=%s",
		                   fileName ? fileName : "", error[0] ? error : "unknown");

	error[0] = '\0';
	if (!ohos_clipboard_write_pixelmap_with_uri(clipboard, bgra, imageWidth, imageHeight,
	                                            response->cbRequested, cachedFileUri, error,
	                                            sizeof(error)))
	{
		ohos_clipboard_log(clipboard,
		                   "HarmonyOS Pasteboard remote file pixelmap write failed: %s",
		                   error);
		free(cachedFileUri);
		free(bgra);
		free(fileName);
		return ERROR_INTERNAL_ERROR;
	}

	++clipboard->remoteResponseCount;
	ohos_clipboard_log(clipboard,
	                   "cliprdr remote file image copied to HarmonyOS Pasteboard: %" PRIu32
	                   "x%" PRIu32 " bytes=%" PRIu32 " name=%s sandboxUri=%s",
	                   imageWidth, imageHeight, response->cbRequested, fileName ? fileName : "",
	                   cachedFileUri ? cachedFileUri : "none");
	free(cachedFileUri);
	free(bgra);
	free(fileName);
	return CHANNEL_RC_OK;
}
