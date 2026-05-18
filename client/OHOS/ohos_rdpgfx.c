/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS rdpgfx callback bridge and diagnostics
 */

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
#include <winpr/crt.h>
#include <winpr/synch.h>
#include <winpr/wtsapi.h>

#include "ohos_graphics.h"

#define OHOS_RDPGFX_DIAGNOSTICS_SIZE 1536

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
	BOOL h264SurfaceMode;
	BOOL h264SurfaceActive;
	UINT32 surfaceTargetWidth;
	UINT32 surfaceTargetHeight;
	RdpgfxClientContext* gfx;
	FREERDP_OHOS_RDPGFX_HOOKS hooks;
	FREERDP_OHOS_RDPGFX_LOG_CALLBACK log;
	FREERDP_OHOS_RDPGFX_H264_SURFACE_CALLBACK h264SurfaceCommand;
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
	UINT64 h264SurfaceSubrectSkips;
	UINT64 h264SurfaceNoDirect;
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

static void ohos_rdpgfx_format_message(char* message, size_t size, const char* format, ...)
{
	if (!message || size == 0)
		return;

	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, size, format, args);
	va_end(args);
}

static const char* ohos_rdpgfx_confirmed_mode_name(UINT32 mode)
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

static void ohos_rdpgfx_log(freerdpOhosRdpgfxBridge* bridge, const char* format, ...)
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

static BOOL ohos_rdpgfx_should_log_counter(UINT64 count)
{
	return count == 1U || (count % 300U) == 0U;
}

static void ohos_rdpgfx_registry_add(freerdpOhosRdpgfxBridge* bridge)
{
	if (!bridge || !ohos_rdpgfx_registry_ready())
		return;

	EnterCriticalSection(&g_ohos_rdpgfx_registry_lock);
	for (freerdpOhosRdpgfxBridge* current = g_ohos_rdpgfx_registry; current; current = current->next)
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

static void ohos_rdpgfx_registry_remove(freerdpOhosRdpgfxBridge* bridge)
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

static freerdpOhosRdpgfxBridge* ohos_rdpgfx_bridge_from_context(RdpgfxClientContext* context)
{
	if (!context || !ohos_rdpgfx_registry_ready())
		return NULL;

	freerdpOhosRdpgfxBridge* bridge = NULL;
	EnterCriticalSection(&g_ohos_rdpgfx_registry_lock);
	for (freerdpOhosRdpgfxBridge* current = g_ohos_rdpgfx_registry; current; current = current->next)
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

static void ohos_rdpgfx_record_caps_values(freerdpOhosRdpgfxBridge* bridge, UINT32 version,
                                           UINT32 flags, const char* source)
{
	if (!bridge)
		return;

	const BOOL avc420 = freerdp_ohos_rdpgfx_caps_confirm_is_avc420(version, flags);
	const BOOL avc444 = freerdp_ohos_rdpgfx_caps_confirm_is_avc444(version, flags);
	const UINT32 mode = avc420 ? OHOS_RDPGFX_CONFIRMED_AVC420
	                           : (avc444 ? OHOS_RDPGFX_CONFIRMED_AVC444
	                                     : OHOS_RDPGFX_CONFIRMED_NON_AVC);

	UINT32 total = 0;
	EnterCriticalSection(&bridge->lock);
	total = ++bridge->capsConfirms;
	bridge->confirmedMode = mode;
	bridge->confirmedVersion = version;
	bridge->confirmedFlags = flags;
	LeaveCriticalSection(&bridge->lock);

	ohos_rdpgfx_log(bridge,
	                "rdpgfx caps confirm: mode=%s version=0x%08" PRIX32
	                " flags=0x%08" PRIX32 " source=%s confirms=%" PRIu32,
	                ohos_rdpgfx_confirmed_mode_name(mode), version, flags,
	                source ? source : "unknown", total);
}

static void ohos_rdpgfx_record_connection_caps_snapshot(freerdpOhosRdpgfxBridge* bridge,
                                                        RdpgfxClientContext* gfx)
{
	if (!bridge || !gfx || !gfx->handle)
		return;

	const FREERDP_OHOS_RDPGFX_PLUGIN_SNAPSHOT* plugin =
	    (const FREERDP_OHOS_RDPGFX_PLUGIN_SNAPSHOT*)gfx->handle;
	if (plugin->connectionCaps.version == 0)
	{
		ohos_rdpgfx_log(bridge, "RDPGFX connection caps snapshot unavailable: version=0");
		return;
	}

	ohos_rdpgfx_record_caps_values(bridge, plugin->connectionCaps.version,
	                               plugin->connectionCaps.flags, "connection-caps-snapshot");
}

static void ohos_rdpgfx_record_caps_advertise(freerdpOhosRdpgfxBridge* bridge,
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
		ohos_rdpgfx_log(bridge, "rdpgfx caps advertise: count=0 advertises=%" PRIu32,
		                total);
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
	                " v81Version=0x%08" PRIX32 " v81Flags=0x%08" PRIX32
	                " advertises=%" PRIu32,
	                capsAdvertise->capsSetCount, advertisedAvc420 ? "yes" : "no",
	                advertisedVersion, advertisedFlags, total);
	for (UINT32 index = 0; index < capsAdvertise->capsSetCount; index++)
	{
		const RDPGFX_CAPSET* capsSet = &(capsAdvertise->capsSets[index]);
		const BOOL avc420 = freerdp_ohos_rdpgfx_caps_confirm_is_avc420(
		    capsSet->version, capsSet->flags);
		const BOOL avc444 = freerdp_ohos_rdpgfx_caps_confirm_is_avc444(
		    capsSet->version, capsSet->flags);
		ohos_rdpgfx_log(bridge,
		                "rdpgfx caps advertise[%" PRIu32 "]: version=0x%08" PRIX32
		                " length=%" PRIu32 " flags=0x%08" PRIX32
		                " avc420=%s avcDisabled=%s avc444Cap=%s",
		                index, capsSet->version, capsSet->length, capsSet->flags,
		                avc420 ? "yes" : "no",
		                (capsSet->flags & RDPGFX_CAPS_FLAG_AVC_DISABLED) != 0 ? "yes" : "no",
		                avc444 ? "yes" : "no");
	}
}

static void ohos_rdpgfx_record_surface_command(freerdpOhosRdpgfxBridge* bridge,
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
		                ") surface=%" PRIu16 " rect=%" PRIu32 ",%" PRIu32 " %" PRIu32
		                "x%" PRIu32
		                " counts=clear:%" PRIu64 ",progressive:%" PRIu64 ",avc420:%" PRIu64
		                ",avc444:%" PRIu64 ",raw:%" PRIu64 ",unknown:%" PRIu64,
		                total, freerdp_ohos_rdpgfx_codec_name(command->codecId),
		                command->codecId, command->surfaceId, command->left, command->top,
		                command->width, command->height, clear, progressive, avc420, avc444, raw,
		                unknown);
	}
}

