/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS rdpgfx AVC444 policy and rectangle validation
 */

#include "ohos_rdpgfx_internal.h"

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
		if ((rect->left >= rect->right) || (rect->top >= rect->bottom) || (rect->right > width) ||
		    (rect->bottom > height))
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

		qsort(intervals, intervalCount, sizeof(OHOS_RDPGFX_INTERVAL), ohos_rdpgfx_interval_compare);
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

void ohos_rdpgfx_set_avc444_gpu_output_active(freerdpOhosRdpgfxBridge* bridge, BOOL active,
                                              const char* reason)
{
	FREERDP_OHOS_RDPGFX_AVC444_OUTPUT_STATE_CALLBACK callback = NULL;
	void* userData = NULL;
	BOOL changed = FALSE;
	UINT64 activations = 0;
	UINT64 releases = 0;

	if (!bridge)
		return;

	EnterCriticalSection(&bridge->lock);
	if (bridge->avc444GpuOutputActive != active)
	{
		bridge->avc444GpuOutputActive = active;
		if (active)
			activations = ++bridge->avc444GpuOutputActivations;
		else
			releases = ++bridge->avc444GpuOutputReleases;
		changed = TRUE;
		callback = bridge->avc444OutputState;
		userData = bridge->userData;
	}
	LeaveCriticalSection(&bridge->lock);

	if (!changed)
		return;
	if (callback)
		callback(active, reason ? reason : "AVC444 GPU output state changed", userData);
	ohos_rdpgfx_log(bridge,
	                "AVC444 GPU output owner changed by FreeRDP policy: active=%s "
	                "reason=%s activations=%" PRIu64 " releases=%" PRIu64,
	                active ? "yes" : "no", reason ? reason : "AVC444 GPU output state changed",
	                activations, releases);
}

static UINT ohos_rdpgfx_validate_avc444_gpu_surface_update(freerdpOhosRdpgfxBridge* bridge,
                                                           RdpgfxClientContext* context,
                                                           const RDPGFX_SURFACE_COMMAND* command,
                                                           const RDPGFX_AVC444_BITMAP_STREAM* bs,
                                                           UINT32* surfaceWidth,
                                                           UINT32* surfaceHeight)
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
		ohos_rdpgfx_log(
		    bridge,
		    "AVC444 GPU compositor rejected suppressed FreeRDP GDI update: command rect "
		    "%" PRIu32 ",%" PRIu32 "-%" PRIu32 ",%" PRIu32 " outside surface=%" PRIu32
		    " size=%" PRIu32 "x%" PRIu32,
		    command->left, command->top, command->right, command->bottom, command->surfaceId,
		    surface->width, surface->height);
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

BOOL ohos_rdpgfx_record_avc444_gpu_candidate(freerdpOhosRdpgfxBridge* bridge,
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
	enabled = bridge->avc444GpuCompositor;
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
			    "AVC444 GPU compositor off; preserving FreeRDP native GDI path: "
			    "codec=%s surface=%" PRIu32 " frame=%" PRIu32 " LC=%" PRIu32 " surfaceSize=%" PRIu32
			    "x%" PRIu32 " commandRect=%" PRIu32 ",%" PRIu32 " %" PRIu32 "x%" PRIu32
			    " stream1Rects=%" PRIu32 " stream2Rects=%" PRIu32 " stream1Bytes=%" PRIu32
			    " stream2Bytes=%" PRIu32 " disabledCount=%" PRIu64,
			    freerdp_ohos_rdpgfx_codec_name(command->codecId), command->surfaceId, frameId, lc,
			    surfaceWidth, surfaceHeight, command->left, command->top, command->width,
			    command->height, stream1Rects, stream2Rects, stream1Bytes, stream2Bytes, disabled);
		}
		return FALSE;
	}

	if (callback)
	{
		FREERDP_OHOS_RDPGFX_AVC444_COMMAND_INFO info = { 0 };
		BOOL callbackReady = FALSE;
		const UINT validationStatus = ohos_rdpgfx_validate_avc444_gpu_surface_update(
		    bridge, context, command, bs, &surfaceWidth, &surfaceHeight);
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
			    command->surfaceId, command->left, command->top, command->width, command->height);
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
			bridge->avc444GpuCallbackReady++;
		LeaveCriticalSection(&bridge->lock);

		if (callbackReady)
		{
			ohos_rdpgfx_set_avc444_gpu_output_active(
			    bridge, TRUE, "AVC444 GPU command handled; FreeRDP suppresses native GDI");
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
				ohos_rdpgfx_log(bridge,
				                "AVC444 GPU compositor completed inter-frame GPU update ordering: "
				                "FreeRDP dirty state skipped, gpuPresentCallback=%s frame=%" PRIu32,
				                endFrameCallback ? "called" : "none", frameId);
			}
			return TRUE;
		}
	}

	EnterCriticalSection(&bridge->lock);
	if (bridge->avc444GpuOutputActive)
	{
		const UINT64 suppressedFailures = ++bridge->avc444GpuActiveSuppressedFailures;
		LeaveCriticalSection(&bridge->lock);
		ohos_rdpgfx_log(
		    bridge,
		    "AVC444 GPU compositor callback did not handle command while GPU output is active; "
		    "FreeRDP policy keeps native GDI suppressed to avoid re-entering stale AVC444 state: "
		    "codec=%s surface=%" PRIu32 " frame=%" PRIu32 " LC=%" PRIu32
		    " activeSuppressed=%" PRIu64,
		    freerdp_ohos_rdpgfx_codec_name(command->codecId), command->surfaceId, frameId, lc,
		    suppressedFailures);
		return TRUE;
	}
	gdiPreserved = ++bridge->avc444GpuGdiPreserved;
	LeaveCriticalSection(&bridge->lock);

	if (ohos_rdpgfx_should_log_counter(candidates))
	{
		ohos_rdpgfx_log(bridge,
		                "AVC444 GPU compositor did not request GDI suppression for this command; "
		                "preserving FreeRDP native GDI path: "
		                "codec=%s surface=%" PRIu32 " frame=%" PRIu32 " frameOpen=%s"
		                " LC=%" PRIu32 " surfaceSize=%" PRIu32 "x%" PRIu32 " commandRect=%" PRIu32
		                ",%" PRIu32 " %" PRIu32 "x%" PRIu32 " stream1Rects=%" PRIu32
		                " stream2Rects=%" PRIu32 " stream1Bytes=%" PRIu32 " stream2Bytes=%" PRIu32
		                " gdiPreserved=%" PRIu64,
		                freerdp_ohos_rdpgfx_codec_name(command->codecId), command->surfaceId,
		                frameId, frameOpen ? "yes" : "no", lc, surfaceWidth, surfaceHeight,
		                command->left, command->top, command->width, command->height, stream1Rects,
		                stream2Rects, stream1Bytes, stream2Bytes, gdiPreserved);
	}
	return FALSE;
}
