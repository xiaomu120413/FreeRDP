/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS rdpgfx surface command routing
 */

#include "ohos_rdpgfx_internal.h"

void ohos_rdpgfx_record_surface_command(freerdpOhosRdpgfxBridge* bridge,
                                        const RDPGFX_SURFACE_COMMAND* command)
{
	if (!bridge || !command)
		return;

	const UINT64 nowUs = ohos_rdpgfx_now_us();
	char inputStats[1024] = { 0 };
	BOOL logInputStats = FALSE;
	EnterCriticalSection(&bridge->lock);
	bridge->surfaceCommands++;
	bridge->lastCodecId = command->codecId;
	bridge->lastSurfaceId = command->surfaceId;
	bridge->lastCommandWidth = command->width;
	bridge->lastCommandHeight = command->height;
	ohos_rdpgfx_record_gap_us(nowUs, &bridge->lastSurfaceCommandTimeUs,
	                           &bridge->maxSurfaceCommandGapUs);

	switch (command->codecId)
	{
		case RDPGFX_CODECID_UNCOMPRESSED:
			bridge->codecUncompressed++;
			break;
		case RDPGFX_CODECID_CAVIDEO:
			bridge->codecCavideo++;
			break;
		case RDPGFX_CODECID_CLEARCODEC:
			bridge->codecClearCodec++;
			break;
		case RDPGFX_CODECID_PLANAR:
			bridge->codecPlanar++;
			break;
		case RDPGFX_CODECID_CAPROGRESSIVE:
		case RDPGFX_CODECID_CAPROGRESSIVE_V2:
			bridge->codecProgressive++;
			break;
		case RDPGFX_CODECID_AVC420:
			bridge->codecAvc420++;
			ohos_rdpgfx_record_gap_us(nowUs, &bridge->lastAvc420CommandTimeUs,
			                           &bridge->maxAvc420CommandGapUs);
			break;
		case RDPGFX_CODECID_ALPHA:
			bridge->codecAlpha++;
			break;
		case RDPGFX_CODECID_AVC444:
			bridge->codecAvc444++;
			break;
		case RDPGFX_CODECID_AVC444v2:
			bridge->codecAvc444v2++;
			break;
		default:
			bridge->codecUnknown++;
			break;
	}
	logInputStats = ohos_rdpgfx_build_input_stats_locked(
	    bridge, "surfaceCommand", nowUs, inputStats, sizeof(inputStats));
	LeaveCriticalSection(&bridge->lock);
	if (logInputStats)
		ohos_rdpgfx_log(bridge, "%s", inputStats);
}

gdiGfxSurface* ohos_rdpgfx_get_gdi_surface(RdpgfxClientContext* context, UINT32 surfaceId)
{
	if (!context || !context->GetSurfaceData)
		return NULL;

	return (gdiGfxSurface*)context->GetSurfaceData(context, (UINT16)MIN(UINT16_MAX, surfaceId));
}

BOOL ohos_rdpgfx_get_gdi_surface_size(RdpgfxClientContext* context, UINT32 surfaceId, UINT32* width,
                                      UINT32* height)
{
	if (!width || !height)
		return FALSE;

	gdiGfxSurface* surface = ohos_rdpgfx_get_gdi_surface(context, surfaceId);
	if (!surface || (surface->width == 0) || (surface->height == 0))
		return FALSE;

	*width = surface->width;
	*height = surface->height;
	return TRUE;
}

BOOL ohos_rdpgfx_command_within_surface(const gdiGfxSurface* surface,
                                        const RDPGFX_SURFACE_COMMAND* command)
{
	if (!surface || !command)
		return FALSE;

	const UINT32 left = MIN(UINT16_MAX, command->left);
	const UINT32 top = MIN(UINT16_MAX, command->top);
	const UINT32 right = MIN(UINT16_MAX, command->right);
	const UINT32 bottom = MIN(UINT16_MAX, command->bottom);
	if ((left > right) || (right > surface->width))
		return FALSE;
	if ((top > bottom) || (bottom > surface->height))
		return FALSE;
	if (left > surface->width || top > surface->height)
		return FALSE;

	return TRUE;
}

UINT ohos_rdpgfx_surface_command(RdpgfxClientContext* context,
                                 const RDPGFX_SURFACE_COMMAND* command)
{
	freerdpOhosRdpgfxBridge* bridge = ohos_rdpgfx_bridge_from_context(context);
	pcRdpgfxSurfaceCommand original = NULL;
	if (bridge && command)
	{
		ohos_rdpgfx_record_surface_command(bridge, command);
		UINT avc420Status = CHANNEL_RC_OK;
		const BOOL avc420Consumed =
		    ohos_rdpgfx_record_avc420_gpu_candidate(bridge, context, command, &avc420Status);
		if (avc420Consumed)
			return avc420Status;
		UINT avc444Status = CHANNEL_RC_OK;
		const BOOL avc444Consumed =
		    ohos_rdpgfx_record_avc444_gpu_candidate(bridge, context, command, &avc444Status);
		if (avc444Consumed)
			return avc444Status;
	}

	if (bridge)
	{
		EnterCriticalSection(&bridge->lock);
		original = bridge->hooks.surfaceCommand;
		LeaveCriticalSection(&bridge->lock);
	}
	return original ? original(context, command) : ERROR_INTERNAL_ERROR;
}
