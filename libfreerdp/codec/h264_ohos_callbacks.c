/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS AVCodec H.264 decoder
 *
 * Licensed under the Apache License, Version 2.0.
 */

#include "h264_ohos_avcodec_internal.h"

FREERDP_LOCAL UINT64 ohos_avcodec_now_ns(void)
{
	struct timespec now = { 0 };
	clock_gettime(CLOCK_MONOTONIC, &now);
	return ((UINT64)now.tv_sec * 1000000000ULL) + (UINT64)now.tv_nsec;
}

FREERDP_LOCAL void ohos_avcodec_make_deadline(struct timespec* deadline, UINT32 timeoutMs)
{
	clock_gettime(CLOCK_REALTIME, deadline);
	deadline->tv_sec += timeoutMs / 1000;
	deadline->tv_nsec += (long)(timeoutMs % 1000) * 1000000L;
	if (deadline->tv_nsec >= 1000000000L)
	{
		deadline->tv_sec++;
		deadline->tv_nsec -= 1000000000L;
	}
}

FREERDP_LOCAL BOOL ohos_avcodec_pop_input(H264_CONTEXT_OHOS_AVCODEC* sys, uint32_t* index,
                                          OH_AVBuffer** buffer)
{
	if (!sys || !index || !buffer || (sys->inputCount == 0))
		return FALSE;

	*index = sys->inputQueue[sys->inputHead].index;
	*buffer = sys->inputQueue[sys->inputHead].buffer;
	sys->inputQueue[sys->inputHead].buffer = NULL;
	sys->inputHead = (sys->inputHead + 1) % OHOS_AVCODEC_INPUT_QUEUE_LENGTH;
	sys->inputCount--;
	return TRUE;
}

FREERDP_LOCAL BOOL ohos_avcodec_wait_for_input(H264_CONTEXT_OHOS_AVCODEC* sys,
                                               uint32_t* index, OH_AVBuffer** buffer)
{
	struct timespec deadline = { 0 };
	int rc = 0;

	if (!sys || !index || !buffer)
		return FALSE;

	ohos_avcodec_make_deadline(&deadline, OHOS_AVCODEC_INPUT_WAIT_MS);
	while ((sys->inputCount == 0) && (sys->asyncError == 0))
	{
		rc = pthread_cond_timedwait(&sys->cond, &sys->lock, &deadline);
		if (rc == ETIMEDOUT)
			break;
	}

	if (sys->asyncError != 0)
		return FALSE;

	return ohos_avcodec_pop_input(sys, index, buffer);
}

FREERDP_LOCAL void ohos_avcodec_reset_async_state(H264_CONTEXT_OHOS_AVCODEC* sys)
{
	if (!sys || !sys->primitivesReady)
		return;

	pthread_mutex_lock(&sys->lock);
	sys->inputHead = 0;
	sys->inputTail = 0;
	sys->inputCount = 0;
	sys->asyncError = 0;
	sys->lastSurfaceRenderNs = 0;
	pthread_cond_broadcast(&sys->cond);
	pthread_mutex_unlock(&sys->lock);
}

FREERDP_LOCAL void ohos_avcodec_on_error(OH_AVCodec* codec, int32_t errorCode, void* userData)
{
	H264_CONTEXT_OHOS_AVCODEC* sys = (H264_CONTEXT_OHOS_AVCODEC*)userData;
	WINPR_UNUSED(codec);

	if (!sys || !sys->primitivesReady)
		return;

	pthread_mutex_lock(&sys->lock);
	sys->asyncError = errorCode ? errorCode : AV_ERR_UNKNOWN;
	pthread_cond_broadcast(&sys->cond);
	pthread_mutex_unlock(&sys->lock);
	ohos_avcodec_record_progress(sys);

	WLog_Print(sys->log, WLOG_WARN, "OHOS AVCodec async error=%d", errorCode);
}

