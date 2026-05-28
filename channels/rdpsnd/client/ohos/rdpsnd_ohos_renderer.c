/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Output Virtual Channel - HarmonyOS renderer lifecycle
 */

#include "rdpsnd_ohos_renderer.h"

#include "rdpsnd_ohos_format.h"
#include "rdpsnd_ohos_queue.h"

#include <ohaudio/native_audiostreambuilder.h>

#include <winpr/assert.h>

static OH_AudioData_Callback_Result rdpsnd_ohos_on_write_data(
    WINPR_ATTR_UNUSED OH_AudioRenderer* renderer, void* userData, void* audioData,
    int32_t audioDataSize)
{
	rdpsndOhosPlugin* ohos = (rdpsndOhosPlugin*)userData;

	if (!ohos || !audioData || (audioDataSize <= 0))
		return AUDIO_DATA_CALLBACK_RESULT_INVALID;

	(void)rdpsnd_ohos_fill_audio_buffer(ohos, audioData, audioDataSize, TRUE);
	return AUDIO_DATA_CALLBACK_RESULT_VALID;
}

static int32_t rdpsnd_ohos_on_stream_event(WINPR_ATTR_UNUSED OH_AudioRenderer* renderer,
                                           WINPR_ATTR_UNUSED void* userData,
                                           OH_AudioStream_Event event)
{
	g_deviceChangeCallbackCount++;
	g_lastDeviceChangeReason = (UINT32)event;
	rdpsnd_ohos_log(WLOG_DEBUG, "renderer stream event=%" PRIu32 " count=%" PRIu64,
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
	rdpsnd_ohos_log(WLOG_WARN,
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
	rdpsnd_ohos_log(WLOG_ERROR,
	                "renderer error result=%" PRIu32 " count=%" PRIu64 " state=%" PRIu32,
	                g_lastOhosResult, g_errorCallbackCount, g_lastRendererState);
	return 0;
}

void rdpsnd_ohos_release_renderer(rdpsndOhosPlugin* ohos)
{
	if (!ohos || !ohos->renderer)
		return;

	rdpsnd_ohos_update_renderer_stats(ohos);
	rdpsnd_ohos_log(WLOG_DEBUG,
	                "releasing renderer state=%" PRIu32 " underflows=%" PRIu32
	                " callbacks=%" PRIu64 " rendered=%" PRIu64 " underrunBytes=%" PRIu64,
	                g_lastRendererState, g_lastUnderflowCount, g_callbackCount, g_renderedBytes,
	                g_underrunBytes);
	(void)OH_AudioRenderer_Stop(ohos->renderer);
	(void)OH_AudioRenderer_Flush(ohos->renderer);
	(void)OH_AudioRenderer_Release(ohos->renderer);
	ohos->renderer = NULL;
	ohos->started = FALSE;
	ohos->primed = FALSE;
}

BOOL rdpsnd_ohos_set_volume(rdpsndDevicePlugin* device, UINT32 value)
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
			rdpsnd_ohos_log(WLOG_WARN, "SetVolume failed result=%" PRIu32 " volume=%f", (UINT32)rc,
			                volume);
			return FALSE;
		}
	}

	return TRUE;
}

UINT32 rdpsnd_ohos_get_volume(rdpsndDevicePlugin* device)
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

BOOL rdpsnd_ohos_open(rdpsndDevicePlugin* device, const AUDIO_FORMAT* format, UINT32 latency)
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

	rdpsnd_ohos_log(WLOG_DEBUG,
	                "opening renderer tag=%" PRIu16 " rate=%" PRIu32 " channels=%" PRIu16
	                " bits=%" PRIu16 " blockAlign=%" PRIu16 " avgBytes=%" PRIu32
	                " cbSize=%" PRIu16 " requestedLatency=%" PRIu32,
	                format->wFormatTag, format->nSamplesPerSec, format->nChannels,
	                format->wBitsPerSample, format->nBlockAlign, format->nAvgBytesPerSec,
	                format->cbSize, latency);

	if (!rdpsnd_ohos_format_supported(device, format))
	{
		rdpsnd_ohos_log(WLOG_ERROR,
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
		rdpsnd_ohos_log(WLOG_ERROR,
		                "renderer queue allocation failed bytesPerSecond=%zu latency=%" PRIu32,
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
		rdpsnd_ohos_log(WLOG_WARN,
		                "SetChannelLayout failed result=%" PRIu32 " channels=%" PRIu16
		                " layout=%" PRIu64,
		                (UINT32)rc, ohos->channels, (UINT64)channelLayout);
	}

	rc = OH_AudioStreamBuilder_SetLatencyMode(builder, AUDIOSTREAM_LATENCY_MODE_FAST);
	if (rc != AUDIOSTREAM_SUCCESS)
	{
		g_lastOhosResult = (UINT32)rc;
		rdpsnd_ohos_log(WLOG_WARN, "FAST latency mode failed result=%" PRIu32 ", fallback NORMAL",
		                (UINT32)rc);
		(void)OH_AudioStreamBuilder_SetLatencyMode(builder, AUDIOSTREAM_LATENCY_MODE_NORMAL);
	}

	rc = OH_AudioStreamBuilder_SetRendererInfo(builder, AUDIOSTREAM_USAGE_MUSIC);
	if (rc != AUDIOSTREAM_SUCCESS)
	{
		g_lastOhosResult = (UINT32)rc;
		rdpsnd_ohos_log(WLOG_WARN, "SetRendererInfo failed result=%" PRIu32, (UINT32)rc);
	}

	rc = OH_AudioStreamBuilder_SetRendererInterruptMode(builder,
	                                                    AUDIOSTREAM_INTERRUPT_MODE_INDEPENDENT);
	if (rc != AUDIOSTREAM_SUCCESS)
	{
		g_lastOhosResult = (UINT32)rc;
		rdpsnd_ohos_log(WLOG_WARN, "SetRendererInterruptMode failed result=%" PRIu32, (UINT32)rc);
	}

	if (frameSize > 0)
	{
		rc = OH_AudioStreamBuilder_SetFrameSizeInCallback(builder, frameSize);
		if (rc != AUDIOSTREAM_SUCCESS)
		{
			g_lastOhosResult = (UINT32)rc;
			rdpsnd_ohos_log(WLOG_WARN, "SetFrameSizeInCallback failed result=%" PRIu32 " frame=%d",
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

	g_openCount++;
	g_lastRate = ohos->rate;
	g_lastChannels = ohos->channels;
	g_lastBitsPerSample = ohos->bitsPerSample;
	g_lastLatencyMs = ohos->latencyMs;
	rdpsnd_ohos_update_renderer_stats(ohos);
	rdpsnd_ohos_log(WLOG_DEBUG,
	                "renderer prepared rate=%" PRIu32 " channels=%" PRIu16 " bits=%" PRIu16
	                " blockAlign=%" PRIu16 " queue=%zu latency=%" PRIu32 "ms state=%" PRIu32,
	                ohos->rate, ohos->channels, ohos->bitsPerSample, ohos->blockAlign,
	                ohos->queueCapacity, ohos->latencyMs, g_lastRendererState);
	return TRUE;

fail:
	g_lastOhosResult = (UINT32)rc;
	rdpsnd_ohos_log(WLOG_ERROR, "renderer open failed stage=%s result=%" PRIu32, stage,
	                (UINT32)rc);
	if (builder)
		OH_AudioStreamBuilder_Destroy(builder);
	rdpsnd_ohos_release_renderer(ohos);
	return FALSE;
}
