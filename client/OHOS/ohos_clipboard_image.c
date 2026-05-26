/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS cliprdr <-> Pasteboard bridge
 */

#include "ohos_clipboard_internal.h"

FREERDP_LOCAL BOOL ohos_clipboard_uri_may_reference_local_image(const char* uri)
{
	const char* kind = ohos_clipboard_uri_kind(uri);
	if (ohos_clipboard_equals_ignore_case(kind, "content") ||
	    ohos_clipboard_equals_ignore_case(kind, "datashare"))
		return TRUE;
	if (ohos_clipboard_uri_has_image_suffix(uri))
		return TRUE;

	char* path = ohos_clipboard_uri_to_local_path(uri);
	if (!path)
		return FALSE;

	BYTE signature[OHOS_CLIPBOARD_IMAGE_SIGNATURE_BYTES] = { 0 };
	UINT32 size = 0;
	const BOOL read = ohos_clipboard_read_local_file_signature(
	    path, signature, sizeof(signature), &size);
	free(path);
	if (!read)
		return FALSE;

	const BOOL supported = ohos_clipboard_image_signature_supported(signature, size);
	return supported;
}

FREERDP_LOCAL BOOL ohos_clipboard_decoded_image_to_bgra(const wImage* image, BYTE** outBgra,
                                                 UINT32* outWidth, UINT32* outHeight,
                                                 char* errorBuffer, size_t errorBufferSize)
{
	BYTE* bgra = NULL;

	if (!outBgra || !outWidth || !outHeight)
		return FALSE;
	*outBgra = NULL;
	*outWidth = 0;
	*outHeight = 0;

	if (!image || (image->bitsPerPixel != 24U && image->bitsPerPixel != 32U) ||
	    image->width == 0U || image->height == 0U)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "unsupported image output bpp=%" PRIu32 " size=%" PRIu32
		                         "x%" PRIu32,
		                         image ? image->bitsPerPixel : 0U, image ? image->width : 0U,
		                         image ? image->height : 0U);
		return FALSE;
	}

	const size_t pixelCount = (size_t)image->width * (size_t)image->height;
	const size_t bgraSize = pixelCount * 4U;
	if (image->width > UINT32_MAX / 4U || (pixelCount != 0U && bgraSize / 4U != pixelCount) ||
	    bgraSize > UINT32_MAX)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize, "image output is too large");
		return FALSE;
	}

	bgra = (BYTE*)malloc(bgraSize);
	if (!bgra)
		return FALSE;

	const UINT32 srcBytesPerPixel = image->bitsPerPixel / 8U;
	BOOL hasAlpha = FALSE;
	for (UINT32 y = 0; y < image->height; y++)
	{
		const BYTE* src = image->data + ((size_t)y * image->scanline);
		BYTE* dst = bgra + ((size_t)y * image->width * 4U);
		for (UINT32 x = 0; x < image->width; x++)
		{
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
			if (srcBytesPerPixel == 4U)
			{
				dst[3] = src[3];
				hasAlpha = hasAlpha || (src[3] != 0U);
			}
			else
			{
				dst[3] = 0xFFU;
			}
			src += srcBytesPerPixel;
			dst += 4U;
		}
	}

	if (srcBytesPerPixel == 4U && !hasAlpha)
	{
		for (size_t index = 3U; index < bgraSize; index += 4U)
			bgra[index] = 0xFFU;
	}

	*outBgra = bgra;
	*outWidth = image->width;
	*outHeight = image->height;
	return TRUE;
}

FREERDP_LOCAL BOOL ohos_clipboard_ohos_image_source_to_bgra(OH_ImageSourceNative* source,
                                                     BYTE** outBgra, UINT32* outWidth,
                                                     UINT32* outHeight, char* errorBuffer,
                                                     size_t errorBufferSize)
{
	OH_DecodingOptions* options = NULL;
	OH_PixelmapNative* pixelmap = NULL;
	Image_ErrorCode imageRc = OH_DecodingOptions_Create(&options);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_DecodingOptions_SetPixelFormat(options, PIXEL_FORMAT_BGRA_8888);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_ImageSourceNative_CreatePixelmap(source, options, &pixelmap);
	if (imageRc != IMAGE_SUCCESS || !pixelmap)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "ImageSource create PixelMap failed: %d", imageRc);
		if (options)
			OH_DecodingOptions_Release(options);
		return FALSE;
	}

	const BOOL ok = ohos_clipboard_pixelmap_native_to_bgra(pixelmap, outBgra, outWidth,
	                                                       outHeight, errorBuffer,
	                                                       errorBufferSize);
	OH_PixelmapNative_Release(pixelmap);
	if (options)
		OH_DecodingOptions_Release(options);
	return ok;
}

