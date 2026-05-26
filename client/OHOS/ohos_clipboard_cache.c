/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS cliprdr <-> Pasteboard bridge
 */

#include "ohos_clipboard_internal.h"

FREERDP_LOCAL BYTE* ohos_clipboard_read_local_file(const char* path, UINT32 maxBytes, UINT32* outSize,
                                            char* errorBuffer, size_t errorBufferSize)
{
	if (!outSize)
		return NULL;
	*outSize = 0;

	if (!path || path[0] == '\0')
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize, "empty image URI path");
		return NULL;
	}

	FILE* file = fopen(path, "rb");
	if (!file)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "open image URI path failed: %s", strerror(errno));
		return NULL;
	}

	if (fseek(file, 0, SEEK_END) != 0)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "seek image URI path failed");
		fclose(file);
		return NULL;
	}

	const long size = ftell(file);
	if (size <= 0 || (unsigned long)size > maxBytes || (unsigned long)size > UINT32_MAX)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "image URI file size is invalid: %ld", size);
		fclose(file);
		return NULL;
	}

	if (fseek(file, 0, SEEK_SET) != 0)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "rewind image URI path failed");
		fclose(file);
		return NULL;
	}

	BYTE* buffer = (BYTE*)malloc((size_t)size);
	if (!buffer)
	{
		fclose(file);
		return NULL;
	}

	const size_t read = fread(buffer, 1U, (size_t)size, file);
	fclose(file);
	if (read != (size_t)size)
	{
		free(buffer);
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "read image URI path failed");
		return NULL;
	}

	*outSize = (UINT32)size;
	return buffer;
}

FREERDP_LOCAL BYTE* ohos_clipboard_read_local_file_range(const char* path, UINT64 offset,
                                                  UINT32 requestedBytes, UINT32* outSize,
                                                  char* errorBuffer, size_t errorBufferSize)
{
	if (!outSize)
		return NULL;
	*outSize = 0;

	if (!path || path[0] == '\0' || requestedBytes == 0U)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "invalid local file range request");
		return NULL;
	}
	if (offset > (UINT64)LONG_MAX)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "local file range offset too large");
		return NULL;
	}

	FILE* file = fopen(path, "rb");
	if (!file)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "open local clipboard file failed: %s", strerror(errno));
		return NULL;
	}

	if (fseek(file, (long)offset, SEEK_SET) != 0)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "seek local clipboard file failed");
		fclose(file);
		return NULL;
	}

	BYTE* data = (BYTE*)malloc(requestedBytes);
	if (!data)
	{
		fclose(file);
		return NULL;
	}

	const size_t read = fread(data, 1U, requestedBytes, file);
	fclose(file);
	if (read == 0U)
	{
		free(data);
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "read local clipboard file range failed");
		return NULL;
	}

	*outSize = (UINT32)read;
	return data;
}

FREERDP_LOCAL BOOL ohos_clipboard_read_local_file_signature(const char* path, BYTE* signature,
                                                     UINT32 signatureSize, UINT32* outSize)
{
	if (outSize)
		*outSize = 0;
	if (!path || !signature || signatureSize == 0U)
		return FALSE;

	FILE* file = fopen(path, "rb");
	if (!file)
		return FALSE;

	const size_t read = fread(signature, 1U, signatureSize, file);
	fclose(file);
	if (read == 0U)
		return FALSE;

	if (outSize)
		*outSize = (UINT32)read;
	return TRUE;
}

FREERDP_LOCAL UINT64 ohos_clipboard_now_ms(void)
{
	struct timespec ts = { 0 };
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return ((UINT64)ts.tv_sec * 1000ULL) + ((UINT64)ts.tv_nsec / 1000000ULL);
}

