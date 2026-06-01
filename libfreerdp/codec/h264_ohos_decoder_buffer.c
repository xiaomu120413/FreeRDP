/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS AVCodec H.264 buffer decoder copy-out
 *
 * Licensed under the Apache License, Version 2.0.
 */

#include "h264_ohos_avcodec_internal.h"

FREERDP_LOCAL const char* ohos_avcodec_decoder_pixel_format_name(int32_t pixelFormat)
{
	switch (pixelFormat)
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

static BOOL ohos_avcodec_pixel_format_supported(const int32_t* formats, uint32_t count,
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

FREERDP_LOCAL int32_t ohos_avcodec_choose_decoder_pixel_format(OH_AVCapability* capability,
                                                               wLog* log)
{
	const int32_t* formats = NULL;
	uint32_t count = 0;
	const OH_AVErrCode rc =
	    OH_AVCapability_GetVideoSupportedPixelFormats(capability, &formats, &count);

	if ((rc == AV_ERR_OK) && formats && (count > 0))
	{
		char detail[160] = { 0 };
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
		WLog_Print(log, WLOG_INFO, "OHOS AVCodec decoder supported pixel formats: [%s]",
		           detail);

		if (ohos_avcodec_pixel_format_supported(formats, count, AV_PIXEL_FORMAT_NV12))
			return AV_PIXEL_FORMAT_NV12;
		if (ohos_avcodec_pixel_format_supported(formats, count, AV_PIXEL_FORMAT_NV21))
			return AV_PIXEL_FORMAT_NV21;
		if (ohos_avcodec_pixel_format_supported(formats, count, AV_PIXEL_FORMAT_YUVI420))
			return AV_PIXEL_FORMAT_YUVI420;
	}
	else
	{
		WLog_Print(log, WLOG_WARN,
		           "OHOS AVCodec decoder pixel format query failed rc=%d count=%u; trying NV12",
		           (int)rc, count);
	}

	return AV_PIXEL_FORMAT_NV12;
}

FREERDP_LOCAL BOOL ohos_avcodec_update_decoder_output_description(
    H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys, const char* reason)
{
	int32_t value = 0;
	OH_AVFormat* description = NULL;

	if (!h264 || !sys || !sys->decoder)
		return FALSE;

	description = OH_VideoDecoder_GetOutputDescription(sys->decoder);
	if (!description)
		return FALSE;

	pthread_mutex_lock(&sys->lock);
	if (OH_AVFormat_GetIntValue(description, OH_MD_KEY_PIXEL_FORMAT, &value))
		sys->decoderPixelFormat = value;
	if (OH_AVFormat_GetIntValue(description, OH_MD_KEY_WIDTH, &value) && (value > 0))
		sys->outputWidth = WINPR_ASSERTING_INT_CAST(UINT32, value);
	if (OH_AVFormat_GetIntValue(description, OH_MD_KEY_HEIGHT, &value) && (value > 0))
		sys->outputHeight = WINPR_ASSERTING_INT_CAST(UINT32, value);
	if (OH_AVFormat_GetIntValue(description, OH_MD_KEY_VIDEO_PIC_WIDTH, &value) && (value > 0))
		sys->outputWidth = WINPR_ASSERTING_INT_CAST(UINT32, value);
	if (OH_AVFormat_GetIntValue(description, OH_MD_KEY_VIDEO_PIC_HEIGHT, &value) && (value > 0))
		sys->outputHeight = WINPR_ASSERTING_INT_CAST(UINT32, value);
	if (OH_AVFormat_GetIntValue(description, OH_MD_KEY_VIDEO_STRIDE, &value) && (value > 0))
		sys->outputStride = WINPR_ASSERTING_INT_CAST(UINT32, value);
	if (OH_AVFormat_GetIntValue(description, OH_MD_KEY_VIDEO_SLICE_HEIGHT, &value) &&
	    (value > 0))
		sys->outputSliceHeight = WINPR_ASSERTING_INT_CAST(UINT32, value);
	pthread_mutex_unlock(&sys->lock);
	OH_AVFormat_Destroy(description);

	WLog_Print(h264->log, WLOG_INFO,
	           "OHOS AVCodec buffer output description after %s: size=%ux%u stride=%u "
	           "slice=%u format=%s",
	           reason ? reason : "query", sys->outputWidth, sys->outputHeight,
	           sys->outputStride, sys->outputSliceHeight,
	           ohos_avcodec_decoder_pixel_format_name(sys->decoderPixelFormat));
	return TRUE;
}

FREERDP_LOCAL int ohos_avcodec_prepare_buffer_decoder(H264_CONTEXT* h264,
                                                      H264_CONTEXT_OHOS_AVCODEC* sys)
{
	BOOL needsReopen = FALSE;
	BOOL hadDecoder = FALSE;
	int32_t asyncError = 0;
	UINT64 refreshCount = 0;

	if (!h264 || !sys || !h264->ohosAvcodecBufferModeAllowed)
		return 1;

	pthread_mutex_lock(&sys->lock);
	hadDecoder = (sys->decoder != NULL) || sys->started;
	asyncError = sys->asyncError;
	needsReopen = (asyncError != 0) || !sys->decoder || !sys->started ||
	              !sys->decoderBufferMode || (sys->width != h264->width) ||
	              (sys->height != h264->height);
	pthread_mutex_unlock(&sys->lock);

	if (!needsReopen)
		return 1;

	if (hadDecoder)
		ohos_avcodec_close_decoder(sys);

	pthread_mutex_lock(&sys->lock);
	refreshCount = ++sys->decoderRefreshes;
	sys->asyncError = 0;
	pthread_mutex_unlock(&sys->lock);

	if (!ohos_avcodec_open_decoder(h264, sys))
		return -1;

	if ((refreshCount <= 3) || ((refreshCount % 30) == 0))
	{
		WLog_Print(h264->log, WLOG_INFO,
		           "OHOS AVCodec buffer decoder refreshed: refreshes=%" PRIu64
		           " asyncError=%d",
		           refreshCount, asyncError);
	}
	ohos_avcodec_record_progress(sys);
	return 1;
}

static BOOL ohos_avcodec_range_fits(UINT64 offset, UINT64 length, UINT32 size)
{
	return (offset <= size) && (length <= size) && (offset + length <= size);
}

static BOOL ohos_avcodec_copy_plane_rows(BYTE* dst, UINT32 dstStride, const BYTE* src,
                                         UINT32 srcStride, UINT32 width, UINT32 height,
                                         UINT64 srcOffset, UINT32 srcSize)
{
	if (!dst || !src || (dstStride < width) || (srcStride < width))
		return FALSE;
	if (width == 0 || height == 0)
		return TRUE;
	if (!ohos_avcodec_range_fits(srcOffset, (UINT64)srcStride * (height - 1U) + width, srcSize))
		return FALSE;

	for (UINT32 y = 0; y < height; y++)
	{
		CopyMemory(dst + (UINT64)y * dstStride, src + srcOffset + (UINT64)y * srcStride, width);
	}
	return TRUE;
}

static BOOL ohos_avcodec_copy_yuvi420(H264_CONTEXT* h264, const OHOS_AVCODEC_OUTPUT_SLOT* slot,
                                      UINT32 width, UINT32 height, UINT32 srcStride,
                                      UINT32 srcSliceHeight)
{
	const UINT32 chromaWidth = (width + 1U) / 2U;
	const UINT32 chromaHeight = (height + 1U) / 2U;
	const UINT32 srcChromaStride = (srcStride + 1U) / 2U;
	const UINT32 srcChromaSliceHeight = (srcSliceHeight + 1U) / 2U;
	const UINT64 yOffset = 0;
	const UINT64 uOffset = (UINT64)srcStride * srcSliceHeight;
	const UINT64 vOffset = uOffset + (UINT64)srcChromaStride * srcChromaSliceHeight;
	const BYTE* src = slot->data;

	return ohos_avcodec_copy_plane_rows(h264->pYUVData[0], h264->iStride[0], src, srcStride,
	                                    width, height, yOffset, slot->size) &&
	       ohos_avcodec_copy_plane_rows(h264->pYUVData[1], h264->iStride[1], src, srcChromaStride,
	                                    chromaWidth, chromaHeight, uOffset, slot->size) &&
	       ohos_avcodec_copy_plane_rows(h264->pYUVData[2], h264->iStride[2], src, srcChromaStride,
	                                    chromaWidth, chromaHeight, vOffset, slot->size);
}

static BOOL ohos_avcodec_copy_semiplanar(H264_CONTEXT* h264,
                                         const OHOS_AVCODEC_OUTPUT_SLOT* slot, UINT32 width,
                                         UINT32 height, UINT32 srcStride, UINT32 srcSliceHeight,
                                         BOOL nv21)
{
	const UINT32 chromaWidth = (width + 1U) / 2U;
	const UINT32 chromaHeight = (height + 1U) / 2U;
	const UINT64 uvOffset = (UINT64)srcStride * srcSliceHeight;
	const BYTE* src = slot->data;

	if (!ohos_avcodec_copy_plane_rows(h264->pYUVData[0], h264->iStride[0], src, srcStride, width,
	                                  height, 0, slot->size))
		return FALSE;
	if (!ohos_avcodec_range_fits(uvOffset, (UINT64)srcStride * chromaHeight, slot->size))
		return FALSE;

	for (UINT32 y = 0; y < chromaHeight; y++)
	{
		const BYTE* srcUv = src + uvOffset + (UINT64)y * srcStride;
		BYTE* dstU = h264->pYUVData[1] + (UINT64)y * h264->iStride[1];
		BYTE* dstV = h264->pYUVData[2] + (UINT64)y * h264->iStride[2];

		for (UINT32 x = 0; x < chromaWidth; x++)
		{
			const BYTE a = srcUv[x * 2U];
			const BYTE b = srcUv[x * 2U + 1U];
			dstU[x] = nv21 ? b : a;
			dstV[x] = nv21 ? a : b;
		}
	}
	return TRUE;
}

FREERDP_LOCAL BOOL ohos_avcodec_copy_output_slot_to_h264(
    H264_CONTEXT* h264, const OHOS_AVCODEC_OUTPUT_SLOT* slot)
{
	if (!h264 || !slot || !slot->data || (slot->size == 0))
		return FALSE;

	const UINT32 width = slot->width ? slot->width : h264->width;
	const UINT32 height = slot->height ? slot->height : h264->height;
	const UINT32 copyWidth = MIN(width, h264->width);
	const UINT32 copyHeight = MIN(height, h264->height);
	const UINT32 srcStride = slot->stride ? slot->stride : width;
	const UINT32 srcSliceHeight = slot->sliceHeight ? slot->sliceHeight : height;

	if ((copyWidth == 0) || (copyHeight == 0) || (srcStride < copyWidth) ||
	    (srcSliceHeight < copyHeight))
		return FALSE;
	if (!avc420_ensure_buffer(h264, h264->width, h264->width, h264->height))
		return FALSE;

	switch (slot->pixelFormat)
	{
		case AV_PIXEL_FORMAT_YUVI420:
			return ohos_avcodec_copy_yuvi420(h264, slot, copyWidth, copyHeight, srcStride,
			                                 srcSliceHeight);
		case AV_PIXEL_FORMAT_NV12:
			return ohos_avcodec_copy_semiplanar(h264, slot, copyWidth, copyHeight, srcStride,
			                                    srcSliceHeight, FALSE);
		case AV_PIXEL_FORMAT_NV21:
			return ohos_avcodec_copy_semiplanar(h264, slot, copyWidth, copyHeight, srcStride,
			                                    srcSliceHeight, TRUE);
		default:
			return FALSE;
	}
}

static BOOL ohos_avcodec_fill_output_slot(H264_CONTEXT_OHOS_AVCODEC* sys, OH_AVBuffer* buffer,
                                          const OH_AVCodecBufferAttr* attr,
                                          OHOS_AVCODEC_OUTPUT_SLOT* slot)
{
	BYTE* src = NULL;
	int32_t capacity = 0;
	int32_t offset = 0;
	int32_t size = 0;

	if (!sys || !buffer || !attr || !slot)
		return FALSE;

	src = OH_AVBuffer_GetAddr(buffer);
	capacity = OH_AVBuffer_GetCapacity(buffer);
	offset = attr->offset > 0 ? attr->offset : 0;
	size = attr->size > 0 ? attr->size : capacity - offset;

	if (!src || (capacity <= 0) || (offset < 0) || (offset >= capacity) || (size <= 0) ||
	    (size > capacity - offset))
		return FALSE;

	pthread_mutex_lock(&sys->lock);
	slot->data = src + offset;
	slot->size = WINPR_ASSERTING_INT_CAST(UINT32, size);
	slot->flags = attr->flags;
	slot->pts = attr->pts;
	slot->pixelFormat = sys->decoderPixelFormat;
	slot->width = sys->outputWidth ? sys->outputWidth : sys->width;
	slot->height = sys->outputHeight ? sys->outputHeight : sys->height;
	slot->stride = sys->outputStride ? sys->outputStride : slot->width;
	slot->sliceHeight = sys->outputSliceHeight ? sys->outputSliceHeight : slot->height;
	pthread_mutex_unlock(&sys->lock);
	return TRUE;
}

static int ohos_avcodec_query_buffer_output(H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys,
                                            int64_t expectedPts)
{
	int64_t waitedUs = 0;

	for (UINT32 attempt = 0;
	     (attempt < OHOS_AVCODEC_DECODER_OUTPUT_MAX_ATTEMPTS) &&
	     (waitedUs < OHOS_AVCODEC_DECODER_OUTPUT_DEADLINE_US);
	     attempt++)
	{
		uint32_t outputIndex = 0;
		OH_AVBuffer* outputBuffer = NULL;
		OH_AVCodecBufferAttr outputAttr = { 0 };
		OHOS_AVCODEC_OUTPUT_SLOT output = { 0 };
		const int64_t remainingUs = OHOS_AVCODEC_DECODER_OUTPUT_DEADLINE_US - waitedUs;
		const int64_t timeoutUs =
		    MIN(attempt == 0 ? OHOS_AVCODEC_DECODER_OUTPUT_TIMEOUT_US
		                     : OHOS_AVCODEC_DECODER_OUTPUT_FOLLOWUP_TIMEOUT_US,
		        remainingUs);
		const OH_AVErrCode rc =
		    OH_VideoDecoder_QueryOutputBuffer(sys->decoder, &outputIndex, timeoutUs);

		if (rc == AV_ERR_STREAM_CHANGED)
		{
			(void)ohos_avcodec_update_decoder_output_description(h264, sys, "stream-changed");
			continue;
		}
		if (rc == AV_ERR_TRY_AGAIN_LATER)
		{
			waitedUs += timeoutUs;
			continue;
		}
		if (rc != AV_ERR_OK)
		{
			sys->failedFrames++;
			ohos_avcodec_record_progress(sys);
			WLog_Print(h264->log, WLOG_WARN,
			           "OHOS AVCodec buffer query output failed rc=%d calls=%" PRIu64,
			           (int)rc, sys->decodeCalls);
			return ohos_avcodec_request_software_fallback(h264, sys, "query output failed");
		}

		outputBuffer = OH_VideoDecoder_GetOutputBuffer(sys->decoder, outputIndex);
		if (!outputBuffer || (OH_AVBuffer_GetBufferAttr(outputBuffer, &outputAttr) != AV_ERR_OK))
		{
			OH_VideoDecoder_FreeOutputBuffer(sys->decoder, outputIndex);
			sys->failedFrames++;
			ohos_avcodec_record_progress(sys);
			return ohos_avcodec_request_software_fallback(h264, sys, "output buffer invalid");
		}

		if ((outputAttr.pts > 0) && (outputAttr.pts != expectedPts))
		{
			OH_VideoDecoder_FreeOutputBuffer(sys->decoder, outputIndex);
			sys->droppedOutputFrames++;
			if ((sys->droppedOutputFrames <= 3) || ((sys->droppedOutputFrames % 120) == 0))
			{
				WLog_Print(h264->log, WLOG_WARN,
				           "OHOS AVCodec buffer discarded stale output pts=%" PRId64
				           " expected=%" PRId64 " dropped=%" PRIu64,
				           outputAttr.pts, expectedPts, sys->droppedOutputFrames);
			}
			continue;
		}

		if (!ohos_avcodec_fill_output_slot(sys, outputBuffer, &outputAttr, &output) ||
		    !ohos_avcodec_copy_output_slot_to_h264(h264, &output))
		{
			OH_VideoDecoder_FreeOutputBuffer(sys->decoder, outputIndex);
			sys->failedFrames++;
			ohos_avcodec_record_progress(sys);
			return ohos_avcodec_request_software_fallback(h264, sys, "copy decoded output failed");
		}

		OH_VideoDecoder_FreeOutputBuffer(sys->decoder, outputIndex);
		sys->decodedFrames++;
		ohos_avcodec_record_progress(sys);
		if ((sys->decodeCalls <= 3) || ((sys->decodeCalls % 120) == 0))
		{
			WLog_Print(h264->log, WLOG_INFO,
			           "OHOS AVCodec buffer frame decoded call=%" PRIu64
			           " srcPts=%" PRId64 " outPts=%" PRId64
			           " size=%ux%u stride=%u slice=%u format=%s rendered=%" PRIu64
			           " dropped=%" PRIu64 " failed=%" PRIu64,
			           sys->decodeCalls, expectedPts, outputAttr.pts, output.width, output.height,
			           output.stride, output.sliceHeight,
			           ohos_avcodec_decoder_pixel_format_name(output.pixelFormat),
			           sys->decodedFrames, sys->droppedOutputFrames, sys->failedFrames);
		}
		return 1;
	}

	sys->outputWaitTimeouts++;
	sys->noOutputFrames++;
	ohos_avcodec_record_progress(sys);
	if ((sys->outputWaitTimeouts <= 3) || ((sys->outputWaitTimeouts % 120) == 0))
	{
		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec buffer output timeout count=%" PRIu64
		           " calls=%" PRIu64 " budgetUs=%d",
		           sys->outputWaitTimeouts, sys->decodeCalls,
		           OHOS_AVCODEC_DECODER_OUTPUT_DEADLINE_US);
	}
	return H264_OHOS_AVCODEC_NO_OUTPUT_RC;
}

FREERDP_LOCAL int ohos_avcodec_decompress_buffer(H264_CONTEXT* h264,
                                                 H264_CONTEXT_OHOS_AVCODEC* sys,
                                                 const BYTE* pSrcData, UINT32 SrcSize)
{
	uint32_t inputIndex = 0;
	OH_AVBuffer* inputBuffer = NULL;
	OH_AVCodecBufferAttr inputAttr = { 0 };
	BYTE* dst = NULL;
	int32_t capacity = 0;
	int64_t expectedOutputPts = 0;
	OH_AVErrCode rc = AV_ERR_OK;

	if (!h264 || !sys || !sys->decoder || !sys->started || !sys->decoderBufferMode)
		return ohos_avcodec_request_software_fallback(h264, sys, "buffer decoder not active");

	h264->pYUVData[0] = NULL;
	h264->pYUVData[1] = NULL;
	h264->pYUVData[2] = NULL;
	sys->decodeCalls++;
	expectedOutputPts = WINPR_ASSERTING_INT_CAST(int64_t, sys->decodeCalls);

	rc = OH_VideoDecoder_QueryInputBuffer(sys->decoder, &inputIndex,
	                                      OHOS_AVCODEC_DECODER_INPUT_TIMEOUT_US);
	if (rc != AV_ERR_OK)
	{
		sys->inputWaitTimeouts++;
		sys->noOutputFrames++;
		ohos_avcodec_record_progress(sys);
		if ((sys->inputWaitTimeouts <= 3) || ((sys->inputWaitTimeouts % 120) == 0))
		{
			WLog_Print(h264->log, WLOG_WARN,
			           "OHOS AVCodec buffer input unavailable rc=%d count=%" PRIu64
			           " calls=%" PRIu64,
			           (int)rc, sys->inputWaitTimeouts, sys->decodeCalls);
		}
		if (rc == AV_ERR_TRY_AGAIN_LATER)
			return H264_OHOS_AVCODEC_NO_OUTPUT_RC;
		return ohos_avcodec_request_software_fallback(h264, sys, "query input failed");
	}

	inputBuffer = OH_VideoDecoder_GetInputBuffer(sys->decoder, inputIndex);
	capacity = inputBuffer ? OH_AVBuffer_GetCapacity(inputBuffer) : -1;
	dst = inputBuffer ? OH_AVBuffer_GetAddr(inputBuffer) : NULL;
	if (!dst || (capacity < 0) || ((UINT32)capacity < SrcSize))
	{
		sys->failedFrames++;
		ohos_avcodec_record_progress(sys);
		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec buffer input invalid capacity=%d size=%u", capacity, SrcSize);
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
		WLog_Print(h264->log, WLOG_WARN, "OHOS AVCodec buffer push input failed rc=%d",
		           (int)rc);
		return ohos_avcodec_request_software_fallback(h264, sys, "push input failed");
	}

	return ohos_avcodec_query_buffer_output(h264, sys, expectedOutputPts);
}
