#ifndef FREERDP_CLIENT_OHOS_GRAPHICS_H
#define FREERDP_CLIENT_OHOS_GRAPHICS_H

#include <stddef.h>

#include <freerdp/api.h>
#include <winpr/wtypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
	FREERDP_OHOS_GRAPHICS_MODE_INVALID = -1,
	FREERDP_OHOS_GRAPHICS_MODE_GDI = 0,
	FREERDP_OHOS_GRAPHICS_MODE_RDPGFX = 1,
	FREERDP_OHOS_GRAPHICS_MODE_RDPGFX_H264 = 2
} FREERDP_OHOS_GRAPHICS_MODE;

typedef struct
{
	FREERDP_OHOS_GRAPHICS_MODE mode;
	BOOL enabled;
	BOOL h264;
	const char* modeName;
} FREERDP_OHOS_GRAPHICS_CONFIG;

FREERDP_API FREERDP_OHOS_GRAPHICS_CONFIG
freerdp_ohos_graphics_config_from_mode(const char* requestedMode);
FREERDP_API size_t freerdp_ohos_graphics_fallback_modes(const char* requestedMode,
                                                        const char** modes, size_t capacity);
FREERDP_API BOOL freerdp_ohos_graphics_should_retry_fallback(BOOL sessionFailed,
                                                             BOOL attemptConnected,
                                                             const char* failedMode,
                                                             size_t attemptIndex,
                                                             size_t attemptCount,
                                                             const char* message);
FREERDP_API UINT32 freerdp_ohos_graphics_align_down_to_multiple(UINT32 value, UINT32 alignment,
                                                                UINT32 minimum);
FREERDP_API void
freerdp_ohos_graphics_align_h264_desktop_size(const FREERDP_OHOS_GRAPHICS_CONFIG* config,
                                              UINT32* width, UINT32* height);

FREERDP_API BOOL freerdp_ohos_rdpgfx_caps_confirm_is_avc420(UINT32 version, UINT32 flags);
FREERDP_API BOOL freerdp_ohos_rdpgfx_caps_confirm_is_avc444(UINT32 version, UINT32 flags);
FREERDP_API BOOL freerdp_ohos_rdpgfx_codec_is_avc420(UINT32 codecId);
FREERDP_API BOOL freerdp_ohos_rdpgfx_surface_command_is_full_window(UINT32 left, UINT32 top,
                                                                    UINT32 width, UINT32 height,
                                                                    UINT32 targetWidth,
                                                                    UINT32 targetHeight);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_GRAPHICS_H */
