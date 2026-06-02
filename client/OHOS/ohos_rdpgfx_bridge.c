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
	if (bridge)
	{
		EnterCriticalSection(&bridge->lock);
		bridge->startFrames++;
		bridge->activeFrameId = startFrame ? startFrame->frameId : 0;
		bridge->frameOpen = TRUE;
		original = bridge->hooks.startFrame;
		LeaveCriticalSection(&bridge->lock);
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
	FREERDP_OHOS_RDPGFX_AVC444_END_FRAME_CALLBACK avc420EndFrame = NULL;
	FREERDP_OHOS_RDPGFX_AVC444_END_FRAME_CALLBACK avc444EndFrame = NULL;
	void* userData = NULL;
	UINT status = ERROR_INTERNAL_ERROR;
	if (bridge)
	{
		EnterCriticalSection(&bridge->lock);
		bridge->endFrames++;
		activeFrameId = bridge->activeFrameId;
		matchedFrame =
		    bridge->frameOpen && (!endFrame || (endFrame->frameId == bridge->activeFrameId));
		bridge->frameOpen = FALSE;
		avc420GpuCompositor = bridge->avc420GpuCompositor;
		avc444GpuCompositor = bridge->avc444GpuCompositor;
		original = bridge->hooks.endFrame;
		avc420EndFrame = bridge->avc420EndFrame;
		avc444EndFrame = bridge->avc444EndFrame;
		userData = bridge->userData;
		LeaveCriticalSection(&bridge->lock);
		if (avc420GpuCompositor && !matchedFrame)
		{
			ohos_rdpgfx_log(bridge,
			                "rdpgfx end frame observed for AVC420 GPU compositor: frameId=%" PRIu32
			                " active=%" PRIu32 " matched=no",
			                endFrame ? endFrame->frameId : 0, activeFrameId);
		}
		if (avc444GpuCompositor && !matchedFrame)
		{
			ohos_rdpgfx_log(bridge,
			                "rdpgfx end frame observed for AVC444 GPU compositor: frameId=%" PRIu32
			                " active=%" PRIu32 " matched=no",
			                endFrame ? endFrame->frameId : 0, activeFrameId);
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
	bridge->avc420GpuCandidates = 0;
	bridge->avc420GpuDisabled = 0;
	bridge->avc420GpuGdiPreserved = 0;
	bridge->avc420GpuCallbacks = 0;
	bridge->avc420GpuCallbackReady = 0;
	bridge->avc420GpuOutputActive = FALSE;
	bridge->avc420GpuOutputActivations = 0;
	bridge->avc420GpuOutputReleases = 0;
	bridge->avc420GpuActiveSuppressedFailures = 0;
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
		wasAvc420OutputActive = bridge->avc420GpuOutputActive;
		bridge->avc420GpuOutputActive = FALSE;
		if (wasAvc420OutputActive)
			bridge->avc420GpuOutputReleases++;
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
