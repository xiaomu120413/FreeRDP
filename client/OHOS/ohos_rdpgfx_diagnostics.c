/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS rdpgfx logging and diagnostics
 */

#include "ohos_rdpgfx_internal.h"

void ohos_rdpgfx_format_message(char* message, size_t size, const char* format, ...)
{
	if (!message || size == 0)
		return;

	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, size, format, args);
	va_end(args);
}

void ohos_rdpgfx_log(freerdpOhosRdpgfxBridge* bridge, const char* format, ...)
{
	if (!bridge)
		return;

	FREERDP_OHOS_RDPGFX_LOG_CALLBACK log = NULL;
	void* userData = NULL;
	EnterCriticalSection(&bridge->lock);
	log = bridge->log;
	userData = bridge->userData;
	LeaveCriticalSection(&bridge->lock);
	if (!log)
		return;

	char message[512] = { 0 };
	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	log(message, userData);
}

BOOL ohos_rdpgfx_should_log_counter(UINT64 count)
{
	return count == 1U || (count % 300U) == 0U;
}

const char* freerdp_ohos_rdpgfx_bridge_get_diagnostics(freerdpOhosRdpgfxBridge* bridge)
{
	if (!bridge)
		return "OHOS rdpgfx stats: unavailable";

	EnterCriticalSection(&bridge->lock);
	(void)snprintf(
	    bridge->diagnostics, sizeof(bridge->diagnostics),
	    "OHOS rdpgfx stats: runtime=%s h264=%s bridge=%s connected=%" PRIu32
	    " disconnected=%" PRIu32 " initFailed=%" PRIu32 " frames=%" PRIu64 "/%" PRIu64
	    " surfaceCommands=%" PRIu64 " capsAdvertise=%" PRIu32 "/%" PRIu32
	    " advertisedAvc420=%s advertisedV81=0x%08" PRIX32 " advertisedFlags=0x%08" PRIX32
	    " confirmed=%s "
	    "capsVersion=0x%08" PRIX32 " capsFlags=0x%08" PRIX32 " capsConfirms=%" PRIu32
	    " codecs=raw:%" PRIu64 ",progressive:%" PRIu64 ",cavideo:%" PRIu64 ",clear:%" PRIu64
	    ",planar:%" PRIu64 ",avc420:%" PRIu64 ",avc444:%" PRIu64 ",avc444v2:%" PRIu64
	    ",alpha:%" PRIu64 ",unknown:%" PRIu64 " lastCodec=%s(%" PRIu32 ") lastSurface=%" PRIu32
	    " lastSize=%" PRIu32 "x%" PRIu32 " frame=open:%s,id:%" PRIu32
	    " avc420Surface=%s target=%" PRIu32 "x%" PRIu32 " skips=%" PRIu64 " noDirect=%" PRIu64
	    " avc444Gpu=compositor:%s,suppressGdi:per-command"
	    ",candidates:%" PRIu64 ",disabled:%" PRIu64 ",frameMismatch:%" PRIu64
	    ",gdiPreserved:%" PRIu64 ",callbacks:%" PRIu64 ",callbackReady:%" PRIu64
	    " avc444Last=frame:%" PRIu32 ",LC:%" PRIu32 ",stream1Rects:%" PRIu32
	    ",stream2Rects:%" PRIu32 ",stream1Bytes:%" PRIu32 ",stream2Bytes:%" PRIu32,
	    bridge->requested ? "requested" : "off", bridge->h264Requested ? "requested" : "off",
	    bridge->gdiAttached ? "attached" : "detached", bridge->connected, bridge->disconnected,
	    bridge->initFailed, bridge->startFrames, bridge->endFrames, bridge->surfaceCommands,
	    bridge->capsAdvertises, bridge->advertisedCapsSets, bridge->advertisedAvc420 ? "yes" : "no",
	    bridge->advertisedVersion, bridge->advertisedFlags,
	    ohos_rdpgfx_confirmed_mode_name(bridge->confirmedMode), bridge->confirmedVersion,
	    bridge->confirmedFlags, bridge->capsConfirms, bridge->codecUncompressed,
	    bridge->codecProgressive, bridge->codecCavideo, bridge->codecClearCodec,
	    bridge->codecPlanar, bridge->codecAvc420, bridge->codecAvc444, bridge->codecAvc444v2,
	    bridge->codecAlpha, bridge->codecUnknown,
	    freerdp_ohos_rdpgfx_codec_name(bridge->lastCodecId), bridge->lastCodecId,
	    bridge->lastSurfaceId, bridge->lastCommandWidth, bridge->lastCommandHeight,
	    bridge->frameOpen ? "yes" : "no", bridge->activeFrameId,
	    bridge->avc420SurfaceActive ? "active" : "inactive", bridge->surfaceTargetWidth,
	    bridge->surfaceTargetHeight, bridge->avc420SurfaceSubrectSkips,
	    bridge->avc420SurfaceNoDirect, bridge->avc444GpuCompositor ? "on" : "off",
	    bridge->avc444GpuCandidates, bridge->avc444GpuDisabled, bridge->avc444GpuFrameMismatchSkips,
	    bridge->avc444GpuGdiPreserved, bridge->avc444GpuCallbacks, bridge->avc444GpuCallbackReady,
	    bridge->lastAvc444FrameId, bridge->lastAvc444LC, bridge->lastAvc444Stream1Rects,
	    bridge->lastAvc444Stream2Rects, bridge->lastAvc444Stream1Bytes,
	    bridge->lastAvc444Stream2Bytes);
	LeaveCriticalSection(&bridge->lock);
	return bridge->diagnostics;
}
