/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Input Redirection Virtual Channel - HarmonyOS capturer lifecycle
 */

#include "audin_ohos_capturer.h"

#include "audin_ohos_format.h"
#include "audin_ohos_permission.h"

#include <ohaudio/native_audiostreambuilder.h>

#include <winpr/error.h>

#include <freerdp/addin.h>

static int32_t audin_ohos_on_read_data(OH_AudioCapturer* capturer, void* userData, void* buffer,
                                       int32_t length)
{
	AudinReceive receive = NULL;
	void* receiveUserData = NULL;
	AUDIO_FORMAT format = { 0 };
	AudinOhosDevice* ohos = (AudinOhosDevice*)userData;

	(void)capturer;
	if (!ohos || !buffer || length <= 0)
		return 0;

	EnterCriticalSection(&ohos->lock);
	if (ohos->started)
	{
		receive = ohos->receive;
		receiveUserData = ohos->userData;
		format = ohos->format;
	}
	LeaveCriticalSection(&ohos->lock);

	if (!receive)
		return 0;

	++g_callbackCount;
	g_lastCallbackBytes = (UINT32)length;
	g_capturedBytes += (UINT32)length;
	if (format.wBitsPerSample == 16)
	{
		const UINT32 peak = audin_ohos_pcm16_peak(buffer, length);
		g_lastCapturePeak = peak;
		if (peak > g_maxCapturePeak)
			g_maxCapturePeak = peak;
		if (peak > 256U)
			++g_nonSilentCallbackCount;
		else
			++g_silentCallbackCount;
	}

	const UINT error = receive(&format, (const BYTE*)buffer, (size_t)length, receiveUserData);
	if (error)
	{
		++g_receiveErrorCount;
		if (ohos->rdpcontext)
			setChannelError(ohos->rdpcontext, error,
			                "audin_ohos_on_read_data reported an error");
	}
	else
	{
		++g_deliveredCallbackCount;
		g_deliveredBytes += (UINT32)length;
	}

	return 0;
}

static int32_t audin_ohos_on_stream_event(OH_AudioCapturer* capturer, void* userData,
                                          OH_AudioStream_Event event)
{
	AudinOhosDevice* ohos = (AudinOhosDevice*)userData;

	(void)capturer;
	++g_streamEventCount;
	g_lastStreamEvent = (UINT32)event;
	audin_ohos_log(ohos, WLOG_DEBUG, "stream event=%" PRIu32, (UINT32)event);
	return 0;
}

static int32_t audin_ohos_on_interrupt_event(OH_AudioCapturer* capturer, void* userData,
                                             OH_AudioInterrupt_ForceType type,
                                             OH_AudioInterrupt_Hint hint)
{
	AudinOhosDevice* ohos = (AudinOhosDevice*)userData;

	(void)capturer;
	++g_interruptCount;
	g_lastInterruptType = (UINT32)type;
	g_lastInterruptHint = (UINT32)hint;
	audin_ohos_log(ohos, WLOG_WARN, "interrupt type=%" PRIu32 " hint=%" PRIu32, (UINT32)type,
	               (UINT32)hint);
	return 0;
}

static int32_t audin_ohos_on_error(OH_AudioCapturer* capturer, void* userData,
                                   OH_AudioStream_Result error)
{
	AudinOhosDevice* ohos = (AudinOhosDevice*)userData;

	(void)capturer;
	++g_errorCallbackCount;
	g_lastError = (UINT32)error;
	audin_ohos_log(ohos, WLOG_ERROR, "capturer error=%" PRIu32, (UINT32)error);
	return 0;
}

