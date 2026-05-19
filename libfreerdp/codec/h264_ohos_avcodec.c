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
#define OHOS_AVCODEC_OUTPUT_WAIT_MS 25
#define OHOS_AVCODEC_SURFACE_OUTPUT_WAIT_MS 2
#define OHOS_AVCODEC_BUFFER_OUTPUT_TIMEOUT_LIMIT 30
#define OHOS_AVCODEC_SURFACE_OUTPUT_TIMEOUT_LIMIT 120
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
	H264_OHOS_SURFACE_TARGET surfaceTarget;
	UINT32 width;
	UINT32 height;
	int32_t outputPixelFormat;
	int32_t outputStride;
	int32_t outputSliceHeight;
	BYTE* displayBuffer;
	size_t displayBufferSize;
	size_t readySize;
	UINT64 decodeCalls;
	UINT64 decodedFrames;
	UINT64 noOutputFrames;
	UINT64 failedFrames;
	UINT64 inputWaitTimeouts;
	UINT64 outputWaitTimeouts;
	UINT64 droppedOutputFrames;
	UINT64 staleOutputFrames;
	UINT64 surfaceRefreshes;
	UINT64 surfaceInvalidations;
	UINT64 lastSurfaceRenderNs;
	UINT64 outputFormatReports;
	UINT64 outputBufferReports;
	BOOL outputReady;
	BOOL outputPtsValid;
	int64_t outputPts;
	BOOL unsupportedFormatLogged;
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

static volatile LONG g_ohos_avcodec_buffer_active_logged = 0;
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
	UINT64 bufferDecoderActive;
	UINT64 surfaceDecoderActive;
	UINT64 decodeCalls;
	UINT64 decodedFrames;
	UINT64 noOutputFrames;
	UINT64 failedFrames;
	UINT64 inputWaitTimeouts;
	UINT64 outputWaitTimeouts;
	UINT64 droppedOutputFrames;
	UINT64 staleOutputFrames;
	UINT64 fallbackRequests;
	UINT64 surfaceRefreshes;
	UINT64 surfaceInvalidations;
	UINT64 lastSurfaceGeneration;
	UINT32 lastWidth;
	UINT32 lastHeight;
	int32_t lastPixelFormat;
	int32_t lastStride;
	int32_t lastSliceHeight;
	int32_t lastAsyncError;
	BOOL lastSurfaceMode;
	char lastFallbackReason[160];
} OHOS_AVCODEC_DIAGNOSTICS;

static pthread_mutex_t g_ohos_avcodec_stats_lock = PTHREAD_MUTEX_INITIALIZER;
static OHOS_AVCODEC_DIAGNOSTICS g_ohos_avcodec_stats = { 0 };

static const char* ohos_avcodec_pixel_format_name(int32_t format)
{
	switch (format)
	{
		case AV_PIXEL_FORMAT_YUVI420:
			return "YUVI420";
		case AV_PIXEL_FORMAT_NV12:
			return "NV12";
		case AV_PIXEL_FORMAT_NV21:
			return "NV21";
		case AV_PIXEL_FORMAT_SURFACE_FORMAT:
			return "SURFACE";
		default:
			return "unknown";
	}
}

static UINT32 ohos_avcodec_sample_byte(const BYTE* data, size_t size, size_t offset)
{
	if (!data || (offset >= size))
		return UINT32_MAX;
	return data[offset];
}

