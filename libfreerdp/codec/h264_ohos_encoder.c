/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS AVCodec H.264 encoder
 *
 * Licensed under the Apache License, Version 2.0.
 */

#include "h264_ohos_avcodec_internal.h"

static void ohos_avcodec_free_output_slot(OHOS_AVCODEC_OUTPUT_SLOT* slot)
{
	if (!slot)
		return;

	free(slot->data);
	ZeroMemory(slot, sizeof(*slot));
}

static BOOL ohos_avcodec_reserve(BYTE** data, UINT32* capacity, UINT32 size)
{
	BYTE* tmp = NULL;

	if (!data || !capacity)
		return FALSE;
	if (size <= *capacity)
		return TRUE;

	tmp = (BYTE*)realloc(*data, size);
	if (!tmp)
		return FALSE;

	*data = tmp;
	*capacity = size;
	return TRUE;
}

static const char* ohos_avcodec_pixel_format_name(int32_t pixelFormat)
{
	switch (pixelFormat)
	{
		case AV_PIXEL_FORMAT_NV12:
			return "NV12";
		case AV_PIXEL_FORMAT_NV21:
			return "NV21";
		default:
			return "unknown";
	}
}

static BOOL ohos_avcodec_pixel_format_in_list(const int32_t* formats, uint32_t count,
                                              int32_t pixelFormat)
{
	if (!formats || (count == 0))
		return FALSE;

	for (uint32_t x = 0; x < count; x++)
	{
		if (formats[x] == pixelFormat)
			return TRUE;
	}
	return FALSE;
}

static int32_t ohos_avcodec_choose_encoder_pixel_format(OH_AVCapability* capability, wLog* log)
{
	const int32_t* formats = NULL;
	uint32_t count = 0;
	const OH_AVErrCode rc =
	    OH_AVCapability_GetVideoSupportedPixelFormats(capability, &formats, &count);

	if ((rc == AV_ERR_OK) && formats && (count > 0))
	{
		char detail[128] = { 0 };
		size_t offset = 0;

		for (uint32_t x = 0; x < count && offset < sizeof(detail); x++)
		{
			const int written =
			    snprintf(&detail[offset], sizeof(detail) - offset, "%s%d",
			             (x == 0) ? "" : ",", formats[x]);
			if (written < 0)
				break;
			offset += (size_t)written;
		}
		WLog_Print(log, WLOG_INFO, "OHOS AVCodec encoder supported pixel formats: [%s]",
		           detail);

		if (ohos_avcodec_pixel_format_in_list(formats, count, AV_PIXEL_FORMAT_NV21))
			return AV_PIXEL_FORMAT_NV21;
		if (ohos_avcodec_pixel_format_in_list(formats, count, AV_PIXEL_FORMAT_NV12))
			return AV_PIXEL_FORMAT_NV12;

		WLog_Print(log, WLOG_WARN,
		           "OHOS AVCodec encoder lists no NV21/NV12 pixel format; trying NV21");
	}
	else
	{
		WLog_Print(log, WLOG_WARN,
		           "OHOS AVCodec encoder pixel format query failed rc=%d count=%u; trying NV21",
		           (int)rc, count);
	}

	return AV_PIXEL_FORMAT_NV21;
}

static void ohos_avcodec_clear_encoder_outputs(H264_CONTEXT_OHOS_AVCODEC* sys)
{
	if (!sys)
		return;

	for (UINT32 x = 0; x < OHOS_AVCODEC_OUTPUT_QUEUE_LENGTH; x++)
		ohos_avcodec_free_output_slot(&sys->outputQueue[x]);

	sys->outputHead = 0;
	sys->outputTail = 0;
	sys->outputCount = 0;
}

