/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Output Virtual Channel - HarmonyOS OHAudio backend internals
 */

#ifndef FREERDP_CHANNEL_RDPSND_CLIENT_OHOS_INTERNAL_H
#define FREERDP_CHANNEL_RDPSND_CLIENT_OHOS_INTERNAL_H

#include <freerdp/config.h>

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <ohaudio/native_audiorenderer.h>
#include <ohaudio/native_audiostreambuilder.h>

#include <winpr/synch.h>

#include "rdpsnd_main.h"

typedef struct
{
	rdpsndDevicePlugin device;

	OH_AudioRenderer* renderer;
	CRITICAL_SECTION lock;
	BOOL lockInitialized;
	BOOL started;
	BOOL primed;

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

typedef struct
{
	UINT64 registeredCount;
	UINT64 openCount;
	UINT64 closeCount;
	UINT64 playCount;
	UINT64 playBytes;
	UINT64 callbackCount;
	UINT64 renderedBytes;
	UINT64 underrunBytes;
	UINT64 errorCallbackCount;
	UINT64 interruptCallbackCount;
	UINT64 deviceChangeCallbackCount;
	UINT64 serverFormatAnnounceCount;
	UINT64 lastServerFormatCount;
	UINT64 lastDirectSupportedServerFormatCount;
	UINT64 formatCheckCount;
	UINT64 formatSupportedCount;
	UINT64 formatRejectedCount;
	UINT64 defaultFormatCount;
	UINT64 rendererStartCount;
	UINT64 rendererPrimeCount;
	UINT64 rendererPrimeBytes;
	UINT64 nonSilentPlayCount;
	UINT64 silentPlayCount;
	UINT64 emptyCallbackCount;
	UINT64 queuePeakBytes;
	UINT32 lastRate;
	UINT16 lastChannels;
	UINT16 lastBitsPerSample;
	UINT32 lastLatencyMs;
	UINT32 lastQueueBytes;
	UINT32 lastCallbackSize;
	UINT32 lastCallbackCopied;
	UINT32 lastRendererState;
	UINT32 lastUnderflowCount;
	UINT32 lastOhosResult;
	UINT32 lastInterruptType;
	UINT32 lastInterruptHint;
	UINT32 lastDeviceChangeReason;
	UINT32 lastPeakSample;
	UINT32 maxPeakSample;
	UINT16 lastRejectedFormatTag;
	UINT32 lastRejectedRate;
	UINT16 lastRejectedChannels;
	UINT16 lastRejectedBitsPerSample;
	UINT16 lastRejectedCbSize;
} RdpsndOhosDiagnostics;

extern RdpsndOhosDiagnostics g_rdpsnd_ohos_diag;

#define g_registeredCount g_rdpsnd_ohos_diag.registeredCount
#define g_openCount g_rdpsnd_ohos_diag.openCount
#define g_closeCount g_rdpsnd_ohos_diag.closeCount
#define g_playCount g_rdpsnd_ohos_diag.playCount
#define g_playBytes g_rdpsnd_ohos_diag.playBytes
#define g_callbackCount g_rdpsnd_ohos_diag.callbackCount
#define g_renderedBytes g_rdpsnd_ohos_diag.renderedBytes
#define g_underrunBytes g_rdpsnd_ohos_diag.underrunBytes
#define g_errorCallbackCount g_rdpsnd_ohos_diag.errorCallbackCount
#define g_interruptCallbackCount g_rdpsnd_ohos_diag.interruptCallbackCount
#define g_deviceChangeCallbackCount g_rdpsnd_ohos_diag.deviceChangeCallbackCount
#define g_serverFormatAnnounceCount g_rdpsnd_ohos_diag.serverFormatAnnounceCount
#define g_lastServerFormatCount g_rdpsnd_ohos_diag.lastServerFormatCount
#define g_lastDirectSupportedServerFormatCount \
	g_rdpsnd_ohos_diag.lastDirectSupportedServerFormatCount
#define g_formatCheckCount g_rdpsnd_ohos_diag.formatCheckCount
#define g_formatSupportedCount g_rdpsnd_ohos_diag.formatSupportedCount
#define g_formatRejectedCount g_rdpsnd_ohos_diag.formatRejectedCount
#define g_defaultFormatCount g_rdpsnd_ohos_diag.defaultFormatCount
#define g_rendererStartCount g_rdpsnd_ohos_diag.rendererStartCount
#define g_rendererPrimeCount g_rdpsnd_ohos_diag.rendererPrimeCount
#define g_rendererPrimeBytes g_rdpsnd_ohos_diag.rendererPrimeBytes
#define g_nonSilentPlayCount g_rdpsnd_ohos_diag.nonSilentPlayCount
#define g_silentPlayCount g_rdpsnd_ohos_diag.silentPlayCount
#define g_emptyCallbackCount g_rdpsnd_ohos_diag.emptyCallbackCount
#define g_queuePeakBytes g_rdpsnd_ohos_diag.queuePeakBytes
#define g_lastRate g_rdpsnd_ohos_diag.lastRate
#define g_lastChannels g_rdpsnd_ohos_diag.lastChannels
#define g_lastBitsPerSample g_rdpsnd_ohos_diag.lastBitsPerSample
#define g_lastLatencyMs g_rdpsnd_ohos_diag.lastLatencyMs
#define g_lastQueueBytes g_rdpsnd_ohos_diag.lastQueueBytes
#define g_lastCallbackSize g_rdpsnd_ohos_diag.lastCallbackSize
#define g_lastCallbackCopied g_rdpsnd_ohos_diag.lastCallbackCopied
#define g_lastRendererState g_rdpsnd_ohos_diag.lastRendererState
#define g_lastUnderflowCount g_rdpsnd_ohos_diag.lastUnderflowCount
#define g_lastOhosResult g_rdpsnd_ohos_diag.lastOhosResult
#define g_lastInterruptType g_rdpsnd_ohos_diag.lastInterruptType
#define g_lastInterruptHint g_rdpsnd_ohos_diag.lastInterruptHint
#define g_lastDeviceChangeReason g_rdpsnd_ohos_diag.lastDeviceChangeReason
#define g_lastPeakSample g_rdpsnd_ohos_diag.lastPeakSample
#define g_maxPeakSample g_rdpsnd_ohos_diag.maxPeakSample
#define g_lastRejectedFormatTag g_rdpsnd_ohos_diag.lastRejectedFormatTag
#define g_lastRejectedRate g_rdpsnd_ohos_diag.lastRejectedRate
#define g_lastRejectedChannels g_rdpsnd_ohos_diag.lastRejectedChannels
#define g_lastRejectedBitsPerSample g_rdpsnd_ohos_diag.lastRejectedBitsPerSample
#define g_lastRejectedCbSize g_rdpsnd_ohos_diag.lastRejectedCbSize

void rdpsnd_ohos_log_at(DWORD level, size_t line, const char* file, const char* function,
                        const char* format, ...);

#define rdpsnd_ohos_log(level, ...) \
	rdpsnd_ohos_log_at(level, __LINE__, __FILE__, __func__, __VA_ARGS__)

void rdpsnd_ohos_update_renderer_stats(rdpsndOhosPlugin* ohos);

#endif /* FREERDP_CHANNEL_RDPSND_CLIENT_OHOS_INTERNAL_H */
