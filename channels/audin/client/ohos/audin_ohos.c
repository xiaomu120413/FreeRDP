/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Input Redirection Virtual Channel - HarmonyOS OHAudio backend
 */

#include <freerdp/config.h>

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <ohaudio/native_audiocapturer.h>
#include <ohaudio/native_audiostreambuilder.h>

#include <winpr/assert.h>
#include <winpr/crt.h>
#include <winpr/error.h>
#include <winpr/synch.h>

#include <freerdp/addin.h>
#include <freerdp/freerdp.h>

#include "audin_main.h"

typedef struct
{
	IAudinDevice iface;

	OH_AudioCapturer* capturer;
	CRITICAL_SECTION lock;
	BOOL lockInitialized;
	BOOL started;

	AUDIO_FORMAT format;
	UINT32 framesPerPacket;
	UINT32 bytesPerFrame;
	AudinReceive receive;
	void* userData;

	rdpContext* rdpcontext;
	wLog* log;
} AudinOhosDevice;

typedef BOOL (*pfnAudinOhosPermissionRequest)(void* userData, UINT32 timeoutMs);

static pthread_mutex_t g_permissionCallbackLock = PTHREAD_MUTEX_INITIALIZER;
static pfnAudinOhosPermissionRequest g_permissionRequest = NULL;
static void* g_permissionRequestUserData = NULL;
static UINT64 g_registeredCount = 0;
static UINT64 g_openCount = 0;
static UINT64 g_closeCount = 0;
static UINT64 g_permissionRequestCount = 0;
static UINT64 g_permissionGrantedCount = 0;
static UINT64 g_permissionDeniedCount = 0;
static UINT64 g_callbackCount = 0;
static UINT64 g_capturedBytes = 0;
static UINT64 g_deliveredCallbackCount = 0;
static UINT64 g_deliveredBytes = 0;
static UINT64 g_silentCallbackCount = 0;
static UINT64 g_nonSilentCallbackCount = 0;
static UINT64 g_receiveErrorCount = 0;
static UINT64 g_streamEventCount = 0;
static UINT64 g_interruptCount = 0;
static UINT64 g_errorCallbackCount = 0;
static UINT64 g_formatCheckCount = 0;
static UINT64 g_formatSupportedCount = 0;
static UINT64 g_formatRejectedCount = 0;
static UINT32 g_lastRate = 0;
static UINT16 g_lastChannels = 0;
static UINT16 g_lastBitsPerSample = 0;
static UINT32 g_lastFramesPerPacket = 0;
static UINT32 g_lastCallbackBytes = 0;
static UINT32 g_lastCapturePeak = 0;
static UINT32 g_maxCapturePeak = 0;
static UINT32 g_lastOhosResult = 0;
static UINT32 g_lastStreamEvent = 0;
static UINT32 g_lastInterruptType = 0;
static UINT32 g_lastInterruptHint = 0;
static UINT32 g_lastError = 0;
static UINT16 g_lastRejectedFormatTag = 0;
static UINT32 g_lastRejectedRate = 0;
static UINT16 g_lastRejectedChannels = 0;
static UINT16 g_lastRejectedBitsPerSample = 0;

static void audin_ohos_log_at(AudinOhosDevice* ohos, DWORD level, size_t line, const char* file,
                              const char* function, const char* format, ...)
{
	wLog* log = ohos ? ohos->log : WLog_Get(TAG);

	if (!WLog_IsLevelActive(log, level))
		return;

	va_list ap;
	va_start(ap, format);
	WLog_PrintTextMessageVA(log, level, line, file, function, format, ap);
	va_end(ap);
}

#define audin_ohos_log(ohos, level, ...) \
	audin_ohos_log_at(ohos, level, __LINE__, __FILE__, __func__, __VA_ARGS__)

FREERDP_API BOOL freerdp_audin_ohos_set_permission_callback(
    pfnAudinOhosPermissionRequest callback, void* userData)
{
	pthread_mutex_lock(&g_permissionCallbackLock);
	g_permissionRequest = callback;
	g_permissionRequestUserData = userData;
	pthread_mutex_unlock(&g_permissionCallbackLock);
	return TRUE;
}

static BOOL audin_ohos_request_microphone_permission(AudinOhosDevice* ohos)
{
	pfnAudinOhosPermissionRequest callback = NULL;
	void* userData = NULL;
	BOOL granted = TRUE;

	pthread_mutex_lock(&g_permissionCallbackLock);
	callback = g_permissionRequest;
	userData = g_permissionRequestUserData;
	pthread_mutex_unlock(&g_permissionCallbackLock);

	if (!callback)
	{
		audin_ohos_log(ohos, WLOG_DEBUG,
		               "microphone permission callback is not registered; relying on OHAudio");
		return TRUE;
	}

	++g_permissionRequestCount;
	audin_ohos_log(ohos, WLOG_INFO,
	               "requesting OHOS microphone permission before starting remote audio capture");
	granted = callback(userData, 60000);
	if (granted)
	{
		++g_permissionGrantedCount;
		audin_ohos_log(ohos, WLOG_INFO, "OHOS microphone permission granted");
		return TRUE;
	}

	++g_permissionDeniedCount;
	audin_ohos_log(ohos, WLOG_WARN,
	               "OHOS microphone permission denied or timed out; audin capture open rejected");
	return FALSE;
}

