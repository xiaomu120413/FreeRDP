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
	sys->started = FALSE;
	sys->decoderBufferMode = FALSE;
	ohos_avcodec_reset_async_state(sys);
}

FREERDP_LOCAL BOOL ohos_avcodec_configure_decoder(H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys)
{
	bool isValid = false;
	OH_AVErrCode rc = AV_ERR_OK;
	OH_AVFormat* format = NULL;
	int32_t pixelFormat = AV_PIXEL_FORMAT_NV12;

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

	format = OH_AVFormat_CreateVideoFormat(OH_AVCODEC_MIMETYPE_VIDEO_AVC,
	                                       WINPR_ASSERTING_INT_CAST(int32_t, h264->width),
	                                       WINPR_ASSERTING_INT_CAST(int32_t, h264->height));
	if (!format)
	{
		ohos_avcodec_close_decoder(sys);
		return FALSE;
	}

	pixelFormat = ohos_avcodec_choose_decoder_pixel_format(capability, h264->log);
	OH_AVFormat_SetIntValue(format, OH_MD_KEY_PIXEL_FORMAT, pixelFormat);
	OH_AVFormat_SetIntValue(format, OH_MD_KEY_ENABLE_SYNC_MODE, 1);
	OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENABLE_LOW_LATENCY, 1);
	OH_AVFormat_SetIntValue(format, OH_MD_KEY_MAX_INPUT_SIZE,
	                        WINPR_ASSERTING_INT_CAST(
	                            int32_t, MAX((UINT64)h264->width * h264->height, 1024ULL * 1024ULL)));
	sys->decoderPixelFormat = pixelFormat;
	sys->outputWidth = h264->width;
	sys->outputHeight = h264->height;
	sys->outputStride = h264->width;
	sys->outputSliceHeight = h264->height;

	rc = OH_VideoDecoder_Configure(sys->decoder, format);
	OH_AVFormat_Destroy(format);
	if (rc != AV_ERR_OK)
	{
		WLog_Print(h264->log, WLOG_DEBUG,
		           "OHOS AVCodec decoder configure failed rc=%d pixelFormat=%s",
		           (int)rc, ohos_avcodec_decoder_pixel_format_name(pixelFormat));
		ohos_avcodec_close_decoder(sys);
		return FALSE;
	}

	sys->decoderBufferMode = TRUE;

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
	(void)ohos_avcodec_update_decoder_output_description(h264, sys, "start");
	return TRUE;
}

FREERDP_LOCAL BOOL ohos_avcodec_open_decoder(H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys)
{
	if (!h264 || !sys || (h264->width == 0) || (h264->height == 0))
		return TRUE;

	sys->width = h264->width;
	sys->height = h264->height;

	if (!h264->ohosAvcodecBufferModeAllowed)
	{
		WLog_Print(h264->log, WLOG_DEBUG,
		           "OHOS AVCodec skipped: AVC420 buffer hardware decode mode is not allowed");
		return FALSE;
	}

	if (!ohos_avcodec_configure_decoder(h264, sys))
	{
		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec AVC420 buffer mode unavailable for %ux%u", h264->width,
		           h264->height);
		return FALSE;
	}

	WLog_Print(h264->log, WLOG_INFO,
	           "OHOS AVCodec AVC420 buffer decoder active: %ux%u format=%s bounded-sync-buffer",
	           h264->width, h264->height,
	           ohos_avcodec_decoder_pixel_format_name(sys->decoderPixelFormat));
	ohos_avcodec_record_decoder_active(sys);
	return TRUE;
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
	H264_CONTEXT_OHOS_AVCODEC* sys = NULL;
	int decoderReady = 0;

	WINPR_ASSERT(h264);
	WINPR_ASSERT(pSrcData || (SrcSize == 0));

	sys = (H264_CONTEXT_OHOS_AVCODEC*)h264->pSystemData;
	if (!sys || !sys->primitivesReady)
		return ohos_avcodec_request_software_fallback(h264, sys, "decoder not initialized");
	if (!h264->ohosAvcodecBufferModeAllowed)
		return ohos_avcodec_request_software_fallback(h264, sys,
		                                               "buffer hardware decode mode not allowed");

	decoderReady = ohos_avcodec_prepare_buffer_decoder(h264, sys);
	if (decoderReady == 0)
		return 0;
	if (decoderReady < 0)
		return ohos_avcodec_request_software_fallback(h264, sys, "decoder refresh failed");

	if (!sys->decoder || !sys->started)
		return ohos_avcodec_request_software_fallback(h264, sys, "decoder not started");
	if (!sys->decoderBufferMode)
		return ohos_avcodec_request_software_fallback(h264, sys, "decoder output mode not active");

	return ohos_avcodec_decompress_buffer(h264, sys, pSrcData, SrcSize);
}

const H264_CONTEXT_SUBSYSTEM g_Subsystem_OHOS_AVCodec = {
	"OHOS-AVCodec", ohos_avcodec_init, ohos_avcodec_uninit, ohos_avcodec_decompress,
	ohos_avcodec_compress
};
