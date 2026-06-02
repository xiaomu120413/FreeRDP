/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS rdpgfx bridge lifecycle and callback hooks
 */

#include "ohos_rdpgfx_internal.h"

static INIT_ONCE g_ohos_rdpgfx_registry_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_ohos_rdpgfx_registry_lock;
static freerdpOhosRdpgfxBridge* g_ohos_rdpgfx_registry = NULL;

static BOOL CALLBACK ohos_rdpgfx_registry_init(PINIT_ONCE once, PVOID parameter, PVOID* context)
{
	WINPR_UNUSED(once);
	WINPR_UNUSED(parameter);
	WINPR_UNUSED(context);
	InitializeCriticalSection(&g_ohos_rdpgfx_registry_lock);
	return TRUE;
}

static BOOL ohos_rdpgfx_registry_ready(void)
{
	return InitOnceExecuteOnce(&g_ohos_rdpgfx_registry_once, ohos_rdpgfx_registry_init, NULL, NULL);
}

UINT64 ohos_rdpgfx_now_us(void)
{
	return winpr_GetTickCount64NS() / 1000ULL;
}

void ohos_rdpgfx_record_gap_us(UINT64 nowUs, UINT64* lastUs, UINT64* maxGapUs)
{
	if (!lastUs || !maxGapUs || (nowUs == 0))
		return;

	if ((*lastUs != 0) && (nowUs >= *lastUs))
	{
		const UINT64 gapUs = nowUs - *lastUs;
		if (gapUs > *maxGapUs)
			*maxGapUs = gapUs;
	}
	*lastUs = nowUs;
}

static void ohos_rdpgfx_rate_parts(UINT64 delta, UINT64 elapsedUs, UINT64* whole, UINT64* tenth)
{
	const UINT64 rate10 = (elapsedUs > 0) ? ((delta * 10000000ULL) / elapsedUs) : 0;
	if (whole)
		*whole = rate10 / 10ULL;
	if (tenth)
		*tenth = rate10 % 10ULL;
}

static void ohos_rdpgfx_ms_parts(UINT64 us, UINT64* whole, UINT64* tenth)
{
	const UINT64 ms10 = us / 100ULL;
	if (whole)
		*whole = ms10 / 10ULL;
	if (tenth)
		*tenth = ms10 % 10ULL;
}

static void ohos_rdpgfx_reset_input_stats_window_locked(freerdpOhosRdpgfxBridge* bridge,
                                                       UINT64 nowUs)
{
	bridge->lastInputStatsTimeUs = nowUs;
	bridge->lastInputStatsStartFrames = bridge->startFrames;
	bridge->lastInputStatsEndFrames = bridge->endFrames;
	bridge->lastInputStatsSurfaceCommands = bridge->surfaceCommands;
	bridge->lastInputStatsCodecUncompressed = bridge->codecUncompressed;
	bridge->lastInputStatsCodecCavideo = bridge->codecCavideo;
	bridge->lastInputStatsCodecClearCodec = bridge->codecClearCodec;
	bridge->lastInputStatsCodecPlanar = bridge->codecPlanar;
	bridge->lastInputStatsCodecProgressive = bridge->codecProgressive;
	bridge->lastInputStatsCodecAvc420 = bridge->codecAvc420;
	bridge->lastInputStatsCodecAlpha = bridge->codecAlpha;
	bridge->lastInputStatsCodecAvc444 = bridge->codecAvc444;
	bridge->lastInputStatsCodecAvc444v2 = bridge->codecAvc444v2;
	bridge->lastInputStatsCodecUnknown = bridge->codecUnknown;
	bridge->maxStartFrameGapUs = 0;
	bridge->maxEndFrameGapUs = 0;
	bridge->maxSurfaceCommandGapUs = 0;
	bridge->maxAvc420CommandGapUs = 0;
}