static void ohos_avcodec_encoder_on_new_output_buffer(OH_AVCodec* codec, uint32_t index,
                                                      OH_AVBuffer* buffer, void* userData)
{
	OH_AVCodecBufferAttr attr = { 0 };
	H264_CONTEXT_OHOS_AVCODEC* sys = (H264_CONTEXT_OHOS_AVCODEC*)userData;
	BYTE* src = NULL;
	BYTE* copy = NULL;
	int32_t capacity = 0;
	BOOL copied = FALSE;

	if (!sys || !sys->primitivesReady || !buffer)
		return;

	if (OH_AVBuffer_GetBufferAttr(buffer, &attr) != AV_ERR_OK)
		ZeroMemory(&attr, sizeof(attr));

	capacity = OH_AVBuffer_GetCapacity(buffer);
	src = OH_AVBuffer_GetAddr(buffer);
	if ((attr.size > 0) && src && (capacity >= 0) && (attr.offset >= 0) &&
	    ((UINT32)attr.offset <= (UINT32)capacity) &&
	    ((UINT32)attr.size <= ((UINT32)capacity - (UINT32)attr.offset)))
	{
		copy = (BYTE*)malloc((size_t)attr.size);
		if (copy)
		{
			CopyMemory(copy, &src[attr.offset], (size_t)attr.size);
			copied = TRUE;
		}
	}

	pthread_mutex_lock(&sys->lock);
	if ((attr.flags & AVCODEC_BUFFER_FLAGS_EOS) == 0)
	{
		if ((attr.flags & AVCODEC_BUFFER_FLAGS_CODEC_DATA) != 0)
		{
			if (copied && ohos_avcodec_reserve(&sys->codecConfig, &sys->codecConfigCapacity,
			                                   WINPR_ASSERTING_INT_CAST(UINT32, attr.size)))
			{
				CopyMemory(sys->codecConfig, copy, (size_t)attr.size);
				sys->codecConfigSize = WINPR_ASSERTING_INT_CAST(UINT32, attr.size);
				sys->prependCodecConfig = TRUE;
			}
			else
			{
				sys->failedFrames++;
				sys->asyncError = AV_ERR_NO_MEMORY;
			}
		}
		else if (copied)
		{
			if (sys->outputCount < OHOS_AVCODEC_OUTPUT_QUEUE_LENGTH)
			{
				OHOS_AVCODEC_OUTPUT_SLOT* slot = &sys->outputQueue[sys->outputTail];
				ohos_avcodec_free_output_slot(slot);
				slot->data = copy;
				slot->size = WINPR_ASSERTING_INT_CAST(UINT32, attr.size);
				slot->flags = attr.flags;
				slot->pts = attr.pts;
				copy = NULL;
				sys->outputTail = (sys->outputTail + 1) % OHOS_AVCODEC_OUTPUT_QUEUE_LENGTH;
				sys->outputCount++;
			}
			else
			{
				if (!sys->outputQueueFullLogged)
				{
					WLog_Print(sys->log, WLOG_WARN, "OHOS AVCodec encoder output queue full");
					sys->outputQueueFullLogged = TRUE;
				}
				sys->droppedOutputFrames++;
			}
		}
		else if (attr.size > 0)
		{
			sys->failedFrames++;
			sys->asyncError = AV_ERR_NO_MEMORY;
		}
	}
	pthread_cond_broadcast(&sys->cond);
	pthread_mutex_unlock(&sys->lock);

	free(copy);
	OH_VideoEncoder_FreeOutputBuffer(codec, index);
}

FREERDP_LOCAL void ohos_avcodec_close_encoder(H264_CONTEXT_OHOS_AVCODEC* sys)
{
	if (!sys)
		return;

	if (sys->encoder)
	{
		if (sys->started)
			OH_VideoEncoder_Stop(sys->encoder);
		OH_VideoEncoder_Destroy(sys->encoder);
	}

	sys->encoder = NULL;
	sys->started = FALSE;
	sys->encoderMode = FALSE;

	if (sys->primitivesReady)
	{
		pthread_mutex_lock(&sys->lock);
		sys->inputHead = 0;
		sys->inputTail = 0;
		sys->inputCount = 0;
		sys->asyncError = 0;
		ohos_avcodec_clear_encoder_outputs(sys);
		pthread_cond_broadcast(&sys->cond);
		pthread_mutex_unlock(&sys->lock);
	}

	free(sys->codecConfig);
	sys->codecConfig = NULL;
	sys->codecConfigSize = 0;
	sys->codecConfigCapacity = 0;
	sys->nalLengthSize = 0;
	sys->encoderPixelFormat = 0;
	sys->inputFrameLayoutLogged = FALSE;
	sys->bitstreamLayoutLogged = FALSE;
	sys->bitstreamConversionWarningLogged = FALSE;
	free(sys->encodedData);
	sys->encodedData = NULL;
	sys->encodedSize = 0;
	sys->encodedCapacity = 0;
}

