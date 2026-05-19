/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS AVCodec H.264 decoder
 *
 * Licensed under the Apache License, Version 2.0.
 */

#include <freerdp/config.h>

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <winpr/crt.h>
#include <winpr/interlocked.h>
#include <winpr/wlog.h>

#include <freerdp/log.h>

#include "h264.h"

#if defined(WITH_OHOS_AVCODEC)
#include <multimedia/player_framework/native_avbuffer.h>
#include <multimedia/player_framework/native_avbuffer_info.h>
#include <multimedia/player_framework/native_avcapability.h>
#include <multimedia/player_framework/native_avcodec_base.h>
#include <multimedia/player_framework/native_avcodec_videodecoder.h>
#include <multimedia/player_framework/native_avformat.h>
#include <native_window/external_window.h>
#endif

#define TAG FREERDP_TAG("codec.ohos-avcodec")

#define OHOS_AVCODEC_INPUT_QUEUE_LENGTH 32
#define OHOS_AVCODEC_INPUT_WAIT_MS 50
#define OHOS_AVCODEC_SURFACE_RENDER_INTERVAL_NS 16666667ULL

typedef struct
{
	uint32_t index;
	OH_AVBuffer* buffer;
} OHOS_AVCODEC_INPUT_SLOT;

typedef struct
{
	OH_AVCodec* decoder;
	OHNativeWindow* outputSurface;
	BOOL started;
	BOOL surfaceMode;
	BOOL primitivesReady;
	UINT64 surfaceGeneration;
	UINT32 width;
	UINT32 height;
	UINT64 decodeCalls;
	UINT64 decodedFrames;
	UINT64 noOutputFrames;
	UINT64 failedFrames;
	UINT64 inputWaitTimeouts;
	UINT64 droppedOutputFrames;
	UINT64 surfaceRefreshes;
	UINT64 surfaceInvalidations;
	UINT64 lastSurfaceRenderNs;
	BOOL inputQueueFullLogged;
	int32_t asyncError;
	wLog* log;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	OHOS_AVCODEC_INPUT_SLOT inputQueue[OHOS_AVCODEC_INPUT_QUEUE_LENGTH];
	UINT32 inputHead;
	UINT32 inputTail;
	UINT32 inputCount;
} H264_CONTEXT_OHOS_AVCODEC;

static volatile LONG g_ohos_avcodec_surface_active_logged = 0;
static pthread_mutex_t g_ohos_avcodec_surface_lock = PTHREAD_MUTEX_INITIALIZER;
static OHNativeWindow* g_ohos_avcodec_surface_window = NULL;
static UINT32 g_ohos_avcodec_surface_width = 0;
static UINT32 g_ohos_avcodec_surface_height = 0;
static BOOL g_ohos_avcodec_surface_enabled = FALSE;
static UINT64 g_ohos_avcodec_surface_generation = 0;
static OHNativeWindow* g_ohos_avcodec_logged_surface_window = NULL;
static UINT32 g_ohos_avcodec_logged_surface_width = 0;
static UINT32 g_ohos_avcodec_logged_surface_height = 0;
static BOOL g_ohos_avcodec_logged_surface_enabled = FALSE;
static pfnH264OhosAvcodecFallbackCallback g_ohos_avcodec_fallback_callback = NULL;
static void* g_ohos_avcodec_fallback_callback_user_data = NULL;

typedef struct
{
	UINT64 decoderAttempts;
	UINT64 surfaceDecoderActive;
	UINT64 decodeCalls;
	UINT64 decodedFrames;
	UINT64 noOutputFrames;
	UINT64 failedFrames;
	UINT64 inputWaitTimeouts;
	UINT64 droppedOutputFrames;
	UINT64 fallbackRequests;
	UINT64 surfaceRefreshes;
	UINT64 surfaceInvalidations;
	UINT64 lastSurfaceGeneration;
	UINT32 lastWidth;
	UINT32 lastHeight;
	int32_t lastAsyncError;
	char lastFallbackReason[160];
} OHOS_AVCODEC_DIAGNOSTICS;

static pthread_mutex_t g_ohos_avcodec_stats_lock = PTHREAD_MUTEX_INITIALIZER;
static OHOS_AVCODEC_DIAGNOSTICS g_ohos_avcodec_stats = { 0 };

