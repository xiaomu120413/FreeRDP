/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * MS-RDPECAM Implementation, HarmonyOS ImageReceiver frame copy
 */

#include "camera_ohos.h"

static BYTE cam_ohos_clip_color(int32_t value)
{
	if (value < 0)
		return 0;
	if (value > 255)
		return 255;
	return (BYTE)value;
}

static BOOL cam_ohos_map_plane(OH_ImageNative* image, uint32_t componentType,
                               CamOhosMappedPlane* plane)
{
	WINPR_ASSERT(image);
	WINPR_ASSERT(plane);
	memset(plane, 0, sizeof(*plane));

	if (OH_ImageNative_GetByteBuffer(image, componentType, &plane->buffer) != IMAGE_SUCCESS ||
	    !plane->buffer)
		return FALSE;

	OH_NativeBuffer_GetConfig(plane->buffer, &plane->config);
	(void)OH_ImageNative_GetBufferSize(image, componentType, &plane->bufferSize);
	(void)OH_ImageNative_GetRowStride(image, componentType, &plane->rowStride);
	(void)OH_ImageNative_GetPixelStride(image, componentType, &plane->pixelStride);

	if (OH_NativeBuffer_Map(plane->buffer, &plane->mapped) != 0 || !plane->mapped)
	{
		memset(plane, 0, sizeof(*plane));
		return FALSE;
	}

	return TRUE;
}

static void cam_ohos_unmap_plane(CamOhosMappedPlane* plane)
{
	if (!plane)
		return;

	if (plane->buffer && plane->mapped)
		(void)OH_NativeBuffer_Unmap(plane->buffer);
	memset(plane, 0, sizeof(*plane));
}

static BOOL cam_ohos_validate_plane(const CamOhosMappedPlane* plane, UINT32 width, UINT32 height,
                                    int32_t defaultRowStride, int32_t defaultPixelStride,
                                    size_t dataOffset, int32_t* rowStride, int32_t* pixelStride)
{
	WINPR_ASSERT(rowStride);
	WINPR_ASSERT(pixelStride);

	if (!plane || !plane->mapped || (width == 0) || (height == 0))
		return FALSE;

	*pixelStride = plane->pixelStride > defaultPixelStride ? plane->pixelStride : defaultPixelStride;
	*rowStride = plane->rowStride > 0 ? plane->rowStride : defaultRowStride;
	if (*pixelStride <= 0 || *rowStride <= 0)
		return FALSE;

	if (*rowStride < (int32_t)(width - 1U) * *pixelStride + 1)
		return FALSE;

	if (plane->bufferSize > 0)
	{
		const size_t required = dataOffset + (size_t)(height - 1U) * (size_t)*rowStride +
		                        (size_t)(width - 1U) * (size_t)*pixelStride + 1U;
		if (required > plane->bufferSize)
			return FALSE;
	}

	return TRUE;
}

static BOOL cam_ohos_chroma_order_uv(const CamOhosMappedPlane* plane)
{
	return plane && (plane->config.format == NATIVEBUFFER_PIXEL_FMT_YCRCB_420_SP) ? FALSE : TRUE;
}

static BOOL cam_ohos_same_native_buffer(const CamOhosMappedPlane* a, const CamOhosMappedPlane* b)
{
	return a && b && ((a->buffer == b->buffer) || (a->mapped == b->mapped));
}

static size_t cam_ohos_chroma_offset(const CamOhosMappedPlane* yPlane,
                                     const CamOhosMappedPlane* plane, int32_t yRowStride,
                                     UINT32 height, int32_t precedingRowStride,
                                     UINT32 precedingHeight)
{
	if (!cam_ohos_same_native_buffer(yPlane, plane))
		return 0;

	size_t offset = (size_t)yRowStride * (size_t)height;
	if (precedingRowStride > 0)
		offset += (size_t)precedingRowStride * (size_t)precedingHeight;
	return offset;
}

static BYTE cam_ohos_read_chroma_component(const CamOhosMappedPlane* plane, const BYTE* row,
                                           UINT32 chromaX, int32_t pixelStride,
                                           BOOL componentIsU, BOOL sharedUvBuffer, BOOL uvOrder)
{
	if (!sharedUvBuffer)
		return row[(size_t)chromaX * (size_t)pixelStride];

	const size_t base = (size_t)chromaX * (size_t)pixelStride;
	const size_t offset = componentIsU == uvOrder ? 0U : 1U;
	return row[base + offset];
}