FREERDP_LOCAL void ohos_avcodec_on_stream_changed(OH_AVCodec* codec, OH_AVFormat* format, void* userData)
{
	H264_CONTEXT_OHOS_AVCODEC* sys = (H264_CONTEXT_OHOS_AVCODEC*)userData;
	WINPR_UNUSED(codec);
	WINPR_UNUSED(format);

	if (!sys || !sys->primitivesReady)
		return;

	pthread_mutex_lock(&sys->lock);
	pthread_cond_broadcast(&sys->cond);
	pthread_mutex_unlock(&sys->lock);
}

FREERDP_LOCAL void ohos_avcodec_on_need_input_buffer(OH_AVCodec* codec, uint32_t index,
                                              OH_AVBuffer* buffer, void* userData)
{
	H264_CONTEXT_OHOS_AVCODEC* sys = (H264_CONTEXT_OHOS_AVCODEC*)userData;
	WINPR_UNUSED(codec);

	if (!sys || !sys->primitivesReady || !buffer)
		return;

	pthread_mutex_lock(&sys->lock);
	if (sys->inputCount < OHOS_AVCODEC_INPUT_QUEUE_LENGTH)
	{
		sys->inputQueue[sys->inputTail].index = index;
		sys->inputQueue[sys->inputTail].buffer = buffer;
		sys->inputTail = (sys->inputTail + 1) % OHOS_AVCODEC_INPUT_QUEUE_LENGTH;
		sys->inputCount++;
		pthread_cond_broadcast(&sys->cond);
	}
	else
	{
		if (!sys->inputQueueFullLogged)
		{
			WLog_Print(sys->log, WLOG_WARN, "OHOS AVCodec input callback queue full");
			sys->inputQueueFullLogged = TRUE;
		}
		sys->asyncError = AV_ERR_INVALID_STATE;
		pthread_cond_broadcast(&sys->cond);
	}
	pthread_mutex_unlock(&sys->lock);
}

