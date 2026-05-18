/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS cliprdr <-> Pasteboard bridge
 */

#include <freerdp/config.h>

#include "ohos_clipboard.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <database/pasteboard/oh_pasteboard.h>
#include <database/pasteboard/oh_pasteboard_err_code.h>
#include <database/udmf/udmf.h>
#include <database/udmf/udmf_err_code.h>
#include <database/udmf/uds.h>
#include <multimedia/image_framework/image/pixelmap_native.h>

#include <freerdp/channels/cliprdr.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/event.h>
#include <winpr/clipboard.h>
#include <winpr/image.h>

typedef struct OH_ImageSourceNative OH_ImageSourceNative;
typedef struct OH_DecodingOptions OH_DecodingOptions;

Image_ErrorCode OH_DecodingOptions_Create(OH_DecodingOptions** options);
Image_ErrorCode OH_DecodingOptions_SetPixelFormat(OH_DecodingOptions* options, int32_t pixelFormat);
Image_ErrorCode OH_DecodingOptions_Release(OH_DecodingOptions* options);
Image_ErrorCode OH_ImageSourceNative_CreateFromUri(char* uri, size_t uriSize,
                                                   OH_ImageSourceNative** res);
Image_ErrorCode OH_ImageSourceNative_CreateFromData(uint8_t* data, size_t dataSize,
                                                    OH_ImageSourceNative** res);
Image_ErrorCode OH_ImageSourceNative_CreatePixelmap(OH_ImageSourceNative* source,
                                                    OH_DecodingOptions* options,
                                                    OH_PixelmapNative** pixelmap);
Image_ErrorCode OH_ImageSourceNative_Release(OH_ImageSourceNative* source);
int OH_FileUri_GetPathFromUri(const char* uri, unsigned int length, char** result);

#define OHOS_CLIPBOARD_ECHO_SUPPRESS_MS 1500ULL
#define OHOS_CLIPBOARD_FORMAT_HTML 0xC001U
#define OHOS_CLIPBOARD_FORMAT_URIW 0xC002U
#define OHOS_CLIPBOARD_FORMAT_URI_LIST 0xC003U
#define OHOS_CLIPBOARD_MAX_URI_IMAGE_BYTES (64U * 1024U * 1024U)
#define OHOS_CLIPBOARD_IMAGE_SIGNATURE_BYTES 12U

static const char OHOS_CLIPBOARD_HTML_FORMAT_NAME[] = "HTML Format";
static const char OHOS_CLIPBOARD_TEXT_HTML_FORMAT_NAME[] = "text/html";
static const char OHOS_CLIPBOARD_URIW_FORMAT_NAME[] = "UniformResourceLocatorW";
static const char OHOS_CLIPBOARD_URI_FORMAT_NAME[] = "UniformResourceLocator";
static const char OHOS_CLIPBOARD_URI_LIST_FORMAT_NAME[] = "text/uri-list";
static const char OHOS_CLIPBOARD_IMAGE_BMP_FORMAT_NAME[] = "image/bmp";

typedef enum
{
	OHOS_CLIPBOARD_REQUEST_NONE = 0,
	OHOS_CLIPBOARD_REQUEST_TEXT,
	OHOS_CLIPBOARD_REQUEST_HTML,
	OHOS_CLIPBOARD_REQUEST_URIW,
	OHOS_CLIPBOARD_REQUEST_URI_LIST,
	OHOS_CLIPBOARD_REQUEST_DIB,
	OHOS_CLIPBOARD_REQUEST_DIBV5,
	OHOS_CLIPBOARD_REQUEST_IMAGE_BMP
} OHOS_CLIPBOARD_REQUEST_KIND;

struct freerdp_ohos_clipboard
{
	rdpContext* context;
	FREERDP_OHOS_CLIPBOARD_CONFIG config;
	CliprdrClientContext* cliprdr;
	OH_Pasteboard* pasteboard;
	OH_PasteboardObserver* observer;
	BOOL pasteboardSubscribed;
	BOOL channelConnectedSubscribed;
	BOOL channelDisconnectedSubscribed;
	BOOL lockInitialized;
	pthread_mutex_t lock;
	UINT32 requestedFormatId;
	OHOS_CLIPBOARD_REQUEST_KIND requestedFormatKind;
	UINT32 ignoreLocalChanges;
	UINT64 ignoreLocalChangesUntil;

	UINT64 registerCount;
	UINT64 unregisterCount;
	UINT64 channelConnectCount;
	UINT64 channelDisconnectCount;
	UINT64 monitorReadyCount;
	UINT64 localFormatListCount;
	UINT64 remoteFormatListCount;
	UINT64 localRequestCount;
	UINT64 remoteResponseCount;
	UINT64 pasteboardReadCount;
	UINT64 pasteboardWriteCount;
	UINT64 pasteboardChangeCount;
	UINT64 suppressedChangeCount;
	UINT64 errorCount;
	UINT32 lastError;
	UINT32 lastRequestedFormat;
	UINT32 lastRemoteFormatCount;
	UINT32 lastLocalFormatCount;
	UINT32 lastTextBytes;
	UINT32 lastHtmlBytes;
	UINT32 lastUriBytes;
	UINT32 lastImageBytes;
	char diagnostics[1024];

	struct freerdp_ohos_clipboard* registryNext;
};

static pthread_mutex_t g_registryMutex = PTHREAD_MUTEX_INITIALIZER;
static freerdpOhosClipboard* g_registryHead = NULL;

static void ohos_clipboard_log(freerdpOhosClipboard* clipboard, const char* format, ...)
{
	if (!clipboard || !clipboard->config.Log)
		return;

	char message[512];
	va_list ap;
	va_start(ap, format);
	(void)vsnprintf(message, sizeof(message), format, ap);
	va_end(ap);
	clipboard->config.Log(clipboard->config.logUserData, message);
}

static void ohos_clipboard_set_error(freerdpOhosClipboard* clipboard, char* errorBuffer,
                                     size_t errorBufferSize, const char* format, ...)
{
	if (clipboard)
		++clipboard->errorCount;

	if (!errorBuffer || errorBufferSize == 0)
		return;

	va_list ap;
	va_start(ap, format);
	(void)vsnprintf(errorBuffer, errorBufferSize, format, ap);
	va_end(ap);
}

static void ohos_clipboard_registry_add(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	pthread_mutex_lock(&g_registryMutex);
	clipboard->registryNext = g_registryHead;
	g_registryHead = clipboard;
	pthread_mutex_unlock(&g_registryMutex);
}

static void ohos_clipboard_registry_remove(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	pthread_mutex_lock(&g_registryMutex);
	freerdpOhosClipboard** current = &g_registryHead;
	while (*current)
	{
		if (*current == clipboard)
		{
			*current = clipboard->registryNext;
			clipboard->registryNext = NULL;
			break;
		}
		current = &(*current)->registryNext;
	}
	pthread_mutex_unlock(&g_registryMutex);
}

static freerdpOhosClipboard* ohos_clipboard_from_context(void* context)
{
	rdpContext* rdp_context = (rdpContext*)context;
	freerdpOhosClipboard* found = NULL;

	pthread_mutex_lock(&g_registryMutex);
	for (freerdpOhosClipboard* current = g_registryHead; current; current = current->registryNext)
	{
		if (current->context == rdp_context)
		{
			found = current;
			break;
		}
	}
	pthread_mutex_unlock(&g_registryMutex);
	return found;
}

static CliprdrClientContext* ohos_clipboard_snapshot_cliprdr(freerdpOhosClipboard* clipboard)
{
	CliprdrClientContext* cliprdr = NULL;

	if (!clipboard || !clipboard->lockInitialized)
		return NULL;

	pthread_mutex_lock(&clipboard->lock);
	cliprdr = clipboard->cliprdr;
	pthread_mutex_unlock(&clipboard->lock);
	return cliprdr;
}

static char* ohos_clipboard_strdup(const char* value)
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

static BOOL ohos_clipboard_equals_ignore_case(const char* left, const char* right)
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

static BOOL ohos_clipboard_starts_with_ignore_case(const char* value, const char* prefix)
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

static BOOL ohos_clipboard_memory_equals_ignore_case(const char* left, const char* right,
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

static BOOL ohos_clipboard_is_uri_text(const char* value)
{
	if (!value || value[0] == '\0')
		return FALSE;
	return ohos_clipboard_starts_with_ignore_case(value, "http://") ||
	       ohos_clipboard_starts_with_ignore_case(value, "https://") ||
	       ohos_clipboard_starts_with_ignore_case(value, "file://") ||
	       ohos_clipboard_starts_with_ignore_case(value, "content://") ||
	       ohos_clipboard_starts_with_ignore_case(value, "datashare://");
}

static int ohos_clipboard_hex_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static char* ohos_clipboard_percent_decode(const char* value)
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

static char* ohos_clipboard_uri_to_local_path(const char* uri)
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

static const char* ohos_clipboard_uri_kind(const char* uri)
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

static BOOL ohos_clipboard_image_signature_supported(const BYTE* data, UINT32 size)
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

static BOOL ohos_clipboard_uri_has_image_suffix(const char* uri)
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

static BYTE* ohos_clipboard_read_local_file(const char* path, UINT32 maxBytes, UINT32* outSize,
                                            char* errorBuffer, size_t errorBufferSize)
{
	if (!outSize)
		return NULL;
	*outSize = 0;

	if (!path || path[0] == '\0')
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize, "empty image URI path");
		return NULL;
	}

	FILE* file = fopen(path, "rb");
	if (!file)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "open image URI path failed: %s", strerror(errno));
		return NULL;
	}

	if (fseek(file, 0, SEEK_END) != 0)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "seek image URI path failed");
		fclose(file);
		return NULL;
	}

	const long size = ftell(file);
	if (size <= 0 || (unsigned long)size > maxBytes || (unsigned long)size > UINT32_MAX)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "image URI file size is invalid: %ld", size);
		fclose(file);
		return NULL;
	}

	if (fseek(file, 0, SEEK_SET) != 0)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "rewind image URI path failed");
		fclose(file);
		return NULL;
	}

	BYTE* buffer = (BYTE*)malloc((size_t)size);
	if (!buffer)
	{
		fclose(file);
		return NULL;
	}

	const size_t read = fread(buffer, 1U, (size_t)size, file);
	fclose(file);
	if (read != (size_t)size)
	{
		free(buffer);
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "read image URI path failed");
		return NULL;
	}

	*outSize = (UINT32)size;
	return buffer;
}

static BOOL ohos_clipboard_read_local_file_signature(const char* path, BYTE* signature,
                                                     UINT32 signatureSize, UINT32* outSize)
{
	if (outSize)
		*outSize = 0;
	if (!path || !signature || signatureSize == 0U)
		return FALSE;

	FILE* file = fopen(path, "rb");
	if (!file)
		return FALSE;

	const size_t read = fread(signature, 1U, signatureSize, file);
	fclose(file);
	if (read == 0U)
		return FALSE;

	if (outSize)
		*outSize = (UINT32)read;
	return TRUE;
}

