/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS AVCodec H.264 decoder fallback state
 *
 * Licensed under the Apache License, Version 2.0.
 */

#include "h264_ohos_avcodec_internal.h"

static pthread_mutex_t g_ohos_avcodec_callback_lock = PTHREAD_MUTEX_INITIALIZER;
static pfnH264OhosAvcodecFallbackCallback g_ohos_avcodec_fallback_callback = NULL;
static void* g_ohos_avcodec_fallback_callback_user_data = NULL;

FREERDP_API BOOL freerdp_ohos_avcodec_set_fallback_callback(
    pfnH264OhosAvcodecFallbackCallback callback, void* userData)
{
	pthread_mutex_lock(&g_ohos_avcodec_callback_lock);
	g_ohos_avcodec_fallback_callback = callback;
	g_ohos_avcodec_fallback_callback_user_data = userData;
	pthread_mutex_unlock(&g_ohos_avcodec_callback_lock);
	return TRUE;
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

	pthread_mutex_lock(&g_ohos_avcodec_callback_lock);
	callback = g_ohos_avcodec_fallback_callback;
	userData = g_ohos_avcodec_fallback_callback_user_data;
	pthread_mutex_unlock(&g_ohos_avcodec_callback_lock);

	if (h264 && h264->log)
		WLog_Print(h264->log, WLOG_WARN, "OHOS AVCodec software fallback requested: %s",
		           safeReason);
	if (callback)
		callback(safeReason, userData);
	return H264_OHOS_AVCODEC_FALLBACK_RC;
}