FREERDP_LOCAL BOOL ohos_clipboard_ohos_image_buffer_to_bgra(const BYTE* data, UINT32 size,
                                                     BYTE** outBgra, UINT32* outWidth,
                                                     UINT32* outHeight, char* errorBuffer,
                                                     size_t errorBufferSize)
{
	OH_ImageSourceNative* source = NULL;
	Image_ErrorCode imageRc = OH_ImageSourceNative_CreateFromData((uint8_t*)data, size, &source);
	if (imageRc != IMAGE_SUCCESS || !source)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "ImageSource create from data failed: %d", imageRc);
		return FALSE;
	}

	const BOOL ok = ohos_clipboard_ohos_image_source_to_bgra(source, outBgra, outWidth,
	                                                        outHeight, errorBuffer,
	                                                        errorBufferSize);
	OH_ImageSourceNative_Release(source);
	return ok;
}

FREERDP_LOCAL BOOL ohos_clipboard_ohos_image_uri_to_bgra(const char* uri, BYTE** outBgra,
                                                  UINT32* outWidth, UINT32* outHeight,
                                                  char* errorBuffer, size_t errorBufferSize)
{
	if (!uri || uri[0] == '\0')
		return FALSE;

	OH_ImageSourceNative* source = NULL;
	const size_t uriSize = strlen(uri);
	Image_ErrorCode imageRc = OH_ImageSourceNative_CreateFromUri((char*)uri, uriSize, &source);
	if (imageRc != IMAGE_SUCCESS || !source)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "ImageSource create from URI failed: %d", imageRc);
		return FALSE;
	}

	const BOOL ok = ohos_clipboard_ohos_image_source_to_bgra(source, outBgra, outWidth,
	                                                        outHeight, errorBuffer,
	                                                        errorBufferSize);
	OH_ImageSourceNative_Release(source);
	return ok;
}

FREERDP_LOCAL BOOL ohos_clipboard_image_buffer_to_bgra(const BYTE* data, UINT32 size, BYTE** outBgra,
                                                UINT32* outWidth, UINT32* outHeight,
                                                char* errorBuffer, size_t errorBufferSize)
{
	wImage image = { 0 };

	if (!data || size == 0U)
		return FALSE;

	if (winpr_image_read_buffer(&image, data, size) < 0)
	{
		memset(&image, 0, sizeof(image));
		return ohos_clipboard_ohos_image_buffer_to_bgra(data, size, outBgra, outWidth,
		                                                outHeight, errorBuffer,
		                                                errorBufferSize);
	}

	const BOOL ok = ohos_clipboard_decoded_image_to_bgra(&image, outBgra, outWidth, outHeight,
	                                                     errorBuffer, errorBufferSize);
	free(image.data);
	return ok;
}

FREERDP_LOCAL BOOL ohos_clipboard_bitmap_to_bgra(const BYTE* data, UINT32 size,
                                          OHOS_CLIPBOARD_REQUEST_KIND kind, BYTE** outBgra,
                                          UINT32* outWidth, UINT32* outHeight,
                                          char* errorBuffer, size_t errorBufferSize)
{
	BYTE* bmp = NULL;
	UINT32 bmpSize = 0;
	BOOL ok = FALSE;

	if (!outBgra || !outWidth || !outHeight)
		return FALSE;
	*outBgra = NULL;
	*outWidth = 0;
	*outHeight = 0;

	if (kind == OHOS_CLIPBOARD_REQUEST_IMAGE_BMP)
	{
		bmp = (BYTE*)malloc(size);
		if (!bmp)
			return FALSE;
		memcpy(bmp, data, size);
		bmpSize = size;
	}
	else
	{
		bmp = ohos_clipboard_dib_to_bmp(data, size, &bmpSize, errorBuffer, errorBufferSize);
		if (!bmp)
			return FALSE;
	}

	ok = ohos_clipboard_image_buffer_to_bgra(bmp, bmpSize, outBgra, outWidth, outHeight,
	                                        errorBuffer, errorBufferSize);
	free(bmp);
	return ok;
}

FREERDP_LOCAL UINT32 ohos_clipboard_pixel_format_bytes(int32_t pixelFormat)
{
	switch (pixelFormat)
	{
		case PIXEL_FORMAT_BGRA_8888:
		case PIXEL_FORMAT_RGBA_8888:
			return 4;
		case PIXEL_FORMAT_RGB_888:
			return 3;
		case PIXEL_FORMAT_RGB_565:
			return 2;
		case PIXEL_FORMAT_ALPHA_8:
			return 1;
		default:
			return 0;
	}
}