static BOOL ohos_clipboard_uri_may_reference_local_image(const char* uri)
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

static UINT64 ohos_clipboard_now_ms(void)
{
	struct timespec ts = { 0 };
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return ((UINT64)ts.tv_sec * 1000ULL) + ((UINT64)ts.tv_nsec / 1000000ULL);
}

static BYTE* ohos_clipboard_utf8_to_utf16le(const char* text, UINT32* outSize)
{
	BYTE* output = NULL;
	size_t capacity = 0;
	size_t count = 0;
	size_t offset = 0;
	const size_t length = text ? strlen(text) : 0;

	if (!outSize)
		return NULL;
	*outSize = 0;

	capacity = (length + 1U) * 4U;
	output = (BYTE*)calloc(capacity, sizeof(BYTE));
	if (!output)
		return NULL;

	while (offset < length)
	{
		uint32_t cp = 0;
		const uint8_t first = (uint8_t)text[offset++];
		if (first < 0x80U)
		{
			cp = first;
		}
		else
		{
			size_t trailing = 0;
			if ((first & 0xE0U) == 0xC0U)
			{
				cp = first & 0x1FU;
				trailing = 1;
			}
			else if ((first & 0xF0U) == 0xE0U)
			{
				cp = first & 0x0FU;
				trailing = 2;
			}
			else if ((first & 0xF8U) == 0xF0U)
			{
				cp = first & 0x07U;
				trailing = 3;
			}
			else
			{
				cp = 0xFFFDU;
				trailing = 0;
			}

			if (offset + trailing > length)
			{
				cp = 0xFFFDU;
				offset = length;
			}
			else
			{
				for (size_t x = 0; x < trailing; x++)
				{
					const uint8_t next = (uint8_t)text[offset++];
					if ((next & 0xC0U) != 0x80U)
					{
						cp = 0xFFFDU;
						break;
					}
					cp = (cp << 6U) | (next & 0x3FU);
				}
			}
		}

		if (cp > 0x10FFFFU)
			cp = 0xFFFDU;

		if (cp <= 0xFFFFU)
		{
			const uint16_t unit = (uint16_t)cp;
			output[count++] = (BYTE)(unit & 0xFFU);
			output[count++] = (BYTE)((unit >> 8U) & 0xFFU);
		}
		else
		{
			cp -= 0x10000U;
			const uint16_t high = (uint16_t)(0xD800U | (cp >> 10U));
			const uint16_t low = (uint16_t)(0xDC00U | (cp & 0x3FFU));
			output[count++] = (BYTE)(high & 0xFFU);
			output[count++] = (BYTE)((high >> 8U) & 0xFFU);
			output[count++] = (BYTE)(low & 0xFFU);
			output[count++] = (BYTE)((low >> 8U) & 0xFFU);
		}
	}

	output[count++] = 0;
	output[count++] = 0;
	if (count > UINT32_MAX)
	{
		free(output);
		return NULL;
	}
	*outSize = (UINT32)count;
	return output;
}

static void ohos_clipboard_append_utf8(char** text, size_t* count, size_t* capacity, uint32_t cp)
{
	BYTE encoded[4];
	size_t encodedCount = 0;

	if (cp <= 0x7FU)
	{
		encoded[encodedCount++] = (BYTE)cp;
	}
	else if (cp <= 0x7FFU)
	{
		encoded[encodedCount++] = (BYTE)(0xC0U | (cp >> 6U));
		encoded[encodedCount++] = (BYTE)(0x80U | (cp & 0x3FU));
	}
	else if (cp <= 0xFFFFU)
	{
		encoded[encodedCount++] = (BYTE)(0xE0U | (cp >> 12U));
		encoded[encodedCount++] = (BYTE)(0x80U | ((cp >> 6U) & 0x3FU));
		encoded[encodedCount++] = (BYTE)(0x80U | (cp & 0x3FU));
	}
	else
	{
		encoded[encodedCount++] = (BYTE)(0xF0U | (cp >> 18U));
		encoded[encodedCount++] = (BYTE)(0x80U | ((cp >> 12U) & 0x3FU));
		encoded[encodedCount++] = (BYTE)(0x80U | ((cp >> 6U) & 0x3FU));
		encoded[encodedCount++] = (BYTE)(0x80U | (cp & 0x3FU));
	}

	if (*count + encodedCount + 1U > *capacity)
	{
		const size_t nextCapacity = (*capacity + encodedCount + 64U) * 2U;
		char* grown = (char*)realloc(*text, nextCapacity);
		if (!grown)
			return;
		*text = grown;
		*capacity = nextCapacity;
	}
	memcpy(*text + *count, encoded, encodedCount);
	*count += encodedCount;
	(*text)[*count] = '\0';
}

static char* ohos_clipboard_utf16le_to_utf8(const BYTE* data, UINT32 size)
{
	size_t count = 0;
	size_t capacity = size + 4U;
	char* text = NULL;
	const UINT32 units = size / 2U;

	if (!data || size < 2)
		return ohos_clipboard_strdup("");

	text = (char*)calloc(capacity, sizeof(char));
	if (!text)
		return NULL;

	for (UINT32 index = 0; index < units; index++)
	{
		uint32_t cp = (uint16_t)data[index * 2U] |
		              ((uint16_t)data[(index * 2U) + 1U] << 8U);
		if (cp == 0)
			break;

		if (cp >= 0xD800U && cp <= 0xDBFFU && index + 1U < units)
		{
			const uint16_t next = (uint16_t)data[(index + 1U) * 2U] |
			                      ((uint16_t)data[((index + 1U) * 2U) + 1U] << 8U);
			if (next >= 0xDC00U && next <= 0xDFFFU)
			{
				cp = 0x10000U + (((cp - 0xD800U) << 10U) | (next - 0xDC00U));
				index++;
			}
		}

		ohos_clipboard_append_utf8(&text, &count, &capacity, cp);
	}
	return text;
}

static char* ohos_clipboard_bytes_to_string(const BYTE* data, UINT32 size)
{
	if (!data)
		return NULL;

	size_t length = size;
	while (length > 0 && data[length - 1U] == '\0')
		length--;

	char* text = (char*)calloc(length + 1U, sizeof(char));
	if (!text)
		return NULL;
	if (length > 0)
		memcpy(text, data, length);
	return text;
}

static const char* ohos_clipboard_find_token(const char* text, const char* token)
{
	if (!text || !token || token[0] == '\0')
		return NULL;

	const size_t tokenLen = strlen(token);
	for (const char* cursor = text; *cursor; cursor++)
	{
		if (strncmp(cursor, token, tokenLen) == 0)
			return cursor;
	}
	return NULL;
}

static UINT32 ohos_clipboard_parse_html_offset(const char* text, const char* key)
{
	const char* cursor = ohos_clipboard_find_token(text, key);
	if (!cursor)
		return UINT32_MAX;

	cursor += strlen(key);
	while (*cursor == ' ' || *cursor == '\t')
		cursor++;

	UINT32 value = 0;
	BOOL found = FALSE;
	while (*cursor >= '0' && *cursor <= '9')
	{
		found = TRUE;
		value = (value * 10U) + (UINT32)(*cursor - '0');
		cursor++;
	}
	return found ? value : UINT32_MAX;
}

static char* ohos_clipboard_extract_ms_html(const BYTE* data, UINT32 size)
{
	char* source = ohos_clipboard_bytes_to_string(data, size);
	if (!source)
		return NULL;

	UINT32 start = ohos_clipboard_parse_html_offset(source, "StartFragment:");
	UINT32 end = ohos_clipboard_parse_html_offset(source, "EndFragment:");
	if (start == UINT32_MAX || end == UINT32_MAX || start >= end || end > size)
	{
		start = ohos_clipboard_parse_html_offset(source, "StartHTML:");
		end = ohos_clipboard_parse_html_offset(source, "EndHTML:");
	}

	if (start != UINT32_MAX && end != UINT32_MAX && start < end && end <= size)
	{
		const UINT32 length = end - start;
		char* html = (char*)calloc((size_t)length + 1U, sizeof(char));
		if (html)
			memcpy(html, source + start, length);
		free(source);
		return html;
	}

	return source;
}

static BYTE* ohos_clipboard_html_to_ms_html(const char* html, UINT32* outSize)
{
	static const char prefix[] =
	    "Version:0.9\r\n"
	    "StartHTML:%010u\r\n"
	    "EndHTML:%010u\r\n"
	    "StartFragment:%010u\r\n"
	    "EndFragment:%010u\r\n";
	static const char startFragment[] = "<html><body>\r\n<!--StartFragment-->";
	static const char endFragment[] = "<!--EndFragment-->\r\n</body></html>";

	if (!outSize)
		return NULL;
	*outSize = 0;

	const char* body = html ? html : "";
	const int headerLen = snprintf(NULL, 0, prefix, 0U, 0U, 0U, 0U);
	if (headerLen <= 0)
		return NULL;

	const UINT32 startHtml = (UINT32)headerLen;
	const UINT32 startFragmentOffset = startHtml + (UINT32)strlen(startFragment);
	const UINT32 endFragmentOffset = startFragmentOffset + (UINT32)strlen(body);
	const UINT32 endHtml = endFragmentOffset + (UINT32)strlen(endFragment);
	const size_t total = (size_t)endHtml + 1U;

	BYTE* output = (BYTE*)calloc(total, sizeof(BYTE));
	if (!output)
		return NULL;

	const int written = snprintf((char*)output, total, prefix, startHtml, endHtml,
	                             startFragmentOffset, endFragmentOffset);
	if (written != headerLen)
	{
		free(output);
		return NULL;
	}
	memcpy(output + startHtml, startFragment, strlen(startFragment));
	memcpy(output + startFragmentOffset, body, strlen(body));
	memcpy(output + endFragmentOffset, endFragment, strlen(endFragment));
	*outSize = (UINT32)total;
	return output;
}

static char* ohos_clipboard_extract_uri_list_first(const BYTE* data, UINT32 size)
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

static BYTE* ohos_clipboard_uri_to_uri_list(const char* uri, UINT32* outSize)
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

static UINT16 ohos_clipboard_read_le16(const BYTE* data)
{
	return (UINT16)data[0] | ((UINT16)data[1] << 8U);
}

static UINT32 ohos_clipboard_read_le32(const BYTE* data)
{
	return (UINT32)data[0] | ((UINT32)data[1] << 8U) | ((UINT32)data[2] << 16U) |
	       ((UINT32)data[3] << 24U);
}