static void ohos_avcodec_log_output_buffer(H264_CONTEXT_OHOS_AVCODEC* sys,
                                           const OH_AVCodecBufferAttr* attr, const BYTE* src,
                                           size_t srcSize, int32_t capacity, int32_t offset,
                                           int32_t stride, int32_t sliceHeight, BOOL copied,
                                           const char* normalizeMode)
{
	UINT64 report = 0;
	const UINT32 width = sys ? sys->width : 0;
	const UINT32 height = sys ? sys->height : 0;
	const UINT32 uvWidth = (width + 1) / 2;
	const UINT32 uvHeight = (height + 1) / 2;
	const size_t ySize = (size_t)width * height;
	const size_t uvSize = (size_t)uvWidth * uvHeight;
	const size_t rawChroma = ((stride > 0) && (sliceHeight > 0)) ? (size_t)stride * sliceHeight : 0;
	const size_t rawI420V =
	    rawChroma + (size_t)(((stride + 1) / 2) * ((sliceHeight + 1) / 2));
	const BYTE* out = sys ? sys->displayBuffer : NULL;
	const size_t outSize = (copied && sys) ? sys->readySize : 0;
	const int32_t attrSize = attr ? attr->size : -1;
	const int32_t attrOffset = attr ? attr->offset : -1;
	const uint32_t attrFlags = attr ? attr->flags : 0;
	const int64_t attrPts = attr ? attr->pts : -1;

	if (!sys || !sys->log)
		return;

	report = ++sys->outputBufferReports;
	if ((report > 6) && ((report % 120) != 0) && copied)
		return;

	WLog_Print(sys->log, copied ? WLOG_INFO : WLOG_WARN,
	           "OHOS AVCodec output buffer report=%" PRIu64
	           " copied=%d normalize=%s pixel=%s(%d) size=%ux%u stride=%d slice=%d"
	           " capacity=%d srcSize=%" PRIuz " attrOffset=%d usedOffset=%d attrSize=%d flags=0x%08" PRIX32
	           " pts=%" PRId64
	           " rawY=%u,%u,%u,%u rawC=%u,%u,%u,%u,%u,%u,%u,%u rawI420V=%u,%u,%u,%u"
	           " outY=%u,%u,%u,%u outU=%u,%u,%u,%u outV=%u,%u,%u,%u",
	           report, copied ? 1 : 0, normalizeMode ? normalizeMode : "none",
	           ohos_avcodec_pixel_format_name(sys->outputPixelFormat), sys->outputPixelFormat, width,
	           height, stride, sliceHeight, capacity, srcSize, attrOffset, offset, attrSize,
	           attrFlags, attrPts, ohos_avcodec_sample_byte(src, srcSize, 0),
	           ohos_avcodec_sample_byte(src, srcSize, 1),
	           ohos_avcodec_sample_byte(src, srcSize, 2),
	           ohos_avcodec_sample_byte(src, srcSize, 3),
	           ohos_avcodec_sample_byte(src, srcSize, rawChroma + 0),
	           ohos_avcodec_sample_byte(src, srcSize, rawChroma + 1),
	           ohos_avcodec_sample_byte(src, srcSize, rawChroma + 2),
	           ohos_avcodec_sample_byte(src, srcSize, rawChroma + 3),
	           ohos_avcodec_sample_byte(src, srcSize, rawChroma + 4),
	           ohos_avcodec_sample_byte(src, srcSize, rawChroma + 5),
	           ohos_avcodec_sample_byte(src, srcSize, rawChroma + 6),
	           ohos_avcodec_sample_byte(src, srcSize, rawChroma + 7),
	           ohos_avcodec_sample_byte(src, srcSize, rawI420V + 0),
	           ohos_avcodec_sample_byte(src, srcSize, rawI420V + 1),
	           ohos_avcodec_sample_byte(src, srcSize, rawI420V + 2),
	           ohos_avcodec_sample_byte(src, srcSize, rawI420V + 3),
	           ohos_avcodec_sample_byte(out, outSize, 0),
	           ohos_avcodec_sample_byte(out, outSize, 1),
	           ohos_avcodec_sample_byte(out, outSize, 2),
	           ohos_avcodec_sample_byte(out, outSize, 3),
	           ohos_avcodec_sample_byte(out, outSize, ySize + 0),
	           ohos_avcodec_sample_byte(out, outSize, ySize + 1),
	           ohos_avcodec_sample_byte(out, outSize, ySize + 2),
	           ohos_avcodec_sample_byte(out, outSize, ySize + 3),
	           ohos_avcodec_sample_byte(out, outSize, ySize + uvSize + 0),
	           ohos_avcodec_sample_byte(out, outSize, ySize + uvSize + 1),
	           ohos_avcodec_sample_byte(out, outSize, ySize + uvSize + 2),
	           ohos_avcodec_sample_byte(out, outSize, ySize + uvSize + 3));
}

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
	               "ohos avcodec: attempts=%" PRIu64 " active=buffer:%" PRIu64
	               ",surface:%" PRIu64 " calls=%" PRIu64 " decoded=%" PRIu64
	               " noOutput=%" PRIu64 " failed=%" PRIu64 " inputTimeout=%" PRIu64
	               " outputTimeout=%" PRIu64 " dropped=%" PRIu64 " stale=%" PRIu64
	               " fallbacks=%" PRIu64 " surfaceRefresh=%" PRIu64 " surfaceInvalid=%" PRIu64
	               " surfaceGeneration=config:%" PRIu64 ",decoder:%" PRIu64
	               " last=%ux%u mode=%s format=%d stride=%d slice=%d asyncError=%d"
	               " outputSurface=%s:%ux%u reason=%s",
	               stats.decoderAttempts, stats.bufferDecoderActive, stats.surfaceDecoderActive,
	               stats.decodeCalls, stats.decodedFrames, stats.noOutputFrames,
	               stats.failedFrames, stats.inputWaitTimeouts, stats.outputWaitTimeouts,
	               stats.droppedOutputFrames, stats.staleOutputFrames, stats.fallbackRequests,
	               stats.surfaceRefreshes, stats.surfaceInvalidations, configSurfaceGeneration,
	               stats.lastSurfaceGeneration, stats.lastWidth, stats.lastHeight,
	               stats.lastSurfaceMode ? "surface" : "buffer",
	               stats.lastPixelFormat, stats.lastStride, stats.lastSliceHeight,
	               stats.lastAsyncError, surfaceEnabled ? "on" : "off", surfaceWidth,
	               surfaceHeight,
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
	if (sys->surfaceMode)
		g_ohos_avcodec_stats.surfaceDecoderActive++;
	else
		g_ohos_avcodec_stats.bufferDecoderActive++;
	g_ohos_avcodec_stats.lastWidth = sys->width;
	g_ohos_avcodec_stats.lastHeight = sys->height;
	g_ohos_avcodec_stats.lastSurfaceMode = sys->surfaceMode;
	g_ohos_avcodec_stats.lastSurfaceGeneration = sys->surfaceGeneration;
	g_ohos_avcodec_stats.lastPixelFormat = sys->outputPixelFormat;
	g_ohos_avcodec_stats.lastStride = sys->outputStride;
	g_ohos_avcodec_stats.lastSliceHeight = sys->outputSliceHeight;
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
	g_ohos_avcodec_stats.outputWaitTimeouts = sys->outputWaitTimeouts;
	g_ohos_avcodec_stats.droppedOutputFrames = sys->droppedOutputFrames;
	g_ohos_avcodec_stats.staleOutputFrames = sys->staleOutputFrames;
	g_ohos_avcodec_stats.surfaceRefreshes = sys->surfaceRefreshes;
	g_ohos_avcodec_stats.surfaceInvalidations = sys->surfaceInvalidations;
	g_ohos_avcodec_stats.lastWidth = sys->width;
	g_ohos_avcodec_stats.lastHeight = sys->height;
	g_ohos_avcodec_stats.lastSurfaceMode = sys->surfaceMode;
	g_ohos_avcodec_stats.lastSurfaceGeneration = sys->surfaceGeneration;
	g_ohos_avcodec_stats.lastPixelFormat = sys->outputPixelFormat;
	g_ohos_avcodec_stats.lastStride = sys->outputStride;
	g_ohos_avcodec_stats.lastSliceHeight = sys->outputSliceHeight;
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