FREERDP_LOCAL BOOL ohos_clipboard_pixelmap_native_to_bgra(OH_PixelmapNative* pixelmapNative,
                                                  BYTE** outBgra, UINT32* outWidth,
                                                  UINT32* outHeight, char* errorBuffer,
                                                  size_t errorBufferSize)
{
	Image_ErrorCode imageRc = IMAGE_SUCCESS;
	OH_Pixelmap_ImageInfo* info = NULL;
	BYTE* pixels = NULL;
	BYTE* bgra = NULL;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t rowStride = 0;
	int32_t pixelFormat = PIXEL_FORMAT_UNKNOWN;

	if (!outBgra || !outWidth || !outHeight)
		return FALSE;
	*outBgra = NULL;
	*outWidth = 0;
	*outHeight = 0;

	if (!pixelmapNative)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize, "PixelMap native is null");
		return FALSE;
	}

	imageRc = OH_PixelmapImageInfo_Create(&info);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_PixelmapNative_GetImageInfo(pixelmapNative, info);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_PixelmapImageInfo_GetWidth(info, &width);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_PixelmapImageInfo_GetHeight(info, &height);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_PixelmapImageInfo_GetRowStride(info, &rowStride);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_PixelmapImageInfo_GetPixelFormat(info, &pixelFormat);
	if (info)
		OH_PixelmapImageInfo_Release(info);
	if (imageRc != IMAGE_SUCCESS)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "PixelMap info read failed: %d", imageRc);
		return FALSE;
	}

	const UINT32 bytesPerPixel = ohos_clipboard_pixel_format_bytes(pixelFormat);
	if (width == 0U || height == 0U || bytesPerPixel == 0U)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "unsupported PixelMap format=%" PRId32 " size=%" PRIu32
		                         "x%" PRIu32,
		                         pixelFormat, width, height);
		return FALSE;
	}

	const size_t minRowBytes = (size_t)width * bytesPerPixel;
	if (minRowBytes > UINT32_MAX)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize, "PixelMap row is too large");
		return FALSE;
	}
	if (rowStride < minRowBytes)
		rowStride = (uint32_t)minRowBytes;
	const size_t pixelBytes = (size_t)rowStride * height;
	const size_t bgraBytes = (size_t)width * height * 4U;
	if (minRowBytes / bytesPerPixel != width || pixelBytes / rowStride != height ||
	    bgraBytes / 4U != ((size_t)width * height) || bgraBytes > UINT32_MAX)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize, "PixelMap data is too large");
		return FALSE;
	}

	pixels = (BYTE*)malloc(pixelBytes);
	bgra = (BYTE*)malloc(bgraBytes);
	if (!pixels || !bgra)
	{
		free(pixels);
		free(bgra);
		return FALSE;
	}

	size_t bufferSize = pixelBytes;
	imageRc = OH_PixelmapNative_ReadPixels(pixelmapNative, pixels, &bufferSize);
	if (imageRc != IMAGE_SUCCESS)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "PixelMap ReadPixels failed: %d", imageRc);
		free(pixels);
		free(bgra);
		return FALSE;
	}

	size_t readStride = rowStride;
	if (bufferSize < pixelBytes)
	{
		const size_t compactBytes = minRowBytes * (size_t)height;
		if (bufferSize >= compactBytes)
			readStride = minRowBytes;
		else
		{
			ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
			                         "PixelMap ReadPixels returned too few bytes: %zu", bufferSize);
			free(pixels);
			free(bgra);
			return FALSE;
		}
	}

	for (UINT32 y = 0; y < height; y++)
	{
		const BYTE* src = pixels + ((size_t)y * readStride);
		BYTE* dst = bgra + ((size_t)y * width * 4U);
		for (UINT32 x = 0; x < width; x++)
		{
			switch (pixelFormat)
			{
				case PIXEL_FORMAT_BGRA_8888:
					dst[0] = src[0];
					dst[1] = src[1];
					dst[2] = src[2];
					dst[3] = src[3];
					src += 4;
					break;
				case PIXEL_FORMAT_RGBA_8888:
					dst[0] = src[2];
					dst[1] = src[1];
					dst[2] = src[0];
					dst[3] = src[3];
					src += 4;
					break;
				case PIXEL_FORMAT_RGB_888:
					dst[0] = src[2];
					dst[1] = src[1];
					dst[2] = src[0];
					dst[3] = 0xFFU;
					src += 3;
					break;
				case PIXEL_FORMAT_RGB_565:
				{
					const UINT16 value = ohos_clipboard_read_le16(src);
					dst[2] = (BYTE)(((value >> 11U) & 0x1FU) * 255U / 31U);
					dst[1] = (BYTE)(((value >> 5U) & 0x3FU) * 255U / 63U);
					dst[0] = (BYTE)((value & 0x1FU) * 255U / 31U);
					dst[3] = 0xFFU;
					src += 2;
					break;
				}
				case PIXEL_FORMAT_ALPHA_8:
					dst[0] = 0;
					dst[1] = 0;
					dst[2] = 0;
					dst[3] = src[0];
					src += 1;
					break;
				default:
					break;
			}
			dst += 4;
		}
	}

	free(pixels);
	*outBgra = bgra;
	*outWidth = width;
	*outHeight = height;
	return TRUE;
}