static BYTE* ohos_clipboard_dib_to_bmp(const BYTE* data, UINT32 size, UINT32* outSize,
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

static BYTE* ohos_clipboard_bgra_to_dib(const BYTE* bgra, UINT32 width, UINT32 height,
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

static BOOL ohos_clipboard_decoded_image_to_bgra(const wImage* image, BYTE** outBgra,
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

static BOOL ohos_clipboard_pixelmap_native_to_bgra(OH_PixelmapNative* pixelmapNative,
                                                   BYTE** outBgra, UINT32* outWidth,
                                                   UINT32* outHeight, char* errorBuffer,
                                                   size_t errorBufferSize);

static BOOL ohos_clipboard_ohos_image_source_to_bgra(OH_ImageSourceNative* source,
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

static BOOL ohos_clipboard_ohos_image_buffer_to_bgra(const BYTE* data, UINT32 size,
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

static BOOL ohos_clipboard_ohos_image_uri_to_bgra(const char* uri, BYTE** outBgra,
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

static BOOL ohos_clipboard_image_buffer_to_bgra(const BYTE* data, UINT32 size, BYTE** outBgra,
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

static BOOL ohos_clipboard_bitmap_to_bgra(const BYTE* data, UINT32 size,
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

static OH_PixelmapNative* ohos_clipboard_get_pixelmap_native(OH_UdsPixelMap* pixelMap)
{
	OH_PixelmapNative* pixelmapNative = NULL;
	if (!pixelMap)
		return NULL;

	/* The NDK declares OH_UdsPixelMap_GetPixelMap with OH_PixelmapNative*, but
	 * the parameter is documented and used by wrappers as an output pointer. */
	OH_UdsPixelMap_GetPixelMap(pixelMap, (OH_PixelmapNative*)&pixelmapNative);
	return pixelmapNative;
}

static UINT32 ohos_clipboard_pixel_format_bytes(int32_t pixelFormat)
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

static BOOL ohos_clipboard_pixelmap_native_to_bgra(OH_PixelmapNative* pixelmapNative,
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

static BOOL ohos_clipboard_has_pixelmap(freerdpOhosClipboard* clipboard, char* errorBuffer,
                                        size_t errorBufferSize)
{
	int status = ERR_OK;
	OH_UdmfData* data = NULL;
	BOOL found = FALSE;

	if (!clipboard || !clipboard->pasteboard)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard unavailable");
		return FALSE;
	}

	data = OH_Pasteboard_GetData(clipboard->pasteboard, &status);
	if (status != ERR_OK || !data)
	{
		clipboard->lastError = (UINT32)status;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_GetData status=%d", status);
		return FALSE;
	}

	const int recordCount = OH_UdmfData_GetRecordCount(data);
	for (int index = 0; index < recordCount; index++)
	{
		OH_UdmfRecord* record = OH_UdmfData_GetRecord(data, (unsigned int)index);
		if (!record)
			continue;

		OH_UdsPixelMap* pixelMap = OH_UdsPixelMap_Create();
		if (!pixelMap)
			continue;
		const int rc = OH_UdmfRecord_GetPixelMap(record, pixelMap);
		if (rc == UDMF_E_OK && ohos_clipboard_get_pixelmap_native(pixelMap))
			found = TRUE;
		OH_UdsPixelMap_Destroy(pixelMap);
		if (found)
			break;
	}

	OH_UdmfData_Destroy(data);
	if (!found)
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "pasteboard has no pixelmap record");
	return found;
}

static BOOL ohos_clipboard_read_uri_image(freerdpOhosClipboard* clipboard, BYTE** outBgra,
                                          UINT32* outWidth, UINT32* outHeight,
                                          char* errorBuffer, size_t errorBufferSize);

static BOOL ohos_clipboard_read_pixelmap(freerdpOhosClipboard* clipboard, BYTE** outBgra,
                                         UINT32* outWidth, UINT32* outHeight,
                                         char* errorBuffer, size_t errorBufferSize)
{
	int status = ERR_OK;
	OH_UdmfData* data = NULL;
	BYTE* bgra = NULL;
	UINT32 width = 0;
	UINT32 height = 0;

	if (!outBgra || !outWidth || !outHeight)
		return FALSE;
	*outBgra = NULL;
	*outWidth = 0;
	*outHeight = 0;

	if (!clipboard || !clipboard->pasteboard)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard unavailable");
		return FALSE;
	}

	data = OH_Pasteboard_GetData(clipboard->pasteboard, &status);
	if (status != ERR_OK || !data)
	{
		clipboard->lastError = (UINT32)status;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_GetData status=%d", status);
		return FALSE;
	}

	const int recordCount = OH_UdmfData_GetRecordCount(data);
	for (int index = 0; index < recordCount; index++)
	{
		OH_UdmfRecord* record = OH_UdmfData_GetRecord(data, (unsigned int)index);
		if (!record)
			continue;

		OH_UdsPixelMap* pixelMap = OH_UdsPixelMap_Create();
		if (!pixelMap)
			continue;

		const int rc = OH_UdmfRecord_GetPixelMap(record, pixelMap);
		if (rc == UDMF_E_OK)
		{
			OH_PixelmapNative* pixelmapNative = ohos_clipboard_get_pixelmap_native(pixelMap);
			if (ohos_clipboard_pixelmap_native_to_bgra(pixelmapNative, &bgra, &width, &height,
			                                           errorBuffer, errorBufferSize))
			{
				OH_UdsPixelMap_Destroy(pixelMap);
				goto success;
			}
		}
		OH_UdsPixelMap_Destroy(pixelMap);
	}

	OH_UdmfData_Destroy(data);
	if (ohos_clipboard_read_uri_image(clipboard, outBgra, outWidth, outHeight, errorBuffer,
	                                  errorBufferSize))
		return TRUE;
	if (errorBuffer && errorBuffer[0] == '\0')
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "pasteboard has no readable pixelmap record");
	return FALSE;

success:
	OH_UdmfData_Destroy(data);
	*outBgra = bgra;
	*outWidth = width;
	*outHeight = height;
	++clipboard->pasteboardReadCount;
	clipboard->lastImageBytes = (UINT32)((size_t)width * (size_t)height * 4U);
	return TRUE;
}

static BOOL ohos_clipboard_read_plain_text(freerdpOhosClipboard* clipboard, char** outText,
                                           char* errorBuffer, size_t errorBufferSize)
{
	int status = ERR_OK;
	OH_UdmfData* data = NULL;
	char* text = NULL;

	if (!outText)
		return FALSE;
	*outText = NULL;

	if (!clipboard || !clipboard->pasteboard)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard unavailable");
		return FALSE;
	}

	data = OH_Pasteboard_GetData(clipboard->pasteboard, &status);
	if (status != ERR_OK || !data)
	{
		clipboard->lastError = (UINT32)status;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_GetData status=%d", status);
		return FALSE;
	}

	OH_UdsPlainText* primaryPlainText = OH_UdsPlainText_Create();
	if (primaryPlainText)
	{
		const int rc = OH_UdmfData_GetPrimaryPlainText(data, primaryPlainText);
		if (rc == UDMF_E_OK)
		{
			const char* content = OH_UdsPlainText_GetContent(primaryPlainText);
			text = ohos_clipboard_strdup(content);
		}
		OH_UdsPlainText_Destroy(primaryPlainText);
		if (text && text[0] != '\0')
			goto success;
		free(text);
		text = NULL;
	}

	const int recordCount = OH_UdmfData_GetRecordCount(data);
	for (int index = 0; index < recordCount; index++)
	{
		OH_UdmfRecord* record = OH_UdmfData_GetRecord(data, (unsigned int)index);
		if (!record)
			continue;
		OH_UdsPlainText* plainText = OH_UdsPlainText_Create();
		if (!plainText)
			continue;
		const int rc = OH_UdmfRecord_GetPlainText(record, plainText);
		if (rc == UDMF_E_OK)
		{
			const char* content = OH_UdsPlainText_GetContent(plainText);
			text = ohos_clipboard_strdup(content);
		}
		OH_UdsPlainText_Destroy(plainText);
		if (text && text[0] != '\0')
			goto success;
		free(text);
		text = NULL;
	}

	OH_UdmfData_Destroy(data);
	ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
	                         "pasteboard has no plain text record");
	return FALSE;

success:
	OH_UdmfData_Destroy(data);
	*outText = text;
	++clipboard->pasteboardReadCount;
	clipboard->lastTextBytes = (UINT32)strlen(text);
	return TRUE;
}

static BOOL ohos_clipboard_read_html(freerdpOhosClipboard* clipboard, char** outHtml,
                                     char** outPlain, char* errorBuffer,
                                     size_t errorBufferSize)
{
	int status = ERR_OK;
	OH_UdmfData* data = NULL;
	char* htmlText = NULL;
	char* plainText = NULL;

	if (!outHtml)
		return FALSE;
	*outHtml = NULL;
	if (outPlain)
		*outPlain = NULL;

	if (!clipboard || !clipboard->pasteboard)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard unavailable");
		return FALSE;
	}

	data = OH_Pasteboard_GetData(clipboard->pasteboard, &status);
	if (status != ERR_OK || !data)
	{
		clipboard->lastError = (UINT32)status;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_GetData status=%d", status);
		return FALSE;
	}

	OH_UdsHtml* primaryHtml = OH_UdsHtml_Create();
	if (primaryHtml)
	{
		const int rc = OH_UdmfData_GetPrimaryHtml(data, primaryHtml);
		if (rc == UDMF_E_OK)
		{
			htmlText = ohos_clipboard_strdup(OH_UdsHtml_GetContent(primaryHtml));
			plainText = ohos_clipboard_strdup(OH_UdsHtml_GetPlainContent(primaryHtml));
		}
		OH_UdsHtml_Destroy(primaryHtml);
		if (htmlText && htmlText[0] != '\0')
			goto success;
		free(htmlText);
		free(plainText);
		htmlText = NULL;
		plainText = NULL;
	}

	const int recordCount = OH_UdmfData_GetRecordCount(data);
	for (int index = 0; index < recordCount; index++)
	{
		OH_UdmfRecord* record = OH_UdmfData_GetRecord(data, (unsigned int)index);
		if (!record)
			continue;
		OH_UdsHtml* html = OH_UdsHtml_Create();
		if (!html)
			continue;
		const int rc = OH_UdmfRecord_GetHtml(record, html);
		if (rc == UDMF_E_OK)
		{
			htmlText = ohos_clipboard_strdup(OH_UdsHtml_GetContent(html));
			plainText = ohos_clipboard_strdup(OH_UdsHtml_GetPlainContent(html));
		}
		OH_UdsHtml_Destroy(html);
		if (htmlText && htmlText[0] != '\0')
			goto success;
		free(htmlText);
		free(plainText);
		htmlText = NULL;
		plainText = NULL;
	}

	OH_UdmfData_Destroy(data);
	ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard has no html record");
	return FALSE;

success:
	OH_UdmfData_Destroy(data);
	*outHtml = htmlText;
	if (outPlain)
		*outPlain = plainText;
	else
		free(plainText);
	++clipboard->pasteboardReadCount;
	clipboard->lastHtmlBytes = (UINT32)strlen(htmlText);
	return TRUE;
}

static BOOL ohos_clipboard_read_uri_from_data(OH_UdmfData* data, char** outUri)
{
	char* uri = NULL;

	if (!outUri)
		return FALSE;
	*outUri = NULL;
	if (!data)
		return FALSE;

	const int recordCount = OH_UdmfData_GetRecordCount(data);
	for (int index = 0; index < recordCount; index++)
	{
		OH_UdmfRecord* record = OH_UdmfData_GetRecord(data, (unsigned int)index);
		if (!record)
			continue;

		OH_UdsHyperlink* hyperlink = OH_UdsHyperlink_Create();
		if (hyperlink)
		{
			const int rc = OH_UdmfRecord_GetHyperlink(record, hyperlink);
			if (rc == UDMF_E_OK)
				uri = ohos_clipboard_strdup(OH_UdsHyperlink_GetUrl(hyperlink));
			OH_UdsHyperlink_Destroy(hyperlink);
			if (uri && uri[0] != '\0')
				goto success;
			free(uri);
			uri = NULL;
		}

		OH_UdsFileUri* fileUri = OH_UdsFileUri_Create();
		if (fileUri)
		{
			const int rc = OH_UdmfRecord_GetFileUri(record, fileUri);
			if (rc == UDMF_E_OK)
				uri = ohos_clipboard_strdup(OH_UdsFileUri_GetFileUri(fileUri));
			OH_UdsFileUri_Destroy(fileUri);
			if (uri && uri[0] != '\0')
				goto success;
			free(uri);
			uri = NULL;
		}
	}

	return FALSE;

success:
	*outUri = uri;
	return TRUE;
}

static BOOL ohos_clipboard_read_uri(freerdpOhosClipboard* clipboard, char** outUri,
                                    char* errorBuffer, size_t errorBufferSize)
{
	int status = ERR_OK;
	OH_UdmfData* data = NULL;
	char* uri = NULL;

	if (!outUri)
		return FALSE;
	*outUri = NULL;

	if (!clipboard || !clipboard->pasteboard)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard unavailable");
		return FALSE;
	}

	data = OH_Pasteboard_GetData(clipboard->pasteboard, &status);
	if (status != ERR_OK || !data)
	{
		clipboard->lastError = (UINT32)status;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_GetData status=%d", status);
		return FALSE;
	}

	if (ohos_clipboard_read_uri_from_data(data, &uri))
		goto success;

	char textError[128] = { 0 };
	char* text = NULL;
	if (ohos_clipboard_read_plain_text(clipboard, &text, textError, sizeof(textError)) &&
	    ohos_clipboard_is_uri_text(text))
	{
		uri = text;
		goto success;
	}
	free(text);

	OH_UdmfData_Destroy(data);
	ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard has no uri record");
	return FALSE;

success:
	OH_UdmfData_Destroy(data);
	*outUri = uri;
	++clipboard->pasteboardReadCount;
	clipboard->lastUriBytes = (UINT32)strlen(uri);
	return TRUE;
}

static BOOL ohos_clipboard_read_uri_image(freerdpOhosClipboard* clipboard, BYTE** outBgra,
                                          UINT32* outWidth, UINT32* outHeight,
                                          char* errorBuffer, size_t errorBufferSize)
{
	char* uri = NULL;
	char* path = NULL;
	BYTE* imageData = NULL;
	UINT32 imageDataSize = 0;
	BOOL ok = FALSE;

	if (!outBgra || !outWidth || !outHeight)
		return FALSE;
	*outBgra = NULL;
	*outWidth = 0;
	*outHeight = 0;

	if (!ohos_clipboard_read_uri(clipboard, &uri, errorBuffer, errorBufferSize))
		return FALSE;

	if (ohos_clipboard_ohos_image_uri_to_bgra(uri, outBgra, outWidth, outHeight, NULL, 0))
	{
		ok = TRUE;
		clipboard->lastImageBytes = 0;
		ohos_clipboard_log(clipboard,
		                   "HarmonyOS Pasteboard image URI decoded for cliprdr: %" PRIu32
		                   "x%" PRIu32 " sourceBytes=uri kind=%s",
		                   *outWidth, *outHeight, ohos_clipboard_uri_kind(uri));
		goto fail;
	}

	path = ohos_clipboard_uri_to_local_path(uri);
	if (!path)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "clipboard URI is not a directly readable local image: kind=%s",
		                         ohos_clipboard_uri_kind(uri));
		goto fail;
	}

	imageData = ohos_clipboard_read_local_file(path, OHOS_CLIPBOARD_MAX_URI_IMAGE_BYTES,
	                                           &imageDataSize, errorBuffer, errorBufferSize);
	if (!imageData)
		goto fail;

	if (!ohos_clipboard_image_signature_supported(imageData, imageDataSize))
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "clipboard URI image type is not supported");
		goto fail;
	}

	ok = ohos_clipboard_image_buffer_to_bgra(imageData, imageDataSize, outBgra, outWidth,
	                                        outHeight, errorBuffer, errorBufferSize);
	if (ok)
	{
		clipboard->lastImageBytes = imageDataSize;
		ohos_clipboard_log(clipboard,
		                   "HarmonyOS Pasteboard image URI decoded for cliprdr: %" PRIu32
		                   "x%" PRIu32 " sourceBytes=%" PRIu32 " kind=%s",
		                   *outWidth, *outHeight, imageDataSize,
		                   ohos_clipboard_uri_kind(uri));
	}