static BOOL cam_ohos_map_yuv420(OH_ImageNative* image, CamOhosMappedPlane* yPlane,
                                CamOhosMappedPlane* uPlane, CamOhosMappedPlane* vPlane)
{
	if (!cam_ohos_map_plane(image, OHOS_IMAGE_COMPONENT_Y, yPlane))
	{
		WLog_WARN(TAG, "OHOS rdpecam YUV frame has no Y plane");
		return FALSE;
	}
	if (!cam_ohos_map_plane(image, OHOS_IMAGE_COMPONENT_U, uPlane))
	{
		WLog_WARN(TAG, "OHOS rdpecam YUV frame has no U plane");
		cam_ohos_unmap_plane(yPlane);
		return FALSE;
	}
	if (!cam_ohos_map_plane(image, OHOS_IMAGE_COMPONENT_V, vPlane))
	{
		WLog_WARN(TAG, "OHOS rdpecam YUV frame has no V plane");
		cam_ohos_unmap_plane(uPlane);
		cam_ohos_unmap_plane(yPlane);
		return FALSE;
	}
	return TRUE;
}

static BOOL cam_ohos_normalize_plane_stride(uint32_t minRowBytes, uint32_t* rowStride,
                                            uint32_t* columnStride)
{
	if (!rowStride || !columnStride)
		return FALSE;

	if ((*columnStride >= minRowBytes) && (*rowStride < minRowBytes))
	{
		const uint32_t tmp = *rowStride;
		*rowStride = *columnStride;
		*columnStride = tmp;
	}
	return (*rowStride >= minRowBytes) && (*columnStride > 0);
}

static BOOL cam_ohos_plane_bounds_valid(size_t bufferSize, uint64_t offset, uint32_t rowStride,
                                        uint32_t columnStride, UINT32 width, UINT32 height,
                                        UINT32 bytesPerPixel)
{
	if ((width == 0) || (height == 0) || (rowStride == 0) || (columnStride == 0))
		return FALSE;
	if (bufferSize == 0)
		return TRUE;

	const uint64_t lastByte = offset + ((uint64_t)height - 1U) * rowStride +
	                          ((uint64_t)width - 1U) * columnStride + bytesPerPixel;
	return lastByte <= (uint64_t)bufferSize;
}