FREERDP_LOCAL BOOL ohos_clipboard_read_uri_image_with_source(
    freerdpOhosClipboard* clipboard, BYTE** outBgra, UINT32* outWidth, UINT32* outHeight,
    BYTE** outSourceData, UINT32* outSourceSize, UINT32* outSourceFormatId, char* errorBuffer,
    size_t errorBufferSize)
{
	char* uri = NULL;
	char* path = NULL;
	BYTE* imageData = NULL;
	UINT32 imageDataSize = 0;
	UINT32 imageFormatId = 0;
	BOOL ok = FALSE;

	if (!outBgra || !outWidth || !outHeight)
		return FALSE;
	*outBgra = NULL;
	*outWidth = 0;
	*outHeight = 0;
	if (outSourceData)
		*outSourceData = NULL;
	if (outSourceSize)
		*outSourceSize = 0;
	if (outSourceFormatId)
		*outSourceFormatId = 0;

	if (!ohos_clipboard_read_uri(clipboard, &uri, errorBuffer, errorBufferSize))
		return FALSE;

	path = ohos_clipboard_uri_to_local_path(uri);
	if (path)
	{
		imageData = ohos_clipboard_read_local_file(path, OHOS_CLIPBOARD_MAX_URI_IMAGE_BYTES,
		                                           &imageDataSize, errorBuffer, errorBufferSize);
		if (imageData && ohos_clipboard_image_signature_supported(imageData, imageDataSize))
		{
			imageFormatId = ohos_clipboard_image_format_id_from_signature(imageData, imageDataSize);
			ok = ohos_clipboard_image_buffer_to_bgra(imageData, imageDataSize, outBgra, outWidth,
			                                        outHeight, errorBuffer, errorBufferSize);
			if (ok)
			{
				clipboard->lastImageBytes = imageDataSize;
				if (outSourceData && outSourceSize && outSourceFormatId && imageFormatId != 0U)
				{
					*outSourceData = imageData;
					*outSourceSize = imageDataSize;
					*outSourceFormatId = imageFormatId;
					imageData = NULL;
				}
				ohos_clipboard_log(
				    clipboard,
				    "HarmonyOS Pasteboard image URI decoded for cliprdr: %" PRIu32
				    "x%" PRIu32 " sourceBytes=%" PRIu32 " sourceFormat=%s kind=%s",
				    *outWidth, *outHeight, imageDataSize,
				    ohos_clipboard_image_format_name_from_id(imageFormatId)
				        ? ohos_clipboard_image_format_name_from_id(imageFormatId)
				        : "none",
				    ohos_clipboard_uri_kind(uri));
				goto fail;
			}
		}
	}

	if (ohos_clipboard_ohos_image_uri_to_bgra(uri, outBgra, outWidth, outHeight, errorBuffer,
	                                          errorBufferSize))
	{
		ok = TRUE;
		clipboard->lastImageBytes = 0;
		ohos_clipboard_log(clipboard,
		                   "HarmonyOS Pasteboard image URI decoded for cliprdr: %" PRIu32
		                   "x%" PRIu32 " sourceBytes=uri kind=%s",
		                   *outWidth, *outHeight, ohos_clipboard_uri_kind(uri));
		goto fail;
	}

	if (errorBuffer && errorBuffer[0] == '\0')
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "clipboard URI is not a readable local image: kind=%s",
		                         ohos_clipboard_uri_kind(uri));

fail:
	free(imageData);
	free(path);
	free(uri);
	return ok;
}

FREERDP_LOCAL BOOL ohos_clipboard_validate_local_image_size(UINT32 width, UINT32 height,
                                                     char* errorBuffer,
                                                     size_t errorBufferSize)
{
	if (width == 0U || height == 0U)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "local clipboard image has invalid size=%" PRIu32
		                         "x%" PRIu32,
		                         width, height);
		return FALSE;
	}

	const size_t pixels = (size_t)width * (size_t)height;
	const size_t bytes = pixels * 4U;
	if (pixels / (size_t)width != (size_t)height || bytes / 4U != pixels ||
	    pixels > OHOS_CLIPBOARD_MAX_LOCAL_IMAGE_PIXELS ||
	    bytes > OHOS_CLIPBOARD_MAX_LOCAL_IMAGE_BYTES)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "local clipboard image too large: %" PRIu32 "x%" PRIu32,
		                         width, height);
		return FALSE;
	}

	return TRUE;
}