fail:
	free(imageData);
	free(path);
	free(uri);
	return ok;
}

static BOOL ohos_clipboard_write_plain_text(freerdpOhosClipboard* clipboard, const char* text,
                                            char* errorBuffer, size_t errorBufferSize)
{
	int rc = UDMF_E_OK;
	OH_UdsPlainText* plainText = NULL;
	OH_UdmfRecord* record = NULL;
	OH_UdmfData* data = NULL;

	if (!clipboard || !clipboard->pasteboard)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard unavailable");
		return FALSE;
	}

	plainText = OH_UdsPlainText_Create();
	record = OH_UdmfRecord_Create();
	data = OH_UdmfData_Create();
	if (!plainText || !record || !data)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "UDMF allocation failed");
		goto fail;
	}

	rc = OH_UdsPlainText_SetContent(plainText, text ? text : "");
	if (rc == UDMF_E_OK)
		rc = OH_UdmfRecord_AddPlainText(record, plainText);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfData_AddRecord(data, record);
	if (rc != UDMF_E_OK)
	{
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "UDMF plain text setup failed: %d", rc);
		goto fail;
	}

	pthread_mutex_lock(&clipboard->lock);
	clipboard->ignoreLocalChanges++;
	clipboard->ignoreLocalChangesUntil = ohos_clipboard_now_ms() + OHOS_CLIPBOARD_ECHO_SUPPRESS_MS;
	pthread_mutex_unlock(&clipboard->lock);

	rc = OH_Pasteboard_SetData(clipboard->pasteboard, data);
	if (rc != ERR_OK)
	{
		pthread_mutex_lock(&clipboard->lock);
		if (clipboard->ignoreLocalChanges > 0)
			clipboard->ignoreLocalChanges--;
		if (clipboard->ignoreLocalChanges == 0)
			clipboard->ignoreLocalChangesUntil = 0;
		pthread_mutex_unlock(&clipboard->lock);
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_SetData status=%d", rc);
		goto fail;
	}

	++clipboard->pasteboardWriteCount;
	clipboard->lastTextBytes = text ? (UINT32)strlen(text) : 0;
	if (plainText)
		OH_UdsPlainText_Destroy(plainText);
	if (record)
		OH_UdmfRecord_Destroy(record);
	if (data)
		OH_UdmfData_Destroy(data);
	return TRUE;

fail:
	if (plainText)
		OH_UdsPlainText_Destroy(plainText);
	if (record)
		OH_UdmfRecord_Destroy(record);
	if (data)
		OH_UdmfData_Destroy(data);
	return FALSE;
}

static BOOL ohos_clipboard_write_html(freerdpOhosClipboard* clipboard, const char* html,
                                      const char* plain, char* errorBuffer,
                                      size_t errorBufferSize)
{
	int rc = UDMF_E_OK;
	OH_UdsHtml* htmlData = NULL;
	OH_UdsPlainText* plainText = NULL;
	OH_UdmfRecord* record = NULL;
	OH_UdmfData* data = NULL;

	if (!clipboard || !clipboard->pasteboard)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard unavailable");
		return FALSE;
	}

	htmlData = OH_UdsHtml_Create();
	plainText = OH_UdsPlainText_Create();
	record = OH_UdmfRecord_Create();
	data = OH_UdmfData_Create();
	if (!htmlData || !plainText || !record || !data)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "UDMF allocation failed");
		goto fail;
	}

	const char* htmlValue = html ? html : "";
	const char* plainValue = (plain && plain[0] != '\0') ? plain : htmlValue;
	rc = OH_UdsHtml_SetContent(htmlData, htmlValue);
	if (rc == UDMF_E_OK)
		rc = OH_UdsHtml_SetPlainContent(htmlData, plainValue);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfRecord_AddHtml(record, htmlData);
	if (rc == UDMF_E_OK)
		rc = OH_UdsPlainText_SetContent(plainText, plainValue);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfRecord_AddPlainText(record, plainText);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfData_AddRecord(data, record);
	if (rc != UDMF_E_OK)
	{
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "UDMF html setup failed: %d", rc);
		goto fail;
	}

	pthread_mutex_lock(&clipboard->lock);
	clipboard->ignoreLocalChanges++;
	clipboard->ignoreLocalChangesUntil = ohos_clipboard_now_ms() + OHOS_CLIPBOARD_ECHO_SUPPRESS_MS;
	pthread_mutex_unlock(&clipboard->lock);

	rc = OH_Pasteboard_SetData(clipboard->pasteboard, data);
	if (rc != ERR_OK)
	{
		pthread_mutex_lock(&clipboard->lock);
		if (clipboard->ignoreLocalChanges > 0)
			clipboard->ignoreLocalChanges--;
		if (clipboard->ignoreLocalChanges == 0)
			clipboard->ignoreLocalChangesUntil = 0;
		pthread_mutex_unlock(&clipboard->lock);
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_SetData status=%d", rc);
		goto fail;
	}

	++clipboard->pasteboardWriteCount;
	clipboard->lastHtmlBytes = htmlValue ? (UINT32)strlen(htmlValue) : 0;
	clipboard->lastTextBytes = plainValue ? (UINT32)strlen(plainValue) : 0;
	if (htmlData)
		OH_UdsHtml_Destroy(htmlData);
	if (plainText)
		OH_UdsPlainText_Destroy(plainText);
	if (record)
		OH_UdmfRecord_Destroy(record);
	if (data)
		OH_UdmfData_Destroy(data);
	return TRUE;

