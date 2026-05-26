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

	UINT64 total = 0;
	UINT64 clear = 0;
	UINT64 progressive = 0;
	UINT64 avc420 = 0;
	UINT64 avc444 = 0;
	UINT64 raw = 0;
	UINT64 unknown = 0;

	EnterCriticalSection(&bridge->lock);
	total = ++bridge->surfaceCommands;
	bridge->lastCodecId = command->codecId;
	bridge->lastSurfaceId = command->surfaceId;
	bridge->lastCommandWidth = command->width;
	bridge->lastCommandHeight = command->height;

	switch (command->codecId)
	{
		case RDPGFX_CODECID_UNCOMPRESSED:
			raw = ++bridge->codecUncompressed;
			break;
		case RDPGFX_CODECID_CAVIDEO:
			bridge->codecCavideo++;
			break;
		case RDPGFX_CODECID_CLEARCODEC:
			clear = ++bridge->codecClearCodec;
			break;
		case RDPGFX_CODECID_PLANAR:
			bridge->codecPlanar++;
			break;
		case RDPGFX_CODECID_CAPROGRESSIVE:
		case RDPGFX_CODECID_CAPROGRESSIVE_V2:
			progressive = ++bridge->codecProgressive;
			break;
		case RDPGFX_CODECID_AVC420:
			avc420 = ++bridge->codecAvc420;
			break;
		case RDPGFX_CODECID_ALPHA:
			bridge->codecAlpha++;
			break;
		case RDPGFX_CODECID_AVC444:
			avc444 = ++bridge->codecAvc444;
			break;
		case RDPGFX_CODECID_AVC444v2:
			bridge->codecAvc444v2++;
			break;
		default:
			unknown = ++bridge->codecUnknown;
			break;
	}
	clear = bridge->codecClearCodec;
	progressive = bridge->codecProgressive;
	avc420 = bridge->codecAvc420;
	avc444 = bridge->codecAvc444;
	raw = bridge->codecUncompressed;
	unknown = bridge->codecUnknown;
	LeaveCriticalSection(&bridge->lock);

	if (total <= 5U || (total % 120U) == 0U)
	{
		ohos_rdpgfx_log(bridge,
		                "rdpgfx surface command: total=%" PRIu64 " codec=%s(%" PRIu32
		                ") surface=%" PRIu16 " rect=%" PRIu32 ",%" PRIu32 " %" PRIu32 "x%" PRIu32
		                " counts=clear:%" PRIu64 ",progressive:%" PRIu64 ",avc420:%" PRIu64
		                ",avc444:%" PRIu64 ",raw:%" PRIu64 ",unknown:%" PRIu64,
		                total, freerdp_ohos_rdpgfx_codec_name(command->codecId), command->codecId,
		                command->surfaceId, command->left, command->top, command->width,
		                command->height, clear, progressive, avc420, avc444, raw, unknown);
	}
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
		UINT avc444Status = CHANNEL_RC_OK;
		const BOOL avc444Consumed =
		    ohos_rdpgfx_record_avc444_gpu_candidate(bridge, context, command, &avc444Status);
		if (avc444Consumed)
			return avc444Status;

		FREERDP_OHOS_RDPGFX_AVC420_SURFACE_CALLBACK callback = NULL;
		void* userData = NULL;
		UINT64 skipCount = 0;
		if (ohos_rdpgfx_should_attempt_avc420_surface(bridge, command, &callback, &userData,
		                                              &skipCount))
		{
			FREERDP_OHOS_RDPGFX_SURFACE_COMMAND_INFO info = { 0 };
			info.codecId = command->codecId;
			info.surfaceId = command->surfaceId;
			info.left = command->left;
			info.top = command->top;
			info.width = command->width;
			info.height = command->height;
			ohos_rdpgfx_mark_avc420_surface_result(bridge, command, callback(&info, userData));
		}
		else if (skipCount > 0 && ohos_rdpgfx_should_log_counter(skipCount))
		{
			ohos_rdpgfx_log(bridge,
			                "AVC420 surface output activation skipped: command is a sub-rectangle "
			                "surface update; command=%" PRIu32 "x%" PRIu32 " at %" PRIu32
			                ",%" PRIu32 " count=%" PRIu64,
			                command->width, command->height, command->left, command->top,
			                skipCount);
		}
	}

	if (bridge)
	{
		EnterCriticalSection(&bridge->lock);
		original = bridge->hooks.surfaceCommand;
		LeaveCriticalSection(&bridge->lock);
	}
	return original ? original(context, command) : ERROR_INTERNAL_ERROR;
}