BOOL ohos_rdpgfx_build_input_stats_locked(freerdpOhosRdpgfxBridge* bridge,
                                          const char* reason, UINT64 nowUs, char* buffer,
                                          size_t size)
{
	const UINT64 statsIntervalUs = 2000000ULL;
	if (!bridge || !buffer || (size == 0))
		return FALSE;

	buffer[0] = '\0';
	if (nowUs == 0)
		nowUs = ohos_rdpgfx_now_us();

	if ((bridge->lastInputStatsTimeUs == 0) || (nowUs < bridge->lastInputStatsTimeUs))
	{
		ohos_rdpgfx_reset_input_stats_window_locked(bridge, nowUs);
		return FALSE;
	}

	const UINT64 elapsedUs = nowUs - bridge->lastInputStatsTimeUs;
	if (elapsedUs < statsIntervalUs)
		return FALSE;

	const UINT64 startDelta = bridge->startFrames - bridge->lastInputStatsStartFrames;
	const UINT64 endDelta = bridge->endFrames - bridge->lastInputStatsEndFrames;
	const UINT64 surfaceDelta =
	    bridge->surfaceCommands - bridge->lastInputStatsSurfaceCommands;
	const UINT64 rawDelta =
	    bridge->codecUncompressed - bridge->lastInputStatsCodecUncompressed;
	const UINT64 cavideoDelta = bridge->codecCavideo - bridge->lastInputStatsCodecCavideo;
	const UINT64 clearDelta = bridge->codecClearCodec - bridge->lastInputStatsCodecClearCodec;
	const UINT64 planarDelta = bridge->codecPlanar - bridge->lastInputStatsCodecPlanar;
	const UINT64 progressiveDelta =
	    bridge->codecProgressive - bridge->lastInputStatsCodecProgressive;
	const UINT64 avc420Delta = bridge->codecAvc420 - bridge->lastInputStatsCodecAvc420;
	const UINT64 alphaDelta = bridge->codecAlpha - bridge->lastInputStatsCodecAlpha;
	const UINT64 avc444Delta = bridge->codecAvc444 - bridge->lastInputStatsCodecAvc444;
	const UINT64 avc444v2Delta =
	    bridge->codecAvc444v2 - bridge->lastInputStatsCodecAvc444v2;
	const UINT64 unknownDelta = bridge->codecUnknown - bridge->lastInputStatsCodecUnknown;
	const UINT64 maxStartGapUs = bridge->maxStartFrameGapUs;
	const UINT64 maxEndGapUs = bridge->maxEndFrameGapUs;
	const UINT64 maxSurfaceGapUs = bridge->maxSurfaceCommandGapUs;
	const UINT64 maxAvc420GapUs = bridge->maxAvc420CommandGapUs;
	UINT64 frameFpsWhole = 0;
	UINT64 frameFpsTenth = 0;
	UINT64 endFrameFpsWhole = 0;
	UINT64 endFrameFpsTenth = 0;
	UINT64 surfaceFpsWhole = 0;
	UINT64 surfaceFpsTenth = 0;
	UINT64 avc420FpsWhole = 0;
	UINT64 avc420FpsTenth = 0;
	UINT64 maxStartGapMsWhole = 0;
	UINT64 maxStartGapMsTenth = 0;
	UINT64 maxEndGapMsWhole = 0;
	UINT64 maxEndGapMsTenth = 0;
	UINT64 maxSurfaceGapMsWhole = 0;
	UINT64 maxSurfaceGapMsTenth = 0;
	UINT64 maxAvc420GapMsWhole = 0;
	UINT64 maxAvc420GapMsTenth = 0;

	ohos_rdpgfx_rate_parts(startDelta, elapsedUs, &frameFpsWhole, &frameFpsTenth);
	ohos_rdpgfx_rate_parts(endDelta, elapsedUs, &endFrameFpsWhole, &endFrameFpsTenth);
	ohos_rdpgfx_rate_parts(surfaceDelta, elapsedUs, &surfaceFpsWhole, &surfaceFpsTenth);
	ohos_rdpgfx_rate_parts(avc420Delta, elapsedUs, &avc420FpsWhole, &avc420FpsTenth);
	ohos_rdpgfx_ms_parts(maxStartGapUs, &maxStartGapMsWhole, &maxStartGapMsTenth);
	ohos_rdpgfx_ms_parts(maxEndGapUs, &maxEndGapMsWhole, &maxEndGapMsTenth);
	ohos_rdpgfx_ms_parts(maxSurfaceGapUs, &maxSurfaceGapMsWhole, &maxSurfaceGapMsTenth);
	ohos_rdpgfx_ms_parts(maxAvc420GapUs, &maxAvc420GapMsWhole, &maxAvc420GapMsTenth);

	(void)snprintf(
	    buffer, size,
	    "RDPGFX input stats: reason=%s windowMs=%" PRIu64 " frameDelta=%" PRIu64 "/%" PRIu64
	    " startFrameDelta=%" PRIu64 " endFrameDelta=%" PRIu64 " frameFps=%" PRIu64
	    ".%" PRIu64 " endFrameFps=%" PRIu64 ".%" PRIu64 " surfaceDelta=%" PRIu64
	    " surfaceFps=%" PRIu64 ".%" PRIu64 " avc420Delta=%" PRIu64 " avc420Fps=%" PRIu64
	    ".%" PRIu64 " clearDelta=%" PRIu64 " progressiveDelta=%" PRIu64
	    " rawDelta=%" PRIu64 " cavideoDelta=%" PRIu64 " planarDelta=%" PRIu64
	    " avc444Delta=%" PRIu64 " avc444v2Delta=%" PRIu64 " alphaDelta=%" PRIu64
	    " unknownDelta=%" PRIu64 " maxRdpgfxFrameGapMs=%" PRIu64 ".%" PRIu64
	    " maxEndFrameGapMs=%" PRIu64 ".%" PRIu64 " maxCommandGapMs=%" PRIu64 ".%" PRIu64
	    " maxAvc420CommandGapMs=%" PRIu64 ".%" PRIu64 " lastCodec=%s(%" PRIu32
	    ") lastSurface=%" PRIu32 " lastSize=%" PRIu32 "x%" PRIu32 " frame=open:%s,id:%" PRIu32,
	    reason ? reason : "unknown", elapsedUs / 1000ULL, startDelta, endDelta, startDelta,
	    endDelta, frameFpsWhole, frameFpsTenth, endFrameFpsWhole, endFrameFpsTenth,
	    surfaceDelta, surfaceFpsWhole, surfaceFpsTenth, avc420Delta, avc420FpsWhole,
	    avc420FpsTenth, clearDelta, progressiveDelta, rawDelta, cavideoDelta, planarDelta,
	    avc444Delta, avc444v2Delta, alphaDelta, unknownDelta, maxStartGapMsWhole,
	    maxStartGapMsTenth, maxEndGapMsWhole, maxEndGapMsTenth, maxSurfaceGapMsWhole,
	    maxSurfaceGapMsTenth, maxAvc420GapMsWhole, maxAvc420GapMsTenth,
	    freerdp_ohos_rdpgfx_codec_name(bridge->lastCodecId), bridge->lastCodecId,
	    bridge->lastSurfaceId, bridge->lastCommandWidth, bridge->lastCommandHeight,
	    bridge->frameOpen ? "yes" : "no", bridge->activeFrameId);

	ohos_rdpgfx_reset_input_stats_window_locked(bridge, nowUs);
	return TRUE;
}