FREERDP_LOCAL char* ohos_clipboard_join_path(const char* left, const char* right)
{
	if (!left || !right)
		return NULL;

	size_t leftLength = strlen(left);
	while (leftLength > 1U &&
	       (left[leftLength - 1U] == '/' || left[leftLength - 1U] == '\\'))
		leftLength--;

	while (*right == '/' || *right == '\\')
		right++;
	const size_t rightLength = strlen(right);
	const BOOL needSeparator =
	    (leftLength > 0U && left[leftLength - 1U] != '/' && left[leftLength - 1U] != '\\');
	const size_t totalLength = leftLength + (needSeparator ? 1U : 0U) + rightLength;
	char* path = (char*)calloc(totalLength + 1U, sizeof(char));
	if (!path)
		return NULL;

	size_t offset = 0;
	if (leftLength > 0U)
	{
		memcpy(path, left, leftLength);
		offset += leftLength;
	}
	if (needSeparator)
		path[offset++] = '/';
	if (rightLength > 0U)
		memcpy(path + offset, right, rightLength);
	return path;
}

FREERDP_LOCAL BOOL ohos_clipboard_ensure_directory(freerdpOhosClipboard* clipboard, const char* path,
                                            char* errorBuffer, size_t errorBufferSize)
{
	if (!path || path[0] == '\0')
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "empty clipboard cache directory");
		return FALSE;
	}

	if (mkdir(path, 0700) == 0)
		return TRUE;
	if (errno == EEXIST)
	{
		struct stat st = { 0 };
		if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
			return TRUE;
	}

	ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
	                         "create clipboard cache directory failed: %s", strerror(errno));
	return FALSE;
}

FREERDP_LOCAL BOOL ohos_clipboard_is_safe_cache_file_char(unsigned char c)
{
	return isalnum(c) || c == '.' || c == '_' || c == '-';
}

FREERDP_LOCAL char* ohos_clipboard_sanitize_cache_file_name(const char* fileName)
{
	const char* baseName = fileName ? fileName : "";
	for (const char* cursor = baseName; *cursor; cursor++)
	{
		if (*cursor == '/' || *cursor == '\\')
			baseName = cursor + 1U;
	}
	if (baseName[0] == '\0')
		baseName = "clipboard-image";

	const char* extension = strrchr(baseName, '.');
	size_t extensionLength = 0;
	if (extension && extension != baseName)
	{
		extensionLength = strlen(extension);
		if (extensionLength > 16U)
		{
			extension = NULL;
			extensionLength = 0;
		}
	}

	const size_t stemLength = extension ? (size_t)(extension - baseName) : strlen(baseName);
	const size_t maxStemLength = 96U;
	const size_t maxNameLength = maxStemLength + 16U;
	char* safeName = (char*)calloc(maxNameLength + 1U, sizeof(char));
	if (!safeName)
		return NULL;

	size_t out = 0;
	BOOL hasStemChar = FALSE;
	for (size_t index = 0; index < stemLength && out < maxStemLength; index++)
	{
		const unsigned char c = (unsigned char)baseName[index];
		const char mapped = ohos_clipboard_is_safe_cache_file_char(c) ? (char)c : '_';
		if (isalnum(c))
			hasStemChar = TRUE;
		safeName[out++] = mapped;
	}
	if (!hasStemChar)
	{
		const char fallback[] = "clipboard-image";
		out = strlen(fallback);
		memcpy(safeName, fallback, out);
	}

	for (size_t index = 0; index < extensionLength && out < maxNameLength; index++)
	{
		const unsigned char c = (unsigned char)extension[index];
		safeName[out++] = ohos_clipboard_is_safe_cache_file_char(c) ? (char)c : '_';
	}
	safeName[out] = '\0';
	return safeName;
}

FREERDP_LOCAL char* ohos_clipboard_file_uri_from_path(const char* path)
{
	if (!path || path[0] == '\0')
		return NULL;

	const char prefix[] = "file://";
	const size_t prefixLength = strlen(prefix);
	const size_t pathLength = strlen(path);
	char* uri = (char*)calloc(prefixLength + pathLength + 1U, sizeof(char));
	if (!uri)
		return NULL;
	memcpy(uri, prefix, prefixLength);
	memcpy(uri + prefixLength, path, pathLength);
	return uri;
}