FREERDP_LOCAL BOOL ohos_avcodec_open_encoder(H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys)
{
	bool isValid = false;
	const UINT32 frameRate = h264 && (h264->FrameRate > 0) ? h264->FrameRate : 30;
	UINT32 bitrate = h264 && (h264->BitRate > 0) ? h264->BitRate : 1000000;
	int32_t preferredPixelFormat = AV_PIXEL_FORMAT_NV21;
	int32_t configuredPixelFormat = AV_PIXEL_FORMAT_NV21;
	OH_BitrateMode bitrateMode = BITRATE_MODE_CBR;
	OH_BitrateMode configuredBitrateMode = BITRATE_MODE_CBR;
	OH_AVErrCode rc = AV_ERR_OK;
	OH_AVCodecCallback callback = { 0 };

	if (!h264 || !sys || !h264->Compressor || !h264->hwAccel || (h264->width == 0) ||
	    (h264->height == 0) || ((h264->width % 2) != 0) || ((h264->height % 2) != 0))
		return FALSE;

	OH_AVCapability* capability =
	    OH_AVCodec_GetCapabilityByCategory(OH_AVCODEC_MIMETYPE_VIDEO_AVC, true, HARDWARE);
	if (capability)
	{
		const char* name = OH_AVCapability_GetName(capability);
		if (name && name[0])
		{
			sys->encoder = OH_VideoEncoder_CreateByName(name);
			if (sys->encoder)
				WLog_Print(h264->log, WLOG_INFO, "OHOS AVCodec hardware H264 encoder selected: %s",
				           name);
			else
				WLog_Print(h264->log, WLOG_WARN,
				           "OHOS AVCodec hardware encoder create failed: %s", name);
		}
		preferredPixelFormat = ohos_avcodec_choose_encoder_pixel_format(capability, h264->log);
	}
	if (!sys->encoder)
		return FALSE;
	bitrate = ohos_avcodec_effective_encoder_bitrate(h264, bitrate);
	bitrateMode = ohos_avcodec_select_bitrate_mode(h264, capability);
	configuredBitrateMode = bitrateMode;

	rc = OH_VideoEncoder_IsValid(sys->encoder, &isValid);
	if ((rc != AV_ERR_OK) || !isValid)
	{
		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec hardware H264 encoder invalid: valid=%d rc=%d",
		           isValid ? 1 : 0, (int)rc);
		ohos_avcodec_close_encoder(sys);
		return FALSE;
	}

	callback.onError = ohos_avcodec_on_error;
	callback.onStreamChanged = ohos_avcodec_on_stream_changed;
	callback.onNeedInputBuffer = ohos_avcodec_on_need_input_buffer;
	callback.onNewOutputBuffer = ohos_avcodec_encoder_on_new_output_buffer;
	rc = OH_VideoEncoder_RegisterCallback(sys->encoder, callback, sys);
	if (rc != AV_ERR_OK)
	{
		WLog_Print(h264->log, WLOG_WARN, "OHOS AVCodec encoder register callback failed rc=%d",
		           (int)rc);
		ohos_avcodec_close_encoder(sys);
		return FALSE;
	}

	rc = ohos_avcodec_configure_encoder(h264, sys, frameRate, bitrate, preferredPixelFormat,
	                                    configuredBitrateMode);
	if ((rc != AV_ERR_OK) && (configuredBitrateMode != BITRATE_MODE_CBR))
	{
		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec encoder configure failed rc=%d mode=%s; retrying CBR",
		           (int)rc, ohos_avcodec_bitrate_mode_name(configuredBitrateMode));
		configuredBitrateMode = BITRATE_MODE_CBR;
		rc = ohos_avcodec_configure_encoder(h264, sys, frameRate, bitrate, preferredPixelFormat,
		                                    configuredBitrateMode);
	}
	if (rc != AV_ERR_OK)
	{
		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec encoder configure failed rc=%d size=%ux%u format=%s mode=%s",
		           (int)rc, h264->width, h264->height,
		           ohos_avcodec_pixel_format_name(preferredPixelFormat),
		           ohos_avcodec_bitrate_mode_name(configuredBitrateMode));
		if (preferredPixelFormat != AV_PIXEL_FORMAT_NV12)
		{
			configuredPixelFormat = AV_PIXEL_FORMAT_NV12;
			configuredBitrateMode = bitrateMode;
			rc = ohos_avcodec_configure_encoder(h264, sys, frameRate, bitrate,
			                                    configuredPixelFormat, configuredBitrateMode);
			if ((rc != AV_ERR_OK) && (configuredBitrateMode != BITRATE_MODE_CBR))
			{
				configuredBitrateMode = BITRATE_MODE_CBR;
				rc = ohos_avcodec_configure_encoder(h264, sys, frameRate, bitrate,
				                                    configuredPixelFormat, configuredBitrateMode);
			}
		}
		if (rc != AV_ERR_OK)
		{
			WLog_Print(h264->log, WLOG_WARN,
			           "OHOS AVCodec encoder configure fallback failed rc=%d size=%ux%u format=NV12 mode=%s",
			           (int)rc, h264->width, h264->height,
			           ohos_avcodec_bitrate_mode_name(configuredBitrateMode));
			ohos_avcodec_close_encoder(sys);
			return FALSE;
		}
	}
	else
		configuredPixelFormat = preferredPixelFormat;
	sys->encoderPixelFormat = configuredPixelFormat;

	rc = OH_VideoEncoder_Prepare(sys->encoder);
	if (rc != AV_ERR_OK)
	{
		WLog_Print(h264->log, WLOG_WARN, "OHOS AVCodec encoder prepare failed rc=%d", (int)rc);
		ohos_avcodec_close_encoder(sys);
		return FALSE;
	}

	rc = OH_VideoEncoder_Start(sys->encoder);
	if (rc != AV_ERR_OK)
	{
		WLog_Print(h264->log, WLOG_WARN, "OHOS AVCodec encoder start failed rc=%d", (int)rc);
		ohos_avcodec_close_encoder(sys);
		return FALSE;
	}

	sys->started = TRUE;
	sys->encoderMode = TRUE;
	sys->width = h264->width;
	sys->height = h264->height;
	WLog_Print(h264->log, WLOG_INFO,
	           "OHOS AVCodec buffer encoder active: %ux%u@%u bitrate=%u mode=%s qp=%u input=%s",
	           h264->width, h264->height, frameRate, bitrate,
	           ohos_avcodec_bitrate_mode_name(configuredBitrateMode), h264->QP,
	           ohos_avcodec_pixel_format_name(sys->encoderPixelFormat));
	return TRUE;
}