void ohos_rdpgfx_registry_add(freerdpOhosRdpgfxBridge* bridge)
{
	if (!bridge || !ohos_rdpgfx_registry_ready())
		return;

	EnterCriticalSection(&g_ohos_rdpgfx_registry_lock);
	for (freerdpOhosRdpgfxBridge* current = g_ohos_rdpgfx_registry; current;
	     current = current->next)
	{
		if (current == bridge)
		{
			LeaveCriticalSection(&g_ohos_rdpgfx_registry_lock);
			return;
		}
	}
	bridge->next = g_ohos_rdpgfx_registry;
	g_ohos_rdpgfx_registry = bridge;
	LeaveCriticalSection(&g_ohos_rdpgfx_registry_lock);
}

void ohos_rdpgfx_registry_remove(freerdpOhosRdpgfxBridge* bridge)
{
	if (!bridge || !ohos_rdpgfx_registry_ready())
		return;

	EnterCriticalSection(&g_ohos_rdpgfx_registry_lock);
	freerdpOhosRdpgfxBridge** current = &g_ohos_rdpgfx_registry;
	while (*current)
	{
		if (*current == bridge)
		{
			*current = bridge->next;
			bridge->next = NULL;
			break;
		}
		current = &((*current)->next);
	}
	LeaveCriticalSection(&g_ohos_rdpgfx_registry_lock);
}

