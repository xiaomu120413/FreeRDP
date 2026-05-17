#ifndef FREERDP_CLIENT_OHOS_SESSION_CONFIG_H
#define FREERDP_CLIENT_OHOS_SESSION_CONFIG_H

#include <stddef.h>

#include <freerdp/api.h>
#include <freerdp/settings.h>
#include <winpr/wtypes.h>

#include "ohos_graphics.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
	BOOL graphicsPipeline;
	BOOL h264;
	BOOL clipboard;
	BOOL displayControl;
	BOOL audioPlayback;
	BOOL audioCapture;
	UINT32 audioPlaybackRate;
	UINT32 audioPlaybackChannels;
	UINT32 audioPlaybackLatencyMs;
	UINT32 audioCaptureRate;
	UINT32 audioCaptureChannels;
} FREERDP_OHOS_SESSION_CONFIG;

FREERDP_API FREERDP_OHOS_SESSION_CONFIG freerdp_ohos_session_config_default(void);
FREERDP_API void
freerdp_ohos_session_config_from_graphics(const FREERDP_OHOS_GRAPHICS_CONFIG* graphics,
                                          FREERDP_OHOS_SESSION_CONFIG* config);
FREERDP_API BOOL freerdp_ohos_session_apply_settings(rdpSettings* settings,
                                                     const FREERDP_OHOS_SESSION_CONFIG* config,
                                                     char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_session_add_standard_channels(
    rdpSettings* settings, const FREERDP_OHOS_SESSION_CONFIG* config, char* message,
    size_t messageSize);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_SESSION_CONFIG_H */
