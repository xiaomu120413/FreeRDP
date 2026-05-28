/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS rdpgfx capability tracking
 */

#include "ohos_rdpgfx_internal.h"

const char* ohos_rdpgfx_confirmed_mode_name(UINT32 mode)
{
	switch (mode)
	{
		case OHOS_RDPGFX_CONFIRMED_AVC420:
			return "avc420";
		case OHOS_RDPGFX_CONFIRMED_AVC444:
			return "avc444";
		case OHOS_RDPGFX_CONFIRMED_NON_AVC:
			return "non-avc";
		case OHOS_RDPGFX_CONFIRMED_NONE:
		default:
			return "none";
	}
}

const char* freerdp_ohos_rdpgfx_codec_name(UINT32 codecId)
{
	switch (codecId)
	{
		case RDPGFX_CODECID_UNCOMPRESSED:
			return "UNCOMPRESSED";
		case RDPGFX_CODECID_CAVIDEO:
			return "CAVIDEO";
		case RDPGFX_CODECID_CLEARCODEC:
			return "CLEARCODEC";
		case RDPGFX_CODECID_PLANAR:
			return "PLANAR";
		case RDPGFX_CODECID_CAPROGRESSIVE:
			return "CAPROGRESSIVE";
		case RDPGFX_CODECID_CAPROGRESSIVE_V2:
			return "CAPROGRESSIVE_V2";
		case RDPGFX_CODECID_AVC420:
			return "AVC420";
		case RDPGFX_CODECID_ALPHA:
			return "ALPHA";
		case RDPGFX_CODECID_AVC444:
			return "AVC444";
		case RDPGFX_CODECID_AVC444v2:
			return "AVC444v2";
		default:
			return "UNKNOWN";
	}
}

void ohos_rdpgfx_record_caps_values(freerdpOhosRdpgfxBridge* bridge, UINT32 version, UINT32 flags,
                                    const char* source)
{
	if (!bridge)
		return;

	const BOOL avc420 = freerdp_ohos_rdpgfx_caps_confirm_is_avc420(version, flags);
	const BOOL avc444 = freerdp_ohos_rdpgfx_caps_confirm_is_avc444(version, flags);
	const UINT32 mode =
	    avc420 ? OHOS_RDPGFX_CONFIRMED_AVC420
	           : (avc444 ? OHOS_RDPGFX_CONFIRMED_AVC444 : OHOS_RDPGFX_CONFIRMED_NON_AVC);

	UINT32 total = 0;
	EnterCriticalSection(&bridge->lock);
	total = ++bridge->capsConfirms;
	bridge->confirmedMode = mode;
	bridge->confirmedVersion = version;
	bridge->confirmedFlags = flags;
	LeaveCriticalSection(&bridge->lock);

	ohos_rdpgfx_log(bridge,
	                "rdpgfx caps confirm: mode=%s version=0x%08" PRIX32 " flags=0x%08" PRIX32
	                " source=%s confirms=%" PRIu32,
	                ohos_rdpgfx_confirmed_mode_name(mode), version, flags,
	                source ? source : "unknown", total);
}

void ohos_rdpgfx_record_connection_caps_snapshot(freerdpOhosRdpgfxBridge* bridge,
                                                 RdpgfxClientContext* gfx)
{
	if (!bridge || !gfx || !gfx->handle)
		return;

	const FREERDP_OHOS_RDPGFX_PLUGIN_SNAPSHOT* plugin =
	    (const FREERDP_OHOS_RDPGFX_PLUGIN_SNAPSHOT*)gfx->handle;
	if (plugin->connectionCaps.version == 0)
		return;

	ohos_rdpgfx_record_caps_values(bridge, plugin->connectionCaps.version,
	                               plugin->connectionCaps.flags, "connection-caps-snapshot");
}

void ohos_rdpgfx_record_caps_advertise(freerdpOhosRdpgfxBridge* bridge,
                                       const RDPGFX_CAPS_ADVERTISE_PDU* capsAdvertise)
{
	if (!bridge)
		return;

	if (!capsAdvertise || !capsAdvertise->capsSets)
	{
		UINT32 total = 0;
		EnterCriticalSection(&bridge->lock);
		total = ++bridge->capsAdvertises;
		bridge->advertisedCapsSets = 0;
		bridge->advertisedAvc420 = FALSE;
		bridge->advertisedVersion = 0;
		bridge->advertisedFlags = 0;
		LeaveCriticalSection(&bridge->lock);
		ohos_rdpgfx_log(bridge, "rdpgfx caps advertise: count=0 advertises=%" PRIu32, total);
		return;
	}

	UINT32 total = 0;
	BOOL advertisedAvc420 = FALSE;
	UINT32 advertisedVersion = 0;
	UINT32 advertisedFlags = 0;
	for (UINT32 index = 0; index < capsAdvertise->capsSetCount; index++)
	{
		const RDPGFX_CAPSET* capsSet = &(capsAdvertise->capsSets[index]);
		if (capsSet->version == RDPGFX_CAPVERSION_81)
		{
			advertisedVersion = capsSet->version;
			advertisedFlags = capsSet->flags;
			if ((capsSet->flags & RDPGFX_CAPS_FLAG_AVC420_ENABLED) != 0)
				advertisedAvc420 = TRUE;
		}
	}

	EnterCriticalSection(&bridge->lock);
	total = ++bridge->capsAdvertises;
	bridge->advertisedCapsSets = capsAdvertise->capsSetCount;
	bridge->advertisedAvc420 = advertisedAvc420;
	bridge->advertisedVersion = advertisedVersion;
	bridge->advertisedFlags = advertisedFlags;
	LeaveCriticalSection(&bridge->lock);

	ohos_rdpgfx_log(bridge,
	                "rdpgfx caps advertise: count=%" PRIu32 " avc420=%s"
	                " v81Version=0x%08" PRIX32 " v81Flags=0x%08" PRIX32 " advertises=%" PRIu32,
	                capsAdvertise->capsSetCount, advertisedAvc420 ? "yes" : "no", advertisedVersion,
	                advertisedFlags, total);
}