static BOOL ohos_avcodec_surface_config_current_for_target(
    H264_OHOS_SURFACE_TARGET target, UINT32 width, UINT32 height,
    const H264_CONTEXT_OHOS_AVCODEC* sys)
{
	BOOL current = FALSE;

	if (!sys || !sys->surfaceMode || !sys->outputSurface)
		return FALSE;

	WINPR_UNUSED(target);
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

static BOOL ohos_avcodec_surface_config_current(const H264_CONTEXT* h264,
                                                const H264_CONTEXT_OHOS_AVCODEC* sys)
{
	if (!h264)
		return FALSE;
	return ohos_avcodec_surface_config_current_for_target(h264->ohosSurfaceTarget, h264->width,
	                                                     h264->height, sys);
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

static void ohos_avcodec_read_output_format(H264_CONTEXT_OHOS_AVCODEC* sys, OH_AVFormat* format)
{
	int32_t value = 0;
	BOOL hasPixelFormat = FALSE;
	BOOL hasStride = FALSE;
	BOOL hasSliceHeight = FALSE;
	UINT64 report = 0;

	if (!sys || !format)
		return;

	hasPixelFormat = OH_AVFormat_GetIntValue(format, OH_MD_KEY_PIXEL_FORMAT, &value);
	if (hasPixelFormat)
		sys->outputPixelFormat = value;
	else if (sys->outputPixelFormat <= 0)
		sys->outputPixelFormat = AV_PIXEL_FORMAT_YUVI420;

	hasStride = OH_AVFormat_GetIntValue(format, OH_MD_KEY_VIDEO_STRIDE, &value);
	if (hasStride && (value > 0))
		sys->outputStride = value;
	else if (sys->outputStride <= 0)
		sys->outputStride = WINPR_ASSERTING_INT_CAST(int32_t, sys->width);

	hasSliceHeight = OH_AVFormat_GetIntValue(format, OH_MD_KEY_VIDEO_SLICE_HEIGHT, &value);
	if (hasSliceHeight && (value > 0))
		sys->outputSliceHeight = value;
	else if (sys->outputSliceHeight <= 0)
		sys->outputSliceHeight = WINPR_ASSERTING_INT_CAST(int32_t, sys->height);

	report = ++sys->outputFormatReports;
	if ((report <= 6) || ((report % 60) == 0))
	{
		WLog_Print(sys->log, WLOG_INFO,
		           "OHOS AVCodec output format report=%" PRIu64
		           " pixel=%s(%d) hasPixel=%d stride=%d hasStride=%d slice=%d hasSlice=%d"
		           " size=%ux%u mode=%s",
		           report, ohos_avcodec_pixel_format_name(sys->outputPixelFormat),
		           sys->outputPixelFormat, hasPixelFormat ? 1 : 0, sys->outputStride,
		           hasStride ? 1 : 0, sys->outputSliceHeight, hasSliceHeight ? 1 : 0,
		           sys->width, sys->height, sys->surfaceMode ? "surface" : "buffer");
	}
}

static BOOL ohos_avcodec_update_output_description(H264_CONTEXT_OHOS_AVCODEC* sys)
{
	OH_AVFormat* description = NULL;

	if (!sys || !sys->decoder)
		return FALSE;

	description = OH_VideoDecoder_GetOutputDescription(sys->decoder);
	if (!description)
		return FALSE;

	ohos_avcodec_read_output_format(sys, description);
	OH_AVFormat_Destroy(description);
	return TRUE;
}

static BOOL ohos_avcodec_ensure_buffer(BYTE** buffer, size_t* bufferSize, size_t required)
{
	BYTE* resized = NULL;

	if (!buffer || !bufferSize || (required == 0))
		return FALSE;

	if (*bufferSize >= required)
		return TRUE;

	resized = (BYTE*)realloc(*buffer, required);
	if (!resized)
		return FALSE;

	*buffer = resized;
	*bufferSize = required;
	return TRUE;
}

static void ohos_avcodec_set_h264_output(H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys)
{
	const size_t ySize = (size_t)sys->width * sys->height;
	const UINT32 uvWidth = (sys->width + 1) / 2;
	const size_t uvSize = (size_t)uvWidth * ((sys->height + 1) / 2);

	h264->pYUVData[0] = sys->displayBuffer;
	h264->pYUVData[1] = sys->displayBuffer + ySize;
	h264->pYUVData[2] = sys->displayBuffer + ySize + uvSize;
	h264->iStride[0] = sys->width;
	h264->iStride[1] = uvWidth;
	h264->iStride[2] = uvWidth;
}

static BOOL ohos_avcodec_copy_i420_to_callback(H264_CONTEXT_OHOS_AVCODEC* sys, const BYTE* src,
                                               size_t srcSize, int32_t srcStride,
                                               int32_t srcSliceHeight)
{
	const UINT32 uvWidth = (sys->width + 1) / 2;
	const UINT32 uvHeight = (sys->height + 1) / 2;
	const UINT32 uvStride = (UINT32)MAX(1, (srcStride + 1) / 2);
	const BYTE* srcY = src;
	const BYTE* srcU = srcY + ((size_t)srcStride * srcSliceHeight);
	const BYTE* srcV = srcU + ((size_t)uvStride * ((srcSliceHeight + 1) / 2));
	const size_t ySize = (size_t)sys->width * sys->height;
	const size_t uvSize = (size_t)uvWidth * uvHeight;
	const size_t required = ySize + (2 * uvSize);
	BYTE* dstY = NULL;
	BYTE* dstU = NULL;
	BYTE* dstV = NULL;
	size_t lastRead = 0;

	if (!ohos_avcodec_ensure_buffer(&sys->displayBuffer, &sys->displayBufferSize, required))
		return FALSE;

	lastRead = (size_t)(srcV - src) + ((uvHeight > 0) ? ((size_t)(uvHeight - 1) * uvStride) : 0) +
	           uvWidth;
	if ((srcSize > 0) && (lastRead > srcSize))
		return FALSE;

	dstY = sys->displayBuffer;
	dstU = sys->displayBuffer + ySize;
	dstV = sys->displayBuffer + ySize + uvSize;

	for (UINT32 y = 0; y < sys->height; y++)
		CopyMemory(&dstY[(size_t)y * sys->width], &srcY[(size_t)y * srcStride], sys->width);

	for (UINT32 y = 0; y < uvHeight; y++)
	{
		CopyMemory(&dstU[(size_t)y * uvWidth], &srcU[(size_t)y * uvStride], uvWidth);
		CopyMemory(&dstV[(size_t)y * uvWidth], &srcV[(size_t)y * uvStride], uvWidth);
	}

	sys->readySize = required;
	return TRUE;
}

static BOOL ohos_avcodec_copy_nvxx_to_callback(H264_CONTEXT_OHOS_AVCODEC* sys, const BYTE* src,
                                               size_t srcSize, int32_t srcStride,
                                               int32_t srcSliceHeight, BOOL nv21)
{
	const UINT32 uvWidth = (sys->width + 1) / 2;
	const UINT32 uvHeight = (sys->height + 1) / 2;
	const BYTE* srcY = src;
	const BYTE* srcUV = srcY + ((size_t)srcStride * srcSliceHeight);
	const size_t ySize = (size_t)sys->width * sys->height;
	const size_t uvSize = (size_t)uvWidth * uvHeight;
	const size_t required = ySize + (2 * uvSize);
	BYTE* dstY = NULL;
	BYTE* dstU = NULL;
	BYTE* dstV = NULL;
	size_t lastRead = 0;

	if (!ohos_avcodec_ensure_buffer(&sys->displayBuffer, &sys->displayBufferSize, required))
		return FALSE;

	lastRead = (size_t)(srcUV - src) + ((uvHeight > 0) ? ((size_t)(uvHeight - 1) * srcStride) : 0) +
	           (2 * uvWidth);
	if ((srcSize > 0) && (lastRead > srcSize))
		return FALSE;

	dstY = sys->displayBuffer;
	dstU = sys->displayBuffer + ySize;
	dstV = sys->displayBuffer + ySize + uvSize;

	for (UINT32 y = 0; y < sys->height; y++)
		CopyMemory(&dstY[(size_t)y * sys->width], &srcY[(size_t)y * srcStride], sys->width);

	for (UINT32 y = 0; y < uvHeight; y++)
	{
		const BYTE* row = &srcUV[(size_t)y * srcStride];
		BYTE* rowU = &dstU[(size_t)y * uvWidth];
		BYTE* rowV = &dstV[(size_t)y * uvWidth];

		for (UINT32 x = 0; x < uvWidth; x++)
		{
			rowU[x] = row[(size_t)(2 * x) + (nv21 ? 1 : 0)];
			rowV[x] = row[(size_t)(2 * x) + (nv21 ? 0 : 1)];
		}
	}

	sys->readySize = required;
	return TRUE;
}

static BOOL ohos_avcodec_copy_output_to_callback(H264_CONTEXT_OHOS_AVCODEC* sys,
                                                 OH_AVBuffer* outputBuffer,
                                                 const OH_AVCodecBufferAttr* attr)
{
	const BYTE* src = NULL;
	int32_t capacity = 0;
	int32_t stride = 0;
	int32_t sliceHeight = 0;
	int32_t offset = 0;
	size_t srcSize = 0;
	BOOL copied = FALSE;
	const char* normalizeMode = "none";

	if (!sys || !outputBuffer || (sys->width == 0) || (sys->height == 0))
		return FALSE;

	src = OH_AVBuffer_GetAddr(outputBuffer);
	if (!src)
		return FALSE;

	capacity = OH_AVBuffer_GetCapacity(outputBuffer);
	if (attr && (attr->offset > 0))
		offset = attr->offset;
	if ((capacity > 0) && (offset >= capacity))
		return FALSE;

	if (capacity > 0)
	{
		src += offset;
		srcSize = (size_t)(capacity - offset);
	}
	else if (attr && (attr->size > 0))
	{
		src += offset;
		srcSize = (size_t)attr->size;
	}

	stride = (sys->outputStride > 0) ? sys->outputStride : WINPR_ASSERTING_INT_CAST(int32_t, sys->width);
	if ((UINT32)stride < sys->width)
		stride = WINPR_ASSERTING_INT_CAST(int32_t, sys->width);

	sliceHeight = (sys->outputSliceHeight > 0) ? sys->outputSliceHeight
	                                           : WINPR_ASSERTING_INT_CAST(int32_t, sys->height);
	if ((UINT32)sliceHeight < sys->height)
		sliceHeight = WINPR_ASSERTING_INT_CAST(int32_t, sys->height);

	switch (sys->outputPixelFormat)
	{
		case AV_PIXEL_FORMAT_YUVI420:
			normalizeMode = "I420";
			copied = ohos_avcodec_copy_i420_to_callback(sys, src, srcSize, stride, sliceHeight);
			break;
		case AV_PIXEL_FORMAT_NV12:
			normalizeMode = "NV12-as-UV";
			copied = ohos_avcodec_copy_nvxx_to_callback(sys, src, srcSize, stride, sliceHeight, FALSE);
			break;
		case AV_PIXEL_FORMAT_NV21:
			normalizeMode = "NV21-as-VU";
			copied = ohos_avcodec_copy_nvxx_to_callback(sys, src, srcSize, stride, sliceHeight, TRUE);
			break;
		default:
			if (!sys->unsupportedFormatLogged)
			{
				WLog_Print(sys->log, WLOG_WARN,
				           "OHOS AVCodec output format %d unsupported; software H264 remains the fallback",
				           sys->outputPixelFormat);
				sys->unsupportedFormatLogged = TRUE;
			}
			normalizeMode = "unsupported";
			copied = FALSE;
			break;
	}

	ohos_avcodec_log_output_buffer(sys, attr, src, srcSize, capacity, offset, stride,
	                               sliceHeight, copied, normalizeMode);
	return copied;
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

static void ohos_avcodec_drop_stale_output_locked(H264_CONTEXT_OHOS_AVCODEC* sys,
                                                  int64_t expectedPts)
{
	if (!sys || !sys->outputReady || !sys->outputPtsValid || (expectedPts <= 0))
		return;
	if (sys->outputPts >= expectedPts)
		return;

	sys->outputReady = FALSE;
	sys->outputPtsValid = FALSE;
	sys->readySize = 0;
	sys->staleOutputFrames++;
	sys->droppedOutputFrames++;
}

static BOOL ohos_avcodec_output_matches_locked(const H264_CONTEXT_OHOS_AVCODEC* sys,
                                               int64_t expectedPts)
{
	if (!sys || !sys->outputReady)
		return FALSE;
	if (!sys->outputPtsValid || (expectedPts <= 0))
		return TRUE;
	return sys->outputPts == expectedPts;
}

static BOOL ohos_avcodec_wait_for_output(H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys,
                                         int64_t expectedPts)
{
	struct timespec deadline = { 0 };
	int rc = 0;
	const UINT32 waitMs = (sys && sys->surfaceMode) ? OHOS_AVCODEC_SURFACE_OUTPUT_WAIT_MS
	                                                : OHOS_AVCODEC_OUTPUT_WAIT_MS;

	if (!h264 || !sys)
		return FALSE;

	ohos_avcodec_make_deadline(&deadline, waitMs);
	ohos_avcodec_drop_stale_output_locked(sys, expectedPts);
	while (!ohos_avcodec_output_matches_locked(sys, expectedPts) && (sys->asyncError == 0))
	{
		if (sys->outputReady && sys->outputPtsValid && (expectedPts > 0) &&
		    (sys->outputPts > expectedPts))
			break;

		rc = pthread_cond_timedwait(&sys->cond, &sys->lock, &deadline);
		if (rc == ETIMEDOUT)
			break;

		ohos_avcodec_drop_stale_output_locked(sys, expectedPts);
	}

	if (!ohos_avcodec_output_matches_locked(sys, expectedPts) || (sys->asyncError != 0))
		return FALSE;

	sys->outputReady = FALSE;
	sys->outputPtsValid = FALSE;
	if (sys->surfaceMode)
		h264->surfaceRendered = TRUE;
	else
		ohos_avcodec_set_h264_output(h264, sys);
	return TRUE;
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

	if (!sys || !sys->primitivesReady)
		return;

	pthread_mutex_lock(&sys->lock);
	ohos_avcodec_read_output_format(sys, format);
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
	BOOL copied = FALSE;
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
			if (!ohos_avcodec_surface_config_current_for_target(
			        sys->surfaceTarget, sys->width, sys->height, sys))
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
			sys->outputReady = FALSE;
			pthread_cond_broadcast(&sys->cond);
		}
		else
		{
			if (sys->outputReady)
			{
				sys->droppedOutputFrames++;
				sys->outputReady = FALSE;
				sys->outputPtsValid = FALSE;
				sys->readySize = 0;
			}
			copied = ohos_avcodec_copy_output_to_callback(sys, buffer, &attr);
			if (copied)
			{
				sys->outputReady = TRUE;
				sys->outputPts = attr.pts;
				sys->outputPtsValid = attr.pts > 0;
				pthread_cond_broadcast(&sys->cond);
			}
			else
			{
				sys->failedFrames++;
			}
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
	sys->outputReady = FALSE;
	sys->outputPtsValid = FALSE;
	sys->outputPts = 0;
	sys->asyncError = 0;
	sys->readySize = 0;
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
	sys->surfaceTarget = H264_OHOS_SURFACE_DEFAULT;
	ohos_avcodec_reset_async_state(sys);
}

static BOOL ohos_avcodec_configure_decoder(H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys,
                                            int32_t pixelFormat, BOOL setPixelFormat,
                                            OHNativeWindow* outputSurface)
{
	bool isValid = false;
	OH_AVErrCode rc = AV_ERR_OK;
	OH_AVFormat* format = NULL;
	OH_AVCodecCallback callback = { 0 };

	if (!h264 || !sys || (h264->width == 0) || (h264->height == 0))
		return TRUE;

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

	if (outputSurface)
		OH_AVFormat_SetIntValue(format, OH_MD_KEY_PIXEL_FORMAT, AV_PIXEL_FORMAT_SURFACE_FORMAT);
	else if (setPixelFormat)
		OH_AVFormat_SetIntValue(format, OH_MD_KEY_PIXEL_FORMAT, pixelFormat);
	if (!outputSurface)
		OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENABLE_LOW_LATENCY, 1);

	rc = OH_VideoDecoder_Configure(sys->decoder, format);
	OH_AVFormat_Destroy(format);
	if (rc != AV_ERR_OK)
	{
		WLog_Print(h264->log, WLOG_DEBUG,
		           "OHOS AVCodec configure failed rc=%d pixelFormat=%d setPixel=%d surface=%d",
		           (int)rc, outputSurface ? AV_PIXEL_FORMAT_SURFACE_FORMAT : pixelFormat,
		           (setPixelFormat || outputSurface) ? 1 : 0, outputSurface ? 1 : 0);
		ohos_avcodec_close_decoder(sys);
		return FALSE;
	}

	if (outputSurface)
	{
		rc = OH_VideoDecoder_SetSurface(sys->decoder, outputSurface);
		if (rc != AV_ERR_OK)
		{
			WLog_Print(h264->log, WLOG_WARN, "OHOS AVCodec set output surface failed rc=%d", (int)rc);
			ohos_avcodec_close_decoder(sys);
			return FALSE;
		}
		sys->outputSurface = outputSurface;
		sys->surfaceMode = TRUE;
	}

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
	sys->outputPixelFormat = sys->surfaceMode ? 0 : (setPixelFormat ? pixelFormat : AV_PIXEL_FORMAT_YUVI420);
	sys->outputStride = WINPR_ASSERTING_INT_CAST(int32_t, h264->width);
	sys->outputSliceHeight = WINPR_ASSERTING_INT_CAST(int32_t, h264->height);
	if (!sys->surfaceMode)
		ohos_avcodec_update_output_description(sys);
	return TRUE;
}

static BOOL ohos_avcodec_open_decoder(H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys)
{
	static const int32_t formats[] = { AV_PIXEL_FORMAT_YUVI420, AV_PIXEL_FORMAT_NV12,
		                               AV_PIXEL_FORMAT_NV21 };
	OHNativeWindow* outputSurface = NULL;
	volatile LONG* activeLogged = NULL;
	UINT32 surfaceWidth = 0;
	UINT32 surfaceHeight = 0;
	UINT64 surfaceGeneration = 0;

	if (!h264 || !sys || (h264->width == 0) || (h264->height == 0))
		return TRUE;

	sys->width = h264->width;
	sys->height = h264->height;
	sys->surfaceTarget = h264->ohosSurfaceTarget;
	sys->outputPixelFormat = AV_PIXEL_FORMAT_YUVI420;
	sys->outputStride = WINPR_ASSERTING_INT_CAST(int32_t, h264->width);
	sys->outputSliceHeight = WINPR_ASSERTING_INT_CAST(int32_t, h264->height);

	if (h264->ohosSurfaceModeAllowed)
	{
		BOOL hasSurface = FALSE;

		hasSurface = ohos_avcodec_get_output_surface(&outputSurface, &surfaceWidth,
		                                             &surfaceHeight, &surfaceGeneration);

		if (!hasSurface || !outputSurface)
		{
			WLog_Print(h264->log, WLOG_WARN,
			           "OHOS AVCodec surface mode required but no output surface is configured target=%u",
			           (unsigned)h264->ohosSurfaceTarget);
			return FALSE;
		}

		if (ohos_avcodec_configure_decoder(h264, sys, AV_PIXEL_FORMAT_SURFACE_FORMAT, TRUE,
		                                    outputSurface))
		{
			sys->surfaceGeneration = surfaceGeneration;
			goto success;
		}

		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec surface mode unavailable for %ux%u surface=%ux%u target=%u",
		           h264->width, h264->height, surfaceWidth, surfaceHeight,
		           (unsigned)h264->ohosSurfaceTarget);
		return FALSE;
	}

	for (size_t x = 0; x < ARRAYSIZE(formats); x++)
	{
		if (ohos_avcodec_configure_decoder(h264, sys, formats[x], TRUE, NULL))
			goto success;
	}

	if (ohos_avcodec_configure_decoder(h264, sys, 0, FALSE, NULL))
		goto success;

	WLog_Print(h264->log, WLOG_WARN,
	           "OHOS AVCodec hardware H264 decoder unavailable for %ux%u",
	           h264->width, h264->height);
	return FALSE;

success:
	activeLogged =
	    sys->surfaceMode ? &g_ohos_avcodec_surface_active_logged : &g_ohos_avcodec_buffer_active_logged;
	if (InterlockedCompareExchange(activeLogged, 1, 0) == 0)
	{
		WLog_Print(h264->log, WLOG_INFO,
		           "OHOS AVCodec H264 decoder active: %ux%u mode=%s format=%s(%d) stride=%d slice=%d async-buffer single-copy",
		           h264->width, h264->height, sys->surfaceMode ? "surface" : "buffer",
		           ohos_avcodec_pixel_format_name(sys->outputPixelFormat),
		           sys->outputPixelFormat, sys->outputStride, sys->outputSliceHeight);
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
	              !ohos_avcodec_surface_config_current(h264, sys);
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
			           " target=%u invalidations=%" PRIu64,
			           (unsigned)h264->ohosSurfaceTarget, invalidationCount);
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
		           "OHOS AVCodec surface decoder refreshed: target=%u generation=%" PRIu64
		           " -> %" PRIu64 " refreshes=%" PRIu64 " asyncError=%d",
		           (unsigned)h264->ohosSurfaceTarget, previousGeneration, sys->surfaceGeneration,
		           refreshCount, asyncError);
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

	free(sys->displayBuffer);
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
	BOOL hasOutput = FALSE;
	BOOL outputReadyAfterWait = FALSE;
	BOOL outputPtsValidAfterWait = FALSE;
	int64_t expectedOutputPts = 0;
	int64_t outputPtsAfterWait = 0;

	WINPR_ASSERT(h264);
	WINPR_ASSERT(pSrcData || (SrcSize == 0));

	sys = (H264_CONTEXT_OHOS_AVCODEC*)h264->pSystemData;
	if (!sys || !sys->primitivesReady)
		return ohos_avcodec_request_software_fallback(h264, sys, "decoder not initialized");
	if (h264->ohosSurfaceModeAllowed)
	{
		const int surfaceReady = ohos_avcodec_prepare_surface_decoder(h264, sys);
		if (surfaceReady == 0)
			return 0;
		if (surfaceReady < 0)
			return ohos_avcodec_request_software_fallback(h264, sys, "surface decoder refresh failed");
	}
	if (!sys->decoder || !sys->started)
		return ohos_avcodec_request_software_fallback(h264, sys, "decoder not started");

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
		const BOOL surfaceMode = sys->surfaceMode;
		pthread_mutex_unlock(&sys->lock);
		sys->inputWaitTimeouts++;
		sys->noOutputFrames++;
		ohos_avcodec_record_progress(sys);
		if (asyncError != 0)
			return ohos_avcodec_request_software_fallback(h264, sys, "async input error");
		if (surfaceMode)
		{
			if ((sys->inputWaitTimeouts <= 3) || ((sys->inputWaitTimeouts % 120) == 0))
				WLog_Print(h264->log, WLOG_WARN,
				           "OHOS AVCodec surface input backpressure count=%" PRIu64
				           " calls=%" PRIu64 "; keeping hardware decoder",
				           sys->inputWaitTimeouts, sys->decodeCalls);
			return 0;
		}
		if (sys->inputWaitTimeouts >= 6)
			return ohos_avcodec_request_software_fallback(h264, sys, "input buffer starvation");
		if ((sys->inputWaitTimeouts <= 3) || ((sys->inputWaitTimeouts % 120) == 0))
			WLog_Print(h264->log, WLOG_WARN,
			           "OHOS AVCodec input wait timeout count=%" PRIu64 " calls=%" PRIu64,
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

	if (sys->surfaceMode)
	{
		h264->surfaceRendered = TRUE;
		ohos_avcodec_record_progress(sys);
		if ((sys->decodeCalls <= 3) || ((sys->decodeCalls % 120) == 0))
		{
			WLog_Print(h264->log, WLOG_INFO,
			           "OHOS AVCodec surface input pushed call=%" PRIu64
			           " src=%u outputSurface=%p size=%ux%u rendered=%" PRIu64
			           " dropped=%" PRIu64 " failed=%" PRIu64,
			           sys->decodeCalls, SrcSize, (void*)sys->outputSurface, sys->width,
			           sys->height, sys->decodedFrames, sys->droppedOutputFrames,
			           sys->failedFrames);
		}
		return 1;
	}

	pthread_mutex_lock(&sys->lock);
	hasOutput = ohos_avcodec_wait_for_output(h264, sys, expectedOutputPts);
	if (!hasOutput)
	{
		outputReadyAfterWait = sys->outputReady;
		outputPtsValidAfterWait = sys->outputPtsValid;
		outputPtsAfterWait = sys->outputPts;
	}
	pthread_mutex_unlock(&sys->lock);

	if (!hasOutput)
	{
		const int32_t asyncError = sys->asyncError;
		sys->outputWaitTimeouts++;
		sys->noOutputFrames++;
		ohos_avcodec_record_progress(sys);
		if (asyncError != 0)
			return ohos_avcodec_request_software_fallback(h264, sys, "async output error");
		if (sys->outputWaitTimeouts >= (sys->surfaceMode ? OHOS_AVCODEC_SURFACE_OUTPUT_TIMEOUT_LIMIT
		                                                 : OHOS_AVCODEC_BUFFER_OUTPUT_TIMEOUT_LIMIT))
			return ohos_avcodec_request_software_fallback(h264, sys, "output buffer starvation");
		if ((sys->outputWaitTimeouts <= 3) || ((sys->outputWaitTimeouts % 120) == 0))
			WLog_Print(h264->log, WLOG_DEBUG,
			           "OHOS AVCodec output wait timeout count=%" PRIu64
			           " calls=%" PRIu64 " expectedPts=%" PRId64
			           " ready=%d outputPts=%" PRId64 " ptsValid=%d stale=%" PRIu64,
			           sys->outputWaitTimeouts, sys->decodeCalls, expectedOutputPts,
			           outputReadyAfterWait ? 1 : 0, outputPtsAfterWait,
			           outputPtsValidAfterWait ? 1 : 0, sys->staleOutputFrames);
		return 0;
	}

	sys->decodedFrames++;
	ohos_avcodec_record_progress(sys);
	if ((sys->decodedFrames <= 3) || ((sys->decodedFrames % 120) == 0))
	{
		WLog_Print(h264->log, WLOG_INFO,
		           "OHOS AVCodec decoded H264 frame=%" PRIu64 " calls=%" PRIu64
		           " noOutput=%" PRIu64 " failures=%" PRIu64 " dropped=%" PRIu64
		           " format=%s(%d) stride=%d slice=%d",
		           sys->decodedFrames, sys->decodeCalls, sys->noOutputFrames, sys->failedFrames,
		           sys->droppedOutputFrames, ohos_avcodec_pixel_format_name(sys->outputPixelFormat),
		           sys->outputPixelFormat, sys->outputStride, sys->outputSliceHeight);
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
