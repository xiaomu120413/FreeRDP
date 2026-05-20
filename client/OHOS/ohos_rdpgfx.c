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
	BOOL avc444GpuExperimental;
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

BOOL freerdp_ohos_rdpgfx_avc444_command_lc_is_valid(
    const FREERDP_OHOS_RDPGFX_AVC444_COMMAND_INFO* command)
{
	if (!command)
		return FALSE;

	switch (command->LC)
	{
		case 0:
			return command->stream1.data && (command->stream1.length > 0) &&
			       command->stream2.data && (command->stream2.length > 0) &&
			       command->stream1.regionRects &&
			       ((command->stream2.numRegionRects == 0) || command->stream2.regionRects);
		case 1:
		case 2:
			return command->stream1.data && (command->stream1.length > 0) &&
			       command->stream1.regionRects;
		default:
			return FALSE;
	}
}

BOOL freerdp_ohos_rdpgfx_rects_valid(const RECTANGLE_16* rects, UINT32 count, UINT32 width,
                                     UINT32 height)
{
	if ((width == 0) || (height == 0))
		return FALSE;
	if (count == 0)
		return TRUE;
	if (!rects)
		return FALSE;

	for (UINT32 index = 0; index < count; index++)
	{
		const RECTANGLE_16* rect = &rects[index];
		if ((rect->left >= rect->right) || (rect->top >= rect->bottom) ||
		    (rect->right > width) || (rect->bottom > height))
			return FALSE;
	}
	return TRUE;
}

static BOOL ohos_rdpgfx_rects_contain_full_surface(const RECTANGLE_16* rects, UINT32 count,
                                                   UINT32 width, UINT32 height)
{
	if (!rects || (count == 0) || (width == 0) || (height == 0))
		return FALSE;

	for (UINT32 index = 0; index < count; index++)
	{
		const RECTANGLE_16* rect = &rects[index];
		if ((rect->left == 0) && (rect->top == 0) && (rect->right == width) &&
		    (rect->bottom == height))
			return TRUE;
	}
	return FALSE;
}

static int ohos_rdpgfx_uint32_compare(const void* lhs, const void* rhs)
{
	const UINT32 left = *(const UINT32*)lhs;
	const UINT32 right = *(const UINT32*)rhs;
	if (left < right)
		return -1;
	if (left > right)
		return 1;
	return 0;
}

typedef struct
{
	UINT32 left;
	UINT32 right;
} OHOS_RDPGFX_INTERVAL;

static int ohos_rdpgfx_interval_compare(const void* lhs, const void* rhs)
{
	const OHOS_RDPGFX_INTERVAL* left = (const OHOS_RDPGFX_INTERVAL*)lhs;
	const OHOS_RDPGFX_INTERVAL* right = (const OHOS_RDPGFX_INTERVAL*)rhs;
	if (left->left < right->left)
		return -1;
	if (left->left > right->left)
		return 1;
	if (left->right < right->right)
		return -1;
	if (left->right > right->right)
		return 1;
	return 0;
}