static BOOL cam_ohos_copy_yuv420_mapplanes_to_nv12(CamOhosHal* hal, OH_ImageNative* image)
{
	OH_NativeBuffer* buffer = NULL;
	OH_NativeBuffer_Config config = { 0 };
	OH_NativeBuffer_Planes planes = { 0 };
	void* mapped = NULL;
	size_t bufferSize = 0;

	if (OH_ImageNative_GetByteBuffer(image, OHOS_IMAGE_COMPONENT_Y, &buffer) != IMAGE_SUCCESS ||
	    !buffer)
		return FALSE;

	OH_NativeBuffer_GetConfig(buffer, &config);
	(void)OH_ImageNative_GetBufferSize(image, OHOS_IMAGE_COMPONENT_Y, &bufferSize);
	if (OH_NativeBuffer_MapPlanes(buffer, &mapped, &planes) != 0 || !mapped ||
	    (planes.planeCount < 2))
		return FALSE;

	const UINT32 width = hal->mediaType.Width;
	const UINT32 height = hal->mediaType.Height;
	const UINT32 chromaWidth = (width + 1U) / 2U;
	const UINT32 chromaHeight = (height + 1U) / 2U;
	const OH_NativeBuffer_Plane yPlane = planes.planes[0];
	const OH_NativeBuffer_Plane uPlane = planes.planes[1];
	const OH_NativeBuffer_Plane vPlane = (planes.planeCount >= 3) ? planes.planes[2] : planes.planes[1];
	uint32_t yRowStride = yPlane.rowStride;
	uint32_t yColumnStride = yPlane.columnStride ? yPlane.columnStride : 1U;
	uint32_t uRowStride = uPlane.rowStride;
	uint32_t uColumnStride = uPlane.columnStride ? uPlane.columnStride : 2U;
	uint32_t vRowStride = vPlane.rowStride;
	uint32_t vColumnStride = vPlane.columnStride ? vPlane.columnStride : 2U;
	const BOOL splitChromaPlanes = planes.planeCount >= 3;
	BOOL ok = cam_ohos_normalize_plane_stride(width, &yRowStride, &yColumnStride) &&
	          cam_ohos_normalize_plane_stride(splitChromaPlanes ? chromaWidth : width,
	                                          &uRowStride, &uColumnStride) &&
	          cam_ohos_normalize_plane_stride(splitChromaPlanes ? chromaWidth : width,
	                                          &vRowStride, &vColumnStride) &&
	          cam_ohos_plane_bounds_valid(bufferSize, yPlane.offset, yRowStride, yColumnStride,
	                                      width, height, 1) &&
	          cam_ohos_plane_bounds_valid(bufferSize, uPlane.offset, uRowStride, uColumnStride,
	                                      chromaWidth, chromaHeight,
	                                      splitChromaPlanes ? 1 : 2) &&
	          cam_ohos_plane_bounds_valid(bufferSize, vPlane.offset, vRowStride, vColumnStride,
	                                      chromaWidth, chromaHeight,
	                                      splitChromaPlanes ? 1 : 2);
	const BOOL sourceUvOrder =
	    (config.format == NATIVEBUFFER_PIXEL_FMT_YCRCB_420_SP) ? FALSE : TRUE;

	static BOOL loggedLayout = FALSE;
	if (!loggedLayout)
	{
		const BYTE* base = (const BYTE*)mapped;
		const BYTE* u = ok ? &base[uPlane.offset] : NULL;
		const BYTE* v = ok ? &base[vPlane.offset] : NULL;
		WLog_INFO(TAG,
		          "OHOS rdpecam MapPlanes YUV layout: size=%" PRIu32 "x%" PRIu32
		          " planes=%" PRIu32 " fmt=%d buffer=%zu Y(off=%" PRIu64
		          " row=%" PRIu32 " col=%" PRIu32 ") U(off=%" PRIu64
		          " row=%" PRIu32 " col=%" PRIu32 ") V(off=%" PRIu64
		          " row=%" PRIu32 " col=%" PRIu32 ") split=%d order=%s sample=%u,%u ok=%d",
		          width, height, planes.planeCount, config.format, bufferSize, yPlane.offset,
		          yRowStride, yColumnStride, uPlane.offset, uRowStride, uColumnStride,
		          vPlane.offset, vRowStride, vColumnStride, splitChromaPlanes ? 1 : 0,
		          sourceUvOrder ? "UV" : "VU", u ? u[0] : 0U, v ? v[0] : 0U, ok ? 1 : 0);
		loggedLayout = TRUE;
	}

	BYTE* yTarget = hal->capture.frame;
	BYTE* uvTarget = yTarget + (size_t)width * (size_t)height;
	const BYTE* base = (const BYTE*)mapped;
	for (UINT32 y = 0; ok && (y < height); y++)
	{
		const BYTE* yRow = base + yPlane.offset + (uint64_t)y * yRowStride;
		BYTE* targetRow = yTarget + (size_t)y * (size_t)width;
		for (UINT32 x = 0; x < width; x++)
			targetRow[x] = yRow[(uint64_t)x * yColumnStride];
	}

	for (UINT32 y = 0; ok && (y < chromaHeight); y++)
	{
		const BYTE* uRow = base + uPlane.offset + (uint64_t)y * uRowStride;
		const BYTE* vRow = base + vPlane.offset + (uint64_t)y * vRowStride;
		BYTE* targetRow = uvTarget + (size_t)y * (size_t)width;
		for (UINT32 x = 0; x < chromaWidth; x++)
		{
			const uint64_t uOffset = (uint64_t)x * uColumnStride;
			const uint64_t vOffset = (uint64_t)x * vColumnStride;
			if (splitChromaPlanes)
			{
				targetRow[(size_t)x * 2U] = uRow[uOffset];
				targetRow[(size_t)x * 2U + 1U] = vRow[vOffset];
			}
			else
			{
				targetRow[(size_t)x * 2U] = uRow[uOffset + (sourceUvOrder ? 0U : 1U)];
				targetRow[(size_t)x * 2U + 1U] = uRow[uOffset + (sourceUvOrder ? 1U : 0U)];
			}
		}
	}

	(void)OH_NativeBuffer_Unmap(buffer);
	return ok;
}

static void cam_ohos_unmap_yuv420(CamOhosMappedPlane* yPlane, CamOhosMappedPlane* uPlane,
                                  CamOhosMappedPlane* vPlane)
{
	cam_ohos_unmap_plane(vPlane);
	cam_ohos_unmap_plane(uPlane);
	cam_ohos_unmap_plane(yPlane);
}

static BOOL cam_ohos_copy_yuv420_to_nv12(CamOhosHal* hal, OH_ImageNative* image)
{
	static BOOL loggedFailure = FALSE;

	if (cam_ohos_copy_yuv420_mapplanes_to_nv12(hal, image))
		return TRUE;

	if (!loggedFailure)
	{
		WLog_WARN(TAG, "OHOS rdpecam MapPlanes YUV_420_SP to NV12 copy failed");
		loggedFailure = TRUE;
	}
	return FALSE;
}