FREERDP_LOCAL void ohos_avcodec_on_new_output_buffer(OH_AVCodec* codec, uint32_t index,
                                              OH_AVBuffer* buffer, void* userData)
{
	OH_AVCodecBufferAttr attr = { 0 };
	H264_CONTEXT_OHOS_AVCODEC* sys = (H264_CONTEXT_OHOS_AVCODEC*)userData;
	OH_AVCodec* decoder = codec;
	BOOL surfaceMode = FALSE;
	BOOL renderSurface = FALSE;
	BOOL releaseSurface = FALSE;
	UINT64 surfaceFrame = 0;
	UINT64 surfaceCalls = 0;
	UINT64 surfaceDropped = 0;
	UINT64 surfaceFailed = 0;
	UINT64 surfaceInvalidations = 0;
	UINT64 surfaceGeneration = 0;
	UINT32 surfaceWidth = 0;
	UINT32 surfaceHeight = 0;
	BOOL surfaceStale = FALSE;
	WINPR_UNUSED(codec);

	if (!sys || !sys->primitivesReady || !buffer)
		return;

	if (OH_AVBuffer_GetBufferAttr(buffer, &attr) != AV_ERR_OK)
		ZeroMemory(&attr, sizeof(attr));

	if ((attr.flags & AVCODEC_BUFFER_FLAGS_EOS) == 0)
	{
		pthread_mutex_lock(&sys->lock);
		surfaceMode = sys->surfaceMode;
		decoder = sys->decoder;
		if (surfaceMode)
		{
			if (!ohos_avcodec_surface_config_current(sys->width, sys->height, sys))
			{
				sys->surfaceInvalidations++;
				sys->asyncError = AV_ERR_INVALID_STATE;
				surfaceStale = TRUE;
				releaseSurface = TRUE;
			}
			else
			{
				const UINT64 nowNs = ohos_avcodec_now_ns();
				if ((sys->lastSurfaceRenderNs == 0) ||
				    (nowNs - sys->lastSurfaceRenderNs >= OHOS_AVCODEC_SURFACE_RENDER_INTERVAL_NS))
				{
					sys->lastSurfaceRenderNs = nowNs;
					sys->decodedFrames++;
					renderSurface = TRUE;
					surfaceFrame = sys->decodedFrames;
				}
				else
				{
					sys->droppedOutputFrames++;
					releaseSurface = TRUE;
				}
			}
			surfaceCalls = sys->decodeCalls;
			surfaceDropped = sys->droppedOutputFrames;
			surfaceFailed = sys->failedFrames;
			surfaceInvalidations = sys->surfaceInvalidations;
			surfaceGeneration = sys->surfaceGeneration;
			surfaceWidth = sys->width;
			surfaceHeight = sys->height;
			pthread_cond_broadcast(&sys->cond);
		}
		else
		{
			sys->failedFrames++;
			surfaceFailed = sys->failedFrames;
			pthread_cond_broadcast(&sys->cond);
		}
		pthread_mutex_unlock(&sys->lock);
	}
	else
	{
		pthread_mutex_lock(&sys->lock);
		surfaceMode = sys->surfaceMode;
		decoder = sys->decoder;
		releaseSurface = surfaceMode;
		pthread_mutex_unlock(&sys->lock);
	}

	if (surfaceMode)
	{
		OH_AVErrCode rc = AV_ERR_OK;
		if (!decoder)
			rc = AV_ERR_INVALID_VAL;
		else if (renderSurface)
			rc = OH_VideoDecoder_RenderOutputBuffer(decoder, index);
		else if (releaseSurface)
			rc = OH_VideoDecoder_FreeOutputBuffer(decoder, index);

		if (rc != AV_ERR_OK)
		{
			pthread_mutex_lock(&sys->lock);
			sys->failedFrames++;
			surfaceFailed = sys->failedFrames;
			pthread_mutex_unlock(&sys->lock);
			if (sys->failedFrames <= 3)
				WLog_Print(sys->log, WLOG_WARN,
				           "OHOS AVCodec surface output release failed rc=%d render=%d",
				           (int)rc, renderSurface ? 1 : 0);
		}
		else if (renderSurface &&
		         ((surfaceFrame <= 3) || ((surfaceFrame % 60) == 0)))
		{
			WLog_Print(sys->log, WLOG_INFO,
			           "OHOS AVCodec surface output rendered frame=%" PRIu64
			           " calls=%" PRIu64 " dropped=%" PRIu64 " failed=%" PRIu64
			           " size=%ux%u attrSize=%d offset=%d flags=0x%x pts=%" PRId64,
			           surfaceFrame, surfaceCalls, surfaceDropped, surfaceFailed, surfaceWidth,
			           surfaceHeight, attr.size, attr.offset, attr.flags, attr.pts);
		}
		else if (releaseSurface &&
		         ((surfaceStale &&
		           ((surfaceInvalidations <= 3) || ((surfaceInvalidations % 120) == 0))) ||
		          (!surfaceStale && ((surfaceDropped <= 3) || ((surfaceDropped % 120) == 0)))))
		{
			if (surfaceStale)
			{
				WLog_Print(sys->log, WLOG_WARN,
				           "OHOS AVCodec skipped stale surface output generation=%" PRIu64
				           " invalidations=%" PRIu64 " calls=%" PRIu64 " size=%ux%u",
				           surfaceGeneration, surfaceInvalidations, surfaceCalls, surfaceWidth,
				           surfaceHeight);
			}
			else
			{
				WLog_Print(sys->log, WLOG_DEBUG,
				           "OHOS AVCodec surface output dropped frame candidate dropped=%" PRIu64
				           " calls=%" PRIu64 " size=%ux%u",
				           surfaceDropped, surfaceCalls, surfaceWidth, surfaceHeight);
			}
		}
		ohos_avcodec_record_progress(sys);
	}
	else
	{
		if (sys->decoder)
			OH_VideoDecoder_FreeOutputBuffer(sys->decoder, index);
	}
}