freerdpOhosRdpgfxBridge* ohos_rdpgfx_bridge_from_context(RdpgfxClientContext* context)
{
	if (!context || !ohos_rdpgfx_registry_ready())
		return NULL;

	freerdpOhosRdpgfxBridge* bridge = NULL;
	EnterCriticalSection(&g_ohos_rdpgfx_registry_lock);
	for (freerdpOhosRdpgfxBridge* current = g_ohos_rdpgfx_registry; current;
	     current = current->next)
	{
		if (current->gfx == context)
		{
			bridge = current;
			break;
		}
	}
	LeaveCriticalSection(&g_ohos_rdpgfx_registry_lock);
	return bridge;
}

UINT ohos_rdpgfx_start_frame(RdpgfxClientContext* context, const RDPGFX_START_FRAME_PDU* startFrame)
{
	freerdpOhosRdpgfxBridge* bridge = ohos_rdpgfx_bridge_from_context(context);
	pcRdpgfxStartFrame original = NULL;
	char inputStats[1024] = { 0 };
	BOOL logInputStats = FALSE;
	if (bridge)
	{
		const UINT64 nowUs = ohos_rdpgfx_now_us();
		EnterCriticalSection(&bridge->lock);
		bridge->startFrames++;
		bridge->activeFrameId = startFrame ? startFrame->frameId : 0;
		bridge->frameOpen = TRUE;
		ohos_rdpgfx_record_gap_us(nowUs, &bridge->lastStartFrameTimeUs,
		                           &bridge->maxStartFrameGapUs);
		logInputStats = ohos_rdpgfx_build_input_stats_locked(
		    bridge, "startFrame", nowUs, inputStats, sizeof(inputStats));
		original = bridge->hooks.startFrame;
		LeaveCriticalSection(&bridge->lock);
		if (logInputStats)
			ohos_rdpgfx_log(bridge, "%s", inputStats);
	}
	return original ? original(context, startFrame) : ERROR_INTERNAL_ERROR;
}

UINT ohos_rdpgfx_end_frame(RdpgfxClientContext* context, const RDPGFX_END_FRAME_PDU* endFrame)
{
	freerdpOhosRdpgfxBridge* bridge = ohos_rdpgfx_bridge_from_context(context);
	pcRdpgfxEndFrame original = NULL;
	BOOL avc420GpuCompositor = FALSE;
	BOOL avc444GpuCompositor = FALSE;
	UINT32 activeFrameId = 0;
	BOOL matchedFrame = FALSE;
	UINT64 avc420FrameMismatches = 0;
	UINT64 avc444FrameMismatches = 0;
	char inputStats[1024] = { 0 };
	BOOL logInputStats = FALSE;
	FREERDP_OHOS_RDPGFX_AVC444_END_FRAME_CALLBACK avc420EndFrame = NULL;
	FREERDP_OHOS_RDPGFX_AVC444_END_FRAME_CALLBACK avc444EndFrame = NULL;
	void* userData = NULL;
	UINT status = ERROR_INTERNAL_ERROR;
	if (bridge)
	{
		const UINT64 nowUs = ohos_rdpgfx_now_us();
		EnterCriticalSection(&bridge->lock);
		bridge->endFrames++;
		activeFrameId = bridge->activeFrameId;
		matchedFrame =
		    bridge->frameOpen && (!endFrame || (endFrame->frameId == bridge->activeFrameId));
		bridge->frameOpen = FALSE;
		ohos_rdpgfx_record_gap_us(nowUs, &bridge->lastEndFrameTimeUs,
		                           &bridge->maxEndFrameGapUs);
		logInputStats = ohos_rdpgfx_build_input_stats_locked(
		    bridge, "endFrame", nowUs, inputStats, sizeof(inputStats));
		avc420GpuCompositor = bridge->avc420GpuCompositor;
		avc444GpuCompositor = bridge->avc444GpuCompositor;
		if (!matchedFrame)
		{
			if (avc420GpuCompositor)
				avc420FrameMismatches = ++bridge->avc420GpuFrameMismatchSkips;
			if (avc444GpuCompositor)
				avc444FrameMismatches = ++bridge->avc444GpuFrameMismatchSkips;
		}
		original = bridge->hooks.endFrame;
		avc420EndFrame = bridge->avc420EndFrame;
		avc444EndFrame = bridge->avc444EndFrame;
		userData = bridge->userData;
		LeaveCriticalSection(&bridge->lock);
		if (logInputStats)
			ohos_rdpgfx_log(bridge, "%s", inputStats);
		if (avc420GpuCompositor && !matchedFrame &&
		    ohos_rdpgfx_should_log_counter(avc420FrameMismatches))
		{
			ohos_rdpgfx_log(bridge,
			                "rdpgfx end frame observed for AVC420 GPU compositor: frameId=%" PRIu32
			                " active=%" PRIu32 " matched=no mismatches=%" PRIu64,
			                endFrame ? endFrame->frameId : 0, activeFrameId,
			                avc420FrameMismatches);
		}
		if (avc444GpuCompositor && !matchedFrame &&
		    ohos_rdpgfx_should_log_counter(avc444FrameMismatches))
		{
			ohos_rdpgfx_log(bridge,
			                "rdpgfx end frame observed for AVC444 GPU compositor: frameId=%" PRIu32
			                " active=%" PRIu32 " matched=no mismatches=%" PRIu64,
			                endFrame ? endFrame->frameId : 0, activeFrameId,
			                avc444FrameMismatches);
		}
	}
	status = original ? original(context, endFrame) : ERROR_INTERNAL_ERROR;
	if (bridge && avc420GpuCompositor && avc420EndFrame)
	{
		FREERDP_OHOS_RDPGFX_FRAME_INFO info = { 0 };
		info.frameId = endFrame ? endFrame->frameId : 0;
		info.activeFrameId = activeFrameId;
		info.matchedFrame = matchedFrame;
		(void)avc420EndFrame(&info, userData);
	}
	if (bridge && avc444GpuCompositor && avc444EndFrame)
	{
		FREERDP_OHOS_RDPGFX_FRAME_INFO info = { 0 };
		info.frameId = endFrame ? endFrame->frameId : 0;
		info.activeFrameId = activeFrameId;
		info.matchedFrame = matchedFrame;
		(void)avc444EndFrame(&info, userData);
	}
	return status;
}

