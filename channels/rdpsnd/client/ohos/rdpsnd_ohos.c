/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Output Virtual Channel - HarmonyOS OHAudio backend
 */

#include <freerdp/config.h>

#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hilog/log.h>
#include <ohaudio/native_audiorenderer.h>
#include <ohaudio/native_audiostreambuilder.h>

#include <winpr/assert.h>
#include <winpr/crt.h>
#include <winpr/synch.h>

#include <freerdp/channels/log.h>
#include <freerdp/types.h>

#include "rdpsnd_main.h"

static UINT64 g_registeredCount = 0;
static UINT64 g_openCount = 0;
static UINT64 g_closeCount = 0;
static UINT64 g_playCount = 0;
static UINT64 g_playBytes = 0;
static UINT64 g_callbackCount = 0;
static UINT64 g_renderedBytes = 0;
static UINT64 g_underrunBytes = 0;
static UINT64 g_errorCallbackCount = 0;
static UINT64 g_interruptCallbackCount = 0;
static UINT64 g_deviceChangeCallbackCount = 0;
static UINT64 g_serverFormatAnnounceCount = 0;
static UINT64 g_lastServerFormatCount = 0;
static UINT64 g_lastDirectSupportedServerFormatCount = 0;
static UINT64 g_formatCheckCount = 0;
static UINT64 g_formatSupportedCount = 0;
static UINT64 g_formatRejectedCount = 0;
static UINT64 g_defaultFormatCount = 0;
static UINT64 g_queuePeakBytes = 0;
static UINT32 g_lastRate = 0;
static UINT16 g_lastChannels = 0;
static UINT16 g_lastBitsPerSample = 0;
static UINT32 g_lastLatencyMs = 0;
static UINT32 g_lastQueueBytes = 0;
static UINT32 g_lastCallbackSize = 0;
static UINT32 g_lastCallbackCopied = 0;
static UINT32 g_lastRendererState = 0;
static UINT32 g_lastUnderflowCount = 0;
static UINT32 g_lastOhosResult = 0;
static UINT32 g_lastInterruptType = 0;
static UINT32 g_lastInterruptHint = 0;
static UINT32 g_lastDeviceChangeReason = 0;
static UINT16 g_lastRejectedFormatTag = 0;
static UINT32 g_lastRejectedRate = 0;
static UINT16 g_lastRejectedChannels = 0;
static UINT16 g_lastRejectedBitsPerSample = 0;
static UINT16 g_lastRejectedCbSize = 0;

static void rdpsnd_ohos_log(LogLevel level, const char* format, ...)
{
	char message[768] = { 0 };
	va_list ap;

	va_start(ap, format);
	(void)vsnprintf(message, sizeof(message), format, ap);
	va_end(ap);
	OH_LOG_Print(LOG_APP, level, 0xF3D2, "FreeRDPAudio", "%{public}s", message);
}

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

FREERDP_API BOOL freerdp_rdpsnd_ohos_get_stats(
    UINT64* registeredCount, UINT64* openCount, UINT64* closeCount, UINT64* playCount,
    UINT64* playBytes, UINT64* callbackCount, UINT64* renderedBytes, UINT64* underrunBytes,
    UINT32* lastRate, UINT16* lastChannels, UINT16* lastBitsPerSample, UINT32* lastLatencyMs)
{
	if (registeredCount)
		*registeredCount = g_registeredCount;
	if (openCount)
		*openCount = g_openCount;
	if (closeCount)
		*closeCount = g_closeCount;
	if (playCount)
		*playCount = g_playCount;
	if (playBytes)
		*playBytes = g_playBytes;
	if (callbackCount)
		*callbackCount = g_callbackCount;
	if (renderedBytes)
		*renderedBytes = g_renderedBytes;
	if (underrunBytes)
		*underrunBytes = g_underrunBytes;
	if (lastRate)
		*lastRate = g_lastRate;
	if (lastChannels)
		*lastChannels = g_lastChannels;
	if (lastBitsPerSample)
		*lastBitsPerSample = g_lastBitsPerSample;
	if (lastLatencyMs)
		*lastLatencyMs = g_lastLatencyMs;
	return TRUE;
}

