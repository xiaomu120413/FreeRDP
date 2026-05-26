#ifndef FREERDP_CLIENT_OHOS_RDPGFX_INTERNAL_H
#define FREERDP_CLIENT_OHOS_RDPGFX_INTERNAL_H

#include "ohos_rdpgfx.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <freerdp/channels/rdpgfx.h>
#include <freerdp/client/channels.h>
#include <freerdp/constants.h>
#include <freerdp/error.h>
#include <freerdp/gdi/gfx.h>
#include <freerdp/gdi/region.h>
#include <winpr/crt.h>
#include <winpr/synch.h>
#include <winpr/wtsapi.h>

#include "ohos_graphics.h"

#define OHOS_RDPGFX_DIAGNOSTICS_SIZE 2304

typedef struct
{
	pcRdpgfxStartFrame startFrame;
	pcRdpgfxEndFrame endFrame;
	pcRdpgfxSurfaceCommand surfaceCommand;
	pcRdpgfxCapsAdvertise capsAdvertise;
	pcRdpgfxCapsConfirm capsConfirm;
} FREERDP_OHOS_RDPGFX_HOOKS;

typedef struct
{
	GENERIC_DYNVC_PLUGIN base;
	void* zgfx;
	UINT32 unacknowledgedFrames;
	UINT32 totalDecodedFrames;
	UINT64 startDecodingTime;
	BOOL suspendFrameAcks;
	BOOL sendFrameAcks;
	void* surfaceTable;
	UINT16 maxCacheSlots;
	void* cacheSlots[25600];
	void* persistent;
	rdpContext* rdpcontext;
	RDPGFX_CAPSET connectionCaps;
	RdpgfxClientContext* context;
} FREERDP_OHOS_RDPGFX_PLUGIN_SNAPSHOT;

struct freerdp_ohos_rdpgfx_bridge
{
	CRITICAL_SECTION lock;
	BOOL initialized;
	BOOL requested;
	BOOL h264Requested;
	BOOL gdiAttached;
	BOOL avc420SurfaceMode;
	BOOL avc420SurfaceActive;
	BOOL avc444GpuCompositor;
	UINT32 surfaceTargetWidth;
	UINT32 surfaceTargetHeight;
	UINT32 activeFrameId;
	BOOL frameOpen;
	RdpgfxClientContext* gfx;
	FREERDP_OHOS_RDPGFX_HOOKS hooks;
	FREERDP_OHOS_RDPGFX_LOG_CALLBACK log;
	FREERDP_OHOS_RDPGFX_AVC420_SURFACE_CALLBACK avc420SurfaceCommand;
	FREERDP_OHOS_RDPGFX_AVC444_SURFACE_CALLBACK avc444SurfaceCommand;
	FREERDP_OHOS_RDPGFX_AVC444_END_FRAME_CALLBACK avc444EndFrame;
	FREERDP_OHOS_RDPGFX_AVC444_OUTPUT_STATE_CALLBACK avc444OutputState;
	void* userData;
	UINT32 connected;
	UINT32 disconnected;
	UINT32 initFailed;
	UINT64 startFrames;
	UINT64 endFrames;
	UINT64 surfaceCommands;
	UINT64 codecUncompressed;
	UINT64 codecCavideo;
	UINT64 codecClearCodec;
	UINT64 codecPlanar;
	UINT64 codecProgressive;
	UINT64 codecAvc420;
	UINT64 codecAlpha;
	UINT64 codecAvc444;
	UINT64 codecAvc444v2;
	UINT64 codecUnknown;
	UINT64 avc420SurfaceSubrectSkips;
	UINT64 avc420SurfaceNoDirect;
	UINT64 avc444GpuCandidates;
	UINT64 avc444GpuDisabled;
	UINT64 avc444GpuGdiPreserved;
	UINT64 avc444GpuFrameMismatchSkips;
	UINT64 avc444GpuCallbacks;
	UINT64 avc444GpuCallbackReady;
	BOOL avc444GpuOutputActive;
	UINT64 avc444GpuOutputActivations;
	UINT64 avc444GpuOutputReleases;
	UINT64 avc444GpuActiveSuppressedFailures;
	UINT32 capsAdvertises;
	UINT32 advertisedCapsSets;
	BOOL advertisedAvc420;
	UINT32 advertisedVersion;
	UINT32 advertisedFlags;
	UINT32 capsConfirms;
	UINT32 confirmedMode;
	UINT32 confirmedVersion;
	UINT32 confirmedFlags;
	UINT32 lastCodecId;
	UINT32 lastSurfaceId;
	UINT32 lastCommandWidth;
	UINT32 lastCommandHeight;
	UINT32 lastAvc444FrameId;
	UINT32 lastAvc444LC;
	UINT32 lastAvc444Stream1Rects;
	UINT32 lastAvc444Stream2Rects;
	UINT32 lastAvc444Stream1Bytes;
	UINT32 lastAvc444Stream2Bytes;
	char diagnostics[OHOS_RDPGFX_DIAGNOSTICS_SIZE];
	struct freerdp_ohos_rdpgfx_bridge* next;
};