FREERDP_API BOOL freerdp_ohos_avcodec_set_output_surface(void* window, UINT32 width, UINT32 height,
                                                          BOOL enabled)
{
	BOOL changed = FALSE;
	const BOOL nextEnabled = enabled && (window != NULL) && (width > 0) && (height > 0);

	pthread_mutex_lock(&g_ohos_avcodec_surface_lock);
	changed = (g_ohos_avcodec_surface_enabled != nextEnabled) ||
	          (g_ohos_avcodec_surface_window != (nextEnabled ? (OHNativeWindow*)window : NULL)) ||
	          (g_ohos_avcodec_surface_width != (nextEnabled ? width : 0)) ||
	          (g_ohos_avcodec_surface_height != (nextEnabled ? height : 0));
	g_ohos_avcodec_surface_window = nextEnabled ? (OHNativeWindow*)window : NULL;
	g_ohos_avcodec_surface_width = nextEnabled ? width : 0;
	g_ohos_avcodec_surface_height = nextEnabled ? height : 0;
	g_ohos_avcodec_surface_enabled = nextEnabled;
	if (changed)
		g_ohos_avcodec_surface_generation++;
	changed = (g_ohos_avcodec_logged_surface_enabled != g_ohos_avcodec_surface_enabled) ||
	          (g_ohos_avcodec_logged_surface_window != g_ohos_avcodec_surface_window) ||
	          (g_ohos_avcodec_logged_surface_width != g_ohos_avcodec_surface_width) ||
	          (g_ohos_avcodec_logged_surface_height != g_ohos_avcodec_surface_height);
	if (changed)
	{
		g_ohos_avcodec_logged_surface_enabled = g_ohos_avcodec_surface_enabled;
		g_ohos_avcodec_logged_surface_window = g_ohos_avcodec_surface_window;
		g_ohos_avcodec_logged_surface_width = g_ohos_avcodec_surface_width;
		g_ohos_avcodec_logged_surface_height = g_ohos_avcodec_surface_height;
	}
	pthread_mutex_unlock(&g_ohos_avcodec_surface_lock);

	if (changed)
	{
		WLog_INFO(TAG, "OHOS AVCodec output surface %s window=%p size=%ux%u",
		          nextEnabled ? "enabled" : "disabled", window, nextEnabled ? width : 0,
		          nextEnabled ? height : 0);
	}
	return TRUE;
}

FREERDP_API BOOL freerdp_ohos_avcodec_set_fallback_callback(
    pfnH264OhosAvcodecFallbackCallback callback, void* userData)
{
	pthread_mutex_lock(&g_ohos_avcodec_surface_lock);
	g_ohos_avcodec_fallback_callback = callback;
	g_ohos_avcodec_fallback_callback_user_data = userData;
	pthread_mutex_unlock(&g_ohos_avcodec_surface_lock);
	return TRUE;
}

FREERDP_API const char* freerdp_ohos_avcodec_get_diagnostics(void)
{
	static char text[768];
	OHOS_AVCODEC_DIAGNOSTICS stats = { 0 };
	BOOL surfaceEnabled = FALSE;
	UINT32 surfaceWidth = 0;
	UINT32 surfaceHeight = 0;
	UINT64 configSurfaceGeneration = 0;

	pthread_mutex_lock(&g_ohos_avcodec_stats_lock);
	stats = g_ohos_avcodec_stats;
	pthread_mutex_unlock(&g_ohos_avcodec_stats_lock);

	pthread_mutex_lock(&g_ohos_avcodec_surface_lock);
	surfaceEnabled = g_ohos_avcodec_surface_enabled;
	surfaceWidth = g_ohos_avcodec_surface_width;
	surfaceHeight = g_ohos_avcodec_surface_height;
	configSurfaceGeneration = g_ohos_avcodec_surface_generation;
	pthread_mutex_unlock(&g_ohos_avcodec_surface_lock);

	(void)snprintf(text, sizeof(text),
	               "ohos avcodec: attempts=%" PRIu64 " active=surface:%" PRIu64
	               " calls=%" PRIu64 " rendered=%" PRIu64
	               " noOutput=%" PRIu64 " failed=%" PRIu64 " inputTimeout=%" PRIu64
	               " dropped=%" PRIu64 " fallbacks=%" PRIu64
	               " surfaceRefresh=%" PRIu64 " surfaceInvalid=%" PRIu64
	               " surfaceGeneration=config:%" PRIu64 ",decoder:%" PRIu64
	               " last=%ux%u asyncError=%d"
	               " outputSurface=%s:%ux%u reason=%s",
	               stats.decoderAttempts, stats.surfaceDecoderActive, stats.decodeCalls,
	               stats.decodedFrames, stats.noOutputFrames, stats.failedFrames,
	               stats.inputWaitTimeouts, stats.droppedOutputFrames, stats.fallbackRequests,
	               stats.surfaceRefreshes, stats.surfaceInvalidations, configSurfaceGeneration,
	               stats.lastSurfaceGeneration, stats.lastWidth, stats.lastHeight,
	               stats.lastAsyncError, surfaceEnabled ? "on" : "off", surfaceWidth, surfaceHeight,
	               stats.lastFallbackReason[0] == '\0' ? "none" : stats.lastFallbackReason);
	return text;
}

