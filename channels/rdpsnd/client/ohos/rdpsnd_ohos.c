/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Output Virtual Channel - HarmonyOS OHAudio backend
 */

#include <freerdp/config.h>

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ohaudio/native_audiorenderer.h>
#include <ohaudio/native_audiostreambuilder.h>

#include <winpr/assert.h>
#include <winpr/crt.h>
#include <winpr/synch.h>

#include <freerdp/channels/log.h>
#include <freerdp/types.h>

#include "rdpsnd_main.h"

typedef struct
{
	rdpsndDevicePlugin device;

	OH_AudioRenderer* renderer;
	CRITICAL_SECTION lock;
	BOOL lockInitialized;

	BYTE* queue;
	size_t queueCapacity;
	size_t queueRead;
	size_t queueWrite;
	size_t queueSize;

	UINT32 volume;
	UINT32 rate;
	UINT16 channels;
	UINT16 bitsPerSample;
	UINT16 blockAlign;
	UINT32 latencyMs;
} rdpsndOhosPlugin;

static size_t rdpsnd_ohos_frame_bytes(const rdpsndOhosPlugin* ohos)
{
	if (!ohos || ohos->blockAlign == 0)
		return 1;

	return ohos->blockAlign;
}

static size_t rdpsnd_ohos_bytes_per_second(const rdpsndOhosPlugin* ohos)
{
	return (size_t)ohos->rate * rdpsnd_ohos_frame_bytes(ohos);
}

static BYTE rdpsnd_ohos_silence_byte(const rdpsndOhosPlugin* ohos)
{
	return (ohos && ohos->bitsPerSample == 8) ? 0x80 : 0x00;
}

static OH_AudioStream_SampleFormat rdpsnd_ohos_sample_format(UINT16 bitsPerSample)
{
	switch (bitsPerSample)
	{
		case 8:
			return AUDIOSTREAM_SAMPLE_U8;
		case 16:
		default:
			return AUDIOSTREAM_SAMPLE_S16LE;
	}
}

static UINT32 rdpsnd_ohos_queued_latency_locked(const rdpsndOhosPlugin* ohos)
{
	const size_t bps = rdpsnd_ohos_bytes_per_second(ohos);
	if ((bps == 0) || (ohos->queueSize == 0))
		return 0;

	return (UINT32)((ohos->queueSize * 1000U) / bps);
}

static void rdpsnd_ohos_clear_queue_locked(rdpsndOhosPlugin* ohos)
{
	ohos->queueRead = 0;
	ohos->queueWrite = 0;
	ohos->queueSize = 0;
}

static void rdpsnd_ohos_drop_locked(rdpsndOhosPlugin* ohos, size_t bytes)
{
	const size_t frameBytes = rdpsnd_ohos_frame_bytes(ohos);

	if (frameBytes > 1)
		bytes -= bytes % frameBytes;

	if (bytes >= ohos->queueSize)
	{
		rdpsnd_ohos_clear_queue_locked(ohos);
		return;
	}

	ohos->queueRead = (ohos->queueRead + bytes) % ohos->queueCapacity;
	ohos->queueSize -= bytes;
}

static size_t rdpsnd_ohos_pop_locked(rdpsndOhosPlugin* ohos, BYTE* dst, size_t bytes)
{
	size_t copied = 0;

	while ((copied < bytes) && (ohos->queueSize > 0))
	{
		const size_t chunk = (ohos->queueRead + ohos->queueSize <= ohos->queueCapacity)
		                         ? ohos->queueSize
		                         : (ohos->queueCapacity - ohos->queueRead);
		const size_t todo = (chunk < (bytes - copied)) ? chunk : (bytes - copied);

		memcpy(dst + copied, ohos->queue + ohos->queueRead, todo);
		ohos->queueRead = (ohos->queueRead + todo) % ohos->queueCapacity;
		ohos->queueSize -= todo;
		copied += todo;
	}

	return copied;
}

