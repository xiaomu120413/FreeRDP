/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Input Redirection Virtual Channel - HarmonyOS OHAudio diagnostics
 */

#include "audin_ohos_internal.h"

#include <stdarg.h>
#include <stdio.h>

#include <winpr/crt.h>

AudinOhosDiagnostics g_audin_ohos_diag = { 0 };

void audin_ohos_log_at(AudinOhosDevice* ohos, DWORD level, size_t line, const char* file,
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
