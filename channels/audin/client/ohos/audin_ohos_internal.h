/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Input Redirection Virtual Channel - HarmonyOS OHAudio backend internals
 */

#ifndef FREERDP_CHANNEL_AUDIN_CLIENT_OHOS_INTERNAL_H
#define FREERDP_CHANNEL_AUDIN_CLIENT_OHOS_INTERNAL_H

#include <freerdp/config.h>

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <ohaudio/native_audiocapturer.h>
#include <ohaudio/native_audiostreambuilder.h>

#include <winpr/synch.h>

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

typedef struct
{
	UINT64 registeredCount;
	UINT64 openCount;
	UINT64 closeCount;
	UINT64 permissionRequestCount;
	UINT64 permissionGrantedCount;
	UINT64 permissionDeniedCount;
	UINT64 callbackCount;
	UINT64 capturedBytes;
	UINT64 deliveredCallbackCount;
	UINT64 deliveredBytes;
	UINT64 silentCallbackCount;
	UINT64 nonSilentCallbackCount;
	UINT64 receiveErrorCount;
	UINT64 streamEventCount;
	UINT64 interruptCount;
	UINT64 errorCallbackCount;
	UINT64 formatCheckCount;
	UINT64 formatSupportedCount;
	UINT64 formatRejectedCount;
	UINT32 lastRate;
	UINT16 lastChannels;
	UINT16 lastBitsPerSample;
	UINT32 lastFramesPerPacket;
	UINT32 lastCallbackBytes;
	UINT32 lastCapturePeak;
	UINT32 maxCapturePeak;
	UINT32 lastOhosResult;
	UINT32 lastStreamEvent;
	UINT32 lastInterruptType;
	UINT32 lastInterruptHint;
	UINT32 lastError;
	UINT16 lastRejectedFormatTag;
	UINT32 lastRejectedRate;
	UINT16 lastRejectedChannels;
	UINT16 lastRejectedBitsPerSample;
} AudinOhosDiagnostics;

extern AudinOhosDiagnostics g_audin_ohos_diag;

#define g_registeredCount g_audin_ohos_diag.registeredCount
#define g_openCount g_audin_ohos_diag.openCount
#define g_closeCount g_audin_ohos_diag.closeCount
#define g_permissionRequestCount g_audin_ohos_diag.permissionRequestCount
#define g_permissionGrantedCount g_audin_ohos_diag.permissionGrantedCount
#define g_permissionDeniedCount g_audin_ohos_diag.permissionDeniedCount
#define g_callbackCount g_audin_ohos_diag.callbackCount
#define g_capturedBytes g_audin_ohos_diag.capturedBytes
#define g_deliveredCallbackCount g_audin_ohos_diag.deliveredCallbackCount
#define g_deliveredBytes g_audin_ohos_diag.deliveredBytes
#define g_silentCallbackCount g_audin_ohos_diag.silentCallbackCount
#define g_nonSilentCallbackCount g_audin_ohos_diag.nonSilentCallbackCount
#define g_receiveErrorCount g_audin_ohos_diag.receiveErrorCount
#define g_streamEventCount g_audin_ohos_diag.streamEventCount
#define g_interruptCount g_audin_ohos_diag.interruptCount
#define g_errorCallbackCount g_audin_ohos_diag.errorCallbackCount
#define g_formatCheckCount g_audin_ohos_diag.formatCheckCount
#define g_formatSupportedCount g_audin_ohos_diag.formatSupportedCount
#define g_formatRejectedCount g_audin_ohos_diag.formatRejectedCount
#define g_lastRate g_audin_ohos_diag.lastRate
#define g_lastChannels g_audin_ohos_diag.lastChannels
#define g_lastBitsPerSample g_audin_ohos_diag.lastBitsPerSample
#define g_lastFramesPerPacket g_audin_ohos_diag.lastFramesPerPacket
#define g_lastCallbackBytes g_audin_ohos_diag.lastCallbackBytes
#define g_lastCapturePeak g_audin_ohos_diag.lastCapturePeak
#define g_maxCapturePeak g_audin_ohos_diag.maxCapturePeak
#define g_lastOhosResult g_audin_ohos_diag.lastOhosResult
#define g_lastStreamEvent g_audin_ohos_diag.lastStreamEvent
#define g_lastInterruptType g_audin_ohos_diag.lastInterruptType
#define g_lastInterruptHint g_audin_ohos_diag.lastInterruptHint
#define g_lastError g_audin_ohos_diag.lastError
#define g_lastRejectedFormatTag g_audin_ohos_diag.lastRejectedFormatTag
#define g_lastRejectedRate g_audin_ohos_diag.lastRejectedRate
#define g_lastRejectedChannels g_audin_ohos_diag.lastRejectedChannels
#define g_lastRejectedBitsPerSample g_audin_ohos_diag.lastRejectedBitsPerSample

void audin_ohos_log_at(AudinOhosDevice* ohos, DWORD level, size_t line, const char* file,
                       const char* function, const char* format, ...);

#define audin_ohos_log(ohos, level, ...) \
	audin_ohos_log_at(ohos, level, __LINE__, __FILE__, __func__, __VA_ARGS__)

#endif /* FREERDP_CHANNEL_AUDIN_CLIENT_OHOS_INTERNAL_H */