static void rdpsnd_ohos_push_locked(rdpsndOhosPlugin* ohos, const BYTE* data, size_t size)
{
	const size_t frameBytes = rdpsnd_ohos_frame_bytes(ohos);

	if (!ohos->queue || (ohos->queueCapacity == 0) || !data || (size == 0))
		return;

	if (frameBytes > 1)
		size -= size % frameBytes;

	if (size == 0)
		return;

	if (size > ohos->queueCapacity)
	{
		const size_t keep = ohos->queueCapacity - (ohos->queueCapacity % frameBytes);
		data += size - keep;
		size = keep;
		rdpsnd_ohos_clear_queue_locked(ohos);
	}
	else if ((ohos->queueCapacity - ohos->queueSize) < size)
	{
		rdpsnd_ohos_drop_locked(ohos, size - (ohos->queueCapacity - ohos->queueSize));
	}

	while (size > 0)
	{
		const size_t tail = ohos->queueCapacity - ohos->queueWrite;
		const size_t todo = (tail < size) ? tail : size;

		memcpy(ohos->queue + ohos->queueWrite, data, todo);
		ohos->queueWrite = (ohos->queueWrite + todo) % ohos->queueCapacity;
		ohos->queueSize += todo;
		data += todo;
		size -= todo;
	}
}

static OH_AudioData_Callback_Result rdpsnd_ohos_on_write_data(
    WINPR_ATTR_UNUSED OH_AudioRenderer* renderer, void* userData, void* audioData,
    int32_t audioDataSize)
{
	rdpsndOhosPlugin* ohos = (rdpsndOhosPlugin*)userData;
	BYTE* dst = (BYTE*)audioData;
	size_t copied = 0;

	if (!ohos || !dst || (audioDataSize <= 0))
		return AUDIO_DATA_CALLBACK_RESULT_INVALID;

	EnterCriticalSection(&ohos->lock);
	copied = rdpsnd_ohos_pop_locked(ohos, dst, (size_t)audioDataSize);
	LeaveCriticalSection(&ohos->lock);

	if (copied < (size_t)audioDataSize)
		memset(dst + copied, rdpsnd_ohos_silence_byte(ohos), (size_t)audioDataSize - copied);

	return AUDIO_DATA_CALLBACK_RESULT_VALID;
}

static void rdpsnd_ohos_release_renderer(rdpsndOhosPlugin* ohos)
{
	if (!ohos || !ohos->renderer)
		return;

	OH_AudioRenderer_Stop(ohos->renderer);
	OH_AudioRenderer_Flush(ohos->renderer);
	OH_AudioRenderer_Release(ohos->renderer);
	ohos->renderer = NULL;
}

static BOOL rdpsnd_ohos_allocate_queue(rdpsndOhosPlugin* ohos, UINT32 latency)
{
	const size_t bytesPerSecond = rdpsnd_ohos_bytes_per_second(ohos);
	UINT32 queueMs = latency ? latency * 2U : 300U;
	size_t capacity = 0;
	BYTE* queue = NULL;

	if (queueMs < 200U)
		queueMs = 200U;
	if (queueMs > 500U)
		queueMs = 500U;

	capacity = (bytesPerSecond * queueMs) / 1000U;
	if (capacity < 32768U)
		capacity = 32768U;

	capacity -= capacity % rdpsnd_ohos_frame_bytes(ohos);
	if (capacity == 0)
		return FALSE;

	queue = (BYTE*)calloc(1, capacity);
	if (!queue)
		return FALSE;

	free(ohos->queue);
	ohos->queue = queue;
	ohos->queueCapacity = capacity;
	rdpsnd_ohos_clear_queue_locked(ohos);
	ohos->latencyMs = queueMs;
	return TRUE;
}

static BOOL rdpsnd_ohos_format_supported(WINPR_ATTR_UNUSED rdpsndDevicePlugin* device,
                                         const AUDIO_FORMAT* format)
{
	WINPR_ASSERT(format);

	if (!format || (format->wFormatTag != WAVE_FORMAT_PCM) || (format->cbSize != 0))
		return FALSE;

	if ((format->nSamplesPerSec < 8000U) || (format->nSamplesPerSec > 48000U))
		return FALSE;

	if ((format->nChannels != 1U) && (format->nChannels != 2U))
		return FALSE;

	return (format->wBitsPerSample == 8U) || (format->wBitsPerSample == 16U);
}

