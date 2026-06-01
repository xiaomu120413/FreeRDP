/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS AVCodec H.264 decoder
 *
 * Licensed under the Apache License, Version 2.0.
 */

#include "h264_ohos_avcodec_internal.h"

FREERDP_LOCAL void ohos_avcodec_close_decoder(H264_CONTEXT_OHOS_AVCODEC* sys)
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

FREERDP_LOCAL BOOL ohos_avcodec_configure_decoder(H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys,
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

FREERDP_LOCAL BOOL ohos_avcodec_open_decoder(H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys)
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

	if (ohos_avcodec_mark_surface_decoder_active_logged())
	{
		WLog_Print(h264->log, WLOG_INFO,
		           "OHOS AVCodec AVC420 surface decoder active: %ux%u surface=%p target=%ux%u async-surface",
		           h264->width, h264->height, (void*)outputSurface, surfaceWidth, surfaceHeight);
	}
	ohos_avcodec_record_decoder_active(sys);
	return TRUE;
}

FREERDP_LOCAL int ohos_avcodec_prepare_surface_decoder(H264_CONTEXT* h264,
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

FREERDP_LOCAL BOOL ohos_avcodec_init_primitives(H264_CONTEXT_OHOS_AVCODEC* sys)
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

FREERDP_LOCAL void ohos_avcodec_uninit(H264_CONTEXT* h264)
{
	H264_CONTEXT_OHOS_AVCODEC* sys = NULL;

	if (!h264)
		return;

	sys = (H264_CONTEXT_OHOS_AVCODEC*)h264->pSystemData;
	if (!sys)
		return;

	ohos_avcodec_close_encoder(sys);
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

FREERDP_LOCAL BOOL ohos_avcodec_init(H264_CONTEXT* h264)
{
	H264_CONTEXT_OHOS_AVCODEC* sys = NULL;

	if (!h264)
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

	if (h264->Compressor)
	{
		if (ohos_avcodec_open_encoder(h264, sys))
			return TRUE;

		ohos_avcodec_uninit(h264);
		return FALSE;
	}

	if (ohos_avcodec_open_decoder(h264, sys))
		return TRUE;

	ohos_avcodec_uninit(h264);
	return FALSE;
}

FREERDP_LOCAL void ohos_avcodec_return_empty_input(H264_CONTEXT_OHOS_AVCODEC* sys, uint32_t inputIndex,
                                            OH_AVBuffer* inputBuffer)
{
	OH_AVCodecBufferAttr attr = { 0 };

	if (!sys || !sys->decoder || !inputBuffer)
		return;

	OH_AVBuffer_SetBufferAttr(inputBuffer, &attr);
	OH_VideoDecoder_PushInputBuffer(sys->decoder, inputIndex);
}

FREERDP_LOCAL int ohos_avcodec_decompress(H264_CONTEXT* WINPR_RESTRICT h264,
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

const H264_CONTEXT_SUBSYSTEM g_Subsystem_OHOS_AVCodec = {
	"OHOS-AVCodec", ohos_avcodec_init, ohos_avcodec_uninit, ohos_avcodec_decompress,
	ohos_avcodec_compress
};