static UINT audin_ohos_create_capturer(AudinOhosDevice* ohos)
{
	OH_AudioStreamBuilder* builder = NULL;
	OH_AudioStream_Result rc = AUDIOSTREAM_SUCCESS;
	OH_AudioCapturer_Callbacks callbacks = { audin_ohos_on_read_data,
		                                     audin_ohos_on_stream_event,
		                                     audin_ohos_on_interrupt_event, audin_ohos_on_error };
	const int32_t frameSize = (int32_t)ohos->framesPerPacket;

	rc = OH_AudioStreamBuilder_Create(&builder, AUDIOSTREAM_TYPE_CAPTURER);
	if (rc != AUDIOSTREAM_SUCCESS)
		goto fail;

	if ((rc = OH_AudioStreamBuilder_SetSamplingRate(builder,
	                                                (int32_t)ohos->format.nSamplesPerSec)) !=
	    AUDIOSTREAM_SUCCESS)
		goto fail;
	if ((rc = OH_AudioStreamBuilder_SetChannelCount(builder, (int32_t)ohos->format.nChannels)) !=
	    AUDIOSTREAM_SUCCESS)
		goto fail;
	if ((rc = OH_AudioStreamBuilder_SetSampleFormat(
	         builder, audin_ohos_sample_format(ohos->format.wBitsPerSample))) !=
	    AUDIOSTREAM_SUCCESS)
		goto fail;
	if ((rc = OH_AudioStreamBuilder_SetEncodingType(builder, AUDIOSTREAM_ENCODING_TYPE_RAW)) !=
	    AUDIOSTREAM_SUCCESS)
		goto fail;
	if ((rc = OH_AudioStreamBuilder_SetLatencyMode(builder, AUDIOSTREAM_LATENCY_MODE_FAST)) !=
	    AUDIOSTREAM_SUCCESS)
		goto fail;
	if ((rc = OH_AudioStreamBuilder_SetCapturerInfo(builder, AUDIOSTREAM_SOURCE_TYPE_MIC)) !=
	    AUDIOSTREAM_SUCCESS)
		goto fail;
	if (frameSize > 0)
		(void)OH_AudioStreamBuilder_SetFrameSizeInCallback(builder, frameSize);
	if ((rc = OH_AudioStreamBuilder_SetCapturerCallback(builder, callbacks, ohos)) !=
	    AUDIOSTREAM_SUCCESS)
		goto fail;
	if ((rc = OH_AudioStreamBuilder_GenerateCapturer(builder, &ohos->capturer)) !=
	    AUDIOSTREAM_SUCCESS)
		goto fail;

	OH_AudioStreamBuilder_Destroy(builder);
	return CHANNEL_RC_OK;

fail:
	g_lastOhosResult = (UINT32)rc;
	audin_ohos_log(ohos, WLOG_ERROR, "failed to create OHAudio capturer: %" PRIu32,
	               (UINT32)rc);
	if (builder)
		OH_AudioStreamBuilder_Destroy(builder);
	return ERROR_INTERNAL_ERROR;
}

UINT audin_ohos_close(IAudinDevice* device)
{
	AudinOhosDevice* ohos = (AudinOhosDevice*)device;
	OH_AudioCapturer* capturer = NULL;

	if (!ohos)
		return ERROR_INVALID_PARAMETER;

	EnterCriticalSection(&ohos->lock);
	capturer = ohos->capturer;
	ohos->capturer = NULL;
	ohos->started = FALSE;
	ohos->receive = NULL;
	ohos->userData = NULL;
	LeaveCriticalSection(&ohos->lock);

	if (capturer)
	{
		OH_AudioStream_Result rc = OH_AudioCapturer_Stop(capturer);
		g_lastOhosResult = (UINT32)rc;
		rc = OH_AudioCapturer_Release(capturer);
		g_lastOhosResult = (UINT32)rc;
	}

	++g_closeCount;
	if (capturer)
		audin_ohos_log(ohos, WLOG_INFO, "capturer closed");
	else
		audin_ohos_log(ohos, WLOG_DEBUG, "capturer close ignored: no active capturer");
	return CHANNEL_RC_OK;
}

UINT audin_ohos_open(IAudinDevice* device, AudinReceive receive, void* userData)
{
	AudinOhosDevice* ohos = (AudinOhosDevice*)device;
	UINT error = CHANNEL_RC_OK;
	OH_AudioStream_Result rc = AUDIOSTREAM_SUCCESS;

	if (!ohos || !receive || !userData)
		return ERROR_INVALID_PARAMETER;
	if (ohos->capturer)
		return ERROR_ALREADY_INITIALIZED;
	if (ohos->bytesPerFrame == 0)
		return ERROR_INVALID_DATA;
	if (!audin_ohos_request_microphone_permission(ohos))
		return ERROR_ACCESS_DENIED;

	EnterCriticalSection(&ohos->lock);
	ohos->receive = receive;
	ohos->userData = userData;
	LeaveCriticalSection(&ohos->lock);

	error = audin_ohos_create_capturer(ohos);
	if (error != CHANNEL_RC_OK)
	{
		audin_ohos_close(device);
		return error;
	}

	rc = OH_AudioCapturer_Start(ohos->capturer);
	g_lastOhosResult = (UINT32)rc;
	if (rc != AUDIOSTREAM_SUCCESS)
	{
		audin_ohos_log(ohos, WLOG_ERROR, "OH_AudioCapturer_Start failed: %" PRIu32,
		               (UINT32)rc);
		audin_ohos_close(device);
		return ERROR_INTERNAL_ERROR;
	}

	EnterCriticalSection(&ohos->lock);
	ohos->started = TRUE;
	LeaveCriticalSection(&ohos->lock);

	++g_openCount;
	audin_ohos_log(ohos, WLOG_INFO,
	               "capturer started: %" PRIu32 "Hz/%" PRIu16 "ch/%" PRIu16
	               "bit framesPerPacket=%" PRIu32,
	               ohos->format.nSamplesPerSec, ohos->format.nChannels,
	               ohos->format.wBitsPerSample, ohos->framesPerPacket);
	return CHANNEL_RC_OK;
}