FREERDP_API const char* freerdp_rdpsnd_ohos_get_diagnostics(void)
{
	static char buffer[1200];
	(void)snprintf(buffer, sizeof(buffer),
	               "OHAudio stats: registered=%" PRIu64 " open=%" PRIu64 " close=%" PRIu64
	               " playCalls=%" PRIu64 " playBytes=%" PRIu64 " callbacks=%" PRIu64
	               " renderedBytes=%" PRIu64 " underrunBytes=%" PRIu64
	               " errors=%" PRIu64 " interrupts=%" PRIu64 " deviceChanges=%" PRIu64
	               " serverFormatAnnounces=%" PRIu64 " serverFormats=%" PRIu64
	               " directSupportedServerFormats=%" PRIu64 " formatChecks=%" PRIu64
	               " formatSupported=%" PRIu64 " formatRejected=%" PRIu64
	               " defaultFormats=%" PRIu64 " queue=%" PRIu32 " peakQueue=%" PRIu64
	               " lastCallback=%" PRIu32 "/%" PRIu32 " state=%" PRIu32
	               " underflows=%" PRIu32 " lastOhosResult=%" PRIu32
	               " lastInterrupt=%" PRIu32 "/%" PRIu32 " lastDeviceReason=%" PRIu32
	               " lastFormat=%" PRIu32 "Hz/%" PRIu16 "ch/%" PRIu16 "bit latency=%" PRIu32
	               "ms lastRejected=tag=%" PRIu16 " rate=%" PRIu32 " channels=%" PRIu16
	               " bits=%" PRIu16 " cbSize=%" PRIu16,
	               g_registeredCount, g_openCount, g_closeCount, g_playCount, g_playBytes,
	               g_callbackCount, g_renderedBytes, g_underrunBytes, g_errorCallbackCount,
	               g_interruptCallbackCount, g_deviceChangeCallbackCount,
	               g_serverFormatAnnounceCount, g_lastServerFormatCount,
	               g_lastDirectSupportedServerFormatCount, g_formatCheckCount, g_formatSupportedCount,
	               g_formatRejectedCount, g_defaultFormatCount, g_lastQueueBytes, g_queuePeakBytes,
	               g_lastCallbackCopied, g_lastCallbackSize, g_lastRendererState,
	               g_lastUnderflowCount, g_lastOhosResult, g_lastInterruptType, g_lastInterruptHint,
	               g_lastDeviceChangeReason, g_lastRate, g_lastChannels, g_lastBitsPerSample,
	               g_lastLatencyMs, g_lastRejectedFormatTag, g_lastRejectedRate,
	               g_lastRejectedChannels, g_lastRejectedBitsPerSample, g_lastRejectedCbSize);
	return buffer;
}

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

static void rdpsnd_ohos_update_renderer_stats(rdpsndOhosPlugin* ohos)
{
	OH_AudioStream_State state = AUDIOSTREAM_STATE_INVALID;
	UINT32 underflowCount = 0;

	if (!ohos || !ohos->renderer)
		return;

	if (OH_AudioRenderer_GetCurrentState(ohos->renderer, &state) == AUDIOSTREAM_SUCCESS)
		g_lastRendererState = (UINT32)state;
	if (OH_AudioRenderer_GetUnderflowCount(ohos->renderer, &underflowCount) == AUDIOSTREAM_SUCCESS)
		g_lastUnderflowCount = underflowCount;
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
		if (ohos->queueSize > g_queuePeakBytes)
			g_queuePeakBytes = (UINT64)ohos->queueSize;
		g_lastQueueBytes = (UINT32)((ohos->queueSize > UINT32_MAX) ? UINT32_MAX : ohos->queueSize);
		data += todo;
		size -= todo;
	}
}