static void ohos_avcodec_record_decoder_attempt(void)
{
	pthread_mutex_lock(&g_ohos_avcodec_stats_lock);
	g_ohos_avcodec_stats.decoderAttempts++;
	pthread_mutex_unlock(&g_ohos_avcodec_stats_lock);
}

static void ohos_avcodec_record_decoder_active(const H264_CONTEXT_OHOS_AVCODEC* sys)
{
	if (!sys)
		return;

	pthread_mutex_lock(&g_ohos_avcodec_stats_lock);
	g_ohos_avcodec_stats.surfaceDecoderActive++;
	g_ohos_avcodec_stats.lastWidth = sys->width;
	g_ohos_avcodec_stats.lastHeight = sys->height;
	g_ohos_avcodec_stats.lastSurfaceGeneration = sys->surfaceGeneration;
	pthread_mutex_unlock(&g_ohos_avcodec_stats_lock);
}

static void ohos_avcodec_record_progress(const H264_CONTEXT_OHOS_AVCODEC* sys)
{
	if (!sys)
		return;

	pthread_mutex_lock(&g_ohos_avcodec_stats_lock);
	g_ohos_avcodec_stats.decodeCalls = sys->decodeCalls;
	g_ohos_avcodec_stats.decodedFrames = sys->decodedFrames;
	g_ohos_avcodec_stats.noOutputFrames = sys->noOutputFrames;
	g_ohos_avcodec_stats.failedFrames = sys->failedFrames;
	g_ohos_avcodec_stats.inputWaitTimeouts = sys->inputWaitTimeouts;
	g_ohos_avcodec_stats.droppedOutputFrames = sys->droppedOutputFrames;
	g_ohos_avcodec_stats.surfaceRefreshes = sys->surfaceRefreshes;
	g_ohos_avcodec_stats.surfaceInvalidations = sys->surfaceInvalidations;
	g_ohos_avcodec_stats.lastWidth = sys->width;
	g_ohos_avcodec_stats.lastHeight = sys->height;
	g_ohos_avcodec_stats.lastSurfaceGeneration = sys->surfaceGeneration;
	g_ohos_avcodec_stats.lastAsyncError = sys->asyncError;
	pthread_mutex_unlock(&g_ohos_avcodec_stats_lock);
}

static int ohos_avcodec_request_software_fallback(H264_CONTEXT* h264,
                                                  H264_CONTEXT_OHOS_AVCODEC* sys,
                                                  const char* reason)
{
	pfnH264OhosAvcodecFallbackCallback callback = NULL;
	void* userData = NULL;
	const char* safeReason = reason ? reason : "unknown";

	if (sys)
		ohos_avcodec_record_progress(sys);

	pthread_mutex_lock(&g_ohos_avcodec_stats_lock);
	g_ohos_avcodec_stats.fallbackRequests++;
	(void)snprintf(g_ohos_avcodec_stats.lastFallbackReason,
	               sizeof(g_ohos_avcodec_stats.lastFallbackReason), "%s", safeReason);
	pthread_mutex_unlock(&g_ohos_avcodec_stats_lock);

	pthread_mutex_lock(&g_ohos_avcodec_surface_lock);
	callback = g_ohos_avcodec_fallback_callback;
	userData = g_ohos_avcodec_fallback_callback_user_data;
	pthread_mutex_unlock(&g_ohos_avcodec_surface_lock);

	if (h264 && h264->log)
		WLog_Print(h264->log, WLOG_WARN, "OHOS AVCodec software fallback requested: %s",
		           safeReason);
	if (callback)
		callback(safeReason, userData);
	return H264_OHOS_AVCODEC_FALLBACK_RC;
}

