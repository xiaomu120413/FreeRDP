/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS cliprdr <-> Pasteboard bridge
 */

#include "ohos_clipboard_internal.h"

FREERDP_LOCAL size_t ohos_clipboard_safe_strlen(const char* value)
{
	return value ? strlen(value) : 0;
}

FREERDP_LOCAL char* ohos_clipboard_strdup(const char* value)
{
	if (!value)
		return NULL;

	const size_t length = strlen(value);
	char* copy = (char*)calloc(length + 1U, sizeof(char));
	if (!copy)
		return NULL;
	memcpy(copy, value, length);
	return copy;
}

FREERDP_LOCAL BOOL ohos_clipboard_equals_ignore_case(const char* left, const char* right)
{
	if (!left || !right)
		return FALSE;
	while (*left && *right)
	{
		if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
			return FALSE;
		left++;
		right++;
	}
	return (*left == '\0') && (*right == '\0');
}

FREERDP_LOCAL BOOL ohos_clipboard_starts_with_ignore_case(const char* value, const char* prefix)
{
	if (!value || !prefix)
		return FALSE;
	while (*prefix)
	{
		if (*value == '\0')
			return FALSE;
		if (tolower((unsigned char)*value) != tolower((unsigned char)*prefix))
			return FALSE;
		value++;
		prefix++;
	}
	return TRUE;
}

FREERDP_LOCAL BOOL ohos_clipboard_memory_equals_ignore_case(const char* left, const char* right,
                                                     size_t length)
{
	if (!left || !right)
		return FALSE;
	for (size_t index = 0; index < length; index++)
	{
		if (tolower((unsigned char)left[index]) != tolower((unsigned char)right[index]))
			return FALSE;
	}
	return TRUE;
}

FREERDP_LOCAL BOOL ohos_clipboard_ends_with_ignore_case(const char* value, const char* suffix)
{
	if (!value || !suffix)
		return FALSE;

	const size_t valueLength = strlen(value);
	const size_t suffixLength = strlen(suffix);
	if (valueLength < suffixLength)
		return FALSE;
	return ohos_clipboard_memory_equals_ignore_case(value + valueLength - suffixLength, suffix,
	                                               suffixLength);
}

FREERDP_LOCAL BOOL ohos_clipboard_is_uri_text(const char* value)
{
	if (!value || value[0] == '\0')
		return FALSE;
	return ohos_clipboard_starts_with_ignore_case(value, "http://") ||
	       ohos_clipboard_starts_with_ignore_case(value, "https://") ||
	       ohos_clipboard_starts_with_ignore_case(value, "file://") ||
	       ohos_clipboard_starts_with_ignore_case(value, "content://") ||
	       ohos_clipboard_starts_with_ignore_case(value, "datashare://");
}

FREERDP_LOCAL int ohos_clipboard_hex_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

FREERDP_LOCAL char* ohos_clipboard_percent_decode(const char* value)
{
	if (!value)
		return NULL;

	const size_t length = strlen(value);
	char* decoded = (char*)calloc(length + 1U, sizeof(char));
	if (!decoded)
		return NULL;

	size_t out = 0;
	for (size_t in = 0; in < length; in++)
	{
		if (value[in] == '%' && (in + 2U) < length)
		{
			const int high = ohos_clipboard_hex_value(value[in + 1U]);
			const int low = ohos_clipboard_hex_value(value[in + 2U]);
			if (high >= 0 && low >= 0)
			{
				decoded[out++] = (char)((high << 4) | low);
				in += 2U;
				continue;
			}
		}
		decoded[out++] = value[in];
	}
	decoded[out] = '\0';
	return decoded;
}