static BOOL rdpsnd_ohos_set_volume(rdpsndDevicePlugin* device, UINT32 value)
{
	rdpsndOhosPlugin* ohos = (rdpsndOhosPlugin*)device;
	const UINT16 left = (UINT16)(value >> 16U);
	const UINT16 right = (UINT16)(value & 0xFFFFU);
	const float volume = ((float)left + (float)right) / (2.0f * 65535.0f);

	WINPR_ASSERT(ohos);
	ohos->volume = value;

	if (ohos->renderer)
		return OH_AudioRenderer_SetVolume(ohos->renderer, volume) == AUDIOSTREAM_SUCCESS;

	return TRUE;
}

static UINT32 rdpsnd_ohos_get_volume(rdpsndDevicePlugin* device)
{
	rdpsndOhosPlugin* ohos = (rdpsndOhosPlugin*)device;
	float volume = 1.0f;
	UINT32 level = 0;

	WINPR_ASSERT(ohos);
	if (ohos->renderer &&
	    (OH_AudioRenderer_GetVolume(ohos->renderer, &volume) == AUDIOSTREAM_SUCCESS))
	{
		if (volume < 0.0f)
			volume = 0.0f;
		if (volume > 1.0f)
			volume = 1.0f;
		level = (UINT32)(volume * 65535.0f);
		ohos->volume = (level << 16U) | level;
	}

	return ohos->volume;
}

static BOOL rdpsnd_ohos_open(rdpsndDevicePlugin* device, const AUDIO_FORMAT* format, UINT32 latency)
{
	rdpsndOhosPlugin* ohos = (rdpsndOhosPlugin*)device;
	OH_AudioStreamBuilder* builder = NULL;
	OH_AudioStream_Result rc = AUDIOSTREAM_SUCCESS;
	const int32_t frameSize = format ? (int32_t)((format->nSamplesPerSec * 20U) / 1000U) : 0;

	WINPR_ASSERT(ohos);
	WINPR_ASSERT(format);

	if (!rdpsnd_ohos_format_supported(device, format))
		return FALSE;

	rdpsnd_ohos_release_renderer(ohos);

	ohos->rate = format->nSamplesPerSec;
	ohos->channels = format->nChannels;
	ohos->bitsPerSample = format->wBitsPerSample;
	ohos->blockAlign = format->nBlockAlign;
	if (ohos->blockAlign == 0)
		ohos->blockAlign = (UINT16)((ohos->channels * ohos->bitsPerSample) / 8U);

	if (!rdpsnd_ohos_allocate_queue(ohos, latency))
		return FALSE;

	rc = OH_AudioStreamBuilder_Create(&builder, AUDIOSTREAM_TYPE_RENDERER);
	if (rc != AUDIOSTREAM_SUCCESS)
		goto fail;

	rc = OH_AudioStreamBuilder_SetSamplingRate(builder, (int32_t)ohos->rate);
	if (rc != AUDIOSTREAM_SUCCESS)
		goto fail;

	rc = OH_AudioStreamBuilder_SetChannelCount(builder, (int32_t)ohos->channels);
	if (rc != AUDIOSTREAM_SUCCESS)
		goto fail;

	rc = OH_AudioStreamBuilder_SetSampleFormat(builder, rdpsnd_ohos_sample_format(ohos->bitsPerSample));
	if (rc != AUDIOSTREAM_SUCCESS)
		goto fail;

	rc = OH_AudioStreamBuilder_SetEncodingType(builder, AUDIOSTREAM_ENCODING_TYPE_RAW);
	if (rc != AUDIOSTREAM_SUCCESS)
		goto fail;

	OH_AudioStreamBuilder_SetLatencyMode(builder, AUDIOSTREAM_LATENCY_MODE_FAST);
	OH_AudioStreamBuilder_SetRendererInfo(builder, AUDIOSTREAM_USAGE_MOVIE);
	if (frameSize > 0)
		OH_AudioStreamBuilder_SetFrameSizeInCallback(builder, frameSize);

	rc = OH_AudioStreamBuilder_SetRendererWriteDataCallback(builder, rdpsnd_ohos_on_write_data, ohos);
	if (rc != AUDIOSTREAM_SUCCESS)
		goto fail;

	rc = OH_AudioStreamBuilder_GenerateRenderer(builder, &ohos->renderer);
	if (rc != AUDIOSTREAM_SUCCESS)
		goto fail;

	OH_AudioStreamBuilder_Destroy(builder);
	builder = NULL;

	rdpsnd_ohos_set_volume(device, ohos->volume);

	rc = OH_AudioRenderer_Start(ohos->renderer);
	if (rc != AUDIOSTREAM_SUCCESS)
	{
		rdpsnd_ohos_release_renderer(ohos);
		return FALSE;
	}

	WLog_INFO(TAG, "OHAudio renderer started: rate=%" PRIu32 " channels=%" PRIu16
	               " bits=%" PRIu16 " queue=%zu latency=%" PRIu32 "ms",
	          ohos->rate, ohos->channels, ohos->bitsPerSample, ohos->queueCapacity,
	          ohos->latencyMs);
	return TRUE;

fail:
	WLog_ERR(TAG, "OHAudio renderer open failed: result=%d", rc);
	if (builder)
		OH_AudioStreamBuilder_Destroy(builder);
	rdpsnd_ohos_release_renderer(ohos);
	return FALSE;
}