static BOOL ohos_avcodec_get_output_surface(OHNativeWindow** window, UINT32* width, UINT32* height,
                                            UINT64* generation)
{
	BOOL enabled = FALSE;

	if (!window || !width || !height || !generation)
		return FALSE;

	pthread_mutex_lock(&g_ohos_avcodec_surface_lock);
	enabled = g_ohos_avcodec_surface_enabled;
	*window = enabled ? g_ohos_avcodec_surface_window : NULL;
	*width = enabled ? g_ohos_avcodec_surface_width : 0;
	*height = enabled ? g_ohos_avcodec_surface_height : 0;
	*generation = g_ohos_avcodec_surface_generation;
	pthread_mutex_unlock(&g_ohos_avcodec_surface_lock);
	return enabled && (*window != NULL) && (*width > 0) && (*height > 0);
}

BOOL h264_context_ohos_output_surface_available(UINT32 width, UINT32 height)
{
	BOOL available = FALSE;

	pthread_mutex_lock(&g_ohos_avcodec_surface_lock);
	available = g_ohos_avcodec_surface_enabled && (g_ohos_avcodec_surface_window != NULL) &&
	            (width > 0) && (height > 0) && (g_ohos_avcodec_surface_width >= width) &&
	            (g_ohos_avcodec_surface_height >= height);
	pthread_mutex_unlock(&g_ohos_avcodec_surface_lock);
	return available;
}

static BOOL ohos_avcodec_surface_target_available(const H264_CONTEXT* h264)
{
	BOOL available = FALSE;

	if (!h264)
		return FALSE;

	pthread_mutex_lock(&g_ohos_avcodec_surface_lock);
	available = g_ohos_avcodec_surface_enabled && (g_ohos_avcodec_surface_window != NULL) &&
	            (g_ohos_avcodec_surface_width >= h264->width) &&
	            (g_ohos_avcodec_surface_height >= h264->height);
	pthread_mutex_unlock(&g_ohos_avcodec_surface_lock);
	return available;
}

static BOOL ohos_avcodec_surface_config_current(UINT32 width, UINT32 height,
                                                const H264_CONTEXT_OHOS_AVCODEC* sys)
{
	BOOL current = FALSE;

	if (!sys || !sys->surfaceMode || !sys->outputSurface)
		return FALSE;

	pthread_mutex_lock(&g_ohos_avcodec_surface_lock);
	if (sys->surfaceGeneration == g_ohos_avcodec_surface_generation)
	{
		current = g_ohos_avcodec_surface_enabled &&
		          (g_ohos_avcodec_surface_window == sys->outputSurface) &&
		          (g_ohos_avcodec_surface_width >= width) &&
		          (g_ohos_avcodec_surface_height >= height);
	}
	pthread_mutex_unlock(&g_ohos_avcodec_surface_lock);
	return current;
}

static void ohos_avcodec_make_deadline(struct timespec* deadline, UINT32 timeoutMs)
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

static UINT64 ohos_avcodec_now_ns(void)
{
	struct timespec now = { 0 };
	clock_gettime(CLOCK_MONOTONIC, &now);
	return ((UINT64)now.tv_sec * 1000000000ULL) + (UINT64)now.tv_nsec;
}