freerdpOhosRdpgfxBridge* freerdp_ohos_rdpgfx_bridge_new(void)
{
	freerdpOhosRdpgfxBridge* bridge =
	    (freerdpOhosRdpgfxBridge*)calloc(1, sizeof(freerdpOhosRdpgfxBridge));
	if (!bridge)
		return NULL;

	InitializeCriticalSection(&bridge->lock);
	bridge->initialized = TRUE;
	bridge->confirmedMode = OHOS_RDPGFX_CONFIRMED_NONE;
	return bridge;
}

void freerdp_ohos_rdpgfx_bridge_free(freerdpOhosRdpgfxBridge* bridge)
{
	if (!bridge)
		return;

	freerdp_ohos_rdpgfx_bridge_detach(bridge, NULL);
	if (bridge->initialized)
		DeleteCriticalSection(&bridge->lock);
	free(bridge);
}

void freerdp_ohos_rdpgfx_bridge_reset(freerdpOhosRdpgfxBridge* bridge, BOOL requested,
                                      BOOL h264Requested)
{
	if (!bridge)
		return;

	EnterCriticalSection(&bridge->lock);
	bridge->requested = requested;
	bridge->h264Requested = h264Requested;
	bridge->gdiAttached = FALSE;
	bridge->avc420GpuCompositor = FALSE;
	bridge->avc420SurfaceCommand = NULL;
	bridge->avc420EndFrame = NULL;
	bridge->avc420OutputState = NULL;
	bridge->avc444GpuCompositor = FALSE;
	bridge->avc444EndFrame = NULL;
	bridge->avc444OutputState = NULL;
	bridge->connected = 0;
	bridge->disconnected = 0;
	bridge->initFailed = 0;
	bridge->activeFrameId = 0;
	bridge->frameOpen = FALSE;
	bridge->startFrames = 0;
	bridge->endFrames = 0;
	bridge->surfaceCommands = 0;
	bridge->codecUncompressed = 0;
	bridge->codecCavideo = 0;
	bridge->codecClearCodec = 0;
	bridge->codecPlanar = 0;
	bridge->codecProgressive = 0;
	bridge->codecAvc420 = 0;
	bridge->codecAlpha = 0;
	bridge->codecAvc444 = 0;
	bridge->codecAvc444v2 = 0;
	bridge->codecUnknown = 0;
	bridge->lastInputStatsTimeUs = 0;
	bridge->lastInputStatsStartFrames = 0;
	bridge->lastInputStatsEndFrames = 0;
	bridge->lastInputStatsSurfaceCommands = 0;
	bridge->lastInputStatsCodecUncompressed = 0;
	bridge->lastInputStatsCodecCavideo = 0;
	bridge->lastInputStatsCodecClearCodec = 0;
	bridge->lastInputStatsCodecPlanar = 0;
	bridge->lastInputStatsCodecProgressive = 0;
	bridge->lastInputStatsCodecAvc420 = 0;
	bridge->lastInputStatsCodecAlpha = 0;
	bridge->lastInputStatsCodecAvc444 = 0;
	bridge->lastInputStatsCodecAvc444v2 = 0;
	bridge->lastInputStatsCodecUnknown = 0;
	bridge->lastStartFrameTimeUs = 0;
	bridge->lastEndFrameTimeUs = 0;
	bridge->lastSurfaceCommandTimeUs = 0;
	bridge->lastAvc420CommandTimeUs = 0;
	bridge->maxStartFrameGapUs = 0;
	bridge->maxEndFrameGapUs = 0;
	bridge->maxSurfaceCommandGapUs = 0;
	bridge->maxAvc420CommandGapUs = 0;
	bridge->avc420GpuCandidates = 0;
	bridge->avc420GpuDisabled = 0;
	bridge->avc420GpuGdiPreserved = 0;
	bridge->avc420GpuCallbacks = 0;
	bridge->avc420GpuCallbackReady = 0;
	bridge->avc420GpuOutputActive = FALSE;
	bridge->avc420GpuOutputActivations = 0;
	bridge->avc420GpuOutputReleases = 0;
	bridge->avc420GpuActiveSuppressedFailures = 0;
	bridge->avc420GpuActiveSuppressedSinceLog = 0;
	bridge->avc420GpuLastSuppressedLogUs = 0;
	bridge->avc420GpuFrameMismatchSkips = 0;
	bridge->avc444GpuCandidates = 0;
	bridge->avc444GpuDisabled = 0;
	bridge->avc444GpuGdiPreserved = 0;
	bridge->avc444GpuFrameMismatchSkips = 0;
	bridge->avc444GpuCallbacks = 0;
	bridge->avc444GpuCallbackReady = 0;
	bridge->avc444GpuOutputActive = FALSE;
	bridge->avc444GpuOutputActivations = 0;
	bridge->avc444GpuOutputReleases = 0;
	bridge->avc444GpuActiveSuppressedFailures = 0;
	bridge->capsAdvertises = 0;
	bridge->advertisedCapsSets = 0;
	bridge->advertisedAvc420 = FALSE;
	bridge->advertisedVersion = 0;
	bridge->advertisedFlags = 0;
	bridge->capsConfirms = 0;
	bridge->confirmedMode = OHOS_RDPGFX_CONFIRMED_NONE;
	bridge->confirmedVersion = 0;
	bridge->confirmedFlags = 0;
	bridge->lastCodecId = 0;
	bridge->lastSurfaceId = 0;
	bridge->lastCommandWidth = 0;
	bridge->lastCommandHeight = 0;
	bridge->lastAvc420FrameId = 0;
	bridge->lastAvc420Rects = 0;
	bridge->lastAvc420Bytes = 0;
	bridge->lastAvc420FullSurface = FALSE;
	bridge->lastAvc444FrameId = 0;
	bridge->lastAvc444LC = 0;
	bridge->lastAvc444Stream1Rects = 0;
	bridge->lastAvc444Stream2Rects = 0;
	bridge->lastAvc444Stream1Bytes = 0;
	bridge->lastAvc444Stream2Bytes = 0;
	bridge->diagnostics[0] = '\0';
	LeaveCriticalSection(&bridge->lock);
}

