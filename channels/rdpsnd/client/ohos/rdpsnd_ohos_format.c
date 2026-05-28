/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Output Virtual Channel - HarmonyOS format negotiation
 */

#include "rdpsnd_ohos_format.h"

#include <string.h>

OH_AudioStream_SampleFormat rdpsnd_ohos_sample_format(UINT16 bitsPerSample)
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

UINT32 rdpsnd_ohos_peak_sample(const rdpsndOhosPlugin* ohos, const BYTE* data, size_t size)
{
	UINT32 peak = 0;

	if (!ohos || !data || (size == 0))
		return 0;

	if (ohos->bitsPerSample == 8U)
	{
		for (size_t index = 0; index < size; index++)
		{
			const int sample = (int)data[index] - 128;
			const UINT32 magnitude = (UINT32)((sample < 0) ? -sample : sample);
			if (magnitude > peak)
				peak = magnitude;
		}
		return peak;
	}

	if (ohos->bitsPerSample == 16U)
	{
		const size_t sampleCount = size / sizeof(INT16);
		const INT16* samples = (const INT16*)data;
		for (size_t index = 0; index < sampleCount; index++)
		{
			const int sample = samples[index];
			const UINT32 magnitude = (UINT32)((sample < 0) ? -sample : sample);
			if (magnitude > peak)
				peak = magnitude;
		}
	}

	return peak;
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

BOOL rdpsnd_ohos_format_supported(WINPR_ATTR_UNUSED rdpsndDevicePlugin* device,
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

BOOL rdpsnd_ohos_default_format(WINPR_ATTR_UNUSED rdpsndDevicePlugin* device,
                                const AUDIO_FORMAT* sourceFormat, AUDIO_FORMAT* defaultFormat)
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
	defaultFormat->nAvgBytesPerSec = defaultFormat->nSamplesPerSec * defaultFormat->nBlockAlign;
	defaultFormat->cbSize = 0;

	g_defaultFormatCount++;
	rdpsnd_ohos_log(WLOG_DEBUG,
	                "default PCM format sourceTag=%" PRIu16 " sourceRate=%" PRIu32
	                " sourceChannels=%" PRIu16 " -> rate=%" PRIu32 " channels=%" PRIu16
	                " bits=%" PRIu16 " blockAlign=%" PRIu16,
	                sourceFormat->wFormatTag, sourceFormat->nSamplesPerSec,
	                sourceFormat->nChannels, defaultFormat->nSamplesPerSec,
	                defaultFormat->nChannels, defaultFormat->wBitsPerSample,
	                defaultFormat->nBlockAlign);
	return TRUE;
}

UINT rdpsnd_ohos_server_format_announce(WINPR_ATTR_UNUSED rdpsndDevicePlugin* device,
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

	rdpsnd_ohos_log(WLOG_DEBUG, "server formats announced count=%zu directSupported=%zu", count,
	                supported);
	return CHANNEL_RC_OK;
}
