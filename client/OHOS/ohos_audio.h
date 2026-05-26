#ifndef FREERDP_CLIENT_OHOS_AUDIO_H
#define FREERDP_CLIENT_OHOS_AUDIO_H

#include <freerdp/api.h>
#include <winpr/wtypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef BOOL (*FREERDP_OHOS_MICROPHONE_PERMISSION_CALLBACK)(void* userData,
                                                            UINT32 timeoutMs);

FREERDP_API BOOL freerdp_audin_ohos_set_permission_callback(
    FREERDP_OHOS_MICROPHONE_PERMISSION_CALLBACK callback, void* userData);
FREERDP_API const char* freerdp_audin_ohos_get_diagnostics(void);

FREERDP_API BOOL freerdp_rdpsnd_ohos_get_stats(
    UINT64* registeredCount, UINT64* openCount, UINT64* closeCount, UINT64* playCount,
    UINT64* playBytes, UINT64* callbackCount, UINT64* renderedBytes, UINT64* underrunBytes,
    UINT32* lastRate, UINT16* lastChannels, UINT16* lastBitsPerSample, UINT32* lastLatencyMs);
FREERDP_API const char* freerdp_rdpsnd_ohos_get_diagnostics(void);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_AUDIO_H */