void freerdp_ohos_rdpgfx_bridge_set_surface_target(freerdpOhosRdpgfxBridge* bridge, UINT32 width,
                                                   UINT32 height)
{
	if (!bridge)
		return;

	EnterCriticalSection(&bridge->lock);
	bridge->surfaceTargetWidth = width;
	bridge->surfaceTargetHeight = height;
	LeaveCriticalSection(&bridge->lock);
}

void freerdp_ohos_rdpgfx_bridge_set_avc420_gpu_output_active(
    freerdpOhosRdpgfxBridge* bridge, BOOL active, const char* reason)
{
	BOOL changed = FALSE;
	UINT64 activations = 0;
	UINT64 releases = 0;

	if (!bridge)
		return;

	EnterCriticalSection(&bridge->lock);
	if (bridge->avc420GpuOutputActive != active)
	{
		bridge->avc420GpuOutputActive = active;
		if (active)
			activations = ++bridge->avc420GpuOutputActivations;
		else
			releases = ++bridge->avc420GpuOutputReleases;
		changed = TRUE;
	}
	else
	{
		activations = bridge->avc420GpuOutputActivations;
		releases = bridge->avc420GpuOutputReleases;
	}
	LeaveCriticalSection(&bridge->lock);

	if (changed)
	{
		ohos_rdpgfx_log(bridge,
		                "AVC420 GPU output owner synced by app policy: active=%s reason=%s "
		                "activations=%" PRIu64 " releases=%" PRIu64,
		                active ? "yes" : "no",
		                reason ? reason : "AVC420 GPU app output policy sync", activations,
		                releases);
	}
}