static BOOL ohos_avcodec_copy_yuv420sp_frame(H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys,
                                             const BYTE** src, const UINT32* stride, BYTE* dst,
                                             UINT32 dstSize)
{
	const UINT32 chromaHeight = h264->height / 2;
	const UINT32 lumaSize = h264->width * h264->height;
	const UINT32 frameSize = lumaSize + (h264->width * chromaHeight);

	if (!h264 || !src || !stride || !dst || (dstSize < frameSize) || !src[0] || !src[1] ||
	    (stride[0] < h264->width) || (stride[1] < h264->width))
		return FALSE;

	for (UINT32 y = 0; y < h264->height; y++)
		CopyMemory(&dst[y * h264->width], &src[0][y * stride[0]], h264->width);

	for (UINT32 y = 0; y < chromaHeight; y++)
	{
		BYTE* target = &dst[lumaSize + (y * h264->width)];
		const BYTE* source = &src[1][y * stride[1]];

		if (sys && (sys->encoderPixelFormat == AV_PIXEL_FORMAT_NV21))
		{
			for (UINT32 x = 0; x < h264->width; x += 2)
			{
				target[x] = source[x + 1];
				target[x + 1] = source[x];
			}
		}
		else
			CopyMemory(target, source, h264->width);
	}

	if (sys && !sys->inputFrameLayoutLogged)
	{
		const BYTE* uv = &dst[lumaSize];
		WLog_Print(sys->log, WLOG_INFO,
		           "OHOS AVCodec encoder input frame: source=NV12 queued=%s y0=%u uv0=%u,%u",
		           ohos_avcodec_pixel_format_name(sys->encoderPixelFormat), dst[0], uv[0],
		           uv[1]);
		sys->inputFrameLayoutLogged = TRUE;
	}

	return TRUE;
}