fail:
	if (htmlData)
		OH_UdsHtml_Destroy(htmlData);
	if (plainText)
		OH_UdsPlainText_Destroy(plainText);
	if (record)
		OH_UdmfRecord_Destroy(record);
	if (data)
		OH_UdmfData_Destroy(data);
	return FALSE;
}

static BOOL ohos_clipboard_write_uri(freerdpOhosClipboard* clipboard, const char* uri,
                                     char* errorBuffer, size_t errorBufferSize)
{
	int rc = UDMF_E_OK;
	OH_UdsHyperlink* hyperlink = NULL;
	OH_UdsFileUri* fileUri = NULL;
	OH_UdsPlainText* plainText = NULL;
	OH_UdmfRecord* record = NULL;
	OH_UdmfData* data = NULL;

	if (!clipboard || !clipboard->pasteboard)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard unavailable");
		return FALSE;
	}

	const char* uriValue = uri ? uri : "";
	record = OH_UdmfRecord_Create();
	data = OH_UdmfData_Create();
	plainText = OH_UdsPlainText_Create();
	if (!record || !data || !plainText)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "UDMF allocation failed");
		goto fail;
	}

	if (ohos_clipboard_starts_with_ignore_case(uriValue, "http://") ||
	    ohos_clipboard_starts_with_ignore_case(uriValue, "https://"))
	{
		hyperlink = OH_UdsHyperlink_Create();
		if (!hyperlink)
		{
			ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
			                         "UDMF hyperlink allocation failed");
			goto fail;
		}
		rc = OH_UdsHyperlink_SetUrl(hyperlink, uriValue);
		if (rc == UDMF_E_OK)
			rc = OH_UdsHyperlink_SetDescription(hyperlink, uriValue);
		if (rc == UDMF_E_OK)
			rc = OH_UdmfRecord_AddHyperlink(record, hyperlink);
	}
	else
	{
		fileUri = OH_UdsFileUri_Create();
		if (!fileUri)
		{
			ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
			                         "UDMF file uri allocation failed");
			goto fail;
		}
		rc = OH_UdsFileUri_SetFileUri(fileUri, uriValue);
		if (rc == UDMF_E_OK)
			rc = OH_UdmfRecord_AddFileUri(record, fileUri);
	}

	if (rc == UDMF_E_OK)
		rc = OH_UdsPlainText_SetContent(plainText, uriValue);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfRecord_AddPlainText(record, plainText);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfData_AddRecord(data, record);
	if (rc != UDMF_E_OK)
	{
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "UDMF uri setup failed: %d", rc);
		goto fail;
	}

	pthread_mutex_lock(&clipboard->lock);
	clipboard->ignoreLocalChanges++;
	clipboard->ignoreLocalChangesUntil = ohos_clipboard_now_ms() + OHOS_CLIPBOARD_ECHO_SUPPRESS_MS;
	pthread_mutex_unlock(&clipboard->lock);

	rc = OH_Pasteboard_SetData(clipboard->pasteboard, data);
	if (rc != ERR_OK)
	{
		pthread_mutex_lock(&clipboard->lock);
		if (clipboard->ignoreLocalChanges > 0)
			clipboard->ignoreLocalChanges--;
		if (clipboard->ignoreLocalChanges == 0)
			clipboard->ignoreLocalChangesUntil = 0;
		pthread_mutex_unlock(&clipboard->lock);
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_SetData status=%d", rc);
		goto fail;
	}

	++clipboard->pasteboardWriteCount;
	clipboard->lastUriBytes = (UINT32)strlen(uriValue);
	clipboard->lastTextBytes = (UINT32)strlen(uriValue);
	if (hyperlink)
		OH_UdsHyperlink_Destroy(hyperlink);
	if (fileUri)
		OH_UdsFileUri_Destroy(fileUri);
	if (plainText)
		OH_UdsPlainText_Destroy(plainText);
	if (record)
		OH_UdmfRecord_Destroy(record);
	if (data)
		OH_UdmfData_Destroy(data);
	return TRUE;

fail:
	if (hyperlink)
		OH_UdsHyperlink_Destroy(hyperlink);
	if (fileUri)
		OH_UdsFileUri_Destroy(fileUri);
	if (plainText)
		OH_UdsPlainText_Destroy(plainText);
	if (record)
		OH_UdmfRecord_Destroy(record);
	if (data)
		OH_UdmfData_Destroy(data);
	return FALSE;
}

static BOOL ohos_clipboard_write_pixelmap(freerdpOhosClipboard* clipboard, const BYTE* bgra,
                                          UINT32 width, UINT32 height, UINT32 sourceBytes,
                                          char* errorBuffer, size_t errorBufferSize)
{
	int rc = UDMF_E_OK;
	Image_ErrorCode imageRc = IMAGE_SUCCESS;
	OH_Pixelmap_InitializationOptions* options = NULL;
	OH_PixelmapNative* pixelmapNative = NULL;
	OH_UdsPixelMap* pixelMap = NULL;
	OH_UdmfRecord* record = NULL;
	OH_UdmfData* data = NULL;

	if (!clipboard || !clipboard->pasteboard)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "pasteboard unavailable");
		return FALSE;
	}
	if (!bgra || width == 0U || height == 0U)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "invalid pixelmap input");
		return FALSE;
	}

	if ((size_t)width > SIZE_MAX / (size_t)height / 4U)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "pixelmap input is too large");
		return FALSE;
	}
	const size_t dataLength = (size_t)width * (size_t)height * 4U;

	imageRc = OH_PixelmapInitializationOptions_Create(&options);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_PixelmapInitializationOptions_SetWidth(options, width);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_PixelmapInitializationOptions_SetHeight(options, height);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_PixelmapInitializationOptions_SetPixelFormat(options, PIXEL_FORMAT_BGRA_8888);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_PixelmapInitializationOptions_SetAlphaType(
		    options, PIXELMAP_ALPHA_TYPE_UNPREMULTIPLIED);
	if (imageRc == IMAGE_SUCCESS)
		imageRc = OH_PixelmapNative_CreatePixelmap((uint8_t*)bgra, dataLength, options,
		                                           &pixelmapNative);
	if (imageRc != IMAGE_SUCCESS || !pixelmapNative)
	{
		clipboard->lastError = (UINT32)imageRc;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "PixelMap create failed: %d", imageRc);
		goto fail;
	}

	pixelMap = OH_UdsPixelMap_Create();
	record = OH_UdmfRecord_Create();
	data = OH_UdmfData_Create();
	if (!pixelMap || !record || !data)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "UDMF allocation failed");
		goto fail;
	}

	rc = OH_UdsPixelMap_SetPixelMap(pixelMap, pixelmapNative);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfRecord_AddPixelMap(record, pixelMap);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfData_AddRecord(data, record);
	if (rc != UDMF_E_OK)
	{
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "UDMF pixelmap setup failed: %d", rc);
		goto fail;
	}

	pthread_mutex_lock(&clipboard->lock);
	clipboard->ignoreLocalChanges++;
	clipboard->ignoreLocalChangesUntil = ohos_clipboard_now_ms() + OHOS_CLIPBOARD_ECHO_SUPPRESS_MS;
	pthread_mutex_unlock(&clipboard->lock);

	rc = OH_Pasteboard_SetData(clipboard->pasteboard, data);
	if (rc != ERR_OK)
	{
		pthread_mutex_lock(&clipboard->lock);
		if (clipboard->ignoreLocalChanges > 0)
			clipboard->ignoreLocalChanges--;
		if (clipboard->ignoreLocalChanges == 0)
			clipboard->ignoreLocalChangesUntil = 0;
		pthread_mutex_unlock(&clipboard->lock);
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_SetData status=%d", rc);
		goto fail;
	}

	++clipboard->pasteboardWriteCount;
	clipboard->lastImageBytes = sourceBytes;
	if (pixelMap)
		OH_UdsPixelMap_Destroy(pixelMap);
	if (record)
		OH_UdmfRecord_Destroy(record);
	if (data)
		OH_UdmfData_Destroy(data);
	if (pixelmapNative)
		OH_PixelmapNative_Release(pixelmapNative);
	if (options)
		OH_PixelmapInitializationOptions_Release(options);
	return TRUE;

fail:
	if (pixelMap)
		OH_UdsPixelMap_Destroy(pixelMap);
	if (record)
		OH_UdmfRecord_Destroy(record);
	if (data)
		OH_UdmfData_Destroy(data);
	if (pixelmapNative)
		OH_PixelmapNative_Release(pixelmapNative);
	if (options)
		OH_PixelmapInitializationOptions_Release(options);
	return FALSE;
}

static UINT ohos_clipboard_send_client_capabilities(CliprdrClientContext* cliprdr)
{
	CLIPRDR_CAPABILITIES capabilities = { 0 };
	CLIPRDR_GENERAL_CAPABILITY_SET generalCapabilitySet = { 0 };

	if (!cliprdr || !cliprdr->ClientCapabilities)
		return ERROR_INVALID_PARAMETER;

	capabilities.cCapabilitiesSets = 1;
	capabilities.capabilitySets = (CLIPRDR_CAPABILITY_SET*)&generalCapabilitySet;
	generalCapabilitySet.capabilitySetType = CB_CAPSTYPE_GENERAL;
	generalCapabilitySet.capabilitySetLength = 12;
	generalCapabilitySet.version = CB_CAPS_VERSION_2;
	generalCapabilitySet.generalFlags = CB_USE_LONG_FORMAT_NAMES;
	return cliprdr->ClientCapabilities(cliprdr, &capabilities);
}

static UINT ohos_clipboard_send_format_list_response(CliprdrClientContext* cliprdr, BOOL accepted)
{
	CLIPRDR_FORMAT_LIST_RESPONSE response = { 0 };

	if (!cliprdr || !cliprdr->ClientFormatListResponse)
		return ERROR_INVALID_PARAMETER;

	response.common.msgType = CB_FORMAT_LIST_RESPONSE;
	response.common.msgFlags = accepted ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
	return cliprdr->ClientFormatListResponse(cliprdr, &response);
}

