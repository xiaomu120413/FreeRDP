/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Input Redirection Virtual Channel - HarmonyOS format negotiation
 */

#include "audin_ohos_format.h"

#include <string.h>

#include <winpr/error.h>

static BOOL audin_ohos_rate_supported(UINT32 rate)
{
	switch (rate)
	{
		case 8000:
		case 16000:
		case 44100:
		case 48000:
			return TRUE;
		default:
			return FALSE;
	}
}

static UINT32 audin_ohos_bytes_per_frame(const AUDIO_FORMAT* format)
{
	if (!format)
		return 0;
	if (format->nBlockAlign > 0)
		return format->nBlockAlign;
	return (UINT32)format->nChannels * (UINT32)(format->wBitsPerSample / 8U);
}

BOOL audin_ohos_format_supported(IAudinDevice* device, const AUDIO_FORMAT* format)
{
	AudinOhosDevice* ohos = (AudinOhosDevice*)device;
	BOOL supported = FALSE;

	if (!ohos || !format)
		return FALSE;

	++g_formatCheckCount;
	supported = format->wFormatTag == WAVE_FORMAT_PCM && format->cbSize == 0 &&
	            format->wBitsPerSample == 16 && format->nChannels == 1 &&
	            audin_ohos_rate_supported(format->nSamplesPerSec);

	if (supported)
	{
		++g_formatSupportedCount;
		return TRUE;
	}

	++g_formatRejectedCount;
	g_lastRejectedFormatTag = format->wFormatTag;
	g_lastRejectedRate = format->nSamplesPerSec;
	g_lastRejectedChannels = format->nChannels;
	g_lastRejectedBitsPerSample = format->wBitsPerSample;
	audin_ohos_log(ohos, WLOG_DEBUG,
	               "rejected format tag=%" PRIu16 " rate=%" PRIu32 " channels=%" PRIu16
	               " bits=%" PRIu16 " cbSize=%" PRIu16,
	               format->wFormatTag, format->nSamplesPerSec, format->nChannels,
	               format->wBitsPerSample, format->cbSize);
	return FALSE;
}

UINT audin_ohos_set_format(IAudinDevice* device, const AUDIO_FORMAT* format,
                           UINT32 framesPerPacket)
{
	AudinOhosDevice* ohos = (AudinOhosDevice*)device;

	if (!ohos || !format)
		return ERROR_INVALID_PARAMETER;
	if (!audin_ohos_format_supported(device, format))
		return ERROR_UNSUPPORTED_TYPE;

	EnterCriticalSection(&ohos->lock);
	ohos->format = *format;
	ohos->framesPerPacket = framesPerPacket > 0 ? framesPerPacket : 1024;
	ohos->bytesPerFrame = audin_ohos_bytes_per_frame(format);
	g_lastRate = format->nSamplesPerSec;
	g_lastChannels = format->nChannels;
	g_lastBitsPerSample = format->wBitsPerSample;
	g_lastFramesPerPacket = ohos->framesPerPacket;
	LeaveCriticalSection(&ohos->lock);

	audin_ohos_log(ohos, WLOG_INFO,
	               "format selected: %" PRIu32 "Hz/%" PRIu16 "ch/%" PRIu16
	               "bit framesPerPacket=%" PRIu32,
	               format->nSamplesPerSec, format->nChannels, format->wBitsPerSample,
	               ohos->framesPerPacket);
	return CHANNEL_RC_OK;
}

OH_AudioStream_SampleFormat audin_ohos_sample_format(UINT16 bitsPerSample)
{
	return bitsPerSample == 16 ? AUDIOSTREAM_SAMPLE_S16LE : AUDIOSTREAM_SAMPLE_U8;
}

UINT32 audin_ohos_pcm16_peak(const void* buffer, int32_t length)
{
	UINT32 peak = 0;
	const BYTE* bytes = (const BYTE*)buffer;
	const size_t samples = (size_t)length / sizeof(int16_t);

	for (size_t x = 0; x < samples; x++)
	{
		int16_t sample = 0;
		memcpy(&sample, &bytes[x * sizeof(sample)], sizeof(sample));
		const UINT32 magnitude = sample == INT16_MIN
		                             ? 32768U
		                             : (UINT32)(sample < 0 ? -sample : sample);
		if (magnitude > peak)
			peak = magnitude;
	}

	return peak;
}
