/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS AVCodec H.264 decoder
 *
 * Licensed under the Apache License, Version 2.0.
 */

#include "h264_ohos_avcodec_internal.h"

FREERDP_LOCAL UINT64 ohos_avcodec_now_ns(void)
{
	struct timespec now = { 0 };
	clock_gettime(CLOCK_MONOTONIC, &now);
	return ((UINT64)now.tv_sec * 1000000000ULL) + (UINT64)now.tv_nsec;
}

FREERDP_LOCAL void ohos_avcodec_make_deadline(struct timespec* deadline, UINT32 timeoutMs)
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

FREERDP_LOCAL BOOL ohos_avcodec_pop_input(H264_CONTEXT_OHOS_AVCODEC* sys, uint32_t* index,
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

FREERDP_LOCAL BOOL ohos_avcodec_wait_for_input(H264_CONTEXT_OHOS_AVCODEC* sys,
                                               uint32_t* index, OH_AVBuffer** buffer)
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

static void ohos_avcodec_free_output_slot(OHOS_AVCODEC_OUTPUT_SLOT* slot)
{
	if (!slot)
		return;

	free(slot->data);
	ZeroMemory(slot, sizeof(*slot));
}

FREERDP_LOCAL BOOL ohos_avcodec_pop_output(H264_CONTEXT_OHOS_AVCODEC* sys,
                                           OHOS_AVCODEC_OUTPUT_SLOT* slot)
{
	if (!sys || !slot || (sys->outputCount == 0))
		return FALSE;

	*slot = sys->outputQueue[sys->outputHead];
	ZeroMemory(&sys->outputQueue[sys->outputHead], sizeof(sys->outputQueue[sys->outputHead]));
	sys->outputHead = (sys->outputHead + 1) % OHOS_AVCODEC_OUTPUT_QUEUE_LENGTH;
	sys->outputCount--;
	return TRUE;
}

FREERDP_LOCAL BOOL ohos_avcodec_wait_for_output(H264_CONTEXT_OHOS_AVCODEC* sys,
                                                OHOS_AVCODEC_OUTPUT_SLOT* slot)
{
	struct timespec deadline = { 0 };
	int rc = 0;

	if (!sys || !slot)
		return FALSE;

	ohos_avcodec_make_deadline(&deadline, OHOS_AVCODEC_DECODER_OUTPUT_WAIT_MS);
	while ((sys->outputCount == 0) && (sys->asyncError == 0))
	{
		rc = pthread_cond_timedwait(&sys->cond, &sys->lock, &deadline);
		if (rc == ETIMEDOUT)
			break;
	}

	if (sys->asyncError != 0)
		return FALSE;

	return ohos_avcodec_pop_output(sys, slot);
}

FREERDP_LOCAL void ohos_avcodec_clear_output_queue(H264_CONTEXT_OHOS_AVCODEC* sys)
{
	if (!sys)
		return;

	for (UINT32 x = 0; x < OHOS_AVCODEC_OUTPUT_QUEUE_LENGTH; x++)
		ohos_avcodec_free_output_slot(&sys->outputQueue[x]);

	sys->outputHead = 0;
	sys->outputTail = 0;
	sys->outputCount = 0;
}

FREERDP_LOCAL void ohos_avcodec_reset_async_state(H264_CONTEXT_OHOS_AVCODEC* sys)
{
	if (!sys || !sys->primitivesReady)
		return;

	pthread_mutex_lock(&sys->lock);
	sys->inputHead = 0;
	sys->inputTail = 0;
	sys->inputCount = 0;
	ohos_avcodec_clear_output_queue(sys);
	sys->asyncError = 0;
	pthread_cond_broadcast(&sys->cond);
	pthread_mutex_unlock(&sys->lock);
}

FREERDP_LOCAL void ohos_avcodec_on_error(OH_AVCodec* codec, int32_t errorCode, void* userData)
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

FREERDP_LOCAL void ohos_avcodec_on_stream_changed(OH_AVCodec* codec, OH_AVFormat* format, void* userData)
{
	H264_CONTEXT_OHOS_AVCODEC* sys = (H264_CONTEXT_OHOS_AVCODEC*)userData;
	int32_t value = 0;
	WINPR_UNUSED(codec);

	if (!sys || !sys->primitivesReady)
		return;

	pthread_mutex_lock(&sys->lock);
	if (format)
	{
		if (OH_AVFormat_GetIntValue(format, OH_MD_KEY_PIXEL_FORMAT, &value))
			sys->decoderPixelFormat = value;
		if (OH_AVFormat_GetIntValue(format, OH_MD_KEY_WIDTH, &value) && (value > 0))
			sys->outputWidth = WINPR_ASSERTING_INT_CAST(UINT32, value);
		if (OH_AVFormat_GetIntValue(format, OH_MD_KEY_HEIGHT, &value) && (value > 0))
			sys->outputHeight = WINPR_ASSERTING_INT_CAST(UINT32, value);
		if (OH_AVFormat_GetIntValue(format, OH_MD_KEY_VIDEO_PIC_WIDTH, &value) && (value > 0))
			sys->outputWidth = WINPR_ASSERTING_INT_CAST(UINT32, value);
		if (OH_AVFormat_GetIntValue(format, OH_MD_KEY_VIDEO_PIC_HEIGHT, &value) && (value > 0))
			sys->outputHeight = WINPR_ASSERTING_INT_CAST(UINT32, value);
		if (OH_AVFormat_GetIntValue(format, OH_MD_KEY_VIDEO_STRIDE, &value) && (value > 0))
			sys->outputStride = WINPR_ASSERTING_INT_CAST(UINT32, value);
		if (OH_AVFormat_GetIntValue(format, OH_MD_KEY_VIDEO_SLICE_HEIGHT, &value) && (value > 0))
			sys->outputSliceHeight = WINPR_ASSERTING_INT_CAST(UINT32, value);
	}
	pthread_cond_broadcast(&sys->cond);
	pthread_mutex_unlock(&sys->lock);
}

FREERDP_LOCAL void ohos_avcodec_on_need_input_buffer(OH_AVCodec* codec, uint32_t index,
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

static void ohos_avcodec_enqueue_output_locked(H264_CONTEXT_OHOS_AVCODEC* sys,
                                               OHOS_AVCODEC_OUTPUT_SLOT* slot)
{
	if (!sys || !slot || !slot->data)
		return;

	if (sys->outputCount >= OHOS_AVCODEC_OUTPUT_QUEUE_LENGTH)
	{
		ohos_avcodec_free_output_slot(&sys->outputQueue[sys->outputHead]);
		sys->outputHead = (sys->outputHead + 1) % OHOS_AVCODEC_OUTPUT_QUEUE_LENGTH;
		sys->outputCount--;
		sys->droppedOutputFrames++;
		if (!sys->outputQueueFullLogged)
		{
			WLog_Print(sys->log, WLOG_WARN,
			           "OHOS AVCodec output queue full; dropping oldest decoded frame");
			sys->outputQueueFullLogged = TRUE;
		}
	}

	sys->outputQueue[sys->outputTail] = *slot;
	ZeroMemory(slot, sizeof(*slot));
	sys->outputTail = (sys->outputTail + 1) % OHOS_AVCODEC_OUTPUT_QUEUE_LENGTH;
	sys->outputCount++;
	sys->decodedFrames++;
	pthread_cond_broadcast(&sys->cond);
}

FREERDP_LOCAL void ohos_avcodec_on_new_output_buffer(OH_AVCodec* codec, uint32_t index,
                                              OH_AVBuffer* buffer, void* userData)
{
	OH_AVCodecBufferAttr attr = { 0 };
	H264_CONTEXT_OHOS_AVCODEC* sys = (H264_CONTEXT_OHOS_AVCODEC*)userData;
	OH_AVCodec* decoder = codec;
	BOOL bufferMode = FALSE;
	BOOL freeOutput = FALSE;
	UINT64 failedFrames = 0;
	OHOS_AVCODEC_OUTPUT_SLOT output = { 0 };

	if (!sys || !sys->primitivesReady || !buffer)
		return;

	if (OH_AVBuffer_GetBufferAttr(buffer, &attr) != AV_ERR_OK)
		ZeroMemory(&attr, sizeof(attr));

	if ((attr.flags & AVCODEC_BUFFER_FLAGS_EOS) == 0)
	{
		pthread_mutex_lock(&sys->lock);
		bufferMode = sys->decoderBufferMode;
		decoder = sys->decoder ? sys->decoder : codec;
		if (bufferMode)
		{
			BYTE* src = OH_AVBuffer_GetAddr(buffer);
			const int32_t capacity = OH_AVBuffer_GetCapacity(buffer);
			const int32_t offset = attr.offset > 0 ? attr.offset : 0;
			int32_t size = attr.size > 0 ? attr.size : capacity - offset;
			if (!src || (capacity <= 0) || (offset < 0) || (offset >= capacity))
				size = 0;
			if (size > capacity - offset)
				size = capacity - offset;

			if (size > 0)
			{
				output.data = (BYTE*)malloc(WINPR_ASSERTING_INT_CAST(size_t, size));
				if (output.data)
				{
					CopyMemory(output.data, src + offset, WINPR_ASSERTING_INT_CAST(size_t, size));
					output.size = WINPR_ASSERTING_INT_CAST(UINT32, size);
					output.flags = attr.flags;
					output.pts = attr.pts;
					output.pixelFormat = sys->decoderPixelFormat;
					output.width = sys->outputWidth ? sys->outputWidth : sys->width;
					output.height = sys->outputHeight ? sys->outputHeight : sys->height;
					output.stride = sys->outputStride ? sys->outputStride : output.width;
					output.sliceHeight = sys->outputSliceHeight ? sys->outputSliceHeight : output.height;
					ohos_avcodec_enqueue_output_locked(sys, &output);
				}
				else
				{
					sys->failedFrames++;
				}
			}
			else
			{
				sys->failedFrames++;
			}
			failedFrames = sys->failedFrames;
			freeOutput = TRUE;
		}
		else
		{
			sys->failedFrames++;
			failedFrames = sys->failedFrames;
			pthread_cond_broadcast(&sys->cond);
			freeOutput = TRUE;
		}
		pthread_mutex_unlock(&sys->lock);
	}
	else
	{
		pthread_mutex_lock(&sys->lock);
		bufferMode = sys->decoderBufferMode;
		decoder = sys->decoder ? sys->decoder : codec;
		freeOutput = TRUE;
		pthread_mutex_unlock(&sys->lock);
	}

	if (freeOutput && decoder)
	{
		const OH_AVErrCode rc = OH_VideoDecoder_FreeOutputBuffer(decoder, index);
		if (rc != AV_ERR_OK)
		{
			pthread_mutex_lock(&sys->lock);
			sys->failedFrames++;
			failedFrames = sys->failedFrames;
			pthread_mutex_unlock(&sys->lock);
			if (failedFrames <= 3)
				WLog_Print(sys->log, WLOG_WARN,
				           "OHOS AVCodec decoder output release failed rc=%d", (int)rc);
		}
		ohos_avcodec_record_progress(sys);
	}
}