enum
{
	OHOS_RDPGFX_CONFIRMED_NONE = 0,
	OHOS_RDPGFX_CONFIRMED_AVC420 = 1,
	OHOS_RDPGFX_CONFIRMED_AVC444 = 2,
	OHOS_RDPGFX_CONFIRMED_NON_AVC = 3
};

void ohos_rdpgfx_format_message(char* message, size_t size, const char* format, ...);
const char* ohos_rdpgfx_confirmed_mode_name(UINT32 mode);
void ohos_rdpgfx_log(freerdpOhosRdpgfxBridge* bridge, const char* format, ...);
BOOL ohos_rdpgfx_should_log_counter(UINT64 count);

void ohos_rdpgfx_registry_add(freerdpOhosRdpgfxBridge* bridge);
void ohos_rdpgfx_registry_remove(freerdpOhosRdpgfxBridge* bridge);
freerdpOhosRdpgfxBridge* ohos_rdpgfx_bridge_from_context(RdpgfxClientContext* context);

void ohos_rdpgfx_record_caps_values(freerdpOhosRdpgfxBridge* bridge, UINT32 version, UINT32 flags,
                                    const char* source);
void ohos_rdpgfx_record_connection_caps_snapshot(freerdpOhosRdpgfxBridge* bridge,
                                                 RdpgfxClientContext* gfx);
void ohos_rdpgfx_record_caps_advertise(freerdpOhosRdpgfxBridge* bridge,
                                       const RDPGFX_CAPS_ADVERTISE_PDU* capsAdvertise);
UINT ohos_rdpgfx_caps_advertise(RdpgfxClientContext* context,
                                const RDPGFX_CAPS_ADVERTISE_PDU* capsAdvertise);
UINT ohos_rdpgfx_caps_confirm(RdpgfxClientContext* context,
                              const RDPGFX_CAPS_CONFIRM_PDU* capsConfirm);

gdiGfxSurface* ohos_rdpgfx_get_gdi_surface(RdpgfxClientContext* context, UINT32 surfaceId);
BOOL ohos_rdpgfx_get_gdi_surface_size(RdpgfxClientContext* context, UINT32 surfaceId, UINT32* width,
                                      UINT32* height);
BOOL ohos_rdpgfx_command_within_surface(const gdiGfxSurface* surface,
                                        const RDPGFX_SURFACE_COMMAND* command);
void ohos_rdpgfx_record_surface_command(freerdpOhosRdpgfxBridge* bridge,
                                        const RDPGFX_SURFACE_COMMAND* command);
UINT ohos_rdpgfx_surface_command(RdpgfxClientContext* context,
                                 const RDPGFX_SURFACE_COMMAND* command);

BOOL ohos_rdpgfx_should_attempt_avc420_surface(
    freerdpOhosRdpgfxBridge* bridge, const RDPGFX_SURFACE_COMMAND* command,
    FREERDP_OHOS_RDPGFX_AVC420_SURFACE_CALLBACK* callback, void** userData, UINT64* skipCount);
void ohos_rdpgfx_mark_avc420_surface_result(freerdpOhosRdpgfxBridge* bridge,
                                            const RDPGFX_SURFACE_COMMAND* command, BOOL activated);

BOOL ohos_rdpgfx_record_avc444_gpu_candidate(freerdpOhosRdpgfxBridge* bridge,
                                             RdpgfxClientContext* context,
                                             const RDPGFX_SURFACE_COMMAND* command,
                                             UINT* consumedStatus);
void ohos_rdpgfx_set_avc444_gpu_output_active(freerdpOhosRdpgfxBridge* bridge, BOOL active,
                                              const char* reason);

UINT ohos_rdpgfx_start_frame(RdpgfxClientContext* context,
                             const RDPGFX_START_FRAME_PDU* startFrame);
UINT ohos_rdpgfx_end_frame(RdpgfxClientContext* context, const RDPGFX_END_FRAME_PDU* endFrame);

#endif /* FREERDP_CLIENT_OHOS_RDPGFX_INTERNAL_H */