FREERDP_LOCAL char* ohos_clipboard_uri_to_local_path(const char* uri)
{
	if (!uri || uri[0] == '\0')
		return NULL;

	if (ohos_clipboard_starts_with_ignore_case(uri, "file://"))
	{
		const size_t uriLength = strlen(uri);
		if (uriLength <= UINT_MAX)
		{
			char* path = NULL;
			const int rc = OH_FileUri_GetPathFromUri(uri, (unsigned int)uriLength, &path);
			if (rc == 0 && path && path[0] != '\0')
				return path;
			free(path);
		}
	}

	BOOL prefixSlash = FALSE;
	const char* path = uri;
	if (ohos_clipboard_starts_with_ignore_case(uri, "file://"))
	{
		path = uri + 7U;
		if (ohos_clipboard_starts_with_ignore_case(path, "localhost/") ||
		    ohos_clipboard_starts_with_ignore_case(path, "localhost\\"))
		{
			path += strlen("localhost");
		}
		else if (path[0] != '/' && path[0] != '\\')
		{
			prefixSlash = TRUE;
		}
	}
	else if (uri[0] != '/')
	{
		return NULL;
	}

	if (path[0] == '\0')
		return NULL;

	char* decoded = ohos_clipboard_percent_decode(path);
	if (!decoded)
		return NULL;

	if (!prefixSlash)
		return decoded;

	const size_t length = strlen(decoded);
	char* absolute = (char*)calloc(length + 2U, sizeof(char));
	if (!absolute)
	{
		free(decoded);
		return NULL;
	}
	absolute[0] = '/';
	memcpy(absolute + 1U, decoded, length + 1U);
	free(decoded);
	return absolute;
}

FREERDP_LOCAL const char* ohos_clipboard_uri_kind(const char* uri)
{
	if (!uri || uri[0] == '\0')
		return "none";
	if (ohos_clipboard_starts_with_ignore_case(uri, "file://"))
		return "file";
	if (ohos_clipboard_starts_with_ignore_case(uri, "content://"))
		return "content";
	if (ohos_clipboard_starts_with_ignore_case(uri, "datashare://"))
		return "datashare";
	if (ohos_clipboard_starts_with_ignore_case(uri, "http://") ||
	    ohos_clipboard_starts_with_ignore_case(uri, "https://"))
		return "web";
	if (uri[0] == '/')
		return "path";
	return "other";
}

FREERDP_LOCAL BOOL ohos_clipboard_image_signature_supported(const BYTE* data, UINT32 size)
{
	if (!data || size < 2U)
		return FALSE;

	if (data[0] == 'B' && data[1] == 'M')
		return TRUE;
	if (size >= 8U && data[0] == 0x89U && data[1] == 'P' && data[2] == 'N' &&
	    data[3] == 'G' && data[4] == '\r' && data[5] == '\n' && data[6] == 0x1AU &&
	    data[7] == '\n')
		return TRUE;
	if (size >= 3U && data[0] == 0xFFU && data[1] == 0xD8U && data[2] == 0xFFU)
		return TRUE;
	if (size >= 12U && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' &&
	    data[3] == 'F' && data[8] == 'W' && data[9] == 'E' && data[10] == 'B' &&
	    data[11] == 'P')
		return TRUE;
	return FALSE;
}

FREERDP_LOCAL UINT32 ohos_clipboard_image_format_id_from_signature(const BYTE* data, UINT32 size)
{
	if (!data || size < 3U)
		return 0;
	if (size >= 8U && data[0] == 0x89U && data[1] == 'P' && data[2] == 'N' &&
	    data[3] == 'G' && data[4] == '\r' && data[5] == '\n' && data[6] == 0x1AU &&
	    data[7] == '\n')
		return OHOS_CLIPBOARD_FORMAT_IMAGE_PNG;
	if (data[0] == 0xFFU && data[1] == 0xD8U && data[2] == 0xFFU)
		return OHOS_CLIPBOARD_FORMAT_IMAGE_JPEG;
	if (size >= 12U && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' &&
	    data[3] == 'F' && data[8] == 'W' && data[9] == 'E' && data[10] == 'B' &&
	    data[11] == 'P')
		return OHOS_CLIPBOARD_FORMAT_IMAGE_WEBP;
	return 0;
}

FREERDP_LOCAL const char* ohos_clipboard_image_format_name_from_id(UINT32 formatId)
{
	if (formatId == OHOS_CLIPBOARD_FORMAT_IMAGE_PNG)
		return OHOS_CLIPBOARD_IMAGE_PNG_FORMAT_NAME;
	if (formatId == OHOS_CLIPBOARD_FORMAT_IMAGE_JPEG)
		return OHOS_CLIPBOARD_IMAGE_JPEG_FORMAT_NAME;
	if (formatId == OHOS_CLIPBOARD_FORMAT_IMAGE_WEBP)
		return OHOS_CLIPBOARD_IMAGE_WEBP_FORMAT_NAME;
	return NULL;
}