static size_t rdpsnd_ohos_fill_audio_buffer(rdpsndOhosPlugin* ohos, void* audioData,
                                            int32_t audioDataSize)
{
	BYTE* dst = (BYTE*)audioData;
	size_t copied = 0;

	if (!ohos || !dst || (audioDataSize <= 0))
		return 0;

	EnterCriticalSection(&ohos->lock);
	copied = rdpsnd_ohos_pop_locked(ohos, dst, (size_t)audioDataSize);
	g_lastQueueBytes = (UINT32)((ohos->queueSize > UINT32_MAX) ? UINT32_MAX : ohos->queueSize);
	LeaveCriticalSection(&ohos->lock);

	g_callbackCount++;
	g_lastCallbackSize = (UINT32)audioDataSize;
	g_lastCallbackCopied = (UINT32)((copied > UINT32_MAX) ? UINT32_MAX : copied);
	g_renderedBytes += copied;
	if (copied < (size_t)audioDataSize)
	{
		g_underrunBytes += (UINT64)((size_t)audioDataSize - copied);
		memset(dst + copied, rdpsnd_ohos_silence_byte(ohos), (size_t)audioDataSize - copied);
	}

	if ((copied > 0) && ((g_callbackCount <= 5U) || ((g_callbackCount % 200U) == 0U)))
	{
		rdpsnd_ohos_update_renderer_stats(ohos);
		rdpsnd_ohos_log(LOG_INFO,
		                "callback copied=%zu requested=%d queue=%" PRIu32 " callbacks=%" PRIu64
		                " underflows=%" PRIu32 " state=%" PRIu32,
		                copied, audioDataSize, g_lastQueueBytes, g_callbackCount,
		                g_lastUnderflowCount, g_lastRendererState);
	}

	return copied;
}

static OH_AudioData_Callback_Result rdpsnd_ohos_on_write_data(
    WINPR_ATTR_UNUSED OH_AudioRenderer* renderer, void* userData, void* audioData,
    int32_t audioDataSize)
{
	rdpsndOhosPlugin* ohos = (rdpsndOhosPlugin*)userData;

	if (!ohos || !audioData || (audioDataSize <= 0))
		return AUDIO_DATA_CALLBACK_RESULT_INVALID;

	(void)rdpsnd_ohos_fill_audio_buffer(ohos, audioData, audioDataSize);
	return AUDIO_DATA_CALLBACK_RESULT_VALID;
}

static int32_t rdpsnd_ohos_on_stream_event(WINPR_ATTR_UNUSED OH_AudioRenderer* renderer,
                                           WINPR_ATTR_UNUSED void* userData,
                                           OH_AudioStream_Event event)
{
	g_deviceChangeCallbackCount++;
	g_lastDeviceChangeReason = (UINT32)event;
	rdpsnd_ohos_log(LOG_INFO, "renderer stream event=%" PRIu32 " count=%" PRIu64,
	                g_lastDeviceChangeReason, g_deviceChangeCallbackCount);
	return 0;
}

static int32_t rdpsnd_ohos_on_interrupt(WINPR_ATTR_UNUSED OH_AudioRenderer* renderer,
                                        void* userData, OH_AudioInterrupt_ForceType type,
                                        OH_AudioInterrupt_Hint hint)
{
	rdpsndOhosPlugin* ohos = (rdpsndOhosPlugin*)userData;

	g_interruptCallbackCount++;
	g_lastInterruptType = (UINT32)type;
	g_lastInterruptHint = (UINT32)hint;
	rdpsnd_ohos_update_renderer_stats(ohos);
	rdpsnd_ohos_log(LOG_WARN,
	                "renderer interrupt type=%" PRIu32 " hint=%" PRIu32 " count=%" PRIu64
	                " state=%" PRIu32,
	                g_lastInterruptType, g_lastInterruptHint, g_interruptCallbackCount,
	                g_lastRendererState);
	return 0;
}

static int32_t rdpsnd_ohos_on_error(WINPR_ATTR_UNUSED OH_AudioRenderer* renderer, void* userData,
                                    OH_AudioStream_Result error)
{
	rdpsndOhosPlugin* ohos = (rdpsndOhosPlugin*)userData;

	g_errorCallbackCount++;
	g_lastOhosResult = (UINT32)error;
	rdpsnd_ohos_update_renderer_stats(ohos);
	rdpsnd_ohos_log(LOG_ERROR,
	                "renderer error result=%" PRIu32 " count=%" PRIu64 " state=%" PRIu32,
	                g_lastOhosResult, g_errorCallbackCount, g_lastRendererState);
	return 0;
}