FREERDP_LOCAL char* ohos_clipboard_cache_remote_file(freerdpOhosClipboard* clipboard,
                                              const char* fileName, UINT32 streamId,
                                              const BYTE* data, UINT32 dataSize,
                                              char* errorBuffer, size_t errorBufferSize)
{
	char* cacheDir = NULL;
	char* safeName = NULL;
	char* cacheName = NULL;
	char* cachePath = NULL;
	char* cacheUri = NULL;
	FILE* file = NULL;

	if (!data || dataSize == 0U)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "empty remote file content");
		return NULL;
	}

	cacheDir =
	    ohos_clipboard_join_path(OHOS_CLIPBOARD_SANDBOX_FILES_DIR, OHOS_CLIPBOARD_CACHE_DIR_NAME);
	safeName = ohos_clipboard_sanitize_cache_file_name(fileName);
	if (!cacheDir || !safeName)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "clipboard sandbox cache allocation failed");
		goto fail;
	}

	if (!ohos_clipboard_ensure_directory(clipboard, cacheDir, errorBuffer, errorBufferSize))
		goto fail;

	const UINT64 cacheTime = ohos_clipboard_now_ms();
	const int cacheNameLength =
	    snprintf(NULL, 0, "remote_%08" PRIx32 "_%" PRIu64 "_%s", streamId, cacheTime,
	             safeName);
	if (cacheNameLength <= 0)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "clipboard sandbox cache name failed");
		goto fail;
	}
	cacheName = (char*)calloc((size_t)cacheNameLength + 1U, sizeof(char));
	if (!cacheName)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "clipboard sandbox cache name allocation failed");
		goto fail;
	}
	(void)snprintf(cacheName, (size_t)cacheNameLength + 1U,
	               "remote_%08" PRIx32 "_%" PRIu64 "_%s", streamId, cacheTime, safeName);

	cachePath = ohos_clipboard_join_path(cacheDir, cacheName);
	if (!cachePath)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "clipboard sandbox cache path allocation failed");
		goto fail;
	}

	file = fopen(cachePath, "wb");
	if (!file)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "open clipboard sandbox cache file failed: %s",
		                         strerror(errno));
		goto fail;
	}
	const size_t written = fwrite(data, 1U, dataSize, file);
	if (written != dataSize)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "write clipboard sandbox cache file failed");
		goto fail;
	}
	if (fclose(file) != 0)
	{
		file = NULL;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "close clipboard sandbox cache file failed: %s",
		                         strerror(errno));
		goto fail;
	}
	file = NULL;

	cacheUri = ohos_clipboard_file_uri_from_path(cachePath);
	if (!cacheUri)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "clipboard sandbox cache uri allocation failed");
		goto fail;
	}

	free(cacheDir);
	free(safeName);
	free(cacheName);
	free(cachePath);
	return cacheUri;

fail:
	if (file)
		fclose(file);
	if (cachePath)
		remove(cachePath);
	free(cacheDir);
	free(safeName);
	free(cacheName);
	free(cachePath);
	return NULL;
}

FREERDP_LOCAL void ohos_clipboard_clear_image_cache_locked(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	free(clipboard->cachedImageDib);
	free(clipboard->cachedImageDibV5);
	free(clipboard->cachedImageEncoded);
	clipboard->cachedImageDib = NULL;
	clipboard->cachedImageDibSize = 0;
	clipboard->cachedImageDibV5 = NULL;
	clipboard->cachedImageDibV5Size = 0;
	clipboard->cachedImageEncoded = NULL;
	clipboard->cachedImageEncodedSize = 0;
	clipboard->cachedImageEncodedFormatId = 0;
	clipboard->cachedImageWidth = 0;
	clipboard->cachedImageHeight = 0;
	clipboard->cachedImageSourceBytes = 0;
	clipboard->imageCacheReady = FALSE;
	clipboard->imageExportFailed = FALSE;
	clipboard->lastImageExportError[0] = '\0';
}

FREERDP_LOCAL void ohos_clipboard_clear_remote_file_transfer_locked(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	free(clipboard->remoteFileName);
	clipboard->remoteFileName = NULL;
	clipboard->remoteFileTransferActive = FALSE;
	clipboard->remoteFileStreamId = 0;
	clipboard->remoteFileListIndex = 0;
	clipboard->remoteFileExpectedBytes = 0;
}