static void ohos_avcodec_return_empty_encoder_input(H264_CONTEXT_OHOS_AVCODEC* sys,
                                                    uint32_t inputIndex, OH_AVBuffer* inputBuffer)
{
	OH_AVCodecBufferAttr attr = { 0 };

	if (!sys || !sys->encoder || !inputBuffer)
		return;

	OH_AVBuffer_SetBufferAttr(inputBuffer, &attr);
	OH_VideoEncoder_PushInputBuffer(sys->encoder, inputIndex);
}

static BOOL ohos_avcodec_build_encoded_output(H264_CONTEXT_OHOS_AVCODEC* sys, BYTE** ppDstData,
                                              UINT32* pDstSize)
{
	OHOS_AVCODEC_OUTPUT_SLOT slot = { 0 };
	const BOOL prependConfig =
	    ((sys->outputQueue[sys->outputHead].flags & AVCODEC_BUFFER_FLAGS_SYNC_FRAME) != 0 &&
	     sys->prependCodecConfig && sys->codecConfig && (sys->codecConfigSize > 0));

	slot = sys->outputQueue[sys->outputHead];
	ZeroMemory(&sys->outputQueue[sys->outputHead], sizeof(sys->outputQueue[sys->outputHead]));
	sys->outputHead = (sys->outputHead + 1) % OHOS_AVCODEC_OUTPUT_QUEUE_LENGTH;
	sys->outputCount--;

	if (!ohos_avcodec_build_annexb_sample(sys, prependConfig ? sys->codecConfig : NULL,
	                                      prependConfig ? sys->codecConfigSize : 0, slot.data,
	                                      slot.size, &sys->encodedData, &sys->encodedCapacity,
	                                      &sys->encodedSize))
	{
		ohos_avcodec_free_output_slot(&slot);
		return FALSE;
	}
	if (prependConfig)
		sys->prependCodecConfig = FALSE;

	sys->encodedFrames++;
	*ppDstData = sys->encodedData;
	*pDstSize = sys->encodedSize;
	ohos_avcodec_free_output_slot(&slot);
	return TRUE;
}

