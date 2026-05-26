/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS AVCodec H.264 decoder
 *
 * Licensed under the Apache License, Version 2.0.
 */

#include "h264_ohos_avcodec_internal.h"

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

FREERDP_LOCAL void ohos_avcodec_get_surface_config_for_diagnostics(
    BOOL* enabled, UINT32* width, UINT32* height, UINT64* generation)
{
	pthread_mutex_lock(&g_ohos_avcodec_surface_lock);
	if (enabled)
		*enabled = g_ohos_avcodec_surface_enabled;
	if (width)
		*width = g_ohos_avcodec_surface_width;
	if (height)
		*height = g_ohos_avcodec_surface_height;
	if (generation)
		*generation = g_ohos_avcodec_surface_generation;
	pthread_mutex_unlock(&g_ohos_avcodec_surface_lock);
}

FREERDP_LOCAL int ohos_avcodec_request_software_fallback(H264_CONTEXT* h264,
                                                         H264_CONTEXT_OHOS_AVCODEC* sys,
                                                         const char* reason)
{
	pfnH264OhosAvcodecFallbackCallback callback = NULL;
	void* userData = NULL;
	const char* safeReason = reason ? reason : "unknown";

	if (sys)
		ohos_avcodec_record_progress(sys);

	ohos_avcodec_record_fallback(safeReason);

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

FREERDP_LOCAL BOOL ohos_avcodec_get_output_surface(OHNativeWindow** window, UINT32* width, UINT32* height,
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

FREERDP_LOCAL BOOL h264_context_ohos_output_surface_available(UINT32 width, UINT32 height)
{
	BOOL available = FALSE;

	pthread_mutex_lock(&g_ohos_avcodec_surface_lock);
	available = g_ohos_avcodec_surface_enabled && (g_ohos_avcodec_surface_window != NULL) &&
	            (width > 0) && (height > 0) && (g_ohos_avcodec_surface_width >= width) &&
	            (g_ohos_avcodec_surface_height >= height);
	pthread_mutex_unlock(&g_ohos_avcodec_surface_lock);
	return available;
}

FREERDP_LOCAL BOOL ohos_avcodec_surface_target_available(const H264_CONTEXT* h264)
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

FREERDP_LOCAL BOOL ohos_avcodec_surface_config_current(UINT32 width, UINT32 height,
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

FREERDP_LOCAL BOOL ohos_avcodec_mark_surface_decoder_active_logged(void)
{
	return InterlockedCompareExchange(&g_ohos_avcodec_surface_active_logged, 1, 0) == 0;
}