BOOL freerdp_ohos_rdpgfx_bridge_attach(freerdpOhosRdpgfxBridge* bridge, RdpgfxClientContext* gfx,
                                       const FREERDP_OHOS_RDPGFX_BRIDGE_CONFIG* config,
                                       char* message, size_t messageSize)
{
	if (!bridge || !gfx || !config)
	{
		ohos_rdpgfx_format_message(message, messageSize, "invalid OHOS rdpgfx bridge arguments");
		return FALSE;
	}

	if (bridge->gfx && bridge->gfx != gfx)
		freerdp_ohos_rdpgfx_bridge_detach(bridge, bridge->gfx);

	EnterCriticalSection(&bridge->lock);
	if (bridge->gfx == gfx)
	{
		bridge->avc420GpuCompositor = config->avc420GpuCompositor;
		bridge->avc444GpuCompositor = config->avc444GpuCompositor;
		bridge->surfaceTargetWidth = config->surfaceTargetWidth;
		bridge->surfaceTargetHeight = config->surfaceTargetHeight;
		bridge->log = config->log;
		bridge->avc420SurfaceCommand = config->avc420SurfaceCommand;
		bridge->avc420EndFrame = config->avc420EndFrame;
		bridge->avc420OutputState = config->avc420OutputState;
		bridge->avc444SurfaceCommand = config->avc444SurfaceCommand;
		bridge->avc444EndFrame = config->avc444EndFrame;
		bridge->avc444OutputState = config->avc444OutputState;
		bridge->userData = config->userData;
		LeaveCriticalSection(&bridge->lock);
		ohos_rdpgfx_format_message(message, messageSize,
		                           "OHOS rdpgfx bridge already attached: avc420GpuCompositor=%s "
		                           "avc444GpuCompositor=%s",
		                           config->avc420GpuCompositor ? "on" : "off",
		                           config->avc444GpuCompositor ? "on" : "off");
		return TRUE;
	}

	bridge->gfx = gfx;
	bridge->hooks.startFrame = gfx->StartFrame;
	bridge->hooks.endFrame = gfx->EndFrame;
	bridge->hooks.surfaceCommand = gfx->SurfaceCommand;
	bridge->hooks.capsAdvertise = gfx->CapsAdvertise;
	bridge->hooks.capsConfirm = gfx->CapsConfirm;
	bridge->avc420GpuCompositor = config->avc420GpuCompositor;
	bridge->avc444GpuCompositor = config->avc444GpuCompositor;
	bridge->surfaceTargetWidth = config->surfaceTargetWidth;
	bridge->surfaceTargetHeight = config->surfaceTargetHeight;
	bridge->log = config->log;
	bridge->avc420SurfaceCommand = config->avc420SurfaceCommand;
	bridge->avc420EndFrame = config->avc420EndFrame;
	bridge->avc420OutputState = config->avc420OutputState;
	bridge->avc444SurfaceCommand = config->avc444SurfaceCommand;
	bridge->avc444EndFrame = config->avc444EndFrame;
	bridge->avc444OutputState = config->avc444OutputState;
	bridge->userData = config->userData;
	bridge->connected++;
	LeaveCriticalSection(&bridge->lock);

	ohos_rdpgfx_registry_add(bridge);
	if (bridge->hooks.startFrame)
		gfx->StartFrame = ohos_rdpgfx_start_frame;
	if (bridge->hooks.endFrame)
		gfx->EndFrame = ohos_rdpgfx_end_frame;
	if (bridge->hooks.surfaceCommand)
		gfx->SurfaceCommand = ohos_rdpgfx_surface_command;
	gfx->CapsAdvertise = ohos_rdpgfx_caps_advertise;
	gfx->CapsConfirm = ohos_rdpgfx_caps_confirm;

	ohos_rdpgfx_record_connection_caps_snapshot(bridge, gfx);
	ohos_rdpgfx_format_message(message, messageSize,
	                           "OHOS rdpgfx bridge attached: avc420GpuCompositor=%s "
	                           "avc444GpuCompositor=%s",
	                           config->avc420GpuCompositor ? "on" : "off",
	                           config->avc444GpuCompositor ? "on" : "off");
	return TRUE;
}

