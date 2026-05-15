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
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <winpr/crt.h>
#include <winpr/interlocked.h>
#include <winpr/wlog.h>

#include "h264.h"

#if defined(WITH_OHOS_AVCODEC)
#include <multimedia/player_framework/native_avbuffer.h>
#include <multimedia/player_framework/native_avbuffer_info.h>
#include <multimedia/player_framework/native_avcodec_base.h>
#include <multimedia/player_framework/native_avcodec_videodecoder.h>
#include <multimedia/player_framework/native_avformat.h>
#endif

#define TAG FREERDP_TAG("codec.ohos-avcodec")

#define OHOS_AVCODEC_INPUT_QUEUE_LENGTH 32
#define OHOS_AVCODEC_INPUT_WAIT_MS 50
#define OHOS_AVCODEC_OUTPUT_WAIT_MS 25

typedef struct
{
	uint32_t index;
	OH_AVBuffer* buffer;
} OHOS_AVCODEC_INPUT_SLOT;

typedef struct
{
	OH_AVCodec* decoder;
	BOOL started;
	BOOL primitivesReady;
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
	BOOL outputReady;
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

static volatile LONG g_ohos_avcodec_active_logged = 0;

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

static void ohos_avcodec_read_output_format(H264_CONTEXT_OHOS_AVCODEC* sys, OH_AVFormat* format)
{
	int32_t value = 0;

	if (!sys || !format)
		return;

	if (OH_AVFormat_GetIntValue(format, OH_MD_KEY_PIXEL_FORMAT, &value))
		sys->outputPixelFormat = value;
	else if (sys->outputPixelFormat <= 0)
		sys->outputPixelFormat = AV_PIXEL_FORMAT_YUVI420;

	if (OH_AVFormat_GetIntValue(format, OH_MD_KEY_VIDEO_STRIDE, &value) && (value > 0))
		sys->outputStride = value;
	else if (sys->outputStride <= 0)
		sys->outputStride = WINPR_ASSERTING_INT_CAST(int32_t, sys->width);

	if (OH_AVFormat_GetIntValue(format, OH_MD_KEY_VIDEO_SLICE_HEIGHT, &value) && (value > 0))
		sys->outputSliceHeight = value;
	else if (sys->outputSliceHeight <= 0)
		sys->outputSliceHeight = WINPR_ASSERTING_INT_CAST(int32_t, sys->height);
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
			return ohos_avcodec_copy_i420_to_callback(sys, src, srcSize, stride, sliceHeight);
		case AV_PIXEL_FORMAT_NV12:
			return ohos_avcodec_copy_nvxx_to_callback(sys, src, srcSize, stride, sliceHeight, FALSE);
		case AV_PIXEL_FORMAT_NV21:
			return ohos_avcodec_copy_nvxx_to_callback(sys, src, srcSize, stride, sliceHeight, TRUE);
		default:
			if (!sys->unsupportedFormatLogged)
			{
				WLog_Print(sys->log, WLOG_WARN,
				           "OHOS AVCodec output format %d unsupported; software H264 remains the fallback",
				           sys->outputPixelFormat);
				sys->unsupportedFormatLogged = TRUE;
			}
			return FALSE;
	}
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

static BOOL ohos_avcodec_wait_for_output(H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys)
{
	struct timespec deadline = { 0 };
	int rc = 0;

	if (!h264 || !sys)
		return FALSE;

	ohos_avcodec_make_deadline(&deadline, OHOS_AVCODEC_OUTPUT_WAIT_MS);
	while (!sys->outputReady && (sys->asyncError == 0))
	{
		rc = pthread_cond_timedwait(&sys->cond, &sys->lock, &deadline);
		if (rc == ETIMEDOUT)
			break;
	}

	if (!sys->outputReady || (sys->asyncError != 0))
		return FALSE;

	sys->outputReady = FALSE;
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
	BOOL copied = FALSE;
	WINPR_UNUSED(codec);

	if (!sys || !sys->primitivesReady || !buffer)
		return;

	if (OH_AVBuffer_GetBufferAttr(buffer, &attr) != AV_ERR_OK)
		ZeroMemory(&attr, sizeof(attr));

	if ((attr.flags & AVCODEC_BUFFER_FLAGS_EOS) == 0)
	{
		pthread_mutex_lock(&sys->lock);
		if (sys->outputReady)
			sys->droppedOutputFrames++;

		copied = ohos_avcodec_copy_output_to_callback(sys, buffer, &attr);
		if (copied)
		{
			sys->outputReady = TRUE;
			pthread_cond_broadcast(&sys->cond);
		}
		else
		{
			sys->failedFrames++;
		}
		pthread_mutex_unlock(&sys->lock);
	}

	OH_VideoDecoder_FreeOutputBuffer(sys->decoder, index);
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
	sys->asyncError = 0;
	sys->readySize = 0;
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
	sys->started = FALSE;
	ohos_avcodec_reset_async_state(sys);
}

static BOOL ohos_avcodec_configure_decoder(H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys,
                                           int32_t pixelFormat, BOOL setPixelFormat)
{
	bool isValid = false;
	OH_AVErrCode rc = AV_ERR_OK;
	OH_AVFormat* format = NULL;
	OH_AVCodecCallback callback = { 0 };

	if (!h264 || !sys || (h264->width == 0) || (h264->height == 0))
		return TRUE;

	sys->decoder = OH_VideoDecoder_CreateByMime(OH_AVCODEC_MIMETYPE_VIDEO_AVC);
	if (!sys->decoder)
		return FALSE;

	rc = OH_VideoDecoder_IsValid(sys->decoder, &isValid);
	if ((rc != AV_ERR_OK) || !isValid)
	{
		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec H264 decoder invalid: valid=%d rc=%d; falling back to software H264",
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

	if (setPixelFormat)
		OH_AVFormat_SetIntValue(format, OH_MD_KEY_PIXEL_FORMAT, pixelFormat);
	OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENABLE_LOW_LATENCY, 1);

	rc = OH_VideoDecoder_Configure(sys->decoder, format);
	OH_AVFormat_Destroy(format);
	if (rc != AV_ERR_OK)
	{
		WLog_Print(h264->log, WLOG_DEBUG,
		           "OHOS AVCodec configure failed rc=%d pixelFormat=%d setPixel=%d", (int)rc,
		           pixelFormat, setPixelFormat ? 1 : 0);
		ohos_avcodec_close_decoder(sys);
		return FALSE;
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
	sys->outputPixelFormat = setPixelFormat ? pixelFormat : AV_PIXEL_FORMAT_YUVI420;
	sys->outputStride = WINPR_ASSERTING_INT_CAST(int32_t, h264->width);
	sys->outputSliceHeight = WINPR_ASSERTING_INT_CAST(int32_t, h264->height);
	ohos_avcodec_update_output_description(sys);
	return TRUE;
}

static BOOL ohos_avcodec_open_decoder(H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys)
{
	static const int32_t formats[] = { AV_PIXEL_FORMAT_YUVI420, AV_PIXEL_FORMAT_NV12,
		                               AV_PIXEL_FORMAT_NV21 };

	if (!h264 || !sys || (h264->width == 0) || (h264->height == 0))
		return TRUE;

	sys->width = h264->width;
	sys->height = h264->height;
	sys->outputPixelFormat = AV_PIXEL_FORMAT_YUVI420;
	sys->outputStride = WINPR_ASSERTING_INT_CAST(int32_t, h264->width);
	sys->outputSliceHeight = WINPR_ASSERTING_INT_CAST(int32_t, h264->height);

	for (size_t x = 0; x < ARRAYSIZE(formats); x++)
	{
		if (ohos_avcodec_configure_decoder(h264, sys, formats[x], TRUE))
			goto success;
	}

	if (ohos_avcodec_configure_decoder(h264, sys, 0, FALSE))
		goto success;

	WLog_Print(h264->log, WLOG_WARN,
	           "OHOS AVCodec H264 decoder unavailable for %ux%u; falling back to software H264",
	           h264->width, h264->height);
	return FALSE;

success:
	if (InterlockedCompareExchange(&g_ohos_avcodec_active_logged, 1, 0) == 0)
	{
		WLog_Print(h264->log, WLOG_INFO,
		           "OHOS AVCodec H264 decoder active: %ux%u format=%d stride=%d slice=%d async-buffer single-copy",
		           h264->width, h264->height, sys->outputPixelFormat, sys->outputStride,
		           sys->outputSliceHeight);
	}
	return TRUE;
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

	WINPR_ASSERT(h264);
	WINPR_ASSERT(pSrcData || (SrcSize == 0));

	sys = (H264_CONTEXT_OHOS_AVCODEC*)h264->pSystemData;
	if (!sys || !sys->decoder || !sys->started)
		return -3001;

	h264->pYUVData[0] = NULL;
	h264->pYUVData[1] = NULL;
	h264->pYUVData[2] = NULL;
	sys->decodeCalls++;

	pthread_mutex_lock(&sys->lock);
	if (!ohos_avcodec_wait_for_input(sys, &inputIndex, &inputBuffer))
	{
		pthread_mutex_unlock(&sys->lock);
		sys->inputWaitTimeouts++;
		sys->noOutputFrames++;
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
		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec input buffer invalid capacity=%d size=%u", capacity, SrcSize);
		ohos_avcodec_return_empty_input(sys, inputIndex, inputBuffer);
		return 0;
	}

	if (SrcSize > 0)
		CopyMemory(dst, pSrcData, SrcSize);

	inputAttr.pts = WINPR_ASSERTING_INT_CAST(int64_t, sys->decodeCalls);
	inputAttr.size = WINPR_ASSERTING_INT_CAST(int32_t, SrcSize);
	inputAttr.offset = 0;
	inputAttr.flags = AVCODEC_BUFFER_FLAGS_NONE;
	if (OH_AVBuffer_SetBufferAttr(inputBuffer, &inputAttr) != AV_ERR_OK)
	{
		sys->failedFrames++;
		ohos_avcodec_return_empty_input(sys, inputIndex, inputBuffer);
		return 0;
	}

	rc = OH_VideoDecoder_PushInputBuffer(sys->decoder, inputIndex);
	if (rc != AV_ERR_OK)
	{
		sys->failedFrames++;
		WLog_Print(h264->log, WLOG_WARN, "OHOS AVCodec push input failed rc=%d", (int)rc);
		return 0;
	}

	pthread_mutex_lock(&sys->lock);
	hasOutput = ohos_avcodec_wait_for_output(h264, sys);
	pthread_mutex_unlock(&sys->lock);

	if (!hasOutput)
	{
		sys->outputWaitTimeouts++;
		sys->noOutputFrames++;
		if ((sys->outputWaitTimeouts <= 3) || ((sys->outputWaitTimeouts % 120) == 0))
			WLog_Print(h264->log, WLOG_DEBUG,
			           "OHOS AVCodec output wait timeout count=%" PRIu64 " calls=%" PRIu64,
			           sys->outputWaitTimeouts, sys->decodeCalls);
		return 0;
	}

	sys->decodedFrames++;
	if ((sys->decodedFrames <= 3) || ((sys->decodedFrames % 120) == 0))
	{
		WLog_Print(h264->log, WLOG_INFO,
		           "OHOS AVCodec decoded H264 frame=%" PRIu64 " calls=%" PRIu64
		           " noOutput=%" PRIu64 " failures=%" PRIu64 " dropped=%" PRIu64
		           " format=%d stride=%d slice=%d",
		           sys->decodedFrames, sys->decodeCalls, sys->noOutputFrames, sys->failedFrames,
		           sys->droppedOutputFrames, sys->outputPixelFormat, sys->outputStride,
		           sys->outputSliceHeight);
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