FREERDP_LOCAL const char* ohos_clipboard_image_extension_from_id(UINT32 formatId)
{
	if (formatId == OHOS_CLIPBOARD_FORMAT_IMAGE_PNG)
		return ".png";
	if (formatId == OHOS_CLIPBOARD_FORMAT_IMAGE_JPEG)
		return ".jpg";
	if (formatId == OHOS_CLIPBOARD_FORMAT_IMAGE_WEBP)
		return ".webp";
	return ".img";
}

FREERDP_LOCAL UINT32 ohos_clipboard_image_format_id_from_uri(const char* uri)
{
	if (!uri)
		return 0;

	char* path = ohos_clipboard_uri_to_local_path(uri);
	if (!path)
		return 0;

	BYTE signature[OHOS_CLIPBOARD_IMAGE_SIGNATURE_BYTES] = { 0 };
	UINT32 size = 0;
	const BOOL read = ohos_clipboard_read_local_file_signature(
	    path, signature, sizeof(signature), &size);
	free(path);
	if (!read)
		return 0;
	return ohos_clipboard_image_format_id_from_signature(signature, size);
}

FREERDP_LOCAL BOOL ohos_clipboard_uri_has_image_suffix(const char* uri)
{
	if (!uri)
		return FALSE;

	const char* end = uri + strlen(uri);
	for (const char* cursor = uri; *cursor; cursor++)
	{
		if (*cursor == '?' || *cursor == '#')
		{
			end = cursor;
			break;
		}
	}

	static const char* suffixes[] = {
		".bmp", ".gif", ".heic", ".heif", ".jpeg", ".jpg", ".png", ".webp"
	};
	for (size_t index = 0; index < ARRAYSIZE(suffixes); index++)
	{
		const char* suffix = suffixes[index];
		const size_t suffixLength = strlen(suffix);
		const size_t uriLength = (size_t)(end - uri);
		if (uriLength >= suffixLength &&
		    ohos_clipboard_memory_equals_ignore_case(end - suffixLength, suffix, suffixLength))
			return TRUE;
	}
	return FALSE;
}

FREERDP_LOCAL char* ohos_clipboard_wchar_to_utf8(const WCHAR* value, size_t maxChars)
{
	if (!value || maxChars == 0U)
		return NULL;

	size_t length = 0;
	while (length < maxChars && value[length] != 0)
		length++;
	if (length == 0U)
		return ohos_clipboard_strdup("");

	return ConvertWCharNToUtf8Alloc(value, length, NULL);
}

FREERDP_LOCAL UINT16 ohos_clipboard_read_le16(const BYTE* data)
{
	return (UINT16)data[0] | ((UINT16)data[1] << 8U);
}

FREERDP_LOCAL UINT32 ohos_clipboard_read_le32(const BYTE* data)
{
	return (UINT32)data[0] | ((UINT32)data[1] << 8U) | ((UINT32)data[2] << 16U) |
	       ((UINT32)data[3] << 24U);
}