static BOOL cam_ohos_copy_yuv420_to_rgb32(CamOhosHal* hal, OH_ImageNative* image)
{
	const UINT32 width = hal->mediaType.Width;
	const UINT32 height = hal->mediaType.Height;
	const UINT32 chromaWidth = (width + 1U) / 2U;
	const UINT32 chromaHeight = (height + 1U) / 2U;
	CamOhosMappedPlane yPlane;
	CamOhosMappedPlane uPlane;
	CamOhosMappedPlane vPlane;

	if (!cam_ohos_map_yuv420(image, &yPlane, &uPlane, &vPlane))
		return FALSE;

	int32_t yRowStride = 0;
	int32_t yPixelStride = 0;
	int32_t uRowStride = 0;
	int32_t uPixelStride = 0;
	int32_t vRowStride = 0;
	int32_t vPixelStride = 0;
	const BOOL sharedUvBuffer = uPlane.mapped == vPlane.mapped;
	const int32_t defaultChromaPixelStride = sharedUvBuffer ? 2 : 1;
	BOOL ok = cam_ohos_validate_plane(&yPlane, width, height, (int32_t)width, 1, 0,
	                                  &yRowStride, &yPixelStride);
	const size_t uOffset =
	    ok ? cam_ohos_chroma_offset(&yPlane, &uPlane, yRowStride, height, 0, 0) : 0;
	ok = ok && cam_ohos_validate_plane(&uPlane, chromaWidth, chromaHeight,
	                                   sharedUvBuffer ? (int32_t)width : (int32_t)chromaWidth,
	                                   defaultChromaPixelStride, uOffset, &uRowStride,
	                                   &uPixelStride);
	const size_t vOffset =
	    ok ? cam_ohos_chroma_offset(&yPlane, &vPlane, yRowStride, height,
	                                sharedUvBuffer ? 0 : uRowStride, chromaHeight)
	       : 0;
	ok = ok && cam_ohos_validate_plane(&vPlane, chromaWidth, chromaHeight,
	                                   sharedUvBuffer ? (int32_t)width : (int32_t)chromaWidth,
	                                   defaultChromaPixelStride, vOffset, &vRowStride,
	                                   &vPixelStride);
	const BOOL uvOrder = cam_ohos_chroma_order_uv(&uPlane);

	for (UINT32 y = 0; ok && (y < height); y++)
	{
		const BYTE* yRow = (const BYTE*)yPlane.mapped + (size_t)y * (size_t)yRowStride;
		const BYTE* uRow =
		    (const BYTE*)uPlane.mapped + uOffset + (size_t)(y / 2U) * (size_t)uRowStride;
		const BYTE* vRow =
		    (const BYTE*)vPlane.mapped + vOffset + (size_t)(y / 2U) * (size_t)vRowStride;
		BYTE* targetRow = &hal->capture.frame[(size_t)y * (size_t)width * 4U];

		for (UINT32 x = 0; x < width; x++)
		{
			const UINT32 chromaX = x / 2U;
			const int32_t yy = (int32_t)yRow[(size_t)x * (size_t)yPixelStride];
			const int32_t uu =
			    (int32_t)cam_ohos_read_chroma_component(&uPlane, uRow, chromaX, uPixelStride,
			                                            TRUE, sharedUvBuffer, uvOrder) -
			    128;
			const int32_t vv =
			    (int32_t)cam_ohos_read_chroma_component(&vPlane, vRow, chromaX, vPixelStride,
			                                            FALSE, sharedUvBuffer, uvOrder) -
			    128;
			BYTE* target = targetRow + (size_t)x * 4U;
			target[0] = cam_ohos_clip_color(yy + ((454 * uu) >> 8));
			target[1] = cam_ohos_clip_color(yy - ((88 * uu + 183 * vv) >> 8));
			target[2] = cam_ohos_clip_color(yy + ((359 * vv) >> 8));
			target[3] = 0xFF;
		}
	}

	cam_ohos_unmap_yuv420(&yPlane, &uPlane, &vPlane);
	return ok;
}

