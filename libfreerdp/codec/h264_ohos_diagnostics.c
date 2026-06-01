/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS AVCodec H.264 decoder
 *
 * Licensed under the Apache License, Version 2.0.
 */

#include "h264_ohos_avcodec_internal.h"

static pthread_mutex_t g_ohos_avcodec_stats_lock = PTHREAD_MUTEX_INITIALIZER;
static OHOS_AVCODEC_DIAGNOSTICS g_ohos_avcodec_stats = { 0 };

FREERDP_API const char* freerdp_ohos_avcodec_get_diagnostics(void)
{
	static char text[768];
	OHOS_AVCODEC_DIAGNOSTICS stats = { 0 };

	pthread_mutex_lock(&g_ohos_avcodec_stats_lock);
	stats = g_ohos_avcodec_stats;
	pthread_mutex_unlock(&g_ohos_avcodec_stats_lock);

	(void)snprintf(text, sizeof(text),
	               "ohos avcodec: attempts=%" PRIu64 " active=buffer:%" PRIu64
	               " calls=%" PRIu64 " decoded=%" PRIu64
	               " noOutput=%" PRIu64 " failed=%" PRIu64 " inputTimeout=%" PRIu64
	               " dropped=%" PRIu64 " fallbacks=%" PRIu64
	               " decoderRefresh=%" PRIu64
	               " last=%ux%u asyncError=%d reason=%s",
	               stats.decoderAttempts, stats.decoderActive, stats.decodeCalls,
	               stats.decodedFrames, stats.noOutputFrames, stats.failedFrames,
	               stats.inputWaitTimeouts, stats.droppedOutputFrames, stats.fallbackRequests,
	               stats.decoderRefreshes, stats.lastWidth, stats.lastHeight, stats.lastAsyncError,
	               stats.lastFallbackReason[0] == '\0' ? "none" : stats.lastFallbackReason);
	return text;
}

FREERDP_LOCAL void ohos_avcodec_record_decoder_attempt(void)
{
	pthread_mutex_lock(&g_ohos_avcodec_stats_lock);
	g_ohos_avcodec_stats.decoderAttempts++;
	pthread_mutex_unlock(&g_ohos_avcodec_stats_lock);
}

FREERDP_LOCAL void ohos_avcodec_record_decoder_active(const H264_CONTEXT_OHOS_AVCODEC* sys)
{
	if (!sys)
		return;

	pthread_mutex_lock(&g_ohos_avcodec_stats_lock);
	g_ohos_avcodec_stats.decoderActive++;
	g_ohos_avcodec_stats.lastWidth = sys->width;
	g_ohos_avcodec_stats.lastHeight = sys->height;
	pthread_mutex_unlock(&g_ohos_avcodec_stats_lock);
}

FREERDP_LOCAL void ohos_avcodec_record_progress(const H264_CONTEXT_OHOS_AVCODEC* sys)
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
	g_ohos_avcodec_stats.decoderRefreshes = sys->decoderRefreshes;
	g_ohos_avcodec_stats.lastWidth = sys->width;
	g_ohos_avcodec_stats.lastHeight = sys->height;
	g_ohos_avcodec_stats.lastAsyncError = sys->asyncError;
	pthread_mutex_unlock(&g_ohos_avcodec_stats_lock);
}

FREERDP_LOCAL void ohos_avcodec_record_fallback(const char* reason)
{
	const char* safeReason = reason ? reason : "unknown";

	pthread_mutex_lock(&g_ohos_avcodec_stats_lock);
	g_ohos_avcodec_stats.fallbackRequests++;
	(void)snprintf(g_ohos_avcodec_stats.lastFallbackReason,
	               sizeof(g_ohos_avcodec_stats.lastFallbackReason), "%s", safeReason);
	pthread_mutex_unlock(&g_ohos_avcodec_stats_lock);
}