static BOOL ohos_rdpgfx_should_attempt_h264_surface(
    freerdpOhosRdpgfxBridge* bridge, const RDPGFX_SURFACE_COMMAND* command,
    FREERDP_OHOS_RDPGFX_H264_SURFACE_CALLBACK* callback, void** userData, UINT64* skipCount)
{
	if (!bridge || !command || !callback || !userData || !skipCount)
		return FALSE;

	*callback = NULL;
	*userData = NULL;
	*skipCount = 0;

	if (!freerdp_ohos_rdpgfx_codec_is_h264(command->codecId))
		return FALSE;

	EnterCriticalSection(&bridge->lock);
	const BOOL configured = bridge->h264SurfaceMode;
	const BOOL active = bridge->h264SurfaceActive;
	const UINT32 targetWidth = bridge->surfaceTargetWidth;
	const UINT32 targetHeight = bridge->surfaceTargetHeight;
	if (!configured || active)
	{
		LeaveCriticalSection(&bridge->lock);
		return FALSE;
	}

	if (!freerdp_ohos_rdpgfx_surface_command_is_full_window(
	        command->left, command->top, command->width, command->height, targetWidth, targetHeight))
	{
		*skipCount = ++bridge->h264SurfaceSubrectSkips;
		LeaveCriticalSection(&bridge->lock);
		return FALSE;
	}

	*callback = bridge->h264SurfaceCommand;
	*userData = bridge->userData;
	LeaveCriticalSection(&bridge->lock);
	return *callback != NULL;
}

static void ohos_rdpgfx_mark_h264_surface_result(freerdpOhosRdpgfxBridge* bridge,
                                                 const RDPGFX_SURFACE_COMMAND* command,
                                                 BOOL activated)
{
	if (!bridge || !command)
		return;

	UINT64 noDirect = 0;
	EnterCriticalSection(&bridge->lock);
	if (activated)
		bridge->h264SurfaceActive = TRUE;
	else
		noDirect = ++bridge->h264SurfaceNoDirect;
	LeaveCriticalSection(&bridge->lock);

	if (!activated && ohos_rdpgfx_should_log_counter(noDirect))
	{
		ohos_rdpgfx_log(bridge,
		                "AVC surface command reached FreeRDP without direct surface output: "
		                "codec=%s surface=%" PRIu16 " size=%" PRIu32 "x%" PRIu32
		                " count=%" PRIu64,
		                freerdp_ohos_rdpgfx_codec_name(command->codecId), command->surfaceId,
		                command->width, command->height, noDirect);
	}
}