static UINT rdpsnd_ohos_play(rdpsndDevicePlugin* device, const BYTE* data, size_t size)
{
	rdpsndOhosPlugin* ohos = (rdpsndOhosPlugin*)device;
	UINT32 latency = 0;

	if (!ohos || !ohos->renderer || !data || (size == 0))
		return 0;

	EnterCriticalSection(&ohos->lock);
	rdpsnd_ohos_push_locked(ohos, data, size);
	latency = rdpsnd_ohos_queued_latency_locked(ohos);
	LeaveCriticalSection(&ohos->lock);

	return latency;
}

static void rdpsnd_ohos_start(WINPR_ATTR_UNUSED rdpsndDevicePlugin* device)
{
}

static void rdpsnd_ohos_close(rdpsndDevicePlugin* device)
{
	rdpsndOhosPlugin* ohos = (rdpsndOhosPlugin*)device;

	if (!ohos)
		return;

	rdpsnd_ohos_release_renderer(ohos);

	EnterCriticalSection(&ohos->lock);
	rdpsnd_ohos_clear_queue_locked(ohos);
	LeaveCriticalSection(&ohos->lock);
}

static void rdpsnd_ohos_free(rdpsndDevicePlugin* device)
{
	rdpsndOhosPlugin* ohos = (rdpsndOhosPlugin*)device;

	if (!ohos)
		return;

	rdpsnd_ohos_release_renderer(ohos);

	if (ohos->lockInitialized)
		DeleteCriticalSection(&ohos->lock);

	free(ohos->queue);
	free(ohos);
}

FREERDP_ENTRY_POINT(UINT VCAPITYPE ohos_freerdp_rdpsnd_client_subsystem_entry(
    PFREERDP_RDPSND_DEVICE_ENTRY_POINTS pEntryPoints))
{
	rdpsndOhosPlugin* ohos = (rdpsndOhosPlugin*)calloc(1, sizeof(rdpsndOhosPlugin));

	if (!ohos)
		return CHANNEL_RC_NO_MEMORY;

	InitializeCriticalSection(&ohos->lock);
	ohos->lockInitialized = TRUE;
	ohos->volume = 0xFFFFFFFFU;
	ohos->rate = 44100U;
	ohos->channels = 2U;
	ohos->bitsPerSample = 16U;
	ohos->blockAlign = 4U;

	ohos->device.Open = rdpsnd_ohos_open;
	ohos->device.FormatSupported = rdpsnd_ohos_format_supported;
	ohos->device.GetVolume = rdpsnd_ohos_get_volume;
	ohos->device.SetVolume = rdpsnd_ohos_set_volume;
	ohos->device.Start = rdpsnd_ohos_start;
	ohos->device.Play = rdpsnd_ohos_play;
	ohos->device.Close = rdpsnd_ohos_close;
	ohos->device.Free = rdpsnd_ohos_free;

	pEntryPoints->pRegisterRdpsndDevice(pEntryPoints->rdpsnd, &ohos->device);
	WLog_INFO(TAG, "OHAudio rdpsnd device registered");
	return CHANNEL_RC_OK;
}