FREERDP_API const char* freerdp_audin_ohos_get_diagnostics(void)
{
	static char buffer[1400];
	(void)snprintf(buffer, sizeof(buffer),
	               "OHAudio audin stats: registered=%" PRIu64 " open=%" PRIu64
	               " close=%" PRIu64 " callbacks=%" PRIu64
	               " permission=requests:%" PRIu64 "/granted:%" PRIu64 "/denied:%" PRIu64
	               " capturedBytes=%" PRIu64 " delivered=%" PRIu64 "/%" PRIu64
	               " receiveErrors=%" PRIu64
	               " captureLevel=silent:%" PRIu64 "/nonSilent:%" PRIu64
	               " peak:%" PRIu32 "/%" PRIu32
	               " streamEvents=%" PRIu64 " interrupts=%" PRIu64
	               " errors=%" PRIu64 " formatChecks=%" PRIu64
	               " formatSupported=%" PRIu64 " formatRejected=%" PRIu64
	               " lastFormat=%" PRIu32 "Hz/%" PRIu16 "ch/%" PRIu16
	               "bit framesPerPacket=%" PRIu32 " lastCallbackBytes=%" PRIu32
	               " lastOhosResult=%" PRIu32 " lastStreamEvent=%" PRIu32
	               " lastInterrupt=%" PRIu32 "/%" PRIu32 " lastError=%" PRIu32
	               " lastRejected=tag=%" PRIu16 " rate=%" PRIu32
	               " channels=%" PRIu16 " bits=%" PRIu16,
	               g_registeredCount, g_openCount, g_closeCount, g_callbackCount,
	               g_permissionRequestCount, g_permissionGrantedCount, g_permissionDeniedCount,
	               g_capturedBytes, g_deliveredCallbackCount, g_deliveredBytes,
	               g_receiveErrorCount, g_silentCallbackCount, g_nonSilentCallbackCount,
	               g_lastCapturePeak, g_maxCapturePeak, g_streamEventCount, g_interruptCount,
	               g_errorCallbackCount, g_formatCheckCount, g_formatSupportedCount,
	               g_formatRejectedCount, g_lastRate, g_lastChannels, g_lastBitsPerSample,
	               g_lastFramesPerPacket, g_lastCallbackBytes, g_lastOhosResult, g_lastStreamEvent,
	               g_lastInterruptType, g_lastInterruptHint, g_lastError, g_lastRejectedFormatTag,
	               g_lastRejectedRate, g_lastRejectedChannels, g_lastRejectedBitsPerSample);
	return buffer;
}

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

static BOOL audin_ohos_format_supported(IAudinDevice* device, const AUDIO_FORMAT* format)
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

static UINT audin_ohos_set_format(IAudinDevice* device, const AUDIO_FORMAT* format,
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

static OH_AudioStream_SampleFormat audin_ohos_sample_format(UINT16 bitsPerSample)
{
	return bitsPerSample == 16 ? AUDIOSTREAM_SAMPLE_S16LE : AUDIOSTREAM_SAMPLE_U8;
}

static UINT32 audin_ohos_pcm16_peak(const void* buffer, int32_t length)
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

static UINT audin_ohos_close(IAudinDevice* device)
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

static UINT audin_ohos_open(IAudinDevice* device, AudinReceive receive, void* userData)
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

static UINT audin_ohos_free(IAudinDevice* device)
{
	AudinOhosDevice* ohos = (AudinOhosDevice*)device;

	if (!ohos)
		return ERROR_INVALID_PARAMETER;

	audin_ohos_close(device);
	if (ohos->lockInitialized)
	{
		DeleteCriticalSection(&ohos->lock);
		ohos->lockInitialized = FALSE;
	}
	free(ohos);
	return CHANNEL_RC_OK;
}

FREERDP_ENTRY_POINT(UINT VCAPITYPE
                       ohos_freerdp_audin_client_subsystem_entry(
                           PFREERDP_AUDIN_DEVICE_ENTRY_POINTS pEntryPoints))
{
	UINT error = CHANNEL_RC_OK;
	AudinOhosDevice* ohos = NULL;

	if (!pEntryPoints || !pEntryPoints->pRegisterAudinDevice)
		return ERROR_INVALID_PARAMETER;

	ohos = (AudinOhosDevice*)calloc(1, sizeof(AudinOhosDevice));
	if (!ohos)
		return CHANNEL_RC_NO_MEMORY;

	ohos->log = WLog_Get(TAG);
	ohos->iface.Open = audin_ohos_open;
	ohos->iface.FormatSupported = audin_ohos_format_supported;
	ohos->iface.SetFormat = audin_ohos_set_format;
	ohos->iface.Close = audin_ohos_close;
	ohos->iface.Free = audin_ohos_free;
	ohos->rdpcontext = pEntryPoints->rdpcontext;
	InitializeCriticalSection(&ohos->lock);
	ohos->lockInitialized = TRUE;

	error = pEntryPoints->pRegisterAudinDevice(pEntryPoints->plugin, (IAudinDevice*)ohos);
	if (error != CHANNEL_RC_OK)
	{
		audin_ohos_log(ohos, WLOG_ERROR, "RegisterAudinDevice failed with error %" PRIu32,
		               error);
		audin_ohos_free((IAudinDevice*)ohos);
		return error;
	}

	++g_registeredCount;
	audin_ohos_log(ohos, WLOG_INFO, "OHOS audin backend registered");
	return CHANNEL_RC_OK;
}