UINT ohos_rdpgfx_caps_advertise(RdpgfxClientContext* context,
                                const RDPGFX_CAPS_ADVERTISE_PDU* capsAdvertise)
{
	freerdpOhosRdpgfxBridge* bridge = ohos_rdpgfx_bridge_from_context(context);
	pcRdpgfxCapsAdvertise original = NULL;
	if (bridge)
	{
		ohos_rdpgfx_record_caps_advertise(bridge, capsAdvertise);
		EnterCriticalSection(&bridge->lock);
		original = bridge->hooks.capsAdvertise;
		LeaveCriticalSection(&bridge->lock);
	}
	return original ? original(context, capsAdvertise) : ERROR_BAD_CONFIGURATION;
}

UINT ohos_rdpgfx_caps_confirm(RdpgfxClientContext* context,
                              const RDPGFX_CAPS_CONFIRM_PDU* capsConfirm)
{
	freerdpOhosRdpgfxBridge* bridge = ohos_rdpgfx_bridge_from_context(context);
	pcRdpgfxCapsConfirm original = NULL;
	if (bridge)
	{
		if (capsConfirm && capsConfirm->capsSet)
		{
			const RDPGFX_CAPSET* capsSet = capsConfirm->capsSet;
			ohos_rdpgfx_record_caps_values(bridge, capsSet->version, capsSet->flags, "callback");
			if (bridge->avc420SurfaceMode)
			{
				if (freerdp_ohos_rdpgfx_caps_confirm_is_avc420(capsSet->version, capsSet->flags))
				{
					ohos_rdpgfx_log(bridge,
					                "RDPGFX negotiated AVC420 surface mode: version=0x%08" PRIX32
					                " flags=0x%08" PRIX32
					                "; GDI remains active until the first AVC420 surface command",
					                capsSet->version, capsSet->flags);
				}
				else if (freerdp_ohos_rdpgfx_caps_confirm_is_avc444(capsSet->version,
				                                                    capsSet->flags))
				{
					BOOL avc444GpuCompositor = FALSE;
					EnterCriticalSection(&bridge->lock);
					avc444GpuCompositor = bridge->avc444GpuCompositor;
					LeaveCriticalSection(&bridge->lock);
					ohos_rdpgfx_log(
					    bridge,
					    "RDPGFX negotiated AVC444 mode: version=0x%08" PRIX32 " flags=0x%08" PRIX32
					    "; AVC420 surface route remains disabled for AVC444"
					    "; avc444GpuCompositor=%s gdiSuppression=per-command",
					    capsSet->version, capsSet->flags, avc444GpuCompositor ? "on" : "off");
				}
				else
				{
					EnterCriticalSection(&bridge->lock);
					bridge->avc420SurfaceMode = FALSE;
					bridge->avc420SurfaceActive = FALSE;
					LeaveCriticalSection(&bridge->lock);
					ohos_rdpgfx_log(bridge,
					                "RDPGFX selected non-AVC graphics mode: version=0x%08" PRIX32
					                " flags=0x%08" PRIX32,
					                capsSet->version, capsSet->flags);
				}
			}
		}
		else
		{
			EnterCriticalSection(&bridge->lock);
			bridge->capsConfirms++;
			bridge->confirmedMode = OHOS_RDPGFX_CONFIRMED_NONE;
			bridge->confirmedVersion = 0;
			bridge->confirmedFlags = 0;
			LeaveCriticalSection(&bridge->lock);
			ohos_rdpgfx_log(bridge, "rdpgfx caps confirm: mode=none capsConfirm=null");
		}

		EnterCriticalSection(&bridge->lock);
		original = bridge->hooks.capsConfirm;
		LeaveCriticalSection(&bridge->lock);
	}
	return original ? original(context, capsConfirm) : CHANNEL_RC_OK;
}
