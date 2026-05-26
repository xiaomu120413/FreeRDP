/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS cliprdr <-> Pasteboard bridge
 */

#include "ohos_clipboard_internal.h"

FREERDP_LOCAL void* ohos_clipboard_image_worker_main(void* arg)
{
	freerdpOhosClipboard* clipboard = (freerdpOhosClipboard*)arg;

	for (;;)
	{
		UINT64 serial = 0;
		BYTE* bgra = NULL;
		BYTE* dib = NULL;
		BYTE* dibv5 = NULL;
		BYTE* encoded = NULL;
		UINT32 width = 0;
		UINT32 height = 0;
		UINT32 dibSize = 0;
		UINT32 dibv5Size = 0;
		UINT32 encodedSize = 0;
		UINT32 encodedFormatId = 0;
		UINT32 sourceBytes = 0;
		BOOL ok = FALSE;
		BOOL stored = FALSE;
		char error[160] = { 0 };

		pthread_mutex_lock(&clipboard->lock);
		while (!clipboard->imageWorkerStop && !clipboard->imageExportPending)
			pthread_cond_wait(&clipboard->imageCond, &clipboard->lock);
		if (clipboard->imageWorkerStop)
		{
			pthread_mutex_unlock(&clipboard->lock);
			break;
		}

		serial = clipboard->imageExportRequestedSerial;
		clipboard->imageExportPending = FALSE;
		clipboard->imageExportBusy = TRUE;
		clipboard->imageExportBusySerial = serial;
		pthread_mutex_unlock(&clipboard->lock);

		if (ohos_clipboard_read_uri_image_with_source(
		        clipboard, &bgra, &width, &height, &encoded, &encodedSize, &encodedFormatId,
		        error, sizeof(error)) &&
		    ohos_clipboard_validate_local_image_size(width, height, error, sizeof(error)))
		{
			const size_t imageBytes = (size_t)width * (size_t)height * 4U;
			sourceBytes = clipboard->lastImageBytes;
			if (sourceBytes == 0U || sourceBytes > OHOS_CLIPBOARD_MAX_LOCAL_IMAGE_BYTES)
				sourceBytes = (imageBytes <= UINT32_MAX) ? (UINT32)imageBytes : 0U;

			dib = ohos_clipboard_bgra_to_dib(bgra, width, height, &dibSize, error,
			                                 sizeof(error));
			if (dib)
				dibv5 = ohos_clipboard_bgra_to_dibv5(bgra, width, height, &dibv5Size,
				                                     error, sizeof(error));
			ok = (dib && dibv5);
		}

		pthread_mutex_lock(&clipboard->lock);
		clipboard->imageExportBusy = FALSE;
		clipboard->imageExportBusySerial = 0;
		if (!clipboard->imageWorkerStop && serial == clipboard->localClipboardSerial)
		{
			ohos_clipboard_clear_image_cache_locked(clipboard);
			if (ok)
			{
				clipboard->cachedImageDib = dib;
				clipboard->cachedImageDibSize = dibSize;
				clipboard->cachedImageDibV5 = dibv5;
				clipboard->cachedImageDibV5Size = dibv5Size;
				clipboard->cachedImageEncoded = encoded;
				clipboard->cachedImageEncodedSize = encodedSize;
				clipboard->cachedImageEncodedFormatId = encodedFormatId;
				clipboard->cachedImageWidth = width;
				clipboard->cachedImageHeight = height;
				clipboard->cachedImageSourceBytes = sourceBytes;
				clipboard->cachedImageSerial = serial;
				clipboard->imageCacheReady = TRUE;
				stored = TRUE;
				dib = NULL;
				dibv5 = NULL;
				encoded = NULL;
			}
			else
			{
				clipboard->failedImageSerial = serial;
				clipboard->imageExportFailed = TRUE;
				(void)snprintf(clipboard->lastImageExportError,
				               sizeof(clipboard->lastImageExportError), "%s",
				               error[0] ? error : "local clipboard image export failed");
			}
		}
		pthread_cond_broadcast(&clipboard->imageCond);
		pthread_mutex_unlock(&clipboard->lock);

		if (stored)
			ohos_clipboard_log(clipboard,
			                   "HarmonyOS Pasteboard image cached for cliprdr: %" PRIu32
			                   "x%" PRIu32 " dib=%" PRIu32 " dibv5=%" PRIu32
			                   " encoded=%s/%" PRIu32,
			                   width, height, dibSize, dibv5Size,
			                   ohos_clipboard_image_format_name_from_id(encodedFormatId)
			                       ? ohos_clipboard_image_format_name_from_id(encodedFormatId)
			                       : "none",
			                   encodedSize);
		else if (ok)
			ohos_clipboard_log(clipboard,
			                   "HarmonyOS Pasteboard stale image export discarded: serial=%" PRIu64,
			                   serial);
		else
			ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard image cache failed: %s",
			                   error[0] ? error : "unknown error");

		free(bgra);
		free(dib);
		free(dibv5);
		free(encoded);
	}

	return NULL;
}