BOOL freerdp_ohos_rdpgfx_rects_cover_full_surface(const RECTANGLE_16* rects, UINT32 count,
                                                  UINT32 width, UINT32 height)
{
	BOOL result = FALSE;
	UINT32* edges = NULL;
	OHOS_RDPGFX_INTERVAL* intervals = NULL;
	UINT32 edgeCount = 0;

	if (!freerdp_ohos_rdpgfx_rects_valid(rects, count, width, height))
		return FALSE;
	if (count == 0)
		return FALSE;
	if (ohos_rdpgfx_rects_contain_full_surface(rects, count, width, height))
		return TRUE;
	if (count > ((~(UINT32)0) - 2U) / 2U)
		return FALSE;

	edges = (UINT32*)calloc((size_t)count * 2U + 2U, sizeof(UINT32));
	intervals = (OHOS_RDPGFX_INTERVAL*)calloc(count, sizeof(OHOS_RDPGFX_INTERVAL));
	if (!edges || !intervals)
		goto fail;

	edges[edgeCount++] = 0;
	edges[edgeCount++] = height;
	for (UINT32 index = 0; index < count; index++)
	{
		edges[edgeCount++] = rects[index].top;
		edges[edgeCount++] = rects[index].bottom;
	}
	qsort(edges, edgeCount, sizeof(UINT32), ohos_rdpgfx_uint32_compare);

	UINT32 uniqueCount = 0;
	for (UINT32 index = 0; index < edgeCount; index++)
	{
		if ((uniqueCount == 0) || (edges[index] != edges[uniqueCount - 1U]))
			edges[uniqueCount++] = edges[index];
	}

	for (UINT32 band = 0; band + 1U < uniqueCount; band++)
	{
		const UINT32 top = edges[band];
		const UINT32 bottom = edges[band + 1U];
		UINT32 intervalCount = 0;
		BOOL started = FALSE;
		UINT32 coveredRight = 0;
		if (top == bottom)
			continue;

		for (UINT32 index = 0; index < count; index++)
		{
			const RECTANGLE_16* rect = &rects[index];
			if ((rect->top <= top) && (rect->bottom >= bottom))
			{
				intervals[intervalCount].left = rect->left;
				intervals[intervalCount].right = rect->right;
				intervalCount++;
			}
		}
		if (intervalCount == 0)
			goto fail;

		qsort(intervals, intervalCount, sizeof(OHOS_RDPGFX_INTERVAL),
		      ohos_rdpgfx_interval_compare);
		for (UINT32 index = 0; index < intervalCount; index++)
		{
			if (!started)
			{
				if (intervals[index].left != 0)
					goto fail;
				coveredRight = intervals[index].right;
				started = TRUE;
			}
			else if (intervals[index].left <= coveredRight)
			{
				coveredRight = MAX(coveredRight, intervals[index].right);
			}
			else
			{
				goto fail;
			}
			if (coveredRight >= width)
				break;
		}
		if (!started || (coveredRight < width))
			goto fail;
	}

	result = TRUE;

fail:
	free(intervals);
	free(edges);
	return result;
}

static UINT32 ohos_rdpgfx_avc444_chroma_v1_padded_height(UINT32 height)
{
	return height + 16U - (height % 16U);
}

