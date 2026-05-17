#ifndef FREERDP_CLIENT_OHOS_RDPGFX_H
#define FREERDP_CLIENT_OHOS_RDPGFX_H

#include <stddef.h>

#include <freerdp/api.h>
#include <freerdp/client/rdpgfx.h>
#include <winpr/wtypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct freerdp_ohos_rdpgfx_bridge freerdpOhosRdpgfxBridge;

typedef struct
{
	UINT32 codecId;
	UINT16 surfaceId;
	UINT32 left;
	UINT32 top;
	UINT32 width;
	UINT32 height;
} FREERDP_OHOS_RDPGFX_SURFACE_COMMAND_INFO;

typedef void (*FREERDP_OHOS_RDPGFX_LOG_CALLBACK)(const char* message, void* userData);
typedef BOOL (*FREERDP_OHOS_RDPGFX_H264_SURFACE_CALLBACK)(
    const FREERDP_OHOS_RDPGFX_SURFACE_COMMAND_INFO* command, void* userData);

typedef struct
{
	BOOL h264SurfaceMode;
	UINT32 surfaceTargetWidth;
	UINT32 surfaceTargetHeight;
	FREERDP_OHOS_RDPGFX_LOG_CALLBACK log;
	FREERDP_OHOS_RDPGFX_H264_SURFACE_CALLBACK h264SurfaceCommand;
	void* userData;
} FREERDP_OHOS_RDPGFX_BRIDGE_CONFIG;

FREERDP_API freerdpOhosRdpgfxBridge* freerdp_ohos_rdpgfx_bridge_new(void);
FREERDP_API void freerdp_ohos_rdpgfx_bridge_free(freerdpOhosRdpgfxBridge* bridge);
FREERDP_API void freerdp_ohos_rdpgfx_bridge_reset(freerdpOhosRdpgfxBridge* bridge,
                                                  BOOL requested, BOOL h264Requested);
FREERDP_API void freerdp_ohos_rdpgfx_bridge_set_surface_target(
    freerdpOhosRdpgfxBridge* bridge, UINT32 width, UINT32 height);
FREERDP_API BOOL freerdp_ohos_rdpgfx_bridge_attach(
    freerdpOhosRdpgfxBridge* bridge, RdpgfxClientContext* gfx,
    const FREERDP_OHOS_RDPGFX_BRIDGE_CONFIG* config, char* message, size_t messageSize);
FREERDP_API void freerdp_ohos_rdpgfx_bridge_detach(freerdpOhosRdpgfxBridge* bridge,
                                                   RdpgfxClientContext* gfx);
FREERDP_API void freerdp_ohos_rdpgfx_bridge_set_gdi_attached(
    freerdpOhosRdpgfxBridge* bridge, BOOL attached);
FREERDP_API const char*
freerdp_ohos_rdpgfx_bridge_get_diagnostics(freerdpOhosRdpgfxBridge* bridge);
FREERDP_API const char* freerdp_ohos_rdpgfx_codec_name(UINT32 codecId);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_RDPGFX_H */