FREERDP_LOCAL BYTE* ohos_clipboard_dib_to_bmp(const BYTE* data, UINT32 size, UINT32* outSize,
                                       char* errorBuffer, size_t errorBufferSize)
{
	if (!data || !outSize)
		return NULL;
	*outSize = 0;

	if (size < sizeof(WINPR_BITMAP_INFO_HEADER))
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize, "DIB data is too small");
		return NULL;
	}

	const UINT32 headerSize = ohos_clipboard_read_le32(data);
	if (headerSize < sizeof(WINPR_BITMAP_INFO_HEADER) || headerSize > size)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "unsupported DIB header size=%" PRIu32, headerSize);
		return NULL;
	}

	const UINT16 bitsPerPixel = ohos_clipboard_read_le16(data + 14U);
	const UINT32 compression = ohos_clipboard_read_le32(data + 16U);
	if ((bitsPerPixel != 24U && bitsPerPixel != 32U) ||
	    (compression != BI_RGB && compression != BI_BITFIELDS))
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "unsupported DIB bpp=%" PRIu16 " compression=%" PRIu32,
		                         bitsPerPixel, compression);
		return NULL;
	}

	if (compression == BI_BITFIELDS && headerSize == sizeof(WINPR_BITMAP_INFO_HEADER))
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "unsupported DIB bitfield masks outside the info header");
		return NULL;
	}

	const size_t total = sizeof(WINPR_BITMAP_FILE_HEADER) + (size_t)size;
	if (total > UINT32_MAX)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize, "DIB data is too large");
		return NULL;
	}

	BYTE* bmp = (BYTE*)calloc(total, sizeof(BYTE));
	if (!bmp)
		return NULL;

	WINPR_BITMAP_FILE_HEADER header = { 0 };
	header.bfType[0] = 'B';
	header.bfType[1] = 'M';
	header.bfSize = (UINT32)total;
	header.bfOffBits = (UINT32)(sizeof(WINPR_BITMAP_FILE_HEADER) + headerSize);
	memcpy(bmp, &header, sizeof(header));
	memcpy(bmp + sizeof(WINPR_BITMAP_FILE_HEADER), data, size);
	*outSize = (UINT32)total;
	return bmp;
}

FREERDP_LOCAL BYTE* ohos_clipboard_bgra_to_dib(const BYTE* bgra, UINT32 width, UINT32 height,
                                        UINT32* outSize, char* errorBuffer,
                                        size_t errorBufferSize)
{
	if (!bgra || !outSize)
		return NULL;
	*outSize = 0;

	if (width == 0U || height == 0U || width > (UINT32)INT32_MAX ||
	    height > (UINT32)INT32_MAX)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "invalid DIB size=%" PRIu32 "x%" PRIu32, width, height);
		return NULL;
	}

	const size_t rowBytes = (size_t)width * 4U;
	const size_t imageBytes = rowBytes * (size_t)height;
	if (rowBytes / 4U != width || imageBytes / rowBytes != height ||
	    imageBytes > UINT32_MAX || imageBytes > SIZE_MAX - sizeof(WINPR_BITMAP_INFO_HEADER))
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize, "DIB data is too large");
		return NULL;
	}

	const size_t total = sizeof(WINPR_BITMAP_INFO_HEADER) + imageBytes;
	BYTE* dib = (BYTE*)calloc(total, sizeof(BYTE));
	if (!dib)
		return NULL;

	WINPR_BITMAP_INFO_HEADER header = { 0 };
	header.biSize = sizeof(WINPR_BITMAP_INFO_HEADER);
	header.biWidth = (INT32)width;
	header.biHeight = (INT32)height;
	header.biPlanes = 1;
	header.biBitCount = 32;
	header.biCompression = BI_RGB;
	header.biSizeImage = (UINT32)imageBytes;
	memcpy(dib, &header, sizeof(header));

	BYTE* pixels = dib + sizeof(WINPR_BITMAP_INFO_HEADER);
	for (UINT32 y = 0; y < height; y++)
	{
		const BYTE* src = bgra + ((size_t)(height - 1U - y) * rowBytes);
		BYTE* dst = pixels + ((size_t)y * rowBytes);
		memcpy(dst, src, rowBytes);
	}

	*outSize = (UINT32)total;
	return dib;
}