UINT32 freerdp_ohos_rdpgfx_avc444_chroma_v1_required_y_height(const RECTANGLE_16* rects,
                                                              UINT32 count)
{
	UINT32 required = 0;
	if (!rects)
		return 0;

	for (UINT32 index = 0; index < count; index++)
	{
		const RECTANGLE_16* rect = &rects[index];
		const UINT32 height = rect->bottom - rect->top;
		required = MAX(required, rect->top + ohos_rdpgfx_avc444_chroma_v1_padded_height(height));
	}
	return required;
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

static gdiGfxSurface* ohos_rdpgfx_get_gdi_surface(RdpgfxClientContext* context, UINT32 surfaceId)
{
	if (!context || !context->GetSurfaceData)
		return NULL;

	return (gdiGfxSurface*)context->GetSurfaceData(context, (UINT16)MIN(UINT16_MAX, surfaceId));
}

static BOOL ohos_rdpgfx_get_gdi_surface_size(RdpgfxClientContext* context, UINT32 surfaceId,
                                             UINT32* width, UINT32* height)
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

static BOOL ohos_rdpgfx_command_within_surface(const gdiGfxSurface* surface,
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

static UINT ohos_rdpgfx_validate_avc444_gpu_surface_update(
    freerdpOhosRdpgfxBridge* bridge, RdpgfxClientContext* context,
    const RDPGFX_SURFACE_COMMAND* command, const RDPGFX_AVC444_BITMAP_STREAM* bs,
    UINT32* surfaceWidth, UINT32* surfaceHeight)
{
	if (!context || !command || !bs)
		return ERROR_INTERNAL_ERROR;

	gdiGfxSurface* surface = ohos_rdpgfx_get_gdi_surface(context, command->surfaceId);
	if (!surface)
	{
		ohos_rdpgfx_log(bridge,
		                "AVC444 GPU compositor could not validate suppressed FreeRDP GDI update: "
		                "surface unavailable surface=%" PRIu32,
		                command->surfaceId);
		return ERROR_NOT_FOUND;
	}
	if (!ohos_rdpgfx_command_within_surface(surface, command))
	{
		ohos_rdpgfx_log(bridge,
		                "AVC444 GPU compositor rejected suppressed FreeRDP GDI update: command rect "
		                "%" PRIu32 ",%" PRIu32 "-%" PRIu32 ",%" PRIu32
		                " outside surface=%" PRIu32 " size=%" PRIu32 "x%" PRIu32,
		                command->left, command->top, command->right, command->bottom,
		                command->surfaceId, surface->width, surface->height);
		return ERROR_INVALID_DATA;
	}

	if (surfaceWidth)
		*surfaceWidth = surface->width;
	if (surfaceHeight)
		*surfaceHeight = surface->height;

	/*
	 * FreeRDP's native AVC444 path only marks invalidRegion after avc444_decompress has
	 * updated surface->data. The GPU compositor suppresses that native decode and keeps the
	 * authoritative pixels in its own textures, so touching invalidRegion here would make
	 * gdi_EndFrame/UpdateSurfaces present stale surface->data.
	 */
	return CHANNEL_RC_OK;
}

static BOOL ohos_rdpgfx_should_attempt_avc420_surface(
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
	        command->left, command->top, command->width, command->height, targetWidth, targetHeight))
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

static void ohos_rdpgfx_mark_avc420_surface_result(freerdpOhosRdpgfxBridge* bridge,
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

static BOOL ohos_rdpgfx_record_avc444_gpu_candidate(freerdpOhosRdpgfxBridge* bridge,
                                                    RdpgfxClientContext* context,
                                                    const RDPGFX_SURFACE_COMMAND* command,
                                                    UINT* consumedStatus)
{
	if (!bridge || !command || !freerdp_ohos_rdpgfx_codec_is_avc444(command->codecId))
		return FALSE;
	if (consumedStatus)
		*consumedStatus = CHANNEL_RC_OK;

	const RDPGFX_AVC444_BITMAP_STREAM* bs = (const RDPGFX_AVC444_BITMAP_STREAM*)command->extra;
	const UINT32 stream1Rects = bs ? bs->bitstream[0].meta.numRegionRects : 0;
	const UINT32 stream2Rects = bs ? bs->bitstream[1].meta.numRegionRects : 0;
	const UINT32 stream1Bytes = bs ? bs->bitstream[0].length : 0;
	const UINT32 stream2Bytes = bs ? bs->bitstream[1].length : 0;
	const UINT32 lc = bs ? bs->LC : 0xFFFFFFFFU;
	UINT64 candidates = 0;
	UINT64 disabled = 0;
	UINT64 gdiPreserved = 0;
	FREERDP_OHOS_RDPGFX_AVC444_SURFACE_CALLBACK callback = NULL;
	FREERDP_OHOS_RDPGFX_AVC444_END_FRAME_CALLBACK endFrameCallback = NULL;
	void* userData = NULL;
	BOOL enabled = FALSE;
	BOOL frameOpen = FALSE;
	UINT32 frameId = 0;
	UINT32 targetWidth = 0;
	UINT32 targetHeight = 0;
	UINT32 surfaceWidth = 0;
	UINT32 surfaceHeight = 0;

	(void)ohos_rdpgfx_get_gdi_surface_size(context, command->surfaceId, &surfaceWidth,
	                                       &surfaceHeight);

	EnterCriticalSection(&bridge->lock);
	enabled = bridge->avc444GpuExperimental;
	frameOpen = bridge->frameOpen;
	frameId = bridge->activeFrameId;
	targetWidth = bridge->surfaceTargetWidth;
	targetHeight = bridge->surfaceTargetHeight;
	bridge->lastAvc444FrameId = frameId;
	bridge->lastAvc444LC = lc;
	bridge->lastAvc444Stream1Rects = stream1Rects;
	bridge->lastAvc444Stream2Rects = stream2Rects;
	bridge->lastAvc444Stream1Bytes = stream1Bytes;
	bridge->lastAvc444Stream2Bytes = stream2Bytes;

	if (!enabled)
		disabled = ++bridge->avc444GpuDisabled;
	else
	{
		candidates = ++bridge->avc444GpuCandidates;
		callback = bridge->avc444SurfaceCommand;
		endFrameCallback = bridge->avc444EndFrame;
		userData = bridge->userData;
	}
	LeaveCriticalSection(&bridge->lock);

	if (!enabled)
	{
		if (ohos_rdpgfx_should_log_counter(disabled))
		{
			ohos_rdpgfx_log(
			    bridge,
			    "AVC444 GPU compositor disabled; preserving FreeRDP native GDI path: "
			    "codec=%s surface=%" PRIu32 " frame=%" PRIu32
			    " LC=%" PRIu32 " surfaceSize=%" PRIu32 "x%" PRIu32
			    " commandRect=%" PRIu32 ",%" PRIu32 " %" PRIu32 "x%" PRIu32
			    " stream1Rects=%" PRIu32 " stream2Rects=%" PRIu32
			    " stream1Bytes=%" PRIu32 " stream2Bytes=%" PRIu32 " disabledCount=%" PRIu64,
			    freerdp_ohos_rdpgfx_codec_name(command->codecId), command->surfaceId, frameId,
			    lc, surfaceWidth, surfaceHeight, command->left, command->top, command->width,
			    command->height, stream1Rects, stream2Rects, stream1Bytes, stream2Bytes, disabled);
		}
		return FALSE;
	}

	if (callback)
	{
		FREERDP_OHOS_RDPGFX_AVC444_COMMAND_INFO info = { 0 };
		BOOL callbackReady = FALSE;
		UINT64 callbackReadyCount = 0;
		const UINT validationStatus =
		    ohos_rdpgfx_validate_avc444_gpu_surface_update(bridge, context, command, bs,
		                                                   &surfaceWidth, &surfaceHeight);
		if (validationStatus != CHANNEL_RC_OK)
		{
			if (consumedStatus)
				*consumedStatus = validationStatus;
			ohos_rdpgfx_log(
			    bridge,
			    "AVC444 GPU compositor rejected command before GPU callback to match FreeRDP "
			    "native AVC444 validation order: status=%" PRIu32 " codec=%s surface=%" PRIu32
			    " commandRect=%" PRIu32 ",%" PRIu32 " %" PRIu32 "x%" PRIu32,
			    validationStatus, freerdp_ohos_rdpgfx_codec_name(command->codecId),
			    command->surfaceId, command->left, command->top, command->width,
			    command->height);
			return TRUE;
		}
		info.codecId = command->codecId;
		info.surfaceId = command->surfaceId;
		info.left = command->left;
		info.top = command->top;
		info.width = surfaceWidth;
		info.height = surfaceHeight;
		info.targetWidth = targetWidth;
		info.targetHeight = targetHeight;
		info.frameId = frameId;
		info.frameOpen = frameOpen;
		info.LC = lc;
		if (bs)
		{
			info.stream1.data = bs->bitstream[0].data;
			info.stream1.length = bs->bitstream[0].length;
			info.stream1.regionRects = bs->bitstream[0].meta.regionRects;
			info.stream1.numRegionRects = bs->bitstream[0].meta.numRegionRects;
			info.stream2.data = bs->bitstream[1].data;
			info.stream2.length = bs->bitstream[1].length;
			info.stream2.regionRects = bs->bitstream[1].meta.regionRects;
			info.stream2.numRegionRects = bs->bitstream[1].meta.numRegionRects;
		}
		callbackReady = callback(&info, userData);

		EnterCriticalSection(&bridge->lock);
		bridge->avc444GpuCallbacks++;
		if (callbackReady)
		{
			callbackReadyCount = ++bridge->avc444GpuCallbackReady;
		}
		LeaveCriticalSection(&bridge->lock);

		if (callbackReady)
		{
			if (!frameOpen)
			{
				if (endFrameCallback)
				{
					FREERDP_OHOS_RDPGFX_FRAME_INFO frameInfo = { 0 };
					frameInfo.frameId = frameId;
					frameInfo.activeFrameId = frameId;
					frameInfo.matchedFrame = TRUE;
					(void)endFrameCallback(&frameInfo, userData);
				}
				ohos_rdpgfx_log(
				    bridge,
				    "AVC444 GPU compositor completed inter-frame GPU update ordering: "
				    "FreeRDP dirty state skipped, gpuPresentCallback=%s frame=%" PRIu32,
				    endFrameCallback ? "called" : "none", frameId);
			}
			if (ohos_rdpgfx_should_log_counter(callbackReadyCount))
			{
				ohos_rdpgfx_log(
				    bridge,
				    "AVC444 GPU compositor handled command; suppressing FreeRDP native GDI "
				    "for this surface command without touching FreeRDP dirty state: codec=%s surface=%" PRIu32
				    " frame=%" PRIu32 " LC=%" PRIu32 " stream1Bytes=%" PRIu32
				    " stream2Bytes=%" PRIu32 " handled=%" PRIu64,
				    freerdp_ohos_rdpgfx_codec_name(command->codecId), command->surfaceId,
				    frameId, lc, stream1Bytes, stream2Bytes, callbackReadyCount);
			}
			return TRUE;
		}
	}

	EnterCriticalSection(&bridge->lock);
	gdiPreserved = ++bridge->avc444GpuGdiPreserved;
	LeaveCriticalSection(&bridge->lock);

	if ((candidates <= 8U) || ((candidates % 120U) == 0U))
	{
		ohos_rdpgfx_log(
		    bridge,
		    "AVC444 GPU compositor did not request GDI suppression for this command; "
		    "preserving FreeRDP native GDI path: "
		    "codec=%s surface=%" PRIu32 " frame=%" PRIu32 " frameOpen=%s"
		    " LC=%" PRIu32 " surfaceSize=%" PRIu32 "x%" PRIu32
		    " commandRect=%" PRIu32 ",%" PRIu32 " %" PRIu32 "x%" PRIu32
		    " stream1Rects=%" PRIu32 " stream2Rects=%" PRIu32
		    " stream1Bytes=%" PRIu32 " stream2Bytes=%" PRIu32
		    " gdiPreserved=%" PRIu64,
		    freerdp_ohos_rdpgfx_codec_name(command->codecId), command->surfaceId, frameId,
		    frameOpen ? "yes" : "no", lc, surfaceWidth, surfaceHeight, command->left,
		    command->top, command->width, command->height, stream1Rects, stream2Rects,
		    stream1Bytes, stream2Bytes, gdiPreserved);
	}
	return FALSE;
}

static UINT ohos_rdpgfx_start_frame(RdpgfxClientContext* context,
                                    const RDPGFX_START_FRAME_PDU* startFrame)
{
	freerdpOhosRdpgfxBridge* bridge = ohos_rdpgfx_bridge_from_context(context);
	pcRdpgfxStartFrame original = NULL;
	UINT64 frameCount = 0;
	BOOL avc444GpuExperimental = FALSE;
	if (bridge)
	{
		EnterCriticalSection(&bridge->lock);
		frameCount = ++bridge->startFrames;
		bridge->activeFrameId = startFrame ? startFrame->frameId : 0;
		bridge->frameOpen = TRUE;
		avc444GpuExperimental = bridge->avc444GpuExperimental;
		original = bridge->hooks.startFrame;
		LeaveCriticalSection(&bridge->lock);
		if (avc444GpuExperimental && ((frameCount <= 5U) || ((frameCount % 120U) == 0U)))
		{
			ohos_rdpgfx_log(bridge,
			                "rdpgfx start frame observed for AVC444 GPU compositor: frameId=%" PRIu32
			                " timestamp=%" PRIu32 " count=%" PRIu64,
			                startFrame ? startFrame->frameId : 0,
			                startFrame ? startFrame->timestamp : 0, frameCount);
		}
	}
	return original ? original(context, startFrame) : ERROR_INTERNAL_ERROR;
}

static UINT ohos_rdpgfx_end_frame(RdpgfxClientContext* context,
                                  const RDPGFX_END_FRAME_PDU* endFrame)
{
	freerdpOhosRdpgfxBridge* bridge = ohos_rdpgfx_bridge_from_context(context);
	pcRdpgfxEndFrame original = NULL;
	UINT64 frameCount = 0;
	BOOL avc444GpuExperimental = FALSE;
	UINT32 activeFrameId = 0;
	BOOL matchedFrame = FALSE;
	FREERDP_OHOS_RDPGFX_AVC444_END_FRAME_CALLBACK avc444EndFrame = NULL;
	void* userData = NULL;
	UINT status = ERROR_INTERNAL_ERROR;
	if (bridge)
	{
		EnterCriticalSection(&bridge->lock);
		frameCount = ++bridge->endFrames;
		activeFrameId = bridge->activeFrameId;
		matchedFrame = bridge->frameOpen &&
		               (!endFrame || (endFrame->frameId == bridge->activeFrameId));
		bridge->frameOpen = FALSE;
		avc444GpuExperimental = bridge->avc444GpuExperimental;
		original = bridge->hooks.endFrame;
		avc444EndFrame = bridge->avc444EndFrame;
		userData = bridge->userData;
		LeaveCriticalSection(&bridge->lock);
		if (avc444GpuExperimental && ((frameCount <= 5U) || ((frameCount % 120U) == 0U) ||
		                              !matchedFrame))
		{
			ohos_rdpgfx_log(bridge,
			                "rdpgfx end frame observed for AVC444 GPU compositor: frameId=%" PRIu32
			                " active=%" PRIu32 " matched=%s count=%" PRIu64,
			                endFrame ? endFrame->frameId : 0, activeFrameId,
			                matchedFrame ? "yes" : "no", frameCount);
		}
	}
	status = original ? original(context, endFrame) : ERROR_INTERNAL_ERROR;
	if (bridge && avc444GpuExperimental && avc444EndFrame)
	{
		FREERDP_OHOS_RDPGFX_FRAME_INFO info = { 0 };
		info.frameId = endFrame ? endFrame->frameId : 0;
		info.activeFrameId = activeFrameId;
		info.matchedFrame = matchedFrame;
		(void)avc444EndFrame(&info, userData);
	}
	return status;
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
					BOOL avc444GpuExperimental = FALSE;
					EnterCriticalSection(&bridge->lock);
					avc444GpuExperimental = bridge->avc444GpuExperimental;
					LeaveCriticalSection(&bridge->lock);
					ohos_rdpgfx_log(bridge,
					                "RDPGFX negotiated AVC444 FreeRDP native composition mode: version=0x%08"
					                PRIX32 " flags=0x%08" PRIX32
					                       "; AVC420 surface route remains disabled for AVC444"
					                       "; avc444GpuCompositor=%s gdiSuppression=per-command",
					                capsSet->version, capsSet->flags,
					                avc444GpuExperimental ? "requested" : "off");
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

static UINT ohos_rdpgfx_surface_command(RdpgfxClientContext* context,
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
	bridge->avc420SurfaceMode = FALSE;
	bridge->avc420SurfaceActive = FALSE;
	bridge->avc444GpuExperimental = FALSE;
	bridge->avc444EndFrame = NULL;
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
	bridge->avc420SurfaceSubrectSkips = 0;
	bridge->avc420SurfaceNoDirect = 0;
	bridge->avc444GpuCandidates = 0;
	bridge->avc444GpuDisabled = 0;
	bridge->avc444GpuGdiPreserved = 0;
	bridge->avc444GpuFrameMismatchSkips = 0;
	bridge->avc444GpuCallbacks = 0;
	bridge->avc444GpuCallbackReady = 0;
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
		bridge->avc420SurfaceMode = config->avc420SurfaceMode;
		bridge->avc444GpuExperimental = config->avc444GpuExperimental;
		bridge->surfaceTargetWidth = config->surfaceTargetWidth;
		bridge->surfaceTargetHeight = config->surfaceTargetHeight;
		bridge->log = config->log;
		bridge->avc420SurfaceCommand = config->avc420SurfaceCommand;
		bridge->avc444SurfaceCommand = config->avc444SurfaceCommand;
		bridge->avc444EndFrame = config->avc444EndFrame;
		bridge->userData = config->userData;
		LeaveCriticalSection(&bridge->lock);
		ohos_rdpgfx_format_message(
		    message, messageSize, "OHOS rdpgfx bridge already attached: avc444GpuCompositor=%s",
		    config->avc444GpuExperimental ? "requested" : "off");
		return TRUE;
	}

	bridge->gfx = gfx;
	bridge->hooks.startFrame = gfx->StartFrame;
	bridge->hooks.endFrame = gfx->EndFrame;
	bridge->hooks.surfaceCommand = gfx->SurfaceCommand;
	bridge->hooks.capsAdvertise = gfx->CapsAdvertise;
	bridge->hooks.capsConfirm = gfx->CapsConfirm;
	bridge->avc420SurfaceMode = config->avc420SurfaceMode;
	bridge->avc420SurfaceActive = FALSE;
	bridge->avc444GpuExperimental = config->avc444GpuExperimental;
	bridge->surfaceTargetWidth = config->surfaceTargetWidth;
	bridge->surfaceTargetHeight = config->surfaceTargetHeight;
	bridge->log = config->log;
	bridge->avc420SurfaceCommand = config->avc420SurfaceCommand;
	bridge->avc444SurfaceCommand = config->avc444SurfaceCommand;
	bridge->avc444EndFrame = config->avc444EndFrame;
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
	ohos_rdpgfx_log(bridge,
	                "OHOS rdpgfx bridge attached: avc420Surface=%s avc444GpuCompositor=%s "
	                "avc444SuppressGdi=per-command-success target=%ux%u",
	                config->avc420SurfaceMode ? "on" : "off",
	                config->avc444GpuExperimental ? "requested" : "off",
	                config->surfaceTargetWidth, config->surfaceTargetHeight);
	ohos_rdpgfx_format_message(
	    message, messageSize, "OHOS rdpgfx bridge attached: avc444GpuCompositor=%s",
	    config->avc444GpuExperimental ? "requested" : "off");
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
		bridge->avc420SurfaceActive = FALSE;
		bridge->frameOpen = FALSE;
		bridge->activeFrameId = 0;
		bridge->avc444EndFrame = NULL;
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
		bridge->avc420SurfaceActive = FALSE;
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
	               " lastSize=%" PRIu32 "x%" PRIu32
	               " frame=open:%s,id:%" PRIu32
	               " avc420Surface=%s target=%" PRIu32
	               "x%" PRIu32 " skips=%" PRIu64 " noDirect=%" PRIu64
	               " avc444Gpu=compositor:%s,suppressGdi:per-command"
	               ",candidates:%" PRIu64 ",disabled:%" PRIu64
	               ",frameMismatch:%" PRIu64 ",gdiPreserved:%" PRIu64
	               ",callbacks:%" PRIu64 ",callbackReady:%" PRIu64
	               " avc444Last=frame:%" PRIu32 ",LC:%" PRIu32
	               ",stream1Rects:%" PRIu32 ",stream2Rects:%" PRIu32
	               ",stream1Bytes:%" PRIu32 ",stream2Bytes:%" PRIu32,
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
	               bridge->lastCommandHeight, bridge->frameOpen ? "yes" : "no",
	               bridge->activeFrameId,
	               bridge->avc420SurfaceActive ? "active" : "inactive",
	               bridge->surfaceTargetWidth, bridge->surfaceTargetHeight,
	               bridge->avc420SurfaceSubrectSkips, bridge->avc420SurfaceNoDirect,
	               bridge->avc444GpuExperimental ? "requested" : "off",
	               bridge->avc444GpuCandidates, bridge->avc444GpuDisabled,
	               bridge->avc444GpuFrameMismatchSkips, bridge->avc444GpuGdiPreserved,
	               bridge->avc444GpuCallbacks, bridge->avc444GpuCallbackReady,
	               bridge->lastAvc444FrameId,
	               bridge->lastAvc444LC, bridge->lastAvc444Stream1Rects,
	               bridge->lastAvc444Stream2Rects, bridge->lastAvc444Stream1Bytes,
	               bridge->lastAvc444Stream2Bytes);
	LeaveCriticalSection(&bridge->lock);
	return bridge->diagnostics;
}