FREERDP_LOCAL BOOL ohos_clipboard_start_image_worker_locked(freerdpOhosClipboard* clipboard)
{
	if (!clipboard || !clipboard->imageCondInitialized)
		return FALSE;
	if (clipboard->imageWorkerStarted)
		return TRUE;

	clipboard->imageWorkerStop = FALSE;
	if (pthread_create(&clipboard->imageWorkerThread, NULL, ohos_clipboard_image_worker_main,
	                   clipboard) != 0)
	{
		(void)snprintf(clipboard->lastImageExportError,
		               sizeof(clipboard->lastImageExportError),
		               "local clipboard image worker start failed");
		clipboard->imageExportFailed = TRUE;
		clipboard->failedImageSerial = clipboard->localClipboardSerial;
		return FALSE;
	}

	clipboard->imageWorkerStarted = TRUE;
	return TRUE;
}

FREERDP_LOCAL BOOL ohos_clipboard_schedule_image_export_locked(freerdpOhosClipboard* clipboard,
                                                        UINT64 serial)
{
	if (!clipboard || !ohos_clipboard_start_image_worker_locked(clipboard))
		return FALSE;
	if (clipboard->imageCacheReady && clipboard->cachedImageSerial == serial)
		return TRUE;
	if (clipboard->imageExportFailed && clipboard->failedImageSerial == serial)
		return FALSE;
	if (clipboard->imageExportPending && clipboard->imageExportRequestedSerial == serial)
		return TRUE;
	if (clipboard->imageExportBusy && clipboard->imageExportBusySerial == serial)
		return TRUE;

	clipboard->imageExportPending = TRUE;
	clipboard->imageExportRequestedSerial = serial;
	clipboard->imageExportFailed = FALSE;
	clipboard->lastImageExportError[0] = '\0';
	pthread_cond_signal(&clipboard->imageCond);
	return TRUE;
}

FREERDP_LOCAL BOOL ohos_clipboard_schedule_image_export(freerdpOhosClipboard* clipboard,
                                                 const char* reason)
{
	if (!clipboard)
		return FALSE;

	pthread_mutex_lock(&clipboard->lock);
	const UINT64 serial = clipboard->localClipboardSerial;
	const BOOL scheduled = ohos_clipboard_schedule_image_export_locked(clipboard, serial);
	pthread_mutex_unlock(&clipboard->lock);

	if (scheduled)
		ohos_clipboard_log(clipboard,
		                   "HarmonyOS Pasteboard image export scheduled: serial=%" PRIu64
		                   " reason=%s",
		                   serial, reason ? reason : "unknown");
	return scheduled;
}

FREERDP_LOCAL void ohos_clipboard_note_local_clipboard_changed(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	pthread_mutex_lock(&clipboard->lock);
	clipboard->localClipboardSerial++;
	clipboard->imageExportPending = FALSE;
	ohos_clipboard_clear_image_cache_locked(clipboard);
	pthread_cond_broadcast(&clipboard->imageCond);
	pthread_mutex_unlock(&clipboard->lock);
}

FREERDP_LOCAL void ohos_clipboard_stop_image_worker(freerdpOhosClipboard* clipboard)
{
	BOOL joinWorker = FALSE;

	if (!clipboard || !clipboard->imageCondInitialized)
		return;

	pthread_mutex_lock(&clipboard->lock);
	if (clipboard->imageWorkerStarted)
	{
		clipboard->imageWorkerStop = TRUE;
		clipboard->imageExportPending = TRUE;
		pthread_cond_broadcast(&clipboard->imageCond);
		joinWorker = TRUE;
	}
	pthread_mutex_unlock(&clipboard->lock);

	if (joinWorker)
		pthread_join(clipboard->imageWorkerThread, NULL);

	pthread_mutex_lock(&clipboard->lock);
	clipboard->imageWorkerStarted = FALSE;
	clipboard->imageWorkerStop = FALSE;
	clipboard->imageExportPending = FALSE;
	clipboard->imageExportBusy = FALSE;
	clipboard->imageExportRequestedSerial = 0;
	clipboard->imageExportBusySerial = 0;
	ohos_clipboard_clear_image_cache_locked(clipboard);
	pthread_mutex_unlock(&clipboard->lock);
}

FREERDP_LOCAL BOOL ohos_clipboard_make_realtime_deadline(UINT64 timeoutMs, struct timespec* deadline)
{
	if (!deadline || clock_gettime(CLOCK_REALTIME, deadline) != 0)
		return FALSE;

	deadline->tv_sec += (time_t)(timeoutMs / 1000ULL);
	deadline->tv_nsec += (long)((timeoutMs % 1000ULL) * 1000000ULL);
	if (deadline->tv_nsec >= 1000000000L)
	{
		deadline->tv_sec++;
		deadline->tv_nsec -= 1000000000L;
	}
	return TRUE;
}