static void rdpsnd_ohos_release_renderer(rdpsndOhosPlugin* ohos)
{
	if (!ohos || !ohos->renderer)
		return;

	rdpsnd_ohos_update_renderer_stats(ohos);
	rdpsnd_ohos_log(LOG_INFO,
	                "releasing renderer state=%" PRIu32 " underflows=%" PRIu32
	                " callbacks=%" PRIu64 " rendered=%" PRIu64 " underrunBytes=%" PRIu64,
	                g_lastRendererState, g_lastUnderflowCount, g_callbackCount, g_renderedBytes,
	                g_underrunBytes);
	(void)OH_AudioRenderer_Stop(ohos->renderer);
	(void)OH_AudioRenderer_Flush(ohos->renderer);
	(void)OH_AudioRenderer_Release(ohos->renderer);
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

static void rdpsnd_ohos_record_rejected_format(const AUDIO_FORMAT* format)
{
	if (!format)
		return;

	g_lastRejectedFormatTag = format->wFormatTag;
	g_lastRejectedRate = format->nSamplesPerSec;
	g_lastRejectedChannels = format->nChannels;
	g_lastRejectedBitsPerSample = format->wBitsPerSample;
	g_lastRejectedCbSize = format->cbSize;
}

static BOOL rdpsnd_ohos_is_format_supported(const AUDIO_FORMAT* format)
{
	if (!format || (format->wFormatTag != WAVE_FORMAT_PCM) || (format->cbSize != 0))
		return FALSE;

	if ((format->nSamplesPerSec < 8000U) || (format->nSamplesPerSec > 48000U))
		return FALSE;

	if ((format->nChannels != 1U) && (format->nChannels != 2U))
		return FALSE;
	return (format->wBitsPerSample == 8U) || (format->wBitsPerSample == 16U);
}

static BOOL rdpsnd_ohos_format_supported(WINPR_ATTR_UNUSED rdpsndDevicePlugin* device,
                                         const AUDIO_FORMAT* format)
{
	const BOOL supported = rdpsnd_ohos_is_format_supported(format);

	g_formatCheckCount++;
	if (supported)
		g_formatSupportedCount++;
	else
	{
		g_formatRejectedCount++;
		rdpsnd_ohos_record_rejected_format(format);
	}

	return supported;
}

static BOOL rdpsnd_ohos_default_format(WINPR_ATTR_UNUSED rdpsndDevicePlugin* device,
                                       const AUDIO_FORMAT* sourceFormat,
                                       AUDIO_FORMAT* defaultFormat)
{
	UINT32 rate = 44100U;
	UINT16 channels = 2U;

	if (!sourceFormat || !defaultFormat)
		return FALSE;

	if ((sourceFormat->nSamplesPerSec >= 8000U) && (sourceFormat->nSamplesPerSec <= 48000U))
		rate = sourceFormat->nSamplesPerSec;
	if ((sourceFormat->nChannels == 1U) || (sourceFormat->nChannels == 2U))
		channels = sourceFormat->nChannels;

	memset(defaultFormat, 0, sizeof(*defaultFormat));
	defaultFormat->wFormatTag = WAVE_FORMAT_PCM;
	defaultFormat->nChannels = channels;
	defaultFormat->nSamplesPerSec = rate;
	defaultFormat->wBitsPerSample = 16U;
	defaultFormat->nBlockAlign =
	    (UINT16)((defaultFormat->nChannels * defaultFormat->wBitsPerSample) / 8U);
	defaultFormat->nAvgBytesPerSec =
	    defaultFormat->nSamplesPerSec * defaultFormat->nBlockAlign;
	defaultFormat->cbSize = 0;

	g_defaultFormatCount++;
	rdpsnd_ohos_log(LOG_INFO,
	                "default PCM format sourceTag=%" PRIu16 " sourceRate=%" PRIu32
	                " sourceChannels=%" PRIu16 " -> rate=%" PRIu32 " channels=%" PRIu16
	                " bits=%" PRIu16 " blockAlign=%" PRIu16,
	                sourceFormat->wFormatTag, sourceFormat->nSamplesPerSec,
	                sourceFormat->nChannels, defaultFormat->nSamplesPerSec,
	                defaultFormat->nChannels, defaultFormat->wBitsPerSample,
	                defaultFormat->nBlockAlign);
	return TRUE;
}

static UINT rdpsnd_ohos_server_format_announce(WINPR_ATTR_UNUSED rdpsndDevicePlugin* device,
                                               const AUDIO_FORMAT* formats, size_t count)
{
	size_t supported = 0;

	g_serverFormatAnnounceCount++;
	g_lastServerFormatCount = count;
	for (size_t index = 0; formats && (index < count); index++)
	{
		if (rdpsnd_ohos_is_format_supported(&formats[index]))
			supported++;
	}
	g_lastDirectSupportedServerFormatCount = supported;

	WLog_INFO(TAG, "OHAudio server formats announced: count=%zu direct-supported=%zu", count,
	          supported);
	rdpsnd_ohos_log(LOG_INFO, "server formats announced count=%zu directSupported=%zu", count,
	                supported);
	return CHANNEL_RC_OK;
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
	{
		const OH_AudioStream_Result rc = OH_AudioRenderer_SetVolume(ohos->renderer, volume);
		if (rc != AUDIOSTREAM_SUCCESS)
		{
			g_lastOhosResult = (UINT32)rc;
			rdpsnd_ohos_log(LOG_WARN, "SetVolume failed result=%" PRIu32 " volume=%f", (UINT32)rc,
			                volume);
			return FALSE;
		}
	}

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
	const char* stage = "start";
	const int32_t frameSize = format ? (int32_t)((format->nSamplesPerSec * 20U) / 1000U) : 0;
	OH_AudioChannelLayout channelLayout = CH_LAYOUT_STEREO;
	OH_AudioRenderer_Callbacks callbacks = { 0 };

	WINPR_ASSERT(ohos);
	WINPR_ASSERT(format);

	rdpsnd_ohos_log(LOG_INFO,
	                "opening renderer tag=%" PRIu16 " rate=%" PRIu32 " channels=%" PRIu16
	                " bits=%" PRIu16 " blockAlign=%" PRIu16 " avgBytes=%" PRIu32
	                " cbSize=%" PRIu16 " requestedLatency=%" PRIu32,
	                format->wFormatTag, format->nSamplesPerSec, format->nChannels,
	                format->wBitsPerSample, format->nBlockAlign, format->nAvgBytesPerSec,
	                format->cbSize, latency);

	if (!rdpsnd_ohos_format_supported(device, format))
	{
		rdpsnd_ohos_log(LOG_ERROR,
		                "renderer format unsupported tag=%" PRIu16 " rate=%" PRIu32
		                " channels=%" PRIu16 " bits=%" PRIu16 " cbSize=%" PRIu16,
		                format->wFormatTag, format->nSamplesPerSec, format->nChannels,
		                format->wBitsPerSample, format->cbSize);
		return FALSE;
	}

	rdpsnd_ohos_release_renderer(ohos);

	ohos->rate = format->nSamplesPerSec;
	ohos->channels = format->nChannels;
	ohos->bitsPerSample = format->wBitsPerSample;
	ohos->blockAlign = (UINT16)((ohos->channels * ohos->bitsPerSample) / 8U);
	if (ohos->blockAlign == 0)
		ohos->blockAlign = format->nBlockAlign;

	if (!rdpsnd_ohos_allocate_queue(ohos, latency))
	{
		rdpsnd_ohos_log(LOG_ERROR, "renderer queue allocation failed bytesPerSecond=%zu latency=%" PRIu32,
		                rdpsnd_ohos_bytes_per_second(ohos), latency);
		return FALSE;
	}

	stage = "OH_AudioStreamBuilder_Create";
	rc = OH_AudioStreamBuilder_Create(&builder, AUDIOSTREAM_TYPE_RENDERER);
	if (rc != AUDIOSTREAM_SUCCESS)
		goto fail;

	stage = "OH_AudioStreamBuilder_SetSamplingRate";
	rc = OH_AudioStreamBuilder_SetSamplingRate(builder, (int32_t)ohos->rate);
	if (rc != AUDIOSTREAM_SUCCESS)
		goto fail;

	stage = "OH_AudioStreamBuilder_SetChannelCount";
	rc = OH_AudioStreamBuilder_SetChannelCount(builder, (int32_t)ohos->channels);
	if (rc != AUDIOSTREAM_SUCCESS)
		goto fail;

	stage = "OH_AudioStreamBuilder_SetSampleFormat";
	rc = OH_AudioStreamBuilder_SetSampleFormat(builder, rdpsnd_ohos_sample_format(ohos->bitsPerSample));
	if (rc != AUDIOSTREAM_SUCCESS)
		goto fail;

	stage = "OH_AudioStreamBuilder_SetEncodingType";
	rc = OH_AudioStreamBuilder_SetEncodingType(builder, AUDIOSTREAM_ENCODING_TYPE_RAW);
	if (rc != AUDIOSTREAM_SUCCESS)
		goto fail;

	channelLayout = (ohos->channels == 1U) ? CH_LAYOUT_MONO : CH_LAYOUT_STEREO;
	rc = OH_AudioStreamBuilder_SetChannelLayout(builder, channelLayout);
	if (rc != AUDIOSTREAM_SUCCESS)
	{
		g_lastOhosResult = (UINT32)rc;
		rdpsnd_ohos_log(LOG_WARN,
		                "SetChannelLayout failed result=%" PRIu32 " channels=%" PRIu16
		                " layout=%" PRIu64,
		                (UINT32)rc, ohos->channels, (UINT64)channelLayout);
	}

	rc = OH_AudioStreamBuilder_SetLatencyMode(builder, AUDIOSTREAM_LATENCY_MODE_FAST);
	if (rc != AUDIOSTREAM_SUCCESS)
	{
		g_lastOhosResult = (UINT32)rc;
		rdpsnd_ohos_log(LOG_WARN, "FAST latency mode failed result=%" PRIu32 ", fallback NORMAL",
		                (UINT32)rc);
		(void)OH_AudioStreamBuilder_SetLatencyMode(builder, AUDIOSTREAM_LATENCY_MODE_NORMAL);
	}

	rc = OH_AudioStreamBuilder_SetRendererInfo(builder, AUDIOSTREAM_USAGE_MOVIE);
	if (rc != AUDIOSTREAM_SUCCESS)
	{
		g_lastOhosResult = (UINT32)rc;
		rdpsnd_ohos_log(LOG_WARN, "SetRendererInfo failed result=%" PRIu32, (UINT32)rc);
	}

	rc = OH_AudioStreamBuilder_SetRendererInterruptMode(builder,
	                                                    AUDIOSTREAM_INTERRUPT_MODE_INDEPENDENT);
	if (rc != AUDIOSTREAM_SUCCESS)
	{
		g_lastOhosResult = (UINT32)rc;
		rdpsnd_ohos_log(LOG_WARN, "SetRendererInterruptMode failed result=%" PRIu32, (UINT32)rc);
	}

	if (frameSize > 0)
	{
		rc = OH_AudioStreamBuilder_SetFrameSizeInCallback(builder, frameSize);
		if (rc != AUDIOSTREAM_SUCCESS)
		{
			g_lastOhosResult = (UINT32)rc;
			rdpsnd_ohos_log(LOG_WARN, "SetFrameSizeInCallback failed result=%" PRIu32 " frame=%d",
			                (UINT32)rc, frameSize);
		}
	}

	callbacks.OH_AudioRenderer_OnWriteData = NULL;
	callbacks.OH_AudioRenderer_OnStreamEvent = rdpsnd_ohos_on_stream_event;
	callbacks.OH_AudioRenderer_OnInterruptEvent = rdpsnd_ohos_on_interrupt;
	callbacks.OH_AudioRenderer_OnError = rdpsnd_ohos_on_error;

	stage = "OH_AudioStreamBuilder_SetRendererCallback";
	rc = OH_AudioStreamBuilder_SetRendererCallback(builder, callbacks, ohos);
	if (rc != AUDIOSTREAM_SUCCESS)
		goto fail;

	stage = "OH_AudioStreamBuilder_SetRendererWriteDataCallback";
	rc = OH_AudioStreamBuilder_SetRendererWriteDataCallback(builder, rdpsnd_ohos_on_write_data,
	                                                       ohos);
	if (rc != AUDIOSTREAM_SUCCESS)
		goto fail;

	stage = "OH_AudioStreamBuilder_GenerateRenderer";
	rc = OH_AudioStreamBuilder_GenerateRenderer(builder, &ohos->renderer);
	if (rc != AUDIOSTREAM_SUCCESS)
		goto fail;

	OH_AudioStreamBuilder_Destroy(builder);
	builder = NULL;

	rdpsnd_ohos_set_volume(device, ohos->volume);

	stage = "OH_AudioRenderer_Start";
	rc = OH_AudioRenderer_Start(ohos->renderer);
	if (rc != AUDIOSTREAM_SUCCESS)
	{
		g_lastOhosResult = (UINT32)rc;
		rdpsnd_ohos_log(LOG_ERROR, "renderer start failed result=%" PRIu32, (UINT32)rc);
		rdpsnd_ohos_release_renderer(ohos);
		return FALSE;
	}

	WLog_INFO(TAG, "OHAudio renderer started: rate=%" PRIu32 " channels=%" PRIu16
	               " bits=%" PRIu16 " queue=%zu latency=%" PRIu32 "ms",
	          ohos->rate, ohos->channels, ohos->bitsPerSample, ohos->queueCapacity,
	          ohos->latencyMs);
	g_openCount++;
	g_lastRate = ohos->rate;
	g_lastChannels = ohos->channels;
	g_lastBitsPerSample = ohos->bitsPerSample;
	g_lastLatencyMs = ohos->latencyMs;
	rdpsnd_ohos_update_renderer_stats(ohos);
	rdpsnd_ohos_log(LOG_INFO,
	                "renderer started rate=%" PRIu32 " channels=%" PRIu16 " bits=%" PRIu16
	                " blockAlign=%" PRIu16 " queue=%zu latency=%" PRIu32 "ms state=%" PRIu32,
	                ohos->rate, ohos->channels, ohos->bitsPerSample, ohos->blockAlign,
	                ohos->queueCapacity, ohos->latencyMs, g_lastRendererState);
	return TRUE;

fail:
	g_lastOhosResult = (UINT32)rc;
	WLog_ERR(TAG, "OHAudio renderer open failed at %s: result=%d", stage, rc);
	rdpsnd_ohos_log(LOG_ERROR, "renderer open failed stage=%s result=%" PRIu32, stage,
	                (UINT32)rc);
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

	g_playCount++;
	g_playBytes += size;
	if ((g_playCount <= 5U) || ((g_playCount % 50U) == 0U))
	{
		rdpsnd_ohos_update_renderer_stats(ohos);
		rdpsnd_ohos_log(LOG_INFO,
		                "play size=%zu latency=%" PRIu32 " queue=%" PRIu32
		                " playCalls=%" PRIu64 " playBytes=%" PRIu64
		                " state=%" PRIu32 " underflows=%" PRIu32,
		                size, latency, g_lastQueueBytes, g_playCount, g_playBytes,
		                g_lastRendererState, g_lastUnderflowCount);
	}
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

	rdpsnd_ohos_log(LOG_INFO, "closing renderer queue=%" PRIu32 " playCalls=%" PRIu64,
	                g_lastQueueBytes, g_playCount);
	rdpsnd_ohos_release_renderer(ohos);
	g_closeCount++;

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
	ohos->device.DefaultFormat = rdpsnd_ohos_default_format;
	ohos->device.GetVolume = rdpsnd_ohos_get_volume;
	ohos->device.SetVolume = rdpsnd_ohos_set_volume;
	ohos->device.Start = rdpsnd_ohos_start;
	ohos->device.Play = rdpsnd_ohos_play;
	ohos->device.Close = rdpsnd_ohos_close;
	ohos->device.Free = rdpsnd_ohos_free;
	ohos->device.ServerFormatAnnounce = rdpsnd_ohos_server_format_announce;

	pEntryPoints->pRegisterRdpsndDevice(pEntryPoints->rdpsnd, &ohos->device);
	g_registeredCount++;
	WLog_INFO(TAG, "OHAudio rdpsnd device registered");
	rdpsnd_ohos_log(LOG_INFO, "rdpsnd OHAudio device registered count=%" PRIu64,
	                g_registeredCount);
	return CHANNEL_RC_OK;
}