void freerdp_ohos_rdpgfx_bridge_detach(freerdpOhosRdpgfxBridge* bridge, RdpgfxClientContext* gfx)
{
	if (!bridge)
		return;

	RdpgfxClientContext* active = NULL;
	FREERDP_OHOS_RDPGFX_HOOKS hooks = { 0 };
	FREERDP_OHOS_RDPGFX_AVC444_OUTPUT_STATE_CALLBACK avc420OutputState = NULL;
	FREERDP_OHOS_RDPGFX_AVC444_OUTPUT_STATE_CALLBACK avc444OutputState = NULL;
	void* userData = NULL;
	BOOL wasAvc420OutputActive = FALSE;
	BOOL wasAvc444OutputActive = FALSE;
	EnterCriticalSection(&bridge->lock);
	if (bridge->gfx && (!gfx || bridge->gfx == gfx))
	{
		active = bridge->gfx;
		hooks = bridge->hooks;
		bridge->gfx = NULL;
		bridge->hooks.startFrame = NULL;
		bridge->hooks.endFrame = NULL;
		bridge->hooks.surfaceCommand = NULL;
		bridge->hooks.capsAdvertise = NULL;
		bridge->hooks.capsConfirm = NULL;
		bridge->gdiAttached = FALSE;
		bridge->frameOpen = FALSE;
		bridge->activeFrameId = 0;
		bridge->avc420SurfaceCommand = NULL;
		bridge->avc420EndFrame = NULL;
		avc420OutputState = bridge->avc420OutputState;
		wasAvc420OutputActive = bridge->avc420GpuOutputActive;
		bridge->avc420GpuOutputActive = FALSE;
		if (wasAvc420OutputActive)
			bridge->avc420GpuOutputReleases++;
		bridge->avc420OutputState = NULL;
		bridge->avc444EndFrame = NULL;
		avc444OutputState = bridge->avc444OutputState;
		userData = bridge->userData;
		wasAvc444OutputActive = bridge->avc444GpuOutputActive;
		bridge->avc444GpuOutputActive = FALSE;
		if (wasAvc444OutputActive)
			bridge->avc444GpuOutputReleases++;
		bridge->avc444OutputState = NULL;
		bridge->disconnected++;
	}
	LeaveCriticalSection(&bridge->lock);

	if (wasAvc420OutputActive && avc420OutputState)
		avc420OutputState(FALSE, "rdpgfx bridge detached", userData);
	if (wasAvc444OutputActive && avc444OutputState)
		avc444OutputState(FALSE, "rdpgfx bridge detached", userData);

	if (!active)
		return;

	ohos_rdpgfx_registry_remove(bridge);
	active->StartFrame = hooks.startFrame;
	active->EndFrame = hooks.endFrame;
	active->SurfaceCommand = hooks.surfaceCommand;
	active->CapsAdvertise = hooks.capsAdvertise;
	active->CapsConfirm = hooks.capsConfirm;
}

void freerdp_ohos_rdpgfx_bridge_set_gdi_attached(freerdpOhosRdpgfxBridge* bridge, BOOL attached)
{
	if (!bridge)
		return;

	EnterCriticalSection(&bridge->lock);
	bridge->gdiAttached = attached;
	LeaveCriticalSection(&bridge->lock);
}