static UINT ohos_clipboard_send_local_format_list(freerdpOhosClipboard* clipboard,
                                                  const char* reason)
{
	char error[160] = { 0 };
	char* text = NULL;
	char* html = NULL;
	char* htmlPlain = NULL;
	char* uri = NULL;
	CLIPRDR_FORMAT formats[5] = { 0 };
	CLIPRDR_FORMAT_LIST formatList = { 0 };
	const BOOL hasText = ohos_clipboard_read_plain_text(clipboard, &text, error, sizeof(error));
	const BOOL hasHtml = ohos_clipboard_read_html(clipboard, &html, &htmlPlain, NULL, 0);
	const BOOL hasUri = ohos_clipboard_read_uri(clipboard, &uri, NULL, 0);
	const BOOL hasPixelMap = ohos_clipboard_has_pixelmap(clipboard, NULL, 0);
	const BOOL hasUriImage = !hasPixelMap && hasUri &&
	                         ohos_clipboard_uri_may_reference_local_image(uri);
	const BOOL hasImage = hasPixelMap || hasUriImage;
	CliprdrClientContext* cliprdr = ohos_clipboard_snapshot_cliprdr(clipboard);
	UINT32 count = 0;

	if (!cliprdr || !cliprdr->ClientFormatList)
	{
		free(text);
		free(html);
		free(htmlPlain);
		free(uri);
		return CHANNEL_RC_OK;
	}

	if (!hasText && !hasHtml && !hasUri && !hasImage && error[0] != '\0')
		ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard read warning: %s", error);

	if (hasImage)
		formats[count++].formatId = CF_DIB;
	if (!hasImage && (hasText || hasHtml || hasUri))
		formats[count++].formatId = CF_UNICODETEXT;
	if (!hasImage && hasHtml)
	{
		formats[count].formatId = OHOS_CLIPBOARD_FORMAT_HTML;
		formats[count++].formatName = (char*)OHOS_CLIPBOARD_HTML_FORMAT_NAME;
	}
	if (!hasImage && hasUri)
	{
		formats[count].formatId = OHOS_CLIPBOARD_FORMAT_URIW;
		formats[count++].formatName = (char*)OHOS_CLIPBOARD_URIW_FORMAT_NAME;
		formats[count].formatId = OHOS_CLIPBOARD_FORMAT_URI_LIST;
		formats[count++].formatName = (char*)OHOS_CLIPBOARD_URI_LIST_FORMAT_NAME;
	}

	formatList.common.msgType = CB_FORMAT_LIST;
	formatList.common.msgFlags = 0;
	formatList.numFormats = count;
	formatList.formats = (count > 0) ? formats : NULL;

	clipboard->lastLocalFormatCount = formatList.numFormats;
	UINT rc = cliprdr->ClientFormatList(cliprdr, &formatList);
	if (rc == CHANNEL_RC_OK)
	{
		++clipboard->localFormatListCount;
		ohos_clipboard_log(clipboard,
		                   "cliprdr local format list sent: text=%d html=%d uri=%d image=%d "
		                   "uriImage=%d uriKind=%s count=%" PRIu32 " reason=%s",
		                   hasText || hasHtml || hasUri, hasHtml, hasUri, hasImage,
		                   hasUriImage, ohos_clipboard_uri_kind(uri), count,
		                   reason ? reason : "unknown");
	}
	free(text);
	free(html);
	free(htmlPlain);
	free(uri);
	return rc;
}

static UINT ohos_clipboard_send_format_data_request(freerdpOhosClipboard* clipboard,
                                                    UINT32 formatId,
                                                    OHOS_CLIPBOARD_REQUEST_KIND kind)
{
	CLIPRDR_FORMAT_DATA_REQUEST request = { 0 };
	CliprdrClientContext* cliprdr = ohos_clipboard_snapshot_cliprdr(clipboard);

	if (!clipboard || !cliprdr || !cliprdr->ClientFormatDataRequest)
		return ERROR_INVALID_PARAMETER;

	request.common.msgType = CB_FORMAT_DATA_REQUEST;
	request.requestedFormatId = formatId;

	pthread_mutex_lock(&clipboard->lock);
	clipboard->requestedFormatId = formatId;
	clipboard->requestedFormatKind = kind;
	clipboard->lastRequestedFormat = formatId;
	pthread_mutex_unlock(&clipboard->lock);

	ohos_clipboard_log(clipboard, "cliprdr remote data request sent: format=%" PRIu32
	                              " kind=%d",
	                   formatId, kind);
	return cliprdr->ClientFormatDataRequest(cliprdr, &request);
}

static freerdpOhosClipboard* ohos_clipboard_from_cliprdr(CliprdrClientContext* cliprdr)
{
	return cliprdr ? (freerdpOhosClipboard*)cliprdr->custom : NULL;
}

static UINT ohos_clipboard_monitor_ready(CliprdrClientContext* cliprdr,
                                         const CLIPRDR_MONITOR_READY* monitorReady)
{
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);
	UINT rc = CHANNEL_RC_OK;

	if (!clipboard || !monitorReady)
		return ERROR_INVALID_PARAMETER;

	rc = ohos_clipboard_send_client_capabilities(cliprdr);
	if (rc != CHANNEL_RC_OK)
		return rc;

	++clipboard->monitorReadyCount;
	ohos_clipboard_log(clipboard, "cliprdr monitor ready");
	return ohos_clipboard_send_local_format_list(clipboard, "monitor ready");
}

static UINT ohos_clipboard_server_capabilities(CliprdrClientContext* cliprdr,
                                               const CLIPRDR_CAPABILITIES* capabilities)
{
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !capabilities)
		return ERROR_INVALID_PARAMETER;

	ohos_clipboard_log(clipboard, "cliprdr server capabilities received");
	return CHANNEL_RC_OK;
}

static UINT ohos_clipboard_server_format_list(CliprdrClientContext* cliprdr,
                                              const CLIPRDR_FORMAT_LIST* formatList)
{
	UINT32 requested = 0;
	OHOS_CLIPBOARD_REQUEST_KIND requestedKind = OHOS_CLIPBOARD_REQUEST_NONE;
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !formatList)
		return ERROR_INVALID_PARAMETER;

	for (UINT32 index = 0; index < formatList->numFormats; index++)
	{
		const UINT32 formatId = formatList->formats[index].formatId;
		const char* formatName = formatList->formats[index].formatName;
		if (formatName &&
		    (ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_HTML_FORMAT_NAME) ||
		     ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_TEXT_HTML_FORMAT_NAME)))
		{
			requested = formatId;
			requestedKind = OHOS_CLIPBOARD_REQUEST_HTML;
			break;
		}
		if (formatName &&
		    ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_URIW_FORMAT_NAME))
		{
			requested = formatId;
			requestedKind = OHOS_CLIPBOARD_REQUEST_URIW;
			continue;
		}
		if (requestedKind == OHOS_CLIPBOARD_REQUEST_NONE && formatName &&
		    (ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_URI_FORMAT_NAME) ||
		     ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_URI_LIST_FORMAT_NAME)))
		{
			requested = formatId;
			requestedKind = OHOS_CLIPBOARD_REQUEST_URI_LIST;
			continue;
		}
		if (formatId == CF_DIB)
		{
			requested = formatId;
			requestedKind = OHOS_CLIPBOARD_REQUEST_DIB;
			continue;
		}
		if ((requestedKind == OHOS_CLIPBOARD_REQUEST_NONE ||
		     requestedKind == OHOS_CLIPBOARD_REQUEST_TEXT) &&
		    formatId == CF_DIBV5)
		{
			requested = formatId;
			requestedKind = OHOS_CLIPBOARD_REQUEST_DIBV5;
			continue;
		}
		if ((requestedKind == OHOS_CLIPBOARD_REQUEST_NONE ||
		     requestedKind == OHOS_CLIPBOARD_REQUEST_TEXT) &&
		    formatName &&
		    ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_IMAGE_BMP_FORMAT_NAME))
		{
			requested = formatId;
			requestedKind = OHOS_CLIPBOARD_REQUEST_IMAGE_BMP;
			continue;
		}
		if (requestedKind == OHOS_CLIPBOARD_REQUEST_NONE && formatId == CF_UNICODETEXT)
		{
			requested = CF_UNICODETEXT;
			requestedKind = OHOS_CLIPBOARD_REQUEST_TEXT;
		}
		else if (requestedKind == OHOS_CLIPBOARD_REQUEST_NONE && formatId == CF_TEXT)
		{
			requested = CF_TEXT;
			requestedKind = OHOS_CLIPBOARD_REQUEST_TEXT;
		}
		else if (requestedKind == OHOS_CLIPBOARD_REQUEST_NONE && formatId == CF_OEMTEXT)
		{
			requested = CF_OEMTEXT;
			requestedKind = OHOS_CLIPBOARD_REQUEST_TEXT;
		}
	}

	clipboard->lastRemoteFormatCount = formatList->numFormats;
	++clipboard->remoteFormatListCount;
	ohos_clipboard_log(clipboard, "cliprdr server format list received: %" PRIu32,
	                   formatList->numFormats);

	UINT rc = ohos_clipboard_send_format_list_response(cliprdr, TRUE);
	if (rc != CHANNEL_RC_OK)
		return rc;

	if (requested == 0)
	{
		ohos_clipboard_log(clipboard, "cliprdr server format list has no supported format");
		return CHANNEL_RC_OK;
	}
	return ohos_clipboard_send_format_data_request(clipboard, requested, requestedKind);
}

static UINT ohos_clipboard_server_format_list_response(
    CliprdrClientContext* cliprdr, const CLIPRDR_FORMAT_LIST_RESPONSE* response)
{
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !response)
		return ERROR_INVALID_PARAMETER;

	ohos_clipboard_log(clipboard, "cliprdr server accepted local format list");
	return CHANNEL_RC_OK;
}

static UINT ohos_clipboard_server_lock_clipboard_data(
    CliprdrClientContext* cliprdr, const CLIPRDR_LOCK_CLIPBOARD_DATA* lockClipboardData)
{
	return (!cliprdr || !lockClipboardData) ? ERROR_INVALID_PARAMETER : CHANNEL_RC_OK;
}

static UINT ohos_clipboard_server_unlock_clipboard_data(
    CliprdrClientContext* cliprdr, const CLIPRDR_UNLOCK_CLIPBOARD_DATA* unlockClipboardData)
{
	return (!cliprdr || !unlockClipboardData) ? ERROR_INVALID_PARAMETER : CHANNEL_RC_OK;
}

