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

typedef struct
{
	const BYTE* data;
	UINT32 length;
	const RECTANGLE_16* regionRects;
	UINT32 numRegionRects;
} FREERDP_OHOS_RDPGFX_AVC444_STREAM_INFO;

typedef struct
{
	UINT32 codecId;
	UINT16 surfaceId;
	UINT32 left;
	UINT32 top;
	UINT32 width;
	UINT32 height;
	UINT32 targetWidth;
	UINT32 targetHeight;
	UINT32 frameId;
	BOOL frameOpen;
	UINT32 LC;
	FREERDP_OHOS_RDPGFX_AVC444_STREAM_INFO stream1;
	FREERDP_OHOS_RDPGFX_AVC444_STREAM_INFO stream2;
} FREERDP_OHOS_RDPGFX_AVC444_COMMAND_INFO;

typedef struct
{
	UINT32 frameId;
	UINT32 activeFrameId;
	BOOL matchedFrame;
} FREERDP_OHOS_RDPGFX_FRAME_INFO;

typedef void (*FREERDP_OHOS_RDPGFX_LOG_CALLBACK)(const char* message, void* userData);
typedef BOOL (*FREERDP_OHOS_RDPGFX_AVC420_SURFACE_CALLBACK)(
    const FREERDP_OHOS_RDPGFX_SURFACE_COMMAND_INFO* command, void* userData);
typedef BOOL (*FREERDP_OHOS_RDPGFX_AVC444_SURFACE_CALLBACK)(
    const FREERDP_OHOS_RDPGFX_AVC444_COMMAND_INFO* command, void* userData);
typedef BOOL (*FREERDP_OHOS_RDPGFX_AVC444_END_FRAME_CALLBACK)(
    const FREERDP_OHOS_RDPGFX_FRAME_INFO* frame, void* userData);
typedef void (*FREERDP_OHOS_RDPGFX_AVC444_OUTPUT_STATE_CALLBACK)(BOOL active,
                                                                 const char* reason,
                                                                 void* userData);

typedef struct
{
	BOOL avc420SurfaceMode;
	BOOL avc444GpuCompositor;
	UINT32 surfaceTargetWidth;
	UINT32 surfaceTargetHeight;
	FREERDP_OHOS_RDPGFX_LOG_CALLBACK log;
	FREERDP_OHOS_RDPGFX_AVC420_SURFACE_CALLBACK avc420SurfaceCommand;
	FREERDP_OHOS_RDPGFX_AVC444_SURFACE_CALLBACK avc444SurfaceCommand;
	FREERDP_OHOS_RDPGFX_AVC444_END_FRAME_CALLBACK avc444EndFrame;
	FREERDP_OHOS_RDPGFX_AVC444_OUTPUT_STATE_CALLBACK avc444OutputState;
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
FREERDP_API BOOL freerdp_ohos_rdpgfx_avc444_command_lc_is_valid(
    const FREERDP_OHOS_RDPGFX_AVC444_COMMAND_INFO* command);
FREERDP_API BOOL freerdp_ohos_rdpgfx_rects_valid(const RECTANGLE_16* rects, UINT32 count,
                                                 UINT32 width, UINT32 height);
FREERDP_API BOOL freerdp_ohos_rdpgfx_rects_cover_full_surface(const RECTANGLE_16* rects,
                                                              UINT32 count, UINT32 width,
                                                              UINT32 height);
FREERDP_API UINT32 freerdp_ohos_rdpgfx_avc444_chroma_v1_required_y_height(
    const RECTANGLE_16* rects, UINT32 count);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_RDPGFX_H */