static UINT ohos_rdpgfx_start_frame(RdpgfxClientContext* context,
                                    const RDPGFX_START_FRAME_PDU* startFrame)
{
	freerdpOhosRdpgfxBridge* bridge = ohos_rdpgfx_bridge_from_context(context);
	pcRdpgfxStartFrame original = NULL;
	if (bridge)
	{
		EnterCriticalSection(&bridge->lock);
		bridge->startFrames++;
		original = bridge->hooks.startFrame;
		LeaveCriticalSection(&bridge->lock);
	}
	return original ? original(context, startFrame) : ERROR_INTERNAL_ERROR;
}

static UINT ohos_rdpgfx_end_frame(RdpgfxClientContext* context,
                                  const RDPGFX_END_FRAME_PDU* endFrame)
{
	freerdpOhosRdpgfxBridge* bridge = ohos_rdpgfx_bridge_from_context(context);
	pcRdpgfxEndFrame original = NULL;
	if (bridge)
	{
		EnterCriticalSection(&bridge->lock);
		bridge->endFrames++;
		original = bridge->hooks.endFrame;
		LeaveCriticalSection(&bridge->lock);
	}
	return original ? original(context, endFrame) : ERROR_INTERNAL_ERROR;
}

static UINT ohos_rdpgfx_caps_advertise(RdpgfxClientContext* context,
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

static UINT ohos_rdpgfx_caps_confirm(RdpgfxClientContext* context,
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
			if (bridge->h264SurfaceMode)
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
					ohos_rdpgfx_log(bridge,
					                "RDPGFX negotiated AVC444 surface mode: version=0x%08" PRIX32
					                " flags=0x%08" PRIX32
					                "; AVC444 primary stream will use the AVC420 output surface",
					                capsSet->version, capsSet->flags);
				}
				else
				{
					EnterCriticalSection(&bridge->lock);
					bridge->h264SurfaceMode = FALSE;
					bridge->h264SurfaceActive = FALSE;
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

static UINT ohos_rdpgfx_surface_command(RdpgfxClientContext* context,
                                        const RDPGFX_SURFACE_COMMAND* command)
{
	freerdpOhosRdpgfxBridge* bridge = ohos_rdpgfx_bridge_from_context(context);
	pcRdpgfxSurfaceCommand original = NULL;
	if (bridge && command)
	{
		ohos_rdpgfx_record_surface_command(bridge, command);

		FREERDP_OHOS_RDPGFX_H264_SURFACE_CALLBACK callback = NULL;
		void* userData = NULL;
		UINT64 skipCount = 0;
		if (ohos_rdpgfx_should_attempt_h264_surface(bridge, command, &callback, &userData,
		                                            &skipCount))
		{
			FREERDP_OHOS_RDPGFX_SURFACE_COMMAND_INFO info = { 0 };
			info.codecId = command->codecId;
			info.surfaceId = command->surfaceId;
			info.left = command->left;
			info.top = command->top;
			info.width = command->width;
			info.height = command->height;
			ohos_rdpgfx_mark_h264_surface_result(bridge, command, callback(&info, userData));
		}
		else if (skipCount > 0 && ohos_rdpgfx_should_log_counter(skipCount))
		{
			ohos_rdpgfx_log(bridge,
			                "AVC surface output activation skipped: command is a sub-rectangle "
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
	bridge->h264SurfaceMode = FALSE;
	bridge->h264SurfaceActive = FALSE;
	bridge->connected = 0;
	bridge->disconnected = 0;
	bridge->initFailed = 0;
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
	bridge->h264SurfaceSubrectSkips = 0;
	bridge->h264SurfaceNoDirect = 0;
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

BOOL freerdp_ohos_rdpgfx_bridge_attach(freerdpOhosRdpgfxBridge* bridge,
                                       RdpgfxClientContext* gfx,
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
		bridge->h264SurfaceMode = config->h264SurfaceMode;
		bridge->surfaceTargetWidth = config->surfaceTargetWidth;
		bridge->surfaceTargetHeight = config->surfaceTargetHeight;
		bridge->log = config->log;
		bridge->h264SurfaceCommand = config->h264SurfaceCommand;
		bridge->userData = config->userData;
		LeaveCriticalSection(&bridge->lock);
		ohos_rdpgfx_format_message(message, messageSize, "OHOS rdpgfx bridge already attached");
		return TRUE;
	}

	bridge->gfx = gfx;
	bridge->hooks.startFrame = gfx->StartFrame;
	bridge->hooks.endFrame = gfx->EndFrame;
	bridge->hooks.surfaceCommand = gfx->SurfaceCommand;
	bridge->hooks.capsAdvertise = gfx->CapsAdvertise;
	bridge->hooks.capsConfirm = gfx->CapsConfirm;
	bridge->h264SurfaceMode = config->h264SurfaceMode;
	bridge->h264SurfaceActive = FALSE;
	bridge->surfaceTargetWidth = config->surfaceTargetWidth;
	bridge->surfaceTargetHeight = config->surfaceTargetHeight;
	bridge->log = config->log;
	bridge->h264SurfaceCommand = config->h264SurfaceCommand;
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
	ohos_rdpgfx_format_message(message, messageSize, "OHOS rdpgfx bridge attached");
	return TRUE;
}

void freerdp_ohos_rdpgfx_bridge_detach(freerdpOhosRdpgfxBridge* bridge, RdpgfxClientContext* gfx)
{
	if (!bridge)
		return;

	RdpgfxClientContext* active = NULL;
	FREERDP_OHOS_RDPGFX_HOOKS hooks = { 0 };
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
		bridge->h264SurfaceActive = FALSE;
		bridge->disconnected++;
	}
	LeaveCriticalSection(&bridge->lock);

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
	if (!attached)
		bridge->h264SurfaceActive = FALSE;
	LeaveCriticalSection(&bridge->lock);
}

const char* freerdp_ohos_rdpgfx_bridge_get_diagnostics(freerdpOhosRdpgfxBridge* bridge)
{
	if (!bridge)
		return "OHOS rdpgfx stats: unavailable";

	EnterCriticalSection(&bridge->lock);
	(void)snprintf(bridge->diagnostics, sizeof(bridge->diagnostics),
	               "OHOS rdpgfx stats: runtime=%s h264=%s bridge=%s connected=%" PRIu32
	               " disconnected=%" PRIu32 " initFailed=%" PRIu32 " frames=%" PRIu64
	               "/%" PRIu64 " surfaceCommands=%" PRIu64
	               " capsAdvertise=%" PRIu32 "/%" PRIu32
	               " advertisedAvc420=%s advertisedV81=0x%08" PRIX32
	               " advertisedFlags=0x%08" PRIX32 " confirmed=%s "
	               "capsVersion=0x%08" PRIX32 " capsFlags=0x%08" PRIX32
	               " capsConfirms=%" PRIu32
	               " codecs=raw:%" PRIu64 ",progressive:%" PRIu64 ",cavideo:%" PRIu64
	               ",clear:%" PRIu64 ",planar:%" PRIu64 ",avc420:%" PRIu64
	               ",avc444:%" PRIu64 ",avc444v2:%" PRIu64 ",alpha:%" PRIu64
	               ",unknown:%" PRIu64 " lastCodec=%s(%" PRIu32 ") lastSurface=%" PRIu32
	               " lastSize=%" PRIu32 "x%" PRIu32 " h264Surface=%s target=%" PRIu32
	               "x%" PRIu32 " skips=%" PRIu64 " noDirect=%" PRIu64,
	               bridge->requested ? "requested" : "off",
	               bridge->h264Requested ? "requested" : "off",
	               bridge->gdiAttached ? "attached" : "detached", bridge->connected,
	               bridge->disconnected, bridge->initFailed, bridge->startFrames, bridge->endFrames,
	               bridge->surfaceCommands, bridge->capsAdvertises, bridge->advertisedCapsSets,
	               bridge->advertisedAvc420 ? "yes" : "no", bridge->advertisedVersion,
	               bridge->advertisedFlags,
	               ohos_rdpgfx_confirmed_mode_name(bridge->confirmedMode),
	               bridge->confirmedVersion, bridge->confirmedFlags, bridge->capsConfirms,
	               bridge->codecUncompressed, bridge->codecProgressive, bridge->codecCavideo,
	               bridge->codecClearCodec, bridge->codecPlanar, bridge->codecAvc420,
	               bridge->codecAvc444, bridge->codecAvc444v2, bridge->codecAlpha,
	               bridge->codecUnknown, freerdp_ohos_rdpgfx_codec_name(bridge->lastCodecId),
	               bridge->lastCodecId, bridge->lastSurfaceId, bridge->lastCommandWidth,
	               bridge->lastCommandHeight, bridge->h264SurfaceActive ? "active" : "inactive",
	               bridge->surfaceTargetWidth, bridge->surfaceTargetHeight,
	               bridge->h264SurfaceSubrectSkips, bridge->h264SurfaceNoDirect);
	LeaveCriticalSection(&bridge->lock);
	return bridge->diagnostics;
}
