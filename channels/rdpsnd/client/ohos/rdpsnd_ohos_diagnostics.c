/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Output Virtual Channel - HarmonyOS OHAudio diagnostics
 */

#include "rdpsnd_ohos_internal.h"

#include <stdarg.h>
#include <stdio.h>

#include <winpr/crt.h>

RdpsndOhosDiagnostics g_rdpsnd_ohos_diag = { 0 };

void rdpsnd_ohos_log_at(DWORD level, size_t line, const char* file, const char* function,
                        const char* format, ...)
{
	wLog* log = WLog_Get(TAG);

	if (!WLog_IsLevelActive(log, level))
		return;

	va_list ap;

	va_start(ap, format);
	WLog_PrintTextMessageVA(log, level, line, file, function, format, ap);
	va_end(ap);
}

void rdpsnd_ohos_update_renderer_stats(rdpsndOhosPlugin* ohos)
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
	               " defaultFormats=%" PRIu64 " starts=%" PRIu64
	               " primes=%" PRIu64 "/%" PRIu64
	               " nonSilent=%" PRIu64 " silent=%" PRIu64
	               " emptyCallbacks=%" PRIu64 " peak=%" PRIu32 "/%" PRIu32
	               " queue=%" PRIu32 " peakQueue=%" PRIu64
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
	               g_formatRejectedCount, g_defaultFormatCount, g_rendererStartCount,
	               g_rendererPrimeCount, g_rendererPrimeBytes,
	               g_nonSilentPlayCount, g_silentPlayCount, g_emptyCallbackCount,
	               g_lastPeakSample, g_maxPeakSample, g_lastQueueBytes, g_queuePeakBytes,
	               g_lastCallbackCopied, g_lastCallbackSize, g_lastRendererState,
	               g_lastUnderflowCount, g_lastOhosResult, g_lastInterruptType, g_lastInterruptHint,
	               g_lastDeviceChangeReason, g_lastRate, g_lastChannels, g_lastBitsPerSample,
	               g_lastLatencyMs, g_lastRejectedFormatTag, g_lastRejectedRate,
	               g_lastRejectedChannels, g_lastRejectedBitsPerSample, g_lastRejectedCbSize);
	return buffer;
}