static UINT ohos_clipboard_server_format_data_request(
    CliprdrClientContext* cliprdr, const CLIPRDR_FORMAT_DATA_REQUEST* request)
{
	char error[160] = { 0 };
	char* text = NULL;
	char* html = NULL;
	char* htmlPlain = NULL;
	char* uri = NULL;
	BYTE* bgra = NULL;
	UINT32 imageWidth = 0;
	UINT32 imageHeight = 0;
	BYTE* data = NULL;
	UINT32 dataLen = 0;
	CLIPRDR_FORMAT_DATA_RESPONSE response = { 0 };
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !request || !cliprdr->ClientFormatDataResponse)
		return ERROR_INVALID_PARAMETER;

	if (request->requestedFormatId == OHOS_CLIPBOARD_FORMAT_HTML)
	{
		if (ohos_clipboard_read_html(clipboard, &html, &htmlPlain, error, sizeof(error)))
			data = ohos_clipboard_html_to_ms_html(html, &dataLen);
	}
	else if (request->requestedFormatId == OHOS_CLIPBOARD_FORMAT_URIW)
	{
		if (ohos_clipboard_read_uri(clipboard, &uri, error, sizeof(error)))
			data = ohos_clipboard_utf8_to_utf16le(uri, &dataLen);
	}
	else if (request->requestedFormatId == OHOS_CLIPBOARD_FORMAT_URI_LIST)
	{
		if (ohos_clipboard_read_uri(clipboard, &uri, error, sizeof(error)))
			data = ohos_clipboard_uri_to_uri_list(uri, &dataLen);
	}
	else if (request->requestedFormatId == CF_DIB)
	{
		if (ohos_clipboard_read_pixelmap(clipboard, &bgra, &imageWidth, &imageHeight, error,
		                                  sizeof(error)))
			data = ohos_clipboard_bgra_to_dib(bgra, imageWidth, imageHeight, &dataLen, error,
			                                  sizeof(error));
	}
	else
	{
		BOOL ok = ohos_clipboard_read_plain_text(clipboard, &text, error, sizeof(error));
		if (!ok && ohos_clipboard_read_uri(clipboard, &uri, NULL, 0))
		{
			text = ohos_clipboard_strdup(uri);
			ok = (text != NULL);
		}
		if (!ok && ohos_clipboard_read_html(clipboard, &html, &htmlPlain, NULL, 0))
		{
			text = ohos_clipboard_strdup((htmlPlain && htmlPlain[0] != '\0') ? htmlPlain : html);
			ok = (text != NULL);
		}

		if (ok && request->requestedFormatId == CF_UNICODETEXT)
		{
			data = ohos_clipboard_utf8_to_utf16le(text, &dataLen);
		}
		else if (ok &&
		         (request->requestedFormatId == CF_TEXT || request->requestedFormatId == CF_OEMTEXT))
		{
			dataLen = (UINT32)strlen(text) + 1U;
			data = (BYTE*)calloc(dataLen, sizeof(BYTE));
			if (data)
				memcpy(data, text, dataLen - 1U);
		}
	}

	response.common.msgType = CB_FORMAT_DATA_RESPONSE;
	response.common.msgFlags = data ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
	response.common.dataLen = data ? dataLen : 0;
	response.requestedFormatData = data;

	++clipboard->localRequestCount;
	if (data)
		ohos_clipboard_log(clipboard, "cliprdr local response sent: format=%" PRIu32
		                              " bytes=%" PRIu32,
		                   request->requestedFormatId, dataLen);
	else
		ohos_clipboard_log(clipboard, "cliprdr local request failed: format=%" PRIu32
		                              " error=%s",
		                   request->requestedFormatId, error);

	UINT rc = cliprdr->ClientFormatDataResponse(cliprdr, &response);
	free(data);
	free(bgra);
	free(text);
	free(html);
	free(htmlPlain);
	free(uri);
	return rc;
}

static UINT ohos_clipboard_server_format_data_response(
    CliprdrClientContext* cliprdr, const CLIPRDR_FORMAT_DATA_RESPONSE* response)
{
	char error[160] = { 0 };
	char* text = NULL;
	char* html = NULL;
	char* uri = NULL;
	BYTE* bgra = NULL;
	UINT32 imageWidth = 0;
	UINT32 imageHeight = 0;
	UINT32 requested = 0;
	OHOS_CLIPBOARD_REQUEST_KIND requestedKind = OHOS_CLIPBOARD_REQUEST_NONE;
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !response)
		return ERROR_INVALID_PARAMETER;

	if ((response->common.msgFlags & CB_RESPONSE_FAIL) != 0 || !response->requestedFormatData)
	{
		ohos_clipboard_log(clipboard, "cliprdr remote text response failed: flags=%" PRIu16
		                              " length=%" PRIu32,
		                   response->common.msgFlags, response->common.dataLen);
		return CHANNEL_RC_OK;
	}

	pthread_mutex_lock(&clipboard->lock);
	requested = clipboard->requestedFormatId;
	requestedKind = clipboard->requestedFormatKind;
	clipboard->requestedFormatKind = OHOS_CLIPBOARD_REQUEST_NONE;
	pthread_mutex_unlock(&clipboard->lock);

	if (requestedKind == OHOS_CLIPBOARD_REQUEST_DIB ||
	    requestedKind == OHOS_CLIPBOARD_REQUEST_DIBV5 ||
	    requestedKind == OHOS_CLIPBOARD_REQUEST_IMAGE_BMP)
	{
		if (!ohos_clipboard_bitmap_to_bgra(response->requestedFormatData,
		                                   response->common.dataLen, requestedKind, &bgra,
		                                   &imageWidth, &imageHeight, error, sizeof(error)))
		{
			ohos_clipboard_log(clipboard, "cliprdr remote image decode failed: %s", error);
			return CHANNEL_RC_OK;
		}
		if (!ohos_clipboard_write_pixelmap(clipboard, bgra, imageWidth, imageHeight,
		                                   response->common.dataLen, error, sizeof(error)))
		{
			ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard pixelmap write failed: %s",
			                   error);
			free(bgra);
			return ERROR_INTERNAL_ERROR;
		}
		++clipboard->remoteResponseCount;
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote image copied to HarmonyOS Pasteboard: %" PRIu32
		                   "x%" PRIu32 " sourceBytes=%" PRIu32,
		                   imageWidth, imageHeight, response->common.dataLen);
		free(bgra);
		return CHANNEL_RC_OK;
	}

	if (requestedKind == OHOS_CLIPBOARD_REQUEST_HTML)
	{
		html = ohos_clipboard_extract_ms_html(response->requestedFormatData,
		                                     response->common.dataLen);
		if (!html || html[0] == '\0')
		{
			free(html);
			ohos_clipboard_log(clipboard, "cliprdr remote html response was empty");
			return CHANNEL_RC_OK;
		}
		if (!ohos_clipboard_write_html(clipboard, html, NULL, error, sizeof(error)))
		{
			ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard html write failed: %s", error);
			free(html);
			return ERROR_INTERNAL_ERROR;
		}
		++clipboard->remoteResponseCount;
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote html copied to HarmonyOS Pasteboard: %zu bytes",
		                   strlen(html));
		free(html);
		return CHANNEL_RC_OK;
	}

	if (requestedKind == OHOS_CLIPBOARD_REQUEST_URIW)
	{
		uri = ohos_clipboard_utf16le_to_utf8(response->requestedFormatData,
		                                    response->common.dataLen);
	}
	else if (requestedKind == OHOS_CLIPBOARD_REQUEST_URI_LIST)
	{
		uri = ohos_clipboard_extract_uri_list_first(response->requestedFormatData,
		                                           response->common.dataLen);
		if (!uri)
			uri = ohos_clipboard_bytes_to_string(response->requestedFormatData,
			                                    response->common.dataLen);
	}
	if (requestedKind == OHOS_CLIPBOARD_REQUEST_URIW ||
	    requestedKind == OHOS_CLIPBOARD_REQUEST_URI_LIST)
	{
		if (!uri || uri[0] == '\0')
		{
			free(uri);
			ohos_clipboard_log(clipboard, "cliprdr remote uri response was empty");
			return CHANNEL_RC_OK;
		}
		if (!ohos_clipboard_write_uri(clipboard, uri, error, sizeof(error)))
		{
			ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard uri write failed: %s", error);
			free(uri);
			return ERROR_INTERNAL_ERROR;
		}
		++clipboard->remoteResponseCount;
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote uri copied to HarmonyOS Pasteboard: %zu bytes",
		                   strlen(uri));
		free(uri);
		return CHANNEL_RC_OK;
	}

	if (requested == CF_UNICODETEXT)
	{
		text = ohos_clipboard_utf16le_to_utf8(response->requestedFormatData,
		                                     response->common.dataLen);
	}
	else if (requested == CF_TEXT || requested == CF_OEMTEXT)
	{
		const char* bytes = (const char*)response->requestedFormatData;
		const size_t length = strnlen(bytes, response->common.dataLen);
		text = (char*)calloc(length + 1U, sizeof(char));
		if (text)
			memcpy(text, bytes, length);
	}

	if (!text || text[0] == '\0')
	{
		free(text);
		ohos_clipboard_log(clipboard, "cliprdr remote text response was empty");
		return CHANNEL_RC_OK;
	}

	if (!ohos_clipboard_write_plain_text(clipboard, text, error, sizeof(error)))
	{
		ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard write failed: %s", error);
		free(text);
		return ERROR_INTERNAL_ERROR;
	}

	++clipboard->remoteResponseCount;
	ohos_clipboard_log(clipboard, "cliprdr remote text copied to HarmonyOS Pasteboard: %zu bytes",
	                   strlen(text));
	free(text);
	return CHANNEL_RC_OK;
}

static UINT ohos_clipboard_server_file_contents_request(
    CliprdrClientContext* cliprdr, const CLIPRDR_FILE_CONTENTS_REQUEST* request)
{
	return (!cliprdr || !request) ? ERROR_INVALID_PARAMETER : CHANNEL_RC_OK;
}

static UINT ohos_clipboard_server_file_contents_response(
    CliprdrClientContext* cliprdr, const CLIPRDR_FILE_CONTENTS_RESPONSE* response)
{
	return (!cliprdr || !response) ? ERROR_INVALID_PARAMETER : CHANNEL_RC_OK;
}

static void ohos_clipboard_attach_cliprdr(freerdpOhosClipboard* clipboard,
                                          CliprdrClientContext* cliprdr)
{
	if (!clipboard || !cliprdr)
		return;

	pthread_mutex_lock(&clipboard->lock);
	clipboard->cliprdr = cliprdr;
	cliprdr->custom = clipboard;
	cliprdr->MonitorReady = ohos_clipboard_monitor_ready;
	cliprdr->ServerCapabilities = ohos_clipboard_server_capabilities;
	cliprdr->ServerFormatList = ohos_clipboard_server_format_list;
	cliprdr->ServerFormatListResponse = ohos_clipboard_server_format_list_response;
	cliprdr->ServerLockClipboardData = ohos_clipboard_server_lock_clipboard_data;
	cliprdr->ServerUnlockClipboardData = ohos_clipboard_server_unlock_clipboard_data;
	cliprdr->ServerFormatDataRequest = ohos_clipboard_server_format_data_request;
	cliprdr->ServerFormatDataResponse = ohos_clipboard_server_format_data_response;
	cliprdr->ServerFileContentsRequest = ohos_clipboard_server_file_contents_request;
	cliprdr->ServerFileContentsResponse = ohos_clipboard_server_file_contents_response;
	pthread_mutex_unlock(&clipboard->lock);

	++clipboard->channelConnectCount;
	ohos_clipboard_log(clipboard, "cliprdr connected to HarmonyOS Pasteboard backend");
}