FREERDP_LOCAL int ohos_avcodec_compress(H264_CONTEXT* WINPR_RESTRICT h264,
                                        const BYTE** WINPR_RESTRICT ppSrcYuv,
                                        const UINT32* WINPR_RESTRICT pStride,
                                        BYTE** WINPR_RESTRICT ppDstData,
                                        UINT32* WINPR_RESTRICT pDstSize)
{
	const UINT32 chromaHeight = h264 ? (h264->height / 2) : 0;
	const UINT32 lumaSize = h264 ? (h264->width * h264->height) : 0;
	const UINT32 frameSize = h264 ? (lumaSize + (h264->width * chromaHeight)) : 0;
	uint32_t inputIndex = 0;
	OH_AVBuffer* inputBuffer = NULL;
	OH_AVCodecBufferAttr inputAttr = { 0 };
	H264_CONTEXT_OHOS_AVCODEC* sys = NULL;
	BYTE* inputData = NULL;
	int32_t capacity = 0;
	OH_AVErrCode rc = AV_ERR_OK;
	struct timespec deadline = { 0 };
	int waitRc = 0;
	int64_t pts = 0;
	BOOL ok = FALSE;

	if (!h264 || !ppSrcYuv || !pStride || !ppDstData || !pDstSize || (frameSize == 0) ||
	    (frameSize < lumaSize) || (frameSize > INT32_MAX))
		return -1;

	sys = (H264_CONTEXT_OHOS_AVCODEC*)h264->pSystemData;
	if (!sys || !sys->encoder || !sys->started || !sys->encoderMode)
		return -1;

	pthread_mutex_lock(&sys->lock);
	if (!ohos_avcodec_wait_for_input(sys, &inputIndex, &inputBuffer))
	{
		sys->inputWaitTimeouts++;
		pthread_mutex_unlock(&sys->lock);
		WLog_Print(h264->log, WLOG_WARN, "OHOS AVCodec encoder input wait timed out");
		return -1;
	}
	pthread_mutex_unlock(&sys->lock);

	capacity = OH_AVBuffer_GetCapacity(inputBuffer);
	inputData = OH_AVBuffer_GetAddr(inputBuffer);
	if (!inputData || (capacity < 0) || ((UINT32)capacity < frameSize) ||
	    !ohos_avcodec_copy_yuv420sp_frame(h264, sys, ppSrcYuv, pStride, inputData,
	                                      (UINT32)capacity))
	{
		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec encoder input buffer invalid capacity=%d frameSize=%u",
		           capacity, frameSize);
		ohos_avcodec_return_empty_encoder_input(sys, inputIndex, inputBuffer);
		return -1;
	}

	sys->encodeCalls++;
	pts = ((int64_t)sys->encodeCalls * 1000000LL) /
	      WINPR_ASSERTING_INT_CAST(int64_t, h264->FrameRate ? h264->FrameRate : 30);
	inputAttr.pts = pts;
	inputAttr.size = WINPR_ASSERTING_INT_CAST(int32_t, frameSize);
	inputAttr.offset = 0;
	inputAttr.flags = AVCODEC_BUFFER_FLAGS_NONE;
	if (OH_AVBuffer_SetBufferAttr(inputBuffer, &inputAttr) != AV_ERR_OK)
	{
		ohos_avcodec_return_empty_encoder_input(sys, inputIndex, inputBuffer);
		return -1;
	}

	rc = OH_VideoEncoder_PushInputBuffer(sys->encoder, inputIndex);
	if (rc != AV_ERR_OK)
	{
		WLog_Print(h264->log, WLOG_WARN, "OHOS AVCodec encoder push input failed rc=%d",
		           (int)rc);
		return -1;
	}

	ohos_avcodec_make_deadline(&deadline, OHOS_AVCODEC_OUTPUT_WAIT_MS);
	pthread_mutex_lock(&sys->lock);
	while ((sys->outputCount == 0) && (sys->asyncError == 0))
	{
		waitRc = pthread_cond_timedwait(&sys->cond, &sys->lock, &deadline);
		if (waitRc == ETIMEDOUT)
			break;
	}

	if ((sys->asyncError == 0) && (sys->outputCount > 0))
		ok = ohos_avcodec_build_encoded_output(sys, ppDstData, pDstSize);
	else
	{
		sys->outputWaitTimeouts++;
		sys->noOutputFrames++;
	}
	pthread_mutex_unlock(&sys->lock);

	if (!ok)
	{
		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec encoder output unavailable calls=%" PRIu64
		           " encoded=%" PRIu64 " waits=%" PRIu64 " asyncError=%d",
		           sys->encodeCalls, sys->encodedFrames, sys->outputWaitTimeouts,
		           sys->asyncError);
		return -1;
	}

	if ((sys->encodedFrames <= 3) || ((sys->encodedFrames % 120) == 0))
	{
		WLog_Print(h264->log, WLOG_INFO,
		           "OHOS AVCodec encoded frame=%" PRIu64 " size=%u calls=%" PRIu64,
		           sys->encodedFrames, *pDstSize, sys->encodeCalls);
	}
	return 1;
}