FREERDP_LOCAL BYTE* ohos_clipboard_bgra_to_dibv5(const BYTE* bgra, UINT32 width, UINT32 height,
                                          UINT32* outSize, char* errorBuffer,
                                          size_t errorBufferSize)
{
	if (!bgra || !outSize)
		return NULL;
	*outSize = 0;

	if (width == 0U || height == 0U || width > (UINT32)INT32_MAX ||
	    height > (UINT32)INT32_MAX)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "invalid DIBV5 size=%" PRIu32 "x%" PRIu32, width,
		                         height);
		return NULL;
	}

	const size_t rowBytes = (size_t)width * 4U;
	const size_t imageBytes = rowBytes * (size_t)height;
	if (rowBytes / 4U != width || imageBytes / rowBytes != height ||
	    imageBytes > UINT32_MAX || imageBytes > SIZE_MAX - sizeof(BITMAPV5HEADER))
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize, "DIBV5 data is too large");
		return NULL;
	}

	const size_t total = sizeof(BITMAPV5HEADER) + imageBytes;
	BYTE* dibv5 = (BYTE*)calloc(total, sizeof(BYTE));
	if (!dibv5)
		return NULL;

	BITMAPV5HEADER header = { 0 };
	header.bV5Size = sizeof(BITMAPV5HEADER);
	header.bV5Width = (LONG)width;
	header.bV5Height = (LONG)height;
	header.bV5Planes = 1;
	header.bV5BitCount = 32;
	header.bV5Compression = BI_BITFIELDS;
	header.bV5SizeImage = (DWORD)imageBytes;
	header.bV5RedMask = 0x00FF0000U;
	header.bV5GreenMask = 0x0000FF00U;
	header.bV5BlueMask = 0x000000FFU;
	header.bV5AlphaMask = 0xFF000000U;
	header.bV5CSType = OHOS_CLIPBOARD_LCS_SRGB;
	header.bV5Intent = OHOS_CLIPBOARD_LCS_GM_IMAGES;
	memcpy(dibv5, &header, sizeof(header));

	BYTE* pixels = dibv5 + sizeof(BITMAPV5HEADER);
	for (UINT32 y = 0; y < height; y++)
	{
		const BYTE* src = bgra + ((size_t)(height - 1U - y) * rowBytes);
		BYTE* dst = pixels + ((size_t)y * rowBytes);
		memcpy(dst, src, rowBytes);
	}

	*outSize = (UINT32)total;
	return dibv5;
}

FREERDP_LOCAL char* ohos_clipboard_local_image_file_name_from_path(const char* path, UINT32 formatId)
{
	char* safeName = ohos_clipboard_sanitize_cache_file_name(path);
	if (!safeName)
		return NULL;
	const char* extension = ohos_clipboard_image_extension_from_id(formatId);
	if (ohos_clipboard_ends_with_ignore_case(safeName, extension))
		return safeName;

	size_t stemLength = strlen(safeName);
	char* dot = strrchr(safeName, '.');
	if (dot && dot != safeName && ohos_clipboard_uri_has_image_suffix(safeName))
		stemLength = (size_t)(dot - safeName);

	const int length = snprintf(NULL, 0, "%.*s%s", (int)stemLength, safeName, extension);
	if (length <= 0)
	{
		free(safeName);
		return NULL;
	}

	char* withExtension = (char*)calloc((size_t)length + 1U, sizeof(char));
	if (!withExtension)
	{
		free(safeName);
		return NULL;
	}
	(void)snprintf(withExtension, (size_t)length + 1U, "%.*s%s", (int)stemLength, safeName,
	               extension);
	free(safeName);
	return withExtension;
}

FREERDP_LOCAL char* ohos_clipboard_extract_uri_list_first(const BYTE* data, UINT32 size)
{
	char* source = ohos_clipboard_bytes_to_string(data, size);
	if (!source)
		return NULL;

	char* cursor = source;
	while (*cursor)
	{
		while (*cursor == '\r' || *cursor == '\n')
			cursor++;
		char* line = cursor;
		while (*cursor && *cursor != '\r' && *cursor != '\n')
			cursor++;
		const char saved = *cursor;
		*cursor = '\0';
		if (line[0] != '#' && line[0] != '\0')
		{
			char* uri = ohos_clipboard_strdup(line);
			free(source);
			return uri;
		}
		if (saved == '\0')
			break;
		cursor++;
	}

	free(source);
	return NULL;
}

FREERDP_LOCAL BYTE* ohos_clipboard_uri_to_uri_list(const char* uri, UINT32* outSize)
{
	if (!uri || !outSize)
		return NULL;
	*outSize = 0;

	const size_t uriLen = strlen(uri);
	const size_t total = uriLen + 3U;
	BYTE* output = (BYTE*)calloc(total, sizeof(BYTE));
	if (!output)
		return NULL;
	memcpy(output, uri, uriLen);
	output[uriLen] = '\r';
	output[uriLen + 1U] = '\n';
	*outSize = (UINT32)total;
	return output;
}