static void ohos_clipboard_detach_cliprdr(freerdpOhosClipboard* clipboard,
                                          CliprdrClientContext* cliprdr)
{
	if (!clipboard)
		return;

	pthread_mutex_lock(&clipboard->lock);
	if (clipboard->cliprdr == cliprdr)
	{
		if (clipboard->cliprdr)
			clipboard->cliprdr->custom = NULL;
		clipboard->cliprdr = NULL;
		clipboard->requestedFormatId = 0;
		clipboard->requestedFormatKind = OHOS_CLIPBOARD_REQUEST_NONE;
	}
	pthread_mutex_unlock(&clipboard->lock);

	++clipboard->channelDisconnectCount;
	ohos_clipboard_log(clipboard, "cliprdr disconnected from HarmonyOS Pasteboard backend");
}

static void ohos_clipboard_channel_connected(void* context, const ChannelConnectedEventArgs* event)
{
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_context(context);

	if (!clipboard || !event || !event->name)
		return;
	if (strcmp(event->name, CLIPRDR_SVC_CHANNEL_NAME) == 0)
		ohos_clipboard_attach_cliprdr(clipboard, (CliprdrClientContext*)event->pInterface);
}

static void ohos_clipboard_channel_disconnected(void* context,
                                                const ChannelDisconnectedEventArgs* event)
{
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_context(context);

	if (!clipboard || !event || !event->name)
		return;
	if (strcmp(event->name, CLIPRDR_SVC_CHANNEL_NAME) == 0)
		ohos_clipboard_detach_cliprdr(clipboard, (CliprdrClientContext*)event->pInterface);
}

static void ohos_clipboard_on_pasteboard_finalize(void* context)
{
	(void)context;
}

static void ohos_clipboard_handle_pasteboard_changed(freerdpOhosClipboard* clipboard,
                                                     Pasteboard_NotifyType type)
{
	BOOL suppress = FALSE;

	if (!clipboard || type != NOTIFY_LOCAL_DATA_CHANGE)
		return;

	++clipboard->pasteboardChangeCount;
	pthread_mutex_lock(&clipboard->lock);
	if (clipboard->ignoreLocalChanges > 0 &&
	    ohos_clipboard_now_ms() <= clipboard->ignoreLocalChangesUntil)
	{
		clipboard->ignoreLocalChanges--;
		if (clipboard->ignoreLocalChanges == 0)
			clipboard->ignoreLocalChangesUntil = 0;
		suppress = TRUE;
	}
	else if (clipboard->ignoreLocalChanges > 0)
	{
		clipboard->ignoreLocalChanges = 0;
		clipboard->ignoreLocalChangesUntil = 0;
	}
	pthread_mutex_unlock(&clipboard->lock);

	if (suppress)
	{
		++clipboard->suppressedChangeCount;
		ohos_clipboard_log(clipboard,
		                   "HarmonyOS Pasteboard local change suppressed after remote clipboard write");
		return;
	}

	(void)ohos_clipboard_send_local_format_list(clipboard, "pasteboard changed");
}

static void ohos_clipboard_on_pasteboard_changed(void* context, Pasteboard_NotifyType type)
{
	ohos_clipboard_handle_pasteboard_changed((freerdpOhosClipboard*)context, type);
}

static void ohos_clipboard_destroy_pasteboard(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	if (clipboard->pasteboard && clipboard->observer && clipboard->pasteboardSubscribed)
	{
		(void)OH_Pasteboard_Unsubscribe(clipboard->pasteboard, NOTIFY_LOCAL_DATA_CHANGE,
		                                clipboard->observer);
		clipboard->pasteboardSubscribed = FALSE;
	}
	if (clipboard->observer)
	{
		(void)OH_PasteboardObserver_Destroy(clipboard->observer);
		clipboard->observer = NULL;
	}
	if (clipboard->pasteboard)
	{
		OH_Pasteboard_Destroy(clipboard->pasteboard);
		clipboard->pasteboard = NULL;
	}
}

static void ohos_clipboard_create_pasteboard(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	clipboard->pasteboard = OH_Pasteboard_Create();
	if (!clipboard->pasteboard)
	{
		ohos_clipboard_log(clipboard,
		                   "HarmonyOS Pasteboard create failed; cliprdr will advertise no local formats");
		return;
	}
	ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard created for cliprdr backend");

	clipboard->observer = OH_PasteboardObserver_Create();
	if (!clipboard->observer)
	{
		ohos_clipboard_log(
		    clipboard, "HarmonyOS Pasteboard observer create failed; local clipboard changes require reconnect");
		return;
	}

	int rc = OH_PasteboardObserver_SetData(clipboard->observer, clipboard,
	                                       ohos_clipboard_on_pasteboard_changed,
	                                       ohos_clipboard_on_pasteboard_finalize);
	if (rc != ERR_OK)
	{
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard observer setup failed: %d", rc);
		return;
	}

	rc = OH_Pasteboard_Subscribe(clipboard->pasteboard, NOTIFY_LOCAL_DATA_CHANGE,
	                             clipboard->observer);
	if (rc == ERR_OK)
	{
		clipboard->pasteboardSubscribed = TRUE;
		ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard observer subscribed");
	}
	else
	{
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard subscribe warning: %d", rc);
	}
}

freerdpOhosClipboard* freerdp_ohos_clipboard_new(void)
{
	freerdpOhosClipboard* clipboard = (freerdpOhosClipboard*)calloc(1, sizeof(freerdpOhosClipboard));
	if (!clipboard)
		return NULL;

	if (pthread_mutex_init(&clipboard->lock, NULL) != 0)
	{
		free(clipboard);
		return NULL;
	}
	clipboard->lockInitialized = TRUE;
	return clipboard;
}

BOOL freerdp_ohos_clipboard_register(freerdpOhosClipboard* clipboard, rdpContext* context,
                                     const FREERDP_OHOS_CLIPBOARD_CONFIG* config,
                                     char* errorBuffer, size_t errorBufferSize)
{
	if (errorBuffer && errorBufferSize > 0)
		errorBuffer[0] = '\0';
	if (!clipboard || !context || !context->pubSub || !config || !config->PubSubSubscribe ||
	    !config->PubSubUnsubscribe)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "invalid OHOS clipboard registration arguments");
		return FALSE;
	}

	clipboard->context = context;
	clipboard->config = *config;

	ohos_clipboard_registry_add(clipboard);

	int rc = clipboard->config.PubSubSubscribe(context->pubSub, "ChannelConnected",
	                                           ohos_clipboard_channel_connected);
	if (rc < 0)
	{
		ohos_clipboard_registry_remove(clipboard);
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "subscribe ChannelConnected for clipboard failed: %d", rc);
		return FALSE;
	}
	clipboard->channelConnectedSubscribed = TRUE;

	rc = clipboard->config.PubSubSubscribe(context->pubSub, "ChannelDisconnected",
	                                       ohos_clipboard_channel_disconnected);
	if (rc < 0)
	{
		freerdp_ohos_clipboard_unregister(clipboard);
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "subscribe ChannelDisconnected for clipboard failed: %d", rc);
		return FALSE;
	}
	clipboard->channelDisconnectedSubscribed = TRUE;

	ohos_clipboard_create_pasteboard(clipboard);
	++clipboard->registerCount;
	ohos_clipboard_log(clipboard, "cliprdr bridge subscribed to FreeRDP channel events");
	return TRUE;
}

void freerdp_ohos_clipboard_unregister(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	if (clipboard->cliprdr)
		ohos_clipboard_detach_cliprdr(clipboard, clipboard->cliprdr);

	ohos_clipboard_destroy_pasteboard(clipboard);

	if (clipboard->config.PubSubUnsubscribe && clipboard->context && clipboard->context->pubSub)
	{
		if (clipboard->channelConnectedSubscribed)
		{
			(void)clipboard->config.PubSubUnsubscribe(clipboard->context->pubSub,
			                                          "ChannelConnected",
			                                          ohos_clipboard_channel_connected);
			clipboard->channelConnectedSubscribed = FALSE;
		}
		if (clipboard->channelDisconnectedSubscribed)
		{
			(void)clipboard->config.PubSubUnsubscribe(clipboard->context->pubSub,
			                                          "ChannelDisconnected",
			                                          ohos_clipboard_channel_disconnected);
			clipboard->channelDisconnectedSubscribed = FALSE;
		}
	}

	ohos_clipboard_registry_remove(clipboard);
	clipboard->context = NULL;
	clipboard->requestedFormatId = 0;
	clipboard->requestedFormatKind = OHOS_CLIPBOARD_REQUEST_NONE;
	++clipboard->unregisterCount;
}

void freerdp_ohos_clipboard_free(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	freerdp_ohos_clipboard_unregister(clipboard);
	if (clipboard->lockInitialized)
	{
		pthread_mutex_destroy(&clipboard->lock);
		clipboard->lockInitialized = FALSE;
	}
	free(clipboard);
}

const char* freerdp_ohos_clipboard_get_diagnostics(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return "OHOS clipboard stats: unavailable";

	(void)snprintf(clipboard->diagnostics, sizeof(clipboard->diagnostics),
	               "OHOS clipboard stats: registered=%" PRIu64 " unregistered=%" PRIu64
	               " channelConnect=%" PRIu64 " channelDisconnect=%" PRIu64
	               " monitorReady=%" PRIu64 " localFormatList=%" PRIu64
	               " remoteFormatList=%" PRIu64 " localRequests=%" PRIu64
	               " remoteResponses=%" PRIu64 " pasteboardRead=%" PRIu64
	               " pasteboardWrite=%" PRIu64 " pasteboardChanges=%" PRIu64
	               " suppressedChanges=%" PRIu64 " errors=%" PRIu64
	               " lastError=%" PRIu32 " lastRequestedFormat=%" PRIu32
	               " lastRemoteFormats=%" PRIu32 " lastLocalFormats=%" PRIu32
	               " lastTextBytes=%" PRIu32 " lastHtmlBytes=%" PRIu32
	               " lastUriBytes=%" PRIu32 " lastImageBytes=%" PRIu32,
	               clipboard->registerCount, clipboard->unregisterCount,
	               clipboard->channelConnectCount, clipboard->channelDisconnectCount,
	               clipboard->monitorReadyCount, clipboard->localFormatListCount,
	               clipboard->remoteFormatListCount, clipboard->localRequestCount,
	               clipboard->remoteResponseCount, clipboard->pasteboardReadCount,
	               clipboard->pasteboardWriteCount, clipboard->pasteboardChangeCount,
	               clipboard->suppressedChangeCount, clipboard->errorCount,
	               clipboard->lastError, clipboard->lastRequestedFormat,
	               clipboard->lastRemoteFormatCount, clipboard->lastLocalFormatCount,
	               clipboard->lastTextBytes, clipboard->lastHtmlBytes, clipboard->lastUriBytes,
	               clipboard->lastImageBytes);
	return clipboard->diagnostics;
}