FREERDP_LOCAL BYTE* ohos_clipboard_copy_cached_image_locked(freerdpOhosClipboard* clipboard,
                                                     UINT32 formatId, UINT32* outSize)
{
	const BYTE* source = NULL;
	UINT32 size = 0;

	if (!clipboard || !outSize || !clipboard->imageCacheReady)
		return NULL;
	*outSize = 0;

	if (formatId == CF_DIB)
	{
		source = clipboard->cachedImageDib;
		size = clipboard->cachedImageDibSize;
	}
	else if (formatId == CF_DIBV5)
	{
		source = clipboard->cachedImageDibV5;
		size = clipboard->cachedImageDibV5Size;
	}
	else if (formatId == clipboard->cachedImageEncodedFormatId)
	{
		source = clipboard->cachedImageEncoded;
		size = clipboard->cachedImageEncodedSize;
	}

	if (!source || size == 0U)
		return NULL;

	BYTE* copy = (BYTE*)malloc(size);
	if (!copy)
		return NULL;
	memcpy(copy, source, size);
	*outSize = size;
	return copy;
}

FREERDP_LOCAL BOOL ohos_clipboard_get_cached_image_data(freerdpOhosClipboard* clipboard,
                                                 UINT32 formatId, BYTE** outData,
                                                 UINT32* outSize, char* errorBuffer,
                                                 size_t errorBufferSize)
{
	if (!outData || !outSize)
		return FALSE;
	*outData = NULL;
	*outSize = 0;

	if (!clipboard || (formatId != CF_DIB && formatId != CF_DIBV5 &&
	                   formatId != OHOS_CLIPBOARD_FORMAT_IMAGE_PNG &&
	                   formatId != OHOS_CLIPBOARD_FORMAT_IMAGE_JPEG &&
	                   formatId != OHOS_CLIPBOARD_FORMAT_IMAGE_WEBP))
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "unsupported local image format=%" PRIu32, formatId);
		return FALSE;
	}

	struct timespec deadline = { 0 };
	if (!ohos_clipboard_make_realtime_deadline(OHOS_CLIPBOARD_IMAGE_REQUEST_TIMEOUT_MS,
	                                           &deadline))
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "local clipboard image timeout setup failed");
		return FALSE;
	}

	pthread_mutex_lock(&clipboard->lock);
	const UINT64 serial = clipboard->localClipboardSerial;
	if (!ohos_clipboard_schedule_image_export_locked(clipboard, serial))
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "%s",
		                         clipboard->lastImageExportError[0]
		                             ? clipboard->lastImageExportError
		                             : "local clipboard image worker unavailable");
		pthread_mutex_unlock(&clipboard->lock);
		return FALSE;
	}

	for (;;)
	{
		if (clipboard->imageCacheReady && clipboard->cachedImageSerial == serial)
		{
			*outData = ohos_clipboard_copy_cached_image_locked(clipboard, formatId, outSize);
			if (*outData)
			{
				pthread_mutex_unlock(&clipboard->lock);
				return TRUE;
			}
			ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
			                         "local clipboard image cache copy failed");
			pthread_mutex_unlock(&clipboard->lock);
			return FALSE;
		}

		if (clipboard->imageExportFailed && clipboard->failedImageSerial == serial)
		{
			ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "%s",
			                         clipboard->lastImageExportError[0]
			                             ? clipboard->lastImageExportError
			                             : "local clipboard image export failed");
			pthread_mutex_unlock(&clipboard->lock);
			return FALSE;
		}

		if (serial != clipboard->localClipboardSerial)
		{
			ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
			                         "local clipboard changed while exporting image");
			pthread_mutex_unlock(&clipboard->lock);
			return FALSE;
		}

		const int rc = pthread_cond_timedwait(&clipboard->imageCond, &clipboard->lock,
		                                      &deadline);
		if (rc == ETIMEDOUT)
		{
			ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
			                         "local clipboard image export timed out after %" PRIu64
			                         "ms",
			                         (UINT64)OHOS_CLIPBOARD_IMAGE_REQUEST_TIMEOUT_MS);
			pthread_mutex_unlock(&clipboard->lock);
			return FALSE;
		}
		if (rc != 0)
		{
			ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
			                         "local clipboard image wait failed: %d", rc);
			pthread_mutex_unlock(&clipboard->lock);
			return FALSE;
		}
	}
}