static BOOL ohos_avcodec_pop_input(H264_CONTEXT_OHOS_AVCODEC* sys, uint32_t* index,
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

static BOOL ohos_avcodec_wait_for_input(H264_CONTEXT_OHOS_AVCODEC* sys, uint32_t* index,
                                        OH_AVBuffer** buffer)
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

static void ohos_avcodec_on_error(OH_AVCodec* codec, int32_t errorCode, void* userData)
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

static void ohos_avcodec_on_stream_changed(OH_AVCodec* codec, OH_AVFormat* format, void* userData)
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

static void ohos_avcodec_on_need_input_buffer(OH_AVCodec* codec, uint32_t index,
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

static void ohos_avcodec_on_new_output_buffer(OH_AVCodec* codec, uint32_t index,
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

static void ohos_avcodec_reset_async_state(H264_CONTEXT_OHOS_AVCODEC* sys)
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

static void ohos_avcodec_close_decoder(H264_CONTEXT_OHOS_AVCODEC* sys)
{
	if (!sys)
		return;

	if (sys->decoder)
	{
		if (sys->started)
			OH_VideoDecoder_Stop(sys->decoder);
		OH_VideoDecoder_Destroy(sys->decoder);
	}
	sys->decoder = NULL;
	sys->outputSurface = NULL;
	sys->started = FALSE;
	sys->surfaceMode = FALSE;
	sys->surfaceGeneration = 0;
	ohos_avcodec_reset_async_state(sys);
}

static BOOL ohos_avcodec_configure_decoder(H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys,
                                            OHNativeWindow* outputSurface)
{
	bool isValid = false;
	OH_AVErrCode rc = AV_ERR_OK;
	OH_AVFormat* format = NULL;
	OH_AVCodecCallback callback = { 0 };

	if (!h264 || !sys || (h264->width == 0) || (h264->height == 0))
		return TRUE;
	if (!outputSurface)
		return FALSE;

	ohos_avcodec_record_decoder_attempt();
	OH_AVCapability* capability =
	    OH_AVCodec_GetCapabilityByCategory(OH_AVCODEC_MIMETYPE_VIDEO_AVC, false, HARDWARE);
	if (capability)
	{
		const char* name = OH_AVCapability_GetName(capability);
		if (name && name[0])
		{
			sys->decoder = OH_VideoDecoder_CreateByName(name);
			if (sys->decoder)
				WLog_Print(h264->log, WLOG_INFO, "OHOS AVCodec hardware H264 decoder selected: %s",
				           name);
			else
				WLog_Print(h264->log, WLOG_WARN,
				           "OHOS AVCodec hardware decoder create failed: %s", name);
		}
	}
	if (!sys->decoder)
	{
		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec hardware H264 decoder unavailable; no OHOS decoder created");
		return FALSE;
	}

	rc = OH_VideoDecoder_IsValid(sys->decoder, &isValid);
	if ((rc != AV_ERR_OK) || !isValid)
	{
		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec hardware H264 decoder invalid: valid=%d rc=%d",
		           isValid ? 1 : 0, (int)rc);
		ohos_avcodec_close_decoder(sys);
		return FALSE;
	}

	callback.onError = ohos_avcodec_on_error;
	callback.onStreamChanged = ohos_avcodec_on_stream_changed;
	callback.onNeedInputBuffer = ohos_avcodec_on_need_input_buffer;
	callback.onNewOutputBuffer = ohos_avcodec_on_new_output_buffer;
	rc = OH_VideoDecoder_RegisterCallback(sys->decoder, callback, sys);
	if (rc != AV_ERR_OK)
	{
		WLog_Print(h264->log, WLOG_WARN, "OHOS AVCodec register callback failed rc=%d", (int)rc);
		ohos_avcodec_close_decoder(sys);
		return FALSE;
	}

	format = OH_AVFormat_CreateVideoFormat(OH_AVCODEC_MIMETYPE_VIDEO_AVC,
	                                       WINPR_ASSERTING_INT_CAST(int32_t, h264->width),
	                                       WINPR_ASSERTING_INT_CAST(int32_t, h264->height));
	if (!format)
	{
		ohos_avcodec_close_decoder(sys);
		return FALSE;
	}

	OH_AVFormat_SetIntValue(format, OH_MD_KEY_PIXEL_FORMAT, AV_PIXEL_FORMAT_SURFACE_FORMAT);

	rc = OH_VideoDecoder_Configure(sys->decoder, format);
	OH_AVFormat_Destroy(format);
	if (rc != AV_ERR_OK)
	{
		WLog_Print(h264->log, WLOG_DEBUG,
		           "OHOS AVCodec surface configure failed rc=%d pixelFormat=%d",
		           (int)rc, AV_PIXEL_FORMAT_SURFACE_FORMAT);
		ohos_avcodec_close_decoder(sys);
		return FALSE;
	}

	rc = OH_VideoDecoder_SetSurface(sys->decoder, outputSurface);
	if (rc != AV_ERR_OK)
	{
		WLog_Print(h264->log, WLOG_WARN, "OHOS AVCodec set output surface failed rc=%d", (int)rc);
		ohos_avcodec_close_decoder(sys);
		return FALSE;
	}
	sys->outputSurface = outputSurface;
	sys->surfaceMode = TRUE;

	rc = OH_VideoDecoder_Prepare(sys->decoder);
	if (rc != AV_ERR_OK)
	{
		WLog_Print(h264->log, WLOG_WARN, "OHOS AVCodec prepare failed rc=%d", (int)rc);
		ohos_avcodec_close_decoder(sys);
		return FALSE;
	}

	rc = OH_VideoDecoder_Start(sys->decoder);
	if (rc != AV_ERR_OK)
	{
		WLog_Print(h264->log, WLOG_WARN, "OHOS AVCodec start failed rc=%d", (int)rc);
		ohos_avcodec_close_decoder(sys);
		return FALSE;
	}

	sys->started = TRUE;
	return TRUE;
}

static BOOL ohos_avcodec_open_decoder(H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys)
{
	OHNativeWindow* outputSurface = NULL;
	UINT32 surfaceWidth = 0;
	UINT32 surfaceHeight = 0;
	UINT64 surfaceGeneration = 0;

	if (!h264 || !sys || (h264->width == 0) || (h264->height == 0))
		return TRUE;

	sys->width = h264->width;
	sys->height = h264->height;

	if (!h264->ohosSurfaceModeAllowed)
	{
		WLog_Print(h264->log, WLOG_DEBUG,
		           "OHOS AVCodec skipped: AVC420 surface mode is not allowed for this H264 context");
		return FALSE;
	}

	if (!ohos_avcodec_get_output_surface(&outputSurface, &surfaceWidth, &surfaceHeight,
	                                     &surfaceGeneration) ||
	    !outputSurface)
	{
		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec AVC420 surface mode required but no output surface is configured");
		return FALSE;
	}

	if (!ohos_avcodec_configure_decoder(h264, sys, outputSurface))
	{
		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec AVC420 surface mode unavailable for %ux%u surface=%ux%u",
		           h264->width, h264->height, surfaceWidth, surfaceHeight);
		return FALSE;
	}
	sys->surfaceGeneration = surfaceGeneration;

	if (InterlockedCompareExchange(&g_ohos_avcodec_surface_active_logged, 1, 0) == 0)
	{
		WLog_Print(h264->log, WLOG_INFO,
		           "OHOS AVCodec AVC420 surface decoder active: %ux%u surface=%p target=%ux%u async-surface",
		           h264->width, h264->height, (void*)outputSurface, surfaceWidth, surfaceHeight);
	}
	ohos_avcodec_record_decoder_active(sys);
	return TRUE;
}

static int ohos_avcodec_prepare_surface_decoder(H264_CONTEXT* h264,
                                                H264_CONTEXT_OHOS_AVCODEC* sys)
{
	BOOL needsReopen = FALSE;
	BOOL hadDecoder = FALSE;
	int32_t asyncError = 0;
	UINT64 previousGeneration = 0;
	UINT64 refreshCount = 0;
	UINT64 invalidationCount = 0;

	if (!h264 || !sys || !h264->ohosSurfaceModeAllowed)
		return 1;

	pthread_mutex_lock(&sys->lock);
	hadDecoder = (sys->decoder != NULL) || sys->started;
	asyncError = sys->asyncError;
	previousGeneration = sys->surfaceGeneration;
	needsReopen = (asyncError != 0) || !sys->decoder || !sys->started || !sys->surfaceMode ||
	              !ohos_avcodec_surface_config_current(h264->width, h264->height, sys);
	pthread_mutex_unlock(&sys->lock);

	if (!needsReopen)
		return 1;

	if (!ohos_avcodec_surface_target_available(h264))
	{
		if (hadDecoder)
			ohos_avcodec_close_decoder(sys);

		pthread_mutex_lock(&sys->lock);
		invalidationCount = ++sys->surfaceInvalidations;
		pthread_cond_broadcast(&sys->cond);
		pthread_mutex_unlock(&sys->lock);
		ohos_avcodec_record_progress(sys);

		if ((invalidationCount <= 3) || ((invalidationCount % 120) == 0))
		{
			WLog_Print(h264->log, WLOG_WARN,
			           "OHOS AVCodec surface decoder paused: output surface unavailable"
			           " invalidations=%" PRIu64,
			           invalidationCount);
		}
		return 0;
	}

	if (hadDecoder)
		ohos_avcodec_close_decoder(sys);

	pthread_mutex_lock(&sys->lock);
	refreshCount = ++sys->surfaceRefreshes;
	sys->asyncError = 0;
	pthread_mutex_unlock(&sys->lock);

	if (!ohos_avcodec_open_decoder(h264, sys))
		return -1;

	if ((refreshCount <= 3) || ((refreshCount % 30) == 0))
	{
		WLog_Print(h264->log, WLOG_INFO,
		           "OHOS AVCodec surface decoder refreshed: generation=%" PRIu64
		           " -> %" PRIu64 " refreshes=%" PRIu64 " asyncError=%d",
		           previousGeneration, sys->surfaceGeneration, refreshCount, asyncError);
	}
	ohos_avcodec_record_progress(sys);
	return 1;
}

static BOOL ohos_avcodec_init_primitives(H264_CONTEXT_OHOS_AVCODEC* sys)
{
	if (!sys)
		return FALSE;

	if (pthread_mutex_init(&sys->lock, NULL) != 0)
		return FALSE;

	if (pthread_cond_init(&sys->cond, NULL) != 0)
	{
		pthread_mutex_destroy(&sys->lock);
		return FALSE;
	}

	sys->primitivesReady = TRUE;
	return TRUE;
}

static void ohos_avcodec_uninit(H264_CONTEXT* h264)
{
	H264_CONTEXT_OHOS_AVCODEC* sys = NULL;

	if (!h264)
		return;

	sys = (H264_CONTEXT_OHOS_AVCODEC*)h264->pSystemData;
	if (!sys)
		return;

	ohos_avcodec_close_decoder(sys);

	if (sys->primitivesReady)
	{
		pthread_cond_destroy(&sys->cond);
		pthread_mutex_destroy(&sys->lock);
	}

	free(sys);
	h264->pSystemData = NULL;
	h264->numSystemData = 0;
}

static BOOL ohos_avcodec_init(H264_CONTEXT* h264)
{
	H264_CONTEXT_OHOS_AVCODEC* sys = NULL;

	if (!h264 || h264->Compressor)
		return FALSE;

	sys = (H264_CONTEXT_OHOS_AVCODEC*)calloc(1, sizeof(H264_CONTEXT_OHOS_AVCODEC));
	if (!sys)
		return FALSE;

	sys->log = h264->log;
	if (!ohos_avcodec_init_primitives(sys))
	{
		free(sys);
		return FALSE;
	}

	h264->pSystemData = sys;
	h264->numSystemData = 1;

	if (ohos_avcodec_open_decoder(h264, sys))
		return TRUE;

	ohos_avcodec_uninit(h264);
	return FALSE;
}

static void ohos_avcodec_return_empty_input(H264_CONTEXT_OHOS_AVCODEC* sys, uint32_t inputIndex,
                                            OH_AVBuffer* inputBuffer)
{
	OH_AVCodecBufferAttr attr = { 0 };

	if (!sys || !sys->decoder || !inputBuffer)
		return;

	OH_AVBuffer_SetBufferAttr(inputBuffer, &attr);
	OH_VideoDecoder_PushInputBuffer(sys->decoder, inputIndex);
}

static int ohos_avcodec_decompress(H264_CONTEXT* WINPR_RESTRICT h264,
                                    const BYTE* WINPR_RESTRICT pSrcData, UINT32 SrcSize)
{
	uint32_t inputIndex = 0;
	OH_AVErrCode rc = AV_ERR_OK;
	OH_AVBuffer* inputBuffer = NULL;
	H264_CONTEXT_OHOS_AVCODEC* sys = NULL;
	OH_AVCodecBufferAttr inputAttr = { 0 };
	int32_t capacity = 0;
	BYTE* dst = NULL;
	int64_t expectedOutputPts = 0;
	int surfaceReady = 0;

	WINPR_ASSERT(h264);
	WINPR_ASSERT(pSrcData || (SrcSize == 0));

	sys = (H264_CONTEXT_OHOS_AVCODEC*)h264->pSystemData;
	if (!sys || !sys->primitivesReady)
		return ohos_avcodec_request_software_fallback(h264, sys, "decoder not initialized");
	if (!h264->ohosSurfaceModeAllowed)
		return ohos_avcodec_request_software_fallback(h264, sys, "surface mode not allowed");

	surfaceReady = ohos_avcodec_prepare_surface_decoder(h264, sys);
	if (surfaceReady == 0)
		return 0;
	if (surfaceReady < 0)
		return ohos_avcodec_request_software_fallback(h264, sys, "surface decoder refresh failed");

	if (!sys->decoder || !sys->started)
		return ohos_avcodec_request_software_fallback(h264, sys, "decoder not started");
	if (!sys->surfaceMode)
		return ohos_avcodec_request_software_fallback(h264, sys, "surface decoder not active");

	h264->pYUVData[0] = NULL;
	h264->pYUVData[1] = NULL;
	h264->pYUVData[2] = NULL;
	h264->surfaceRendered = FALSE;
	sys->decodeCalls++;
	expectedOutputPts = WINPR_ASSERTING_INT_CAST(int64_t, sys->decodeCalls);

	pthread_mutex_lock(&sys->lock);
	if (!ohos_avcodec_wait_for_input(sys, &inputIndex, &inputBuffer))
	{
		const int32_t asyncError = sys->asyncError;
		pthread_mutex_unlock(&sys->lock);
		sys->inputWaitTimeouts++;
		sys->noOutputFrames++;
		ohos_avcodec_record_progress(sys);
		if (asyncError != 0)
			return ohos_avcodec_request_software_fallback(h264, sys, "async input error");
		if ((sys->inputWaitTimeouts <= 3) || ((sys->inputWaitTimeouts % 120) == 0))
			WLog_Print(h264->log, WLOG_WARN,
			           "OHOS AVCodec surface input backpressure count=%" PRIu64
			           " calls=%" PRIu64 "; keeping hardware decoder",
			           sys->inputWaitTimeouts, sys->decodeCalls);
		return 0;
	}
	pthread_mutex_unlock(&sys->lock);

	capacity = OH_AVBuffer_GetCapacity(inputBuffer);
	dst = OH_AVBuffer_GetAddr(inputBuffer);
	if (!dst || (capacity < 0) || ((UINT32)capacity < SrcSize))
	{
		sys->failedFrames++;
		ohos_avcodec_record_progress(sys);
		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec input buffer invalid capacity=%d size=%u", capacity, SrcSize);
		ohos_avcodec_return_empty_input(sys, inputIndex, inputBuffer);
		return ohos_avcodec_request_software_fallback(h264, sys, "input buffer invalid");
	}

	if (SrcSize > 0)
		CopyMemory(dst, pSrcData, SrcSize);

	inputAttr.pts = expectedOutputPts;
	inputAttr.size = WINPR_ASSERTING_INT_CAST(int32_t, SrcSize);
	inputAttr.offset = 0;
	inputAttr.flags = AVCODEC_BUFFER_FLAGS_NONE;
	if (OH_AVBuffer_SetBufferAttr(inputBuffer, &inputAttr) != AV_ERR_OK)
	{
		sys->failedFrames++;
		ohos_avcodec_record_progress(sys);
		ohos_avcodec_return_empty_input(sys, inputIndex, inputBuffer);
		return ohos_avcodec_request_software_fallback(h264, sys, "set input attr failed");
	}

	rc = OH_VideoDecoder_PushInputBuffer(sys->decoder, inputIndex);
	if (rc != AV_ERR_OK)
	{
		sys->failedFrames++;
		ohos_avcodec_record_progress(sys);
		WLog_Print(h264->log, WLOG_WARN, "OHOS AVCodec push input failed rc=%d", (int)rc);
		return ohos_avcodec_request_software_fallback(h264, sys, "push input failed");
	}

	h264->surfaceRendered = TRUE;
	ohos_avcodec_record_progress(sys);
	if ((sys->decodeCalls <= 3) || ((sys->decodeCalls % 120) == 0))
	{
		WLog_Print(h264->log, WLOG_INFO,
		           "OHOS AVCodec surface input pushed call=%" PRIu64
		           " src=%u outputSurface=%p size=%ux%u rendered=%" PRIu64
		           " dropped=%" PRIu64 " failed=%" PRIu64,
		           sys->decodeCalls, SrcSize, (void*)sys->outputSurface, sys->width, sys->height,
		           sys->decodedFrames, sys->droppedOutputFrames, sys->failedFrames);
	}
	return 1;
}

static int ohos_avcodec_compress(H264_CONTEXT* WINPR_RESTRICT h264,
                                 const BYTE** WINPR_RESTRICT ppSrcYuv,
                                 const UINT32* WINPR_RESTRICT pStride,
                                 BYTE** WINPR_RESTRICT ppDstData,
                                 UINT32* WINPR_RESTRICT pDstSize)
{
	WINPR_UNUSED(h264);
	WINPR_UNUSED(ppSrcYuv);
	WINPR_UNUSED(pStride);
	WINPR_UNUSED(ppDstData);
	WINPR_UNUSED(pDstSize);
	return -1;
}

const H264_CONTEXT_SUBSYSTEM g_Subsystem_OHOS_AVCodec = {
	"OHOS-AVCodec", ohos_avcodec_init, ohos_avcodec_uninit, ohos_avcodec_decompress,
	ohos_avcodec_compress
};