static BOOL cam_ohos_copy_native_buffer(CamOhosHal* hal, OH_ImageNative* image,
                                        OH_NativeBuffer* buffer, uint32_t componentType)
{
	OH_NativeBuffer_Config config = { 0 };
	OH_NativeBuffer_GetConfig(buffer, &config);

	void* mapped = NULL;
	if (OH_NativeBuffer_Map(buffer, &mapped) != 0 || !mapped)
		return FALSE;

	size_t bufferSize = 0;
	int32_t rowStride = 0;
	int32_t pixelStride = 0;
	(void)OH_ImageNative_GetBufferSize(image, componentType, &bufferSize);
	(void)OH_ImageNative_GetRowStride(image, componentType, &rowStride);
	(void)OH_ImageNative_GetPixelStride(image, componentType, &pixelStride);

	const UINT32 width = hal->mediaType.Width;
	const UINT32 height = hal->mediaType.Height;
	const int32_t sourcePixelStride = pixelStride > 0 ? pixelStride : 4;
	int32_t sourceRowStride = rowStride > 0 ? rowStride : config.stride;
	if (sourceRowStride <= 0)
		sourceRowStride = (int32_t)width * sourcePixelStride;

	BOOL ok = TRUE;
	if ((sourcePixelStride < 3) || (sourceRowStride < (int32_t)width * sourcePixelStride) ||
	    (config.width < (int32_t)width) || (config.height < (int32_t)height))
		ok = FALSE;

	if (ok && (bufferSize > 0))
	{
		const size_t required = ((size_t)height - 1U) * (size_t)sourceRowStride +
		                        (size_t)width * (size_t)sourcePixelStride;
		ok = required <= bufferSize;
	}

	const BOOL rgbaOrder = (config.format == NATIVEBUFFER_PIXEL_FMT_RGBA_8888) ||
	                       (config.format == NATIVEBUFFER_PIXEL_FMT_RGBX_8888);
	const BOOL bgraOrder = (config.format == NATIVEBUFFER_PIXEL_FMT_BGRA_8888) ||
	                       (config.format == NATIVEBUFFER_PIXEL_FMT_BGRX_8888);
	if (!rgbaOrder && !bgraOrder)
		ok = FALSE;

	for (UINT32 y = 0; ok && (y < height); y++)
	{
		const BYTE* sourceRow = (const BYTE*)mapped + (size_t)y * (size_t)sourceRowStride;
		BYTE* targetRow = &hal->capture.frame[(size_t)y * (size_t)width * 4U];
		for (UINT32 x = 0; x < width; x++)
		{
			const BYTE* source = sourceRow + (size_t)x * (size_t)sourcePixelStride;
			BYTE* target = targetRow + (size_t)x * 4U;
			target[0] = rgbaOrder ? source[2] : source[0];
			target[1] = source[1];
			target[2] = rgbaOrder ? source[0] : source[2];
			target[3] = ((rgbaOrder && (config.format == NATIVEBUFFER_PIXEL_FMT_RGBX_8888)) ||
			             (bgraOrder && (config.format == NATIVEBUFFER_PIXEL_FMT_BGRX_8888)))
			                ? 0xFF
			                : source[3];
		}
	}

	(void)OH_NativeBuffer_Unmap(buffer);
	return ok;
}

BOOL cam_ohos_read_latest_frame(CamOhosHal* hal)
{
	OH_ImageNative* image = NULL;
	if (OH_ImageReceiverNative_ReadLatestImage(hal->capture.receiver, &image) != IMAGE_SUCCESS ||
	    !image)
		return FALSE;

	if ((hal->capture.profile.format == CAMERA_FORMAT_YUV_420_SP) &&
	    (hal->mediaType.Format == CAM_MEDIA_FORMAT_NV12))
	{
		const BOOL copied = cam_ohos_copy_yuv420_to_nv12(hal, image);
		(void)OH_ImageNative_Release(image);
		return copied;
	}

	if (hal->capture.profile.format == CAMERA_FORMAT_YUV_420_SP)
	{
		const BOOL copied = cam_ohos_copy_yuv420_to_rgb32(hal, image);
		(void)OH_ImageNative_Release(image);
		return copied;
	}

	BOOL copied = FALSE;
	uint32_t* types = NULL;
	size_t typeSize = 0;
	if (OH_ImageNative_GetComponentTypes(image, &types, &typeSize) == IMAGE_SUCCESS)
	{
		for (size_t index = 0; index < typeSize; index++)
		{
			OH_NativeBuffer* buffer = NULL;
			if (OH_ImageNative_GetByteBuffer(image, types[index], &buffer) != IMAGE_SUCCESS || !buffer)
				continue;
			if (cam_ohos_copy_native_buffer(hal, image, buffer, types[index]))
			{
				copied = TRUE;
				break;
			}
		}
	}

	(void)OH_ImageNative_Release(image);
	return copied;
}
