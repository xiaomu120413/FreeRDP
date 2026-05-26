/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS rdpgfx AVC420 direct surface route
 */

#include "ohos_rdpgfx_internal.h"

BOOL ohos_rdpgfx_should_attempt_avc420_surface(

    freerdpOhosRdpgfxBridge* bridge, const RDPGFX_SURFACE_COMMAND* command,

    FREERDP_OHOS_RDPGFX_AVC420_SURFACE_CALLBACK* callback, void** userData, UINT64* skipCount)

{

	if (!bridge || !command || !callback || !userData || !skipCount)

		return FALSE;

	*callback = NULL;

	*userData = NULL;

	*skipCount = 0;

	if (!freerdp_ohos_rdpgfx_codec_is_avc420(command->codecId))

		return FALSE;

	EnterCriticalSection(&bridge->lock);

	const BOOL configured = bridge->avc420SurfaceMode;

	const BOOL active = bridge->avc420SurfaceActive;

	const UINT32 targetWidth = bridge->surfaceTargetWidth;

	const UINT32 targetHeight = bridge->surfaceTargetHeight;

	if (!configured || active)

	{

		LeaveCriticalSection(&bridge->lock);

		return FALSE;
	}

	if (!freerdp_ohos_rdpgfx_surface_command_is_full_window(

	        command->left, command->top, command->width, command->height, targetWidth,
	        targetHeight))

	{

		*skipCount = ++bridge->avc420SurfaceSubrectSkips;

		LeaveCriticalSection(&bridge->lock);

		return FALSE;
	}

	*callback = bridge->avc420SurfaceCommand;

	*userData = bridge->userData;

	LeaveCriticalSection(&bridge->lock);

	return *callback != NULL;
}

void ohos_rdpgfx_mark_avc420_surface_result(freerdpOhosRdpgfxBridge* bridge,

                                            const RDPGFX_SURFACE_COMMAND* command,

                                            BOOL activated)

{

	if (!bridge || !command)

		return;

	UINT64 noDirect = 0;

	EnterCriticalSection(&bridge->lock);

	if (activated)

		bridge->avc420SurfaceActive = TRUE;

	else

		noDirect = ++bridge->avc420SurfaceNoDirect;

	LeaveCriticalSection(&bridge->lock);

	if (!activated && ohos_rdpgfx_should_log_counter(noDirect))

	{

		ohos_rdpgfx_log(bridge,

		                "AVC420 surface command reached FreeRDP without direct surface output: "

		                "codec=%s surface=%" PRIu16 " size=%" PRIu32 "x%" PRIu32

		                " count=%" PRIu64,

		                freerdp_ohos_rdpgfx_codec_name(command->codecId), command->surfaceId,

		                command->width, command->height, noDirect);
	}
}
