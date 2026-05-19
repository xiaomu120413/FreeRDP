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
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include <database/pasteboard/oh_pasteboard.h>
#include <database/pasteboard/oh_pasteboard_err_code.h>
#include <database/udmf/udmf.h>
#include <database/udmf/udmf_err_code.h>
#include <database/udmf/udmf_meta.h>
#include <database/udmf/uds.h>
#include <multimedia/image_framework/image/pixelmap_native.h>

#include <freerdp/channels/cliprdr.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/event.h>
#include <freerdp/utils/cliprdr_utils.h>
#include <winpr/clipboard.h>
#include <winpr/file.h>
#include <winpr/image.h>
#include <winpr/string.h>
#include <winpr/user.h>

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
#define OHOS_CLIPBOARD_FORMAT_IMAGE_PNG 0xC004U
#define OHOS_CLIPBOARD_FORMAT_IMAGE_JPEG 0xC005U
#define OHOS_CLIPBOARD_FORMAT_IMAGE_WEBP 0xC006U
#define OHOS_CLIPBOARD_FORMAT_FILE_GROUP_DESCRIPTOR_W 0xC007U
#define OHOS_CLIPBOARD_MAX_URI_IMAGE_BYTES (64U * 1024U * 1024U)
#define OHOS_CLIPBOARD_IMAGE_SIGNATURE_BYTES 12U
#define OHOS_CLIPBOARD_IMAGE_REQUEST_TIMEOUT_MS 10000ULL
#define OHOS_CLIPBOARD_MAX_LOCAL_IMAGE_PIXELS (4096U * 4096U)
#define OHOS_CLIPBOARD_MAX_LOCAL_IMAGE_BYTES (64U * 1024U * 1024U)
#define OHOS_CLIPBOARD_LCS_SRGB 0x73524742U
#define OHOS_CLIPBOARD_LCS_GM_IMAGES 0x00000004U
#define OHOS_CLIPBOARD_SANDBOX_FILES_DIR "/data/storage/el2/base/files"
#define OHOS_CLIPBOARD_CACHE_DIR_NAME "clipboard-cache"

static const char OHOS_CLIPBOARD_HTML_FORMAT_NAME[] = "HTML Format";
static const char OHOS_CLIPBOARD_TEXT_HTML_FORMAT_NAME[] = "text/html";
static const char OHOS_CLIPBOARD_URIW_FORMAT_NAME[] = "UniformResourceLocatorW";
static const char OHOS_CLIPBOARD_URI_FORMAT_NAME[] = "UniformResourceLocator";
static const char OHOS_CLIPBOARD_URI_LIST_FORMAT_NAME[] = "text/uri-list";
static const char OHOS_CLIPBOARD_IMAGE_BMP_FORMAT_NAME[] = "image/bmp";
static const char OHOS_CLIPBOARD_IMAGE_PNG_FORMAT_NAME[] = "image/png";
static const char OHOS_CLIPBOARD_IMAGE_JPEG_FORMAT_NAME[] = "image/jpeg";
static const char OHOS_CLIPBOARD_IMAGE_WEBP_FORMAT_NAME[] = "image/webp";
static const char OHOS_CLIPBOARD_FILE_GROUP_DESCRIPTOR_W_FORMAT_NAME[] = "FileGroupDescriptorW";

typedef enum
{
	OHOS_CLIPBOARD_REQUEST_NONE = 0,
	OHOS_CLIPBOARD_REQUEST_TEXT,
	OHOS_CLIPBOARD_REQUEST_HTML,
	OHOS_CLIPBOARD_REQUEST_URIW,
	OHOS_CLIPBOARD_REQUEST_URI_LIST,
	OHOS_CLIPBOARD_REQUEST_DIB,
	OHOS_CLIPBOARD_REQUEST_DIBV5,
	OHOS_CLIPBOARD_REQUEST_IMAGE_BMP,
	OHOS_CLIPBOARD_REQUEST_IMAGE_PNG,
	OHOS_CLIPBOARD_REQUEST_IMAGE_JPEG,
	OHOS_CLIPBOARD_REQUEST_IMAGE_WEBP,
	OHOS_CLIPBOARD_REQUEST_FILE_LIST
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
	BOOL imageCondInitialized;
	pthread_mutex_t lock;
	pthread_cond_t imageCond;
	pthread_t imageWorkerThread;
	UINT32 requestedFormatId;
	OHOS_CLIPBOARD_REQUEST_KIND requestedFormatKind;
	UINT32 ignoreLocalChanges;
	UINT64 ignoreLocalChangesUntil;
	BOOL imageWorkerStarted;
	BOOL imageWorkerStop;
	BOOL imageExportPending;
	BOOL imageExportBusy;
	BOOL imageCacheReady;
	BOOL imageExportFailed;
	UINT64 localClipboardSerial;
	UINT64 imageExportRequestedSerial;
	UINT64 imageExportBusySerial;
	UINT64 cachedImageSerial;
	UINT64 failedImageSerial;
	BYTE* cachedImageDib;
	UINT32 cachedImageDibSize;
	BYTE* cachedImageDibV5;
	UINT32 cachedImageDibV5Size;
	BYTE* cachedImageEncoded;
	UINT32 cachedImageEncodedSize;
	UINT32 cachedImageEncodedFormatId;
	UINT32 cachedImageWidth;
	UINT32 cachedImageHeight;
	UINT32 cachedImageSourceBytes;
	char lastImageExportError[160];
	BOOL remoteFileTransferActive;
	UINT32 remoteFileStreamId;
	UINT32 remoteFileListIndex;
	UINT32 remoteFileExpectedBytes;
	UINT32 remoteFileNextStreamId;
	char* remoteFileName;

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

static const char* ohos_clipboard_pasteboard_status_name(int status)
{
	switch (status)
	{
		case ERR_OK:
			return "ERR_OK";
		case ERR_PERMISSION_ERROR:
			return "ERR_PERMISSION_ERROR";
		case ERR_INVALID_PARAMETER:
			return "ERR_INVALID_PARAMETER";
		case ERR_DEVICE_NOT_SUPPORTED:
			return "ERR_DEVICE_NOT_SUPPORTED";
		case ERR_INNER_ERROR:
			return "ERR_INNER_ERROR";
		case ERR_BUSY:
			return "ERR_BUSY";
		case ERR_PASTEBOARD_COPY_FILE_ERROR:
			return "ERR_PASTEBOARD_COPY_FILE_ERROR";
		case ERR_PASTEBOARD_PROGRESS_START_ERROR:
			return "ERR_PASTEBOARD_PROGRESS_START_ERROR";
		case ERR_PASTEBOARD_PROGRESS_ABNORMAL:
			return "ERR_PASTEBOARD_PROGRESS_ABNORMAL";
		case ERR_PASTEBOARD_GET_DATA_FAILED:
			return "ERR_PASTEBOARD_GET_DATA_FAILED";
		default:
			return "unknown";
	}
}

static size_t ohos_clipboard_safe_strlen(const char* value)
{
	return value ? strlen(value) : 0;
}

static void ohos_clipboard_trace_pasteboard_state(freerdpOhosClipboard* clipboard,
                                                  const char* reason)
{
	if (!clipboard || !clipboard->pasteboard)
		return;

	char source[128] = { 0 };
	const BOOL hasData = OH_Pasteboard_HasData(clipboard->pasteboard) ? TRUE : FALSE;
	const BOOL remoteData = OH_Pasteboard_IsRemoteData(clipboard->pasteboard) ? TRUE : FALSE;
	const int sourceRc =
	    OH_Pasteboard_GetDataSource(clipboard->pasteboard, source, (unsigned int)sizeof(source));
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard state before %s: hasData=%d remoteData=%d "
	                   "sourceRc=%d source=%s",
	                   reason ? reason : "unknown", hasData, remoteData, sourceRc,
	                   source[0] ? source : "none");
}

static void ohos_clipboard_trace_udmf_data(freerdpOhosClipboard* clipboard, const char* reason,
                                           OH_UdmfData* data)
{
	if (!clipboard || !data)
		return;

	const int recordCount = OH_UdmfData_GetRecordCount(data);
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard data after %s: records=%d local=%d "
	                   "hasPlain=%d hasHtml=%d hasHyperlink=%d hasFileUri=%d hasPixelMap=%d "
	                   "hasImage=%d",
	                   reason ? reason : "unknown", recordCount, OH_UdmfData_IsLocal(data) ? 1 : 0,
	                   OH_UdmfData_HasType(data, UDMF_META_PLAIN_TEXT) ? 1 : 0,
	                   OH_UdmfData_HasType(data, UDMF_META_HTML) ? 1 : 0,
	                   OH_UdmfData_HasType(data, UDMF_META_HYPERLINK) ? 1 : 0,
	                   OH_UdmfData_HasType(data, UDMF_META_GENERAL_FILE_URI) ? 1 : 0,
	                   OH_UdmfData_HasType(data, UDMF_META_OPENHARMONY_PIXEL_MAP) ? 1 : 0,
	                   OH_UdmfData_HasType(data, UDMF_META_IMAGE) ? 1 : 0);
}

static OH_UdmfData* ohos_clipboard_get_pasteboard_data(freerdpOhosClipboard* clipboard,
                                                       const char* reason, int* outStatus)
{
	int status = ERR_OK;
	OH_UdmfData* data = NULL;

	if (outStatus)
		*outStatus = ERR_OK;
	if (!clipboard || !clipboard->pasteboard)
	{
		if (outStatus)
			*outStatus = ERR_INVALID_PARAMETER;
		return NULL;
	}

	ohos_clipboard_trace_pasteboard_state(clipboard, reason);
	data = OH_Pasteboard_GetData(clipboard->pasteboard, &status);
	if (outStatus)
		*outStatus = status;
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard GetData after %s: status=%d(%s) data=%p",
	                   reason ? reason : "unknown", status,
	                   ohos_clipboard_pasteboard_status_name(status), (void*)data);
	if (status == ERR_OK && data)
		ohos_clipboard_trace_udmf_data(clipboard, reason, data);
	return data;
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

static BOOL ohos_clipboard_ends_with_ignore_case(const char* value, const char* suffix)
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

static UINT32 ohos_clipboard_image_format_id_from_signature(const BYTE* data, UINT32 size)
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

static const char* ohos_clipboard_image_format_name_from_id(UINT32 formatId)
{
	if (formatId == OHOS_CLIPBOARD_FORMAT_IMAGE_PNG)
		return OHOS_CLIPBOARD_IMAGE_PNG_FORMAT_NAME;
	if (formatId == OHOS_CLIPBOARD_FORMAT_IMAGE_JPEG)
		return OHOS_CLIPBOARD_IMAGE_JPEG_FORMAT_NAME;
	if (formatId == OHOS_CLIPBOARD_FORMAT_IMAGE_WEBP)
		return OHOS_CLIPBOARD_IMAGE_WEBP_FORMAT_NAME;
	return NULL;
}

static const char* ohos_clipboard_image_extension_from_id(UINT32 formatId)
{
	if (formatId == OHOS_CLIPBOARD_FORMAT_IMAGE_PNG)
		return ".png";
	if (formatId == OHOS_CLIPBOARD_FORMAT_IMAGE_JPEG)
		return ".jpg";
	if (formatId == OHOS_CLIPBOARD_FORMAT_IMAGE_WEBP)
		return ".webp";
	return ".img";
}

static BOOL ohos_clipboard_read_local_file_signature(const char* path, BYTE* signature,
                                                     UINT32 signatureSize, UINT32* outSize);

static UINT32 ohos_clipboard_image_format_id_from_uri(const char* uri)
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

static char* ohos_clipboard_wchar_to_utf8(const WCHAR* value, size_t maxChars)
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

static UINT64 ohos_clipboard_file_descriptor_size(const FILEDESCRIPTORW* file)
{
	if (!file)
		return 0;
	return (((UINT64)file->nFileSizeHigh) << 32U) | (UINT64)file->nFileSizeLow;
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

static BYTE* ohos_clipboard_read_local_file_range(const char* path, UINT64 offset,
                                                  UINT32 requestedBytes, UINT32* outSize,
                                                  char* errorBuffer, size_t errorBufferSize)
{
	if (!outSize)
		return NULL;
	*outSize = 0;

	if (!path || path[0] == '\0' || requestedBytes == 0U)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "invalid local file range request");
		return NULL;
	}
	if (offset > (UINT64)LONG_MAX)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "local file range offset too large");
		return NULL;
	}

	FILE* file = fopen(path, "rb");
	if (!file)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "open local clipboard file failed: %s", strerror(errno));
		return NULL;
	}

	if (fseek(file, (long)offset, SEEK_SET) != 0)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "seek local clipboard file failed");
		fclose(file);
		return NULL;
	}

	BYTE* data = (BYTE*)malloc(requestedBytes);
	if (!data)
	{
		fclose(file);
		return NULL;
	}

	const size_t read = fread(data, 1U, requestedBytes, file);
	fclose(file);
	if (read == 0U)
	{
		free(data);
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "read local clipboard file range failed");
		return NULL;
	}

	*outSize = (UINT32)read;
	return data;
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

static char* ohos_clipboard_join_path(const char* left, const char* right)
{
	if (!left || !right)
		return NULL;

	size_t leftLength = strlen(left);
	while (leftLength > 1U &&
	       (left[leftLength - 1U] == '/' || left[leftLength - 1U] == '\\'))
		leftLength--;

	while (*right == '/' || *right == '\\')
		right++;
	const size_t rightLength = strlen(right);
	const BOOL needSeparator =
	    (leftLength > 0U && left[leftLength - 1U] != '/' && left[leftLength - 1U] != '\\');
	const size_t totalLength = leftLength + (needSeparator ? 1U : 0U) + rightLength;
	char* path = (char*)calloc(totalLength + 1U, sizeof(char));
	if (!path)
		return NULL;

	size_t offset = 0;
	if (leftLength > 0U)
	{
		memcpy(path, left, leftLength);
		offset += leftLength;
	}
	if (needSeparator)
		path[offset++] = '/';
	if (rightLength > 0U)
		memcpy(path + offset, right, rightLength);
	return path;
}

static BOOL ohos_clipboard_ensure_directory(freerdpOhosClipboard* clipboard, const char* path,
                                            char* errorBuffer, size_t errorBufferSize)
{
	if (!path || path[0] == '\0')
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "empty clipboard cache directory");
		return FALSE;
	}

	if (mkdir(path, 0700) == 0)
		return TRUE;
	if (errno == EEXIST)
	{
		struct stat st = { 0 };
		if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
			return TRUE;
	}

	ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
	                         "create clipboard cache directory failed: %s", strerror(errno));
	return FALSE;
}

static BOOL ohos_clipboard_is_safe_cache_file_char(unsigned char c)
{
	return isalnum(c) || c == '.' || c == '_' || c == '-';
}

static char* ohos_clipboard_sanitize_cache_file_name(const char* fileName)
{
	const char* baseName = fileName ? fileName : "";
	for (const char* cursor = baseName; *cursor; cursor++)
	{
		if (*cursor == '/' || *cursor == '\\')
			baseName = cursor + 1U;
	}
	if (baseName[0] == '\0')
		baseName = "clipboard-image";

	const char* extension = strrchr(baseName, '.');
	size_t extensionLength = 0;
	if (extension && extension != baseName)
	{
		extensionLength = strlen(extension);
		if (extensionLength > 16U)
		{
			extension = NULL;
			extensionLength = 0;
		}
	}

	const size_t stemLength = extension ? (size_t)(extension - baseName) : strlen(baseName);
	const size_t maxStemLength = 96U;
	const size_t maxNameLength = maxStemLength + 16U;
	char* safeName = (char*)calloc(maxNameLength + 1U, sizeof(char));
	if (!safeName)
		return NULL;

	size_t out = 0;
	BOOL hasStemChar = FALSE;
	for (size_t index = 0; index < stemLength && out < maxStemLength; index++)
	{
		const unsigned char c = (unsigned char)baseName[index];
		const char mapped = ohos_clipboard_is_safe_cache_file_char(c) ? (char)c : '_';
		if (isalnum(c))
			hasStemChar = TRUE;
		safeName[out++] = mapped;
	}
	if (!hasStemChar)
	{
		const char fallback[] = "clipboard-image";
		out = strlen(fallback);
		memcpy(safeName, fallback, out);
	}

	for (size_t index = 0; index < extensionLength && out < maxNameLength; index++)
	{
		const unsigned char c = (unsigned char)extension[index];
		safeName[out++] = ohos_clipboard_is_safe_cache_file_char(c) ? (char)c : '_';
	}
	safeName[out] = '\0';
	return safeName;
}

static char* ohos_clipboard_file_uri_from_path(const char* path)
{
	if (!path || path[0] == '\0')
		return NULL;

	const char prefix[] = "file://";
	const size_t prefixLength = strlen(prefix);
	const size_t pathLength = strlen(path);
	char* uri = (char*)calloc(prefixLength + pathLength + 1U, sizeof(char));
	if (!uri)
		return NULL;
	memcpy(uri, prefix, prefixLength);
	memcpy(uri + prefixLength, path, pathLength);
	return uri;
}

static char* ohos_clipboard_cache_remote_file(freerdpOhosClipboard* clipboard,
                                              const char* fileName, UINT32 streamId,
                                              const BYTE* data, UINT32 dataSize,
                                              char* errorBuffer, size_t errorBufferSize)
{
	char* cacheDir = NULL;
	char* safeName = NULL;
	char* cacheName = NULL;
	char* cachePath = NULL;
	char* cacheUri = NULL;
	FILE* file = NULL;

	if (!data || dataSize == 0U)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "empty remote file content");
		return NULL;
	}

	cacheDir =
	    ohos_clipboard_join_path(OHOS_CLIPBOARD_SANDBOX_FILES_DIR, OHOS_CLIPBOARD_CACHE_DIR_NAME);
	safeName = ohos_clipboard_sanitize_cache_file_name(fileName);
	if (!cacheDir || !safeName)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "clipboard sandbox cache allocation failed");
		goto fail;
	}

	if (!ohos_clipboard_ensure_directory(clipboard, cacheDir, errorBuffer, errorBufferSize))
		goto fail;

	const UINT64 cacheTime = ohos_clipboard_now_ms();
	const int cacheNameLength =
	    snprintf(NULL, 0, "remote_%08" PRIx32 "_%" PRIu64 "_%s", streamId, cacheTime,
	             safeName);
	if (cacheNameLength <= 0)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "clipboard sandbox cache name failed");
		goto fail;
	}
	cacheName = (char*)calloc((size_t)cacheNameLength + 1U, sizeof(char));
	if (!cacheName)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "clipboard sandbox cache name allocation failed");
		goto fail;
	}
	(void)snprintf(cacheName, (size_t)cacheNameLength + 1U,
	               "remote_%08" PRIx32 "_%" PRIu64 "_%s", streamId, cacheTime, safeName);

	cachePath = ohos_clipboard_join_path(cacheDir, cacheName);
	if (!cachePath)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "clipboard sandbox cache path allocation failed");
		goto fail;
	}

	file = fopen(cachePath, "wb");
	if (!file)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "open clipboard sandbox cache file failed: %s",
		                         strerror(errno));
		goto fail;
	}
	const size_t written = fwrite(data, 1U, dataSize, file);
	if (written != dataSize)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "write clipboard sandbox cache file failed");
		goto fail;
	}
	if (fclose(file) != 0)
	{
		file = NULL;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "close clipboard sandbox cache file failed: %s",
		                         strerror(errno));
		goto fail;
	}
	file = NULL;

	cacheUri = ohos_clipboard_file_uri_from_path(cachePath);
	if (!cacheUri)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "clipboard sandbox cache uri allocation failed");
		goto fail;
	}

	free(cacheDir);
	free(safeName);
	free(cacheName);
	free(cachePath);
	return cacheUri;

fail:
	if (file)
		fclose(file);
	if (cachePath)
		remove(cachePath);
	free(cacheDir);
	free(safeName);
	free(cacheName);
	free(cachePath);
	return NULL;
}

static char* ohos_clipboard_local_image_file_name_from_path(const char* path, UINT32 formatId)
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

static BOOL ohos_clipboard_get_local_image_file_info_from_uri(
    const char* uri, char** outPath, char** outFileName, UINT64* outSize,
    UINT32* outFormatId, char* errorBuffer, size_t errorBufferSize)
{
	char* path = NULL;
	char* fileName = NULL;
	struct stat st = { 0 };
	BYTE signature[OHOS_CLIPBOARD_IMAGE_SIGNATURE_BYTES] = { 0 };
	UINT32 signatureSize = 0;
	UINT32 formatId = 0;

	if (outPath)
		*outPath = NULL;
	if (outFileName)
		*outFileName = NULL;
	if (outSize)
		*outSize = 0;
	if (outFormatId)
		*outFormatId = 0;
	if (!uri)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "empty local clipboard image uri");
		return FALSE;
	}

	path = ohos_clipboard_uri_to_local_path(uri);
	if (!path)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "clipboard image uri is not a local file: kind=%s",
		                         ohos_clipboard_uri_kind(uri));
		return FALSE;
	}

	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "clipboard image local file stat failed: %s",
		                         strerror(errno));
		goto fail;
	}

	const UINT64 fileSize = (UINT64)st.st_size;
	if (fileSize > OHOS_CLIPBOARD_MAX_URI_IMAGE_BYTES || fileSize > UINT32_MAX)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "clipboard image local file too large: %" PRIu64,
		                         fileSize);
		goto fail;
	}

	if (!ohos_clipboard_read_local_file_signature(path, signature, sizeof(signature),
	                                             &signatureSize))
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "clipboard image local file signature read failed");
		goto fail;
	}
	formatId = ohos_clipboard_image_format_id_from_signature(signature, signatureSize);
	if (formatId == 0U)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "clipboard local file is not supported png/jpg/webp");
		goto fail;
	}

	if (outFileName)
	{
		fileName = ohos_clipboard_local_image_file_name_from_path(path, formatId);
		if (!fileName)
		{
			ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
			                         "clipboard local file name allocation failed");
			goto fail;
		}
		*outFileName = fileName;
		fileName = NULL;
	}
	if (outPath)
	{
		*outPath = path;
		path = NULL;
	}
	if (outSize)
		*outSize = fileSize;
	if (outFormatId)
		*outFormatId = formatId;

	free(fileName);
	free(path);
	return TRUE;

fail:
	free(fileName);
	free(path);
	return FALSE;
}

static BOOL ohos_clipboard_set_file_descriptor_name(FILEDESCRIPTORW* descriptor,
                                                    const char* fileName)
{
	if (!descriptor || !fileName || fileName[0] == '\0')
		return FALSE;

	WCHAR* wideName = ConvertUtf8ToWCharAlloc(fileName, NULL);
	if (!wideName)
		return FALSE;

	size_t index = 0;
	for (; index + 1U < ARRAYSIZE(descriptor->cFileName) && wideName[index] != 0; index++)
		descriptor->cFileName[index] = wideName[index];
	descriptor->cFileName[index] = 0;
	free(wideName);
	return TRUE;
}

static BYTE* ohos_clipboard_make_local_file_descriptor_data(const char* fileName,
                                                           UINT64 fileSize,
                                                           UINT32* outSize,
                                                           char* errorBuffer,
                                                           size_t errorBufferSize)
{
	BYTE* data = NULL;
	FILEDESCRIPTORW descriptor = { 0 };

	if (!outSize)
		return NULL;
	*outSize = 0;
	if (!fileName || fileSize == 0U || fileSize > UINT32_MAX)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "invalid local file descriptor input");
		return NULL;
	}

	descriptor.dwFlags = FD_ATTRIBUTES | FD_FILESIZE;
	descriptor.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
	descriptor.nFileSizeHigh = (DWORD)((fileSize >> 32U) & 0xFFFFFFFFU);
	descriptor.nFileSizeLow = (DWORD)(fileSize & 0xFFFFFFFFU);
	if (!ohos_clipboard_set_file_descriptor_name(&descriptor, fileName))
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "local file descriptor name conversion failed");
		return NULL;
	}

	const UINT rc = cliprdr_serialize_file_list_ex(
	    CB_STREAM_FILECLIP_ENABLED | CB_FILECLIP_NO_FILE_PATHS, &descriptor, 1U, &data,
	    outSize);
	if (rc != NO_ERROR)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "local file descriptor serialize failed: rc=%" PRIu32, rc);
		free(data);
		return NULL;
	}
	return data;
}

static BYTE* ohos_clipboard_read_local_image_file_data_from_uri(
    const char* uri, UINT32 requestedFormatId, UINT32* outSize, char* errorBuffer,
    size_t errorBufferSize)
{
	char* path = NULL;
	UINT64 fileSize = 0;
	UINT32 fileFormatId = 0;
	BYTE* data = NULL;

	if (!outSize)
		return NULL;
	*outSize = 0;

	if (!ohos_clipboard_get_local_image_file_info_from_uri(uri, &path, NULL, &fileSize,
	                                                       &fileFormatId, errorBuffer,
	                                                       errorBufferSize))
		return NULL;
	if (fileFormatId != requestedFormatId)
	{
		ohos_clipboard_set_error(NULL, errorBuffer, errorBufferSize,
		                         "clipboard local image format mismatch: have=%s requested=%s",
		                         ohos_clipboard_image_format_name_from_id(fileFormatId)
		                             ? ohos_clipboard_image_format_name_from_id(fileFormatId)
		                             : "none",
		                         ohos_clipboard_image_format_name_from_id(requestedFormatId)
		                             ? ohos_clipboard_image_format_name_from_id(requestedFormatId)
		                             : "none");
		free(path);
		return NULL;
	}

	data = ohos_clipboard_read_local_file(path, OHOS_CLIPBOARD_MAX_URI_IMAGE_BYTES, outSize,
	                                      errorBuffer, errorBufferSize);
	free(path);
	(void)fileSize;
	return data;
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

static BYTE* ohos_clipboard_bgra_to_dibv5(const BYTE* bgra, UINT32 width, UINT32 height,
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

	data = ohos_clipboard_get_pasteboard_data(clipboard, "read plain text", &status);
	if (status != ERR_OK || !data)
	{
		clipboard->lastError = (UINT32)status;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_GetData status=%d(%s)", status,
		                         ohos_clipboard_pasteboard_status_name(status));
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

	data = ohos_clipboard_get_pasteboard_data(clipboard, "read html", &status);
	if (status != ERR_OK || !data)
	{
		clipboard->lastError = (UINT32)status;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_GetData status=%d(%s)", status,
		                         ohos_clipboard_pasteboard_status_name(status));
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

	data = ohos_clipboard_get_pasteboard_data(clipboard, "read uri", &status);
	if (status != ERR_OK || !data)
	{
		clipboard->lastError = (UINT32)status;
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "OH_Pasteboard_GetData status=%d(%s)", status,
		                         ohos_clipboard_pasteboard_status_name(status));
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

static BOOL ohos_clipboard_read_uri_image_with_source(
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

static void ohos_clipboard_clear_image_cache_locked(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	free(clipboard->cachedImageDib);
	free(clipboard->cachedImageDibV5);
	free(clipboard->cachedImageEncoded);
	clipboard->cachedImageDib = NULL;
	clipboard->cachedImageDibSize = 0;
	clipboard->cachedImageDibV5 = NULL;
	clipboard->cachedImageDibV5Size = 0;
	clipboard->cachedImageEncoded = NULL;
	clipboard->cachedImageEncodedSize = 0;
	clipboard->cachedImageEncodedFormatId = 0;
	clipboard->cachedImageWidth = 0;
	clipboard->cachedImageHeight = 0;
	clipboard->cachedImageSourceBytes = 0;
	clipboard->imageCacheReady = FALSE;
	clipboard->imageExportFailed = FALSE;
	clipboard->lastImageExportError[0] = '\0';
}

static void ohos_clipboard_clear_remote_file_transfer_locked(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	free(clipboard->remoteFileName);
	clipboard->remoteFileName = NULL;
	clipboard->remoteFileTransferActive = FALSE;
	clipboard->remoteFileStreamId = 0;
	clipboard->remoteFileListIndex = 0;
	clipboard->remoteFileExpectedBytes = 0;
}

static BOOL ohos_clipboard_validate_local_image_size(UINT32 width, UINT32 height,
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

static void* ohos_clipboard_image_worker_main(void* arg)
{
	freerdpOhosClipboard* clipboard = (freerdpOhosClipboard*)arg;

	for (;;)
	{
		UINT64 serial = 0;
		BYTE* bgra = NULL;
		BYTE* dib = NULL;
		BYTE* dibv5 = NULL;
		BYTE* encoded = NULL;
		UINT32 width = 0;
		UINT32 height = 0;
		UINT32 dibSize = 0;
		UINT32 dibv5Size = 0;
		UINT32 encodedSize = 0;
		UINT32 encodedFormatId = 0;
		UINT32 sourceBytes = 0;
		BOOL ok = FALSE;
		BOOL stored = FALSE;
		char error[160] = { 0 };

		pthread_mutex_lock(&clipboard->lock);
		while (!clipboard->imageWorkerStop && !clipboard->imageExportPending)
			pthread_cond_wait(&clipboard->imageCond, &clipboard->lock);
		if (clipboard->imageWorkerStop)
		{
			pthread_mutex_unlock(&clipboard->lock);
			break;
		}

		serial = clipboard->imageExportRequestedSerial;
		clipboard->imageExportPending = FALSE;
		clipboard->imageExportBusy = TRUE;
		clipboard->imageExportBusySerial = serial;
		pthread_mutex_unlock(&clipboard->lock);

		if (ohos_clipboard_read_uri_image_with_source(
		        clipboard, &bgra, &width, &height, &encoded, &encodedSize, &encodedFormatId,
		        error, sizeof(error)) &&
		    ohos_clipboard_validate_local_image_size(width, height, error, sizeof(error)))
		{
			const size_t imageBytes = (size_t)width * (size_t)height * 4U;
			sourceBytes = clipboard->lastImageBytes;
			if (sourceBytes == 0U || sourceBytes > OHOS_CLIPBOARD_MAX_LOCAL_IMAGE_BYTES)
				sourceBytes = (imageBytes <= UINT32_MAX) ? (UINT32)imageBytes : 0U;

			dib = ohos_clipboard_bgra_to_dib(bgra, width, height, &dibSize, error,
			                                 sizeof(error));
			if (dib)
				dibv5 = ohos_clipboard_bgra_to_dibv5(bgra, width, height, &dibv5Size,
				                                     error, sizeof(error));
			ok = (dib && dibv5);
		}

		pthread_mutex_lock(&clipboard->lock);
		clipboard->imageExportBusy = FALSE;
		clipboard->imageExportBusySerial = 0;
		if (!clipboard->imageWorkerStop && serial == clipboard->localClipboardSerial)
		{
			ohos_clipboard_clear_image_cache_locked(clipboard);
			if (ok)
			{
				clipboard->cachedImageDib = dib;
				clipboard->cachedImageDibSize = dibSize;
				clipboard->cachedImageDibV5 = dibv5;
				clipboard->cachedImageDibV5Size = dibv5Size;
				clipboard->cachedImageEncoded = encoded;
				clipboard->cachedImageEncodedSize = encodedSize;
				clipboard->cachedImageEncodedFormatId = encodedFormatId;
				clipboard->cachedImageWidth = width;
				clipboard->cachedImageHeight = height;
				clipboard->cachedImageSourceBytes = sourceBytes;
				clipboard->cachedImageSerial = serial;
				clipboard->imageCacheReady = TRUE;
				stored = TRUE;
				dib = NULL;
				dibv5 = NULL;
				encoded = NULL;
			}
			else
			{
				clipboard->failedImageSerial = serial;
				clipboard->imageExportFailed = TRUE;
				(void)snprintf(clipboard->lastImageExportError,
				               sizeof(clipboard->lastImageExportError), "%s",
				               error[0] ? error : "local clipboard image export failed");
			}
		}
		pthread_cond_broadcast(&clipboard->imageCond);
		pthread_mutex_unlock(&clipboard->lock);

		if (stored)
			ohos_clipboard_log(clipboard,
			                   "HarmonyOS Pasteboard image cached for cliprdr: %" PRIu32
			                   "x%" PRIu32 " dib=%" PRIu32 " dibv5=%" PRIu32
			                   " encoded=%s/%" PRIu32,
			                   width, height, dibSize, dibv5Size,
			                   ohos_clipboard_image_format_name_from_id(encodedFormatId)
			                       ? ohos_clipboard_image_format_name_from_id(encodedFormatId)
			                       : "none",
			                   encodedSize);
		else if (ok)
			ohos_clipboard_log(clipboard,
			                   "HarmonyOS Pasteboard stale image export discarded: serial=%" PRIu64,
			                   serial);
		else
			ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard image cache failed: %s",
			                   error[0] ? error : "unknown error");

		free(bgra);
		free(dib);
		free(dibv5);
		free(encoded);
	}

	return NULL;
}

static BOOL ohos_clipboard_start_image_worker_locked(freerdpOhosClipboard* clipboard)
{
	if (!clipboard || !clipboard->imageCondInitialized)
		return FALSE;
	if (clipboard->imageWorkerStarted)
		return TRUE;

	clipboard->imageWorkerStop = FALSE;
	if (pthread_create(&clipboard->imageWorkerThread, NULL, ohos_clipboard_image_worker_main,
	                   clipboard) != 0)
	{
		(void)snprintf(clipboard->lastImageExportError,
		               sizeof(clipboard->lastImageExportError),
		               "local clipboard image worker start failed");
		clipboard->imageExportFailed = TRUE;
		clipboard->failedImageSerial = clipboard->localClipboardSerial;
		return FALSE;
	}

	clipboard->imageWorkerStarted = TRUE;
	return TRUE;
}

static BOOL ohos_clipboard_schedule_image_export_locked(freerdpOhosClipboard* clipboard,
                                                        UINT64 serial)
{
	if (!clipboard || !ohos_clipboard_start_image_worker_locked(clipboard))
		return FALSE;
	if (clipboard->imageCacheReady && clipboard->cachedImageSerial == serial)
		return TRUE;
	if (clipboard->imageExportFailed && clipboard->failedImageSerial == serial)
		return FALSE;
	if (clipboard->imageExportPending && clipboard->imageExportRequestedSerial == serial)
		return TRUE;
	if (clipboard->imageExportBusy && clipboard->imageExportBusySerial == serial)
		return TRUE;

	clipboard->imageExportPending = TRUE;
	clipboard->imageExportRequestedSerial = serial;
	clipboard->imageExportFailed = FALSE;
	clipboard->lastImageExportError[0] = '\0';
	pthread_cond_signal(&clipboard->imageCond);
	return TRUE;
}

static BOOL ohos_clipboard_schedule_image_export(freerdpOhosClipboard* clipboard,
                                                 const char* reason)
{
	if (!clipboard)
		return FALSE;

	pthread_mutex_lock(&clipboard->lock);
	const UINT64 serial = clipboard->localClipboardSerial;
	const BOOL scheduled = ohos_clipboard_schedule_image_export_locked(clipboard, serial);
	pthread_mutex_unlock(&clipboard->lock);

	if (scheduled)
		ohos_clipboard_log(clipboard,
		                   "HarmonyOS Pasteboard image export scheduled: serial=%" PRIu64
		                   " reason=%s",
		                   serial, reason ? reason : "unknown");
	return scheduled;
}

static void ohos_clipboard_note_local_clipboard_changed(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	pthread_mutex_lock(&clipboard->lock);
	clipboard->localClipboardSerial++;
	clipboard->imageExportPending = FALSE;
	ohos_clipboard_clear_image_cache_locked(clipboard);
	pthread_cond_broadcast(&clipboard->imageCond);
	pthread_mutex_unlock(&clipboard->lock);
}

static void ohos_clipboard_stop_image_worker(freerdpOhosClipboard* clipboard)
{
	BOOL joinWorker = FALSE;

	if (!clipboard || !clipboard->imageCondInitialized)
		return;

	pthread_mutex_lock(&clipboard->lock);
	if (clipboard->imageWorkerStarted)
	{
		clipboard->imageWorkerStop = TRUE;
		clipboard->imageExportPending = TRUE;
		pthread_cond_broadcast(&clipboard->imageCond);
		joinWorker = TRUE;
	}
	pthread_mutex_unlock(&clipboard->lock);

	if (joinWorker)
		pthread_join(clipboard->imageWorkerThread, NULL);

	pthread_mutex_lock(&clipboard->lock);
	clipboard->imageWorkerStarted = FALSE;
	clipboard->imageWorkerStop = FALSE;
	clipboard->imageExportPending = FALSE;
	clipboard->imageExportBusy = FALSE;
	clipboard->imageExportRequestedSerial = 0;
	clipboard->imageExportBusySerial = 0;
	ohos_clipboard_clear_image_cache_locked(clipboard);
	pthread_mutex_unlock(&clipboard->lock);
}

static BOOL ohos_clipboard_make_realtime_deadline(UINT64 timeoutMs, struct timespec* deadline)
{
	if (!deadline || clock_gettime(CLOCK_REALTIME, deadline) != 0)
		return FALSE;

	deadline->tv_sec += (time_t)(timeoutMs / 1000ULL);
	deadline->tv_nsec += (long)((timeoutMs % 1000ULL) * 1000000ULL);
	if (deadline->tv_nsec >= 1000000000L)
	{
		deadline->tv_sec++;
		deadline->tv_nsec -= 1000000000L;
	}
	return TRUE;
}

static BYTE* ohos_clipboard_copy_cached_image_locked(freerdpOhosClipboard* clipboard,
                                                     UINT32 formatId, UINT32* outSize)
{
	const BYTE* source = NULL;
	UINT32 size = 0;

	if (!clipboard || !outSize || !clipboard->imageCacheReady)
		return NULL;
	*outSize = 0;

	if (formatId == CF_DIB)
	{
		source = clipboard->cachedImageDib;
		size = clipboard->cachedImageDibSize;
	}
	else if (formatId == CF_DIBV5)
	{
		source = clipboard->cachedImageDibV5;
		size = clipboard->cachedImageDibV5Size;
	}
	else if (formatId == clipboard->cachedImageEncodedFormatId)
	{
		source = clipboard->cachedImageEncoded;
		size = clipboard->cachedImageEncodedSize;
	}

	if (!source || size == 0U)
		return NULL;

	BYTE* copy = (BYTE*)malloc(size);
	if (!copy)
		return NULL;
	memcpy(copy, source, size);
	*outSize = size;
	return copy;
}

static BOOL ohos_clipboard_get_cached_image_data(freerdpOhosClipboard* clipboard,
                                                 UINT32 formatId, BYTE** outData,
                                                 UINT32* outSize, char* errorBuffer,
                                                 size_t errorBufferSize)
{
	if (!outData || !outSize)
		return FALSE;
	*outData = NULL;
	*outSize = 0;

	if (!clipboard || (formatId != CF_DIB && formatId != CF_DIBV5 &&
	                   formatId != OHOS_CLIPBOARD_FORMAT_IMAGE_PNG &&
	                   formatId != OHOS_CLIPBOARD_FORMAT_IMAGE_JPEG &&
	                   formatId != OHOS_CLIPBOARD_FORMAT_IMAGE_WEBP))
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "unsupported local image format=%" PRIu32, formatId);
		return FALSE;
	}

	struct timespec deadline = { 0 };
	if (!ohos_clipboard_make_realtime_deadline(OHOS_CLIPBOARD_IMAGE_REQUEST_TIMEOUT_MS,
	                                           &deadline))
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "local clipboard image timeout setup failed");
		return FALSE;
	}

	pthread_mutex_lock(&clipboard->lock);
	const UINT64 serial = clipboard->localClipboardSerial;
	if (!ohos_clipboard_schedule_image_export_locked(clipboard, serial))
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "%s",
		                         clipboard->lastImageExportError[0]
		                             ? clipboard->lastImageExportError
		                             : "local clipboard image worker unavailable");
		pthread_mutex_unlock(&clipboard->lock);
		return FALSE;
	}

	for (;;)
	{
		if (clipboard->imageCacheReady && clipboard->cachedImageSerial == serial)
		{
			*outData = ohos_clipboard_copy_cached_image_locked(clipboard, formatId, outSize);
			if (*outData)
			{
				pthread_mutex_unlock(&clipboard->lock);
				return TRUE;
			}
			ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
			                         "local clipboard image cache copy failed");
			pthread_mutex_unlock(&clipboard->lock);
			return FALSE;
		}

		if (clipboard->imageExportFailed && clipboard->failedImageSerial == serial)
		{
			ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "%s",
			                         clipboard->lastImageExportError[0]
			                             ? clipboard->lastImageExportError
			                             : "local clipboard image export failed");
			pthread_mutex_unlock(&clipboard->lock);
			return FALSE;
		}

		if (serial != clipboard->localClipboardSerial)
		{
			ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
			                         "local clipboard changed while exporting image");
			pthread_mutex_unlock(&clipboard->lock);
			return FALSE;
		}

		const int rc = pthread_cond_timedwait(&clipboard->imageCond, &clipboard->lock,
		                                      &deadline);
		if (rc == ETIMEDOUT)
		{
			ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
			                         "local clipboard image export timed out after %" PRIu64
			                         "ms",
			                         (UINT64)OHOS_CLIPBOARD_IMAGE_REQUEST_TIMEOUT_MS);
			pthread_mutex_unlock(&clipboard->lock);
			return FALSE;
		}
		if (rc != 0)
		{
			ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
			                         "local clipboard image wait failed: %d", rc);
			pthread_mutex_unlock(&clipboard->lock);
			return FALSE;
		}
	}
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
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard SetData plain text: status=%d(%s) bytes=%zu",
	                   rc, ohos_clipboard_pasteboard_status_name(rc),
	                   ohos_clipboard_safe_strlen(text));
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
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard SetData html: status=%d(%s) htmlBytes=%zu "
	                   "plainBytes=%zu",
	                   rc, ohos_clipboard_pasteboard_status_name(rc),
	                   ohos_clipboard_safe_strlen(htmlValue),
	                   ohos_clipboard_safe_strlen(plainValue));
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
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard SetData uri: status=%d(%s) bytes=%zu kind=%s", rc,
	                   ohos_clipboard_pasteboard_status_name(rc),
	                   ohos_clipboard_safe_strlen(uriValue), ohos_clipboard_uri_kind(uriValue));
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

static BOOL ohos_clipboard_write_pixelmap_with_uri(freerdpOhosClipboard* clipboard,
                                                   const BYTE* bgra, UINT32 width,
                                                   UINT32 height, UINT32 sourceBytes,
                                                   const char* fileUriValue,
                                                   char* errorBuffer, size_t errorBufferSize)
{
	int rc = UDMF_E_OK;
	Image_ErrorCode imageRc = IMAGE_SUCCESS;
	OH_Pixelmap_InitializationOptions* options = NULL;
	OH_PixelmapNative* pixelmapNative = NULL;
	OH_UdsPixelMap* pixelMap = NULL;
	OH_UdsFileUri* fileUri = NULL;
	OH_UdmfRecord* record = NULL;
	OH_UdmfData* data = NULL;
	const BOOL includeFileUri = (fileUriValue && fileUriValue[0] != '\0');

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
	if (includeFileUri)
		fileUri = OH_UdsFileUri_Create();
	record = OH_UdmfRecord_Create();
	data = OH_UdmfData_Create();
	if (!pixelMap || !record || !data || (includeFileUri && !fileUri))
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize, "UDMF allocation failed");
		goto fail;
	}

	rc = OH_UdsPixelMap_SetPixelMap(pixelMap, pixelmapNative);
	if (rc == UDMF_E_OK)
		rc = OH_UdmfRecord_AddPixelMap(record, pixelMap);
	if (rc == UDMF_E_OK && includeFileUri)
		rc = OH_UdsFileUri_SetFileUri(fileUri, fileUriValue);
	if (rc == UDMF_E_OK && includeFileUri)
		rc = OH_UdmfRecord_AddFileUri(record, fileUri);
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
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard SetData pixelmap: status=%d(%s) size=%" PRIu32
	                   "x%" PRIu32 " sourceBytes=%" PRIu32 " includeFileUri=%d uriBytes=%zu",
	                   rc, ohos_clipboard_pasteboard_status_name(rc), width, height, sourceBytes,
	                   includeFileUri,
	                   includeFileUri ? ohos_clipboard_safe_strlen(fileUriValue) : 0);
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
	if (includeFileUri)
		clipboard->lastUriBytes = (UINT32)strlen(fileUriValue);
	if (pixelMap)
		OH_UdsPixelMap_Destroy(pixelMap);
	if (fileUri)
		OH_UdsFileUri_Destroy(fileUri);
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
	if (fileUri)
		OH_UdsFileUri_Destroy(fileUri);
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

static BOOL ohos_clipboard_write_pixelmap(freerdpOhosClipboard* clipboard, const BYTE* bgra,
                                          UINT32 width, UINT32 height, UINT32 sourceBytes,
                                          char* errorBuffer, size_t errorBufferSize)
{
	return ohos_clipboard_write_pixelmap_with_uri(clipboard, bgra, width, height, sourceBytes,
	                                             NULL, errorBuffer, errorBufferSize);
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
	generalCapabilitySet.generalFlags =
	    CB_USE_LONG_FORMAT_NAMES | CB_STREAM_FILECLIP_ENABLED | CB_FILECLIP_NO_FILE_PATHS;
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
	char* localImageFileName = NULL;
	CLIPRDR_FORMAT formats[10] = { 0 };
	CLIPRDR_FORMAT_LIST formatList = { 0 };
	const BOOL hasText = ohos_clipboard_read_plain_text(clipboard, &text, error, sizeof(error));
	const BOOL hasHtml = ohos_clipboard_read_html(clipboard, &html, &htmlPlain, NULL, 0);
	const BOOL hasUri = ohos_clipboard_read_uri(clipboard, &uri, NULL, 0);
	const BOOL hasUriImage = hasUri && ohos_clipboard_uri_may_reference_local_image(uri);
	const BOOL hasImage = hasUriImage;
	UINT32 uriImageFormatId = hasUriImage ? ohos_clipboard_image_format_id_from_uri(uri) : 0;
	UINT32 localImageFileFormatId = 0;
	UINT64 localImageFileSize = 0;
	const BOOL hasUriImageFile =
	    hasUriImage &&
	    ohos_clipboard_get_local_image_file_info_from_uri(
	        uri, NULL, &localImageFileName, &localImageFileSize, &localImageFileFormatId, NULL,
	        0);
	CliprdrClientContext* cliprdr = ohos_clipboard_snapshot_cliprdr(clipboard);
	UINT32 count = 0;

	if (hasUriImageFile && uriImageFormatId == 0U)
		uriImageFormatId = localImageFileFormatId;

	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard format probe: reason=%s text=%d textBytes=%zu "
	                   "html=%d htmlBytes=%zu htmlPlainBytes=%zu uri=%d uriBytes=%zu "
	                   "uriKind=%s uriImage=%d file=%d fileName=%s fileBytes=%" PRIu64
	                   " imageFormat=%s error=%s",
	                   reason ? reason : "unknown", hasText, ohos_clipboard_safe_strlen(text),
	                   hasHtml, ohos_clipboard_safe_strlen(html),
	                   ohos_clipboard_safe_strlen(htmlPlain), hasUri,
	                   ohos_clipboard_safe_strlen(uri), ohos_clipboard_uri_kind(uri),
	                   hasUriImage, hasUriImageFile,
	                   localImageFileName ? localImageFileName : "none", localImageFileSize,
	                   ohos_clipboard_image_format_name_from_id(uriImageFormatId)
	                       ? ohos_clipboard_image_format_name_from_id(uriImageFormatId)
	                       : "none",
	                   error[0] ? error : "none");

	if (!cliprdr || !cliprdr->ClientFormatList)
	{
		free(text);
		free(html);
		free(htmlPlain);
		free(uri);
		free(localImageFileName);
		return CHANNEL_RC_OK;
	}

	if (!hasText && !hasHtml && !hasUri && !hasImage && error[0] != '\0')
		ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard read warning: %s", error);

	if (hasUriImageFile)
	{
		formats[count].formatId = OHOS_CLIPBOARD_FORMAT_FILE_GROUP_DESCRIPTOR_W;
		formats[count++].formatName =
		    (char*)OHOS_CLIPBOARD_FILE_GROUP_DESCRIPTOR_W_FORMAT_NAME;
	}
	if (hasImage && uriImageFormatId != 0U)
	{
		formats[count].formatId = uriImageFormatId;
		formats[count++].formatName =
		    (char*)ohos_clipboard_image_format_name_from_id(uriImageFormatId);
	}
	if (hasImage && !hasUriImageFile)
		formats[count++].formatId = CF_DIBV5;
	if (hasImage && !hasUriImageFile)
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

	if (hasImage && !hasUriImageFile)
		(void)ohos_clipboard_schedule_image_export(clipboard, reason);
	else
		ohos_clipboard_note_local_clipboard_changed(clipboard);

	clipboard->lastLocalFormatCount = formatList.numFormats;
	UINT rc = cliprdr->ClientFormatList(cliprdr, &formatList);
	if (rc == CHANNEL_RC_OK)
	{
		++clipboard->localFormatListCount;
		ohos_clipboard_log(clipboard,
		                   "cliprdr local format list sent: text=%d html=%d uri=%d image=%d "
		                   "uriImage=%d file=%d fileBytes=%" PRIu64
		                   " dib=%d uriKind=%s encoded=%s count=%" PRIu32 " reason=%s",
		                   hasText || hasHtml || hasUri, hasHtml, hasUri, hasImage,
		                   hasUriImage, hasUriImageFile, localImageFileSize,
		                   hasImage && !hasUriImageFile,
		                   ohos_clipboard_uri_kind(uri),
		                   ohos_clipboard_image_format_name_from_id(uriImageFormatId)
		                       ? ohos_clipboard_image_format_name_from_id(uriImageFormatId)
		                       : "none",
		                   count,
		                   reason ? reason : "unknown");
	}
	free(text);
	free(html);
	free(htmlPlain);
	free(uri);
	free(localImageFileName);
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
	UINT32 imageRequested = 0;
	OHOS_CLIPBOARD_REQUEST_KIND imageRequestedKind = OHOS_CLIPBOARD_REQUEST_NONE;
	UINT32 imagePriority = 0;
	UINT32 htmlRequested = 0;
	UINT32 uriWRequested = 0;
	UINT32 uriListRequested = 0;
	UINT32 textRequested = 0;
	OHOS_CLIPBOARD_REQUEST_KIND textRequestedKind = OHOS_CLIPBOARD_REQUEST_NONE;
	UINT32 fileGroupRequested = 0;
	BOOL sawFileClipboard = FALSE;
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !formatList)
		return ERROR_INVALID_PARAMETER;

	pthread_mutex_lock(&clipboard->lock);
	ohos_clipboard_clear_remote_file_transfer_locked(clipboard);
	pthread_mutex_unlock(&clipboard->lock);

	for (UINT32 index = 0; index < formatList->numFormats; index++)
	{
		const UINT32 formatId = formatList->formats[index].formatId;
		const char* formatName = formatList->formats[index].formatName;
		ohos_clipboard_log(clipboard,
		                   "cliprdr server format[%" PRIu32 "]: id=%" PRIu32
		                   " name=%s",
		                   index, formatId, formatName ? formatName : "");
		if (formatName &&
		    ohos_clipboard_equals_ignore_case(
		        formatName, OHOS_CLIPBOARD_FILE_GROUP_DESCRIPTOR_W_FORMAT_NAME))
		{
			sawFileClipboard = TRUE;
			fileGroupRequested = formatId;
			continue;
		}
		if (formatName &&
		    (ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_HTML_FORMAT_NAME) ||
		     ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_TEXT_HTML_FORMAT_NAME)))
		{
			htmlRequested = formatId;
			continue;
		}
		if (formatName &&
		    ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_URIW_FORMAT_NAME))
		{
			uriWRequested = formatId;
			continue;
		}
		if (formatName &&
		    (ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_URI_FORMAT_NAME) ||
		     ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_URI_LIST_FORMAT_NAME)))
		{
			uriListRequested = formatId;
			continue;
		}
		if (formatId == CF_DIB && imagePriority < 80U)
		{
			imageRequested = formatId;
			imageRequestedKind = OHOS_CLIPBOARD_REQUEST_DIB;
			imagePriority = 80U;
			continue;
		}
		if (formatId == CF_DIBV5 && imagePriority < 70U)
		{
			imageRequested = formatId;
			imageRequestedKind = OHOS_CLIPBOARD_REQUEST_DIBV5;
			imagePriority = 70U;
			continue;
		}
		if (formatName &&
		    ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_IMAGE_BMP_FORMAT_NAME))
		{
			if (imagePriority < 60U)
			{
				imageRequested = formatId;
				imageRequestedKind = OHOS_CLIPBOARD_REQUEST_IMAGE_BMP;
				imagePriority = 60U;
			}
			continue;
		}
		if (formatName &&
		    ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_IMAGE_PNG_FORMAT_NAME))
		{
			if (imagePriority < 100U)
			{
				imageRequested = formatId;
				imageRequestedKind = OHOS_CLIPBOARD_REQUEST_IMAGE_PNG;
				imagePriority = 100U;
			}
			continue;
		}
		if (formatName &&
		    ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_IMAGE_JPEG_FORMAT_NAME))
		{
			if (imagePriority < 100U)
			{
				imageRequested = formatId;
				imageRequestedKind = OHOS_CLIPBOARD_REQUEST_IMAGE_JPEG;
				imagePriority = 100U;
			}
			continue;
		}
		if (formatName &&
		    ohos_clipboard_equals_ignore_case(formatName, OHOS_CLIPBOARD_IMAGE_WEBP_FORMAT_NAME))
		{
			if (imagePriority < 100U)
			{
				imageRequested = formatId;
				imageRequestedKind = OHOS_CLIPBOARD_REQUEST_IMAGE_WEBP;
				imagePriority = 100U;
			}
			continue;
		}
		if (formatId == CF_UNICODETEXT && textRequestedKind == OHOS_CLIPBOARD_REQUEST_NONE)
		{
			textRequested = CF_UNICODETEXT;
			textRequestedKind = OHOS_CLIPBOARD_REQUEST_TEXT;
		}
		else if (formatId == CF_TEXT && textRequestedKind == OHOS_CLIPBOARD_REQUEST_NONE)
		{
			textRequested = CF_TEXT;
			textRequestedKind = OHOS_CLIPBOARD_REQUEST_TEXT;
		}
		else if (formatId == CF_OEMTEXT && textRequestedKind == OHOS_CLIPBOARD_REQUEST_NONE)
		{
			textRequested = CF_OEMTEXT;
			textRequestedKind = OHOS_CLIPBOARD_REQUEST_TEXT;
		}
	}

	if (fileGroupRequested != 0U)
	{
		requested = fileGroupRequested;
		requestedKind = OHOS_CLIPBOARD_REQUEST_FILE_LIST;
	}
	else if (imageRequested != 0U)
	{
		requested = imageRequested;
		requestedKind = imageRequestedKind;
	}
	else if (htmlRequested != 0U)
	{
		requested = htmlRequested;
		requestedKind = OHOS_CLIPBOARD_REQUEST_HTML;
	}
	else if (uriWRequested != 0U)
	{
		requested = uriWRequested;
		requestedKind = OHOS_CLIPBOARD_REQUEST_URIW;
	}
	else if (uriListRequested != 0U)
	{
		requested = uriListRequested;
		requestedKind = OHOS_CLIPBOARD_REQUEST_URI_LIST;
	}
	else if (textRequested != 0U)
	{
		requested = textRequested;
		requestedKind = textRequestedKind;
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
		if (sawFileClipboard)
			ohos_clipboard_log(
			    clipboard,
			    "cliprdr server offered file clipboard but no usable FileGroupDescriptorW format id");
		ohos_clipboard_log(clipboard, "cliprdr server format list has no supported format");
		return CHANNEL_RC_OK;
	}
	ohos_clipboard_log(clipboard, "cliprdr server format selected: format=%" PRIu32
	                              " kind=%d",
	                   requested, requestedKind);
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
	else if (request->requestedFormatId == OHOS_CLIPBOARD_FORMAT_FILE_GROUP_DESCRIPTOR_W)
	{
		char* fileName = NULL;
		UINT64 fileSize = 0;
		UINT32 fileFormatId = 0;
		if (ohos_clipboard_read_uri(clipboard, &uri, error, sizeof(error)) &&
		    ohos_clipboard_get_local_image_file_info_from_uri(uri, NULL, &fileName, &fileSize,
		                                                     &fileFormatId, error,
		                                                     sizeof(error)))
		{
			data = ohos_clipboard_make_local_file_descriptor_data(fileName, fileSize, &dataLen,
			                                                     error, sizeof(error));
			if (data)
				ohos_clipboard_log(clipboard,
				                   "cliprdr local file descriptor sent: name=%s bytes=%" PRIu64
				                   " format=%s",
				                   fileName, fileSize,
				                   ohos_clipboard_image_format_name_from_id(fileFormatId)
				                       ? ohos_clipboard_image_format_name_from_id(fileFormatId)
				                       : "none");
		}
		free(fileName);
	}
	else if (request->requestedFormatId == CF_DIB)
	{
		(void)ohos_clipboard_get_cached_image_data(clipboard, request->requestedFormatId,
		                                           &data, &dataLen, error, sizeof(error));
	}
	else if (request->requestedFormatId == CF_DIBV5)
	{
		(void)ohos_clipboard_get_cached_image_data(clipboard, request->requestedFormatId,
		                                           &data, &dataLen, error, sizeof(error));
	}
	else if (request->requestedFormatId == OHOS_CLIPBOARD_FORMAT_IMAGE_PNG ||
	         request->requestedFormatId == OHOS_CLIPBOARD_FORMAT_IMAGE_JPEG ||
	         request->requestedFormatId == OHOS_CLIPBOARD_FORMAT_IMAGE_WEBP)
	{
		if (ohos_clipboard_read_uri(clipboard, &uri, error, sizeof(error)))
		{
			data = ohos_clipboard_read_local_image_file_data_from_uri(
			    uri, request->requestedFormatId, &dataLen, error, sizeof(error));
			if (data)
				ohos_clipboard_log(clipboard,
				                   "cliprdr local encoded image file response prepared: "
				                   "format=%s bytes=%" PRIu32,
				                   ohos_clipboard_image_format_name_from_id(
				                       request->requestedFormatId),
				                   dataLen);
		}
		if (!data)
			(void)ohos_clipboard_get_cached_image_data(clipboard, request->requestedFormatId,
			                                           &data, &dataLen, error, sizeof(error));
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
	free(text);
	free(html);
	free(htmlPlain);
	free(uri);
	return rc;
}

static UINT ohos_clipboard_request_remote_file_range(freerdpOhosClipboard* clipboard,
                                                     UINT32 listIndex, UINT64 fileSize,
                                                     const char* fileName)
{
	if (!clipboard || fileSize == 0U || fileSize > OHOS_CLIPBOARD_MAX_URI_IMAGE_BYTES ||
	    fileSize > UINT32_MAX)
		return ERROR_INVALID_PARAMETER;

	CliprdrClientContext* cliprdr = ohos_clipboard_snapshot_cliprdr(clipboard);
	if (!cliprdr || !cliprdr->ClientFileContentsRequest)
		return ERROR_INVALID_PARAMETER;

	char* storedName = ohos_clipboard_strdup(fileName ? fileName : "");
	if (!storedName)
		return ERROR_NOT_ENOUGH_MEMORY;

	UINT32 streamId = 0;
	pthread_mutex_lock(&clipboard->lock);
	ohos_clipboard_clear_remote_file_transfer_locked(clipboard);
	if (clipboard->remoteFileNextStreamId == 0U)
		clipboard->remoteFileNextStreamId = 1U;
	streamId = clipboard->remoteFileNextStreamId++;
	if (clipboard->remoteFileNextStreamId == 0U)
		clipboard->remoteFileNextStreamId = 1U;
	clipboard->remoteFileTransferActive = TRUE;
	clipboard->remoteFileStreamId = streamId;
	clipboard->remoteFileListIndex = listIndex;
	clipboard->remoteFileExpectedBytes = (UINT32)fileSize;
	clipboard->remoteFileName = storedName;
	storedName = NULL;
	pthread_mutex_unlock(&clipboard->lock);

	CLIPRDR_FILE_CONTENTS_REQUEST request = { 0 };
	request.common.msgType = CB_FILECONTENTS_REQUEST;
	request.streamId = streamId;
	request.listIndex = listIndex;
	request.dwFlags = FILECONTENTS_RANGE;
	request.nPositionLow = 0;
	request.nPositionHigh = 0;
	request.cbRequested = (UINT32)fileSize;

	const UINT rc = cliprdr->ClientFileContentsRequest(cliprdr, &request);
	if (rc != CHANNEL_RC_OK)
	{
		pthread_mutex_lock(&clipboard->lock);
		if (clipboard->remoteFileStreamId == streamId)
			ohos_clipboard_clear_remote_file_transfer_locked(clipboard);
		pthread_mutex_unlock(&clipboard->lock);
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file content request failed: stream=%" PRIu32
		                   " index=%" PRIu32 " bytes=%" PRIu64 " rc=%" PRIu32,
		                   streamId, listIndex, fileSize, rc);
	}
	else
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file content request sent: stream=%" PRIu32
		                   " index=%" PRIu32 " bytes=%" PRIu64 " name=%s",
		                   streamId, listIndex, fileSize, fileName ? fileName : "");
	}

	free(storedName);
	return rc;
}

static UINT ohos_clipboard_handle_remote_file_list(freerdpOhosClipboard* clipboard,
                                                   const BYTE* data, UINT32 dataLen)
{
	if (!clipboard || !data || dataLen == 0U)
		return ERROR_INVALID_PARAMETER;

	FILEDESCRIPTORW* files = NULL;
	UINT32 fileCount = 0;
	const UINT parseRc = cliprdr_parse_file_list(data, dataLen, &files, &fileCount);
	if (parseRc != NO_ERROR)
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file list parse failed: rc=%" PRIu32
		                   " bytes=%" PRIu32,
		                   parseRc, dataLen);
		return CHANNEL_RC_OK;
	}

	UINT32 selectedIndex = UINT32_MAX;
	UINT64 selectedSize = 0;
	char* selectedName = NULL;
	for (UINT32 index = 0; index < fileCount; index++)
	{
		const FILEDESCRIPTORW* file = &files[index];
		char* name = ohos_clipboard_wchar_to_utf8(file->cFileName, ARRAYSIZE(file->cFileName));
		if (!name || name[0] == '\0')
		{
			free(name);
			continue;
		}

		if ((file->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U)
		{
			ohos_clipboard_log(clipboard,
			                   "cliprdr remote file list skipped directory: index=%" PRIu32
			                   " name=%s",
			                   index, name);
			free(name);
			continue;
		}

		if (!ohos_clipboard_uri_has_image_suffix(name))
		{
			free(name);
			continue;
		}

		const UINT64 fileSize = ohos_clipboard_file_descriptor_size(file);
		if ((file->dwFlags & FD_FILESIZE) == 0U || fileSize == 0U ||
		    fileSize > OHOS_CLIPBOARD_MAX_URI_IMAGE_BYTES || fileSize > UINT32_MAX)
		{
			ohos_clipboard_log(clipboard,
			                   "cliprdr remote image file skipped by size: index=%" PRIu32
			                   " bytes=%" PRIu64 " flags=0x%08" PRIx32 " name=%s",
			                   index, fileSize, (UINT32)file->dwFlags, name);
			free(name);
			continue;
		}

		selectedIndex = index;
		selectedSize = fileSize;
		selectedName = name;
		break;
	}

	ohos_clipboard_log(clipboard, "cliprdr remote file list received: files=%" PRIu32
	                              " selected=%s",
	                   fileCount, selectedName ? selectedName : "none");

	UINT rc = CHANNEL_RC_OK;
	if (selectedName)
		rc = ohos_clipboard_request_remote_file_range(clipboard, selectedIndex, selectedSize,
		                                             selectedName);
	else
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file list has no supported jpg/png/webp image");

	free(selectedName);
	free(files);
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

	if (requestedKind == OHOS_CLIPBOARD_REQUEST_FILE_LIST)
		return ohos_clipboard_handle_remote_file_list(clipboard, response->requestedFormatData,
		                                              response->common.dataLen);

	if (requestedKind == OHOS_CLIPBOARD_REQUEST_DIB ||
	    requestedKind == OHOS_CLIPBOARD_REQUEST_DIBV5 ||
	    requestedKind == OHOS_CLIPBOARD_REQUEST_IMAGE_BMP ||
	    requestedKind == OHOS_CLIPBOARD_REQUEST_IMAGE_PNG ||
	    requestedKind == OHOS_CLIPBOARD_REQUEST_IMAGE_JPEG ||
	    requestedKind == OHOS_CLIPBOARD_REQUEST_IMAGE_WEBP)
	{
		const BOOL decoded =
		    (requestedKind == OHOS_CLIPBOARD_REQUEST_IMAGE_PNG ||
		     requestedKind == OHOS_CLIPBOARD_REQUEST_IMAGE_JPEG ||
		     requestedKind == OHOS_CLIPBOARD_REQUEST_IMAGE_WEBP)
		        ? ohos_clipboard_image_buffer_to_bgra(response->requestedFormatData,
		                                             response->common.dataLen, &bgra,
		                                             &imageWidth, &imageHeight, error,
		                                             sizeof(error))
		        : ohos_clipboard_bitmap_to_bgra(response->requestedFormatData,
		                                       response->common.dataLen, requestedKind, &bgra,
		                                       &imageWidth, &imageHeight, error, sizeof(error));
		if (!decoded)
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

static UINT ohos_clipboard_send_local_file_contents_response(
    CliprdrClientContext* cliprdr, const CLIPRDR_FILE_CONTENTS_REQUEST* request,
    const BYTE* data, UINT32 dataSize, BOOL ok)
{
	if (!cliprdr || !request || !cliprdr->ClientFileContentsResponse)
		return ERROR_INVALID_PARAMETER;

	CLIPRDR_FILE_CONTENTS_RESPONSE response = { 0 };
	response.common.msgType = CB_FILECONTENTS_RESPONSE;
	response.common.msgFlags = ok ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
	response.common.dataLen = ok ? dataSize : 0;
	response.streamId = request->streamId;
	response.cbRequested = ok ? dataSize : 0;
	response.requestedData = ok ? data : NULL;
	return cliprdr->ClientFileContentsResponse(cliprdr, &response);
}

static UINT ohos_clipboard_server_file_contents_request(
    CliprdrClientContext* cliprdr, const CLIPRDR_FILE_CONTENTS_REQUEST* request)
{
	char error[160] = { 0 };
	char* uri = NULL;
	char* path = NULL;
	char* fileName = NULL;
	BYTE* data = NULL;
	UINT32 dataSize = 0;
	UINT64 fileSize = 0;
	UINT32 fileFormatId = 0;
	UINT rc = CHANNEL_RC_OK;
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !request || !cliprdr->ClientFileContentsResponse)
		return ERROR_INVALID_PARAMETER;

	const BOOL wantsSize = (request->dwFlags & FILECONTENTS_SIZE) != 0U;
	const BOOL wantsRange = (request->dwFlags & FILECONTENTS_RANGE) != 0U;
	if (wantsSize == wantsRange || request->listIndex != 0U)
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr local file contents request rejected: stream=%" PRIu32
		                   " index=%" PRIu32 " flags=0x%08" PRIx32,
		                   request->streamId, request->listIndex, request->dwFlags);
		return ohos_clipboard_send_local_file_contents_response(cliprdr, request, NULL, 0,
		                                                        FALSE);
	}

	if (!ohos_clipboard_read_uri(clipboard, &uri, error, sizeof(error)) ||
	    !ohos_clipboard_get_local_image_file_info_from_uri(uri, &path, &fileName, &fileSize,
	                                                       &fileFormatId, error, sizeof(error)))
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr local file contents source unavailable: stream=%" PRIu32
		                   " error=%s",
		                   request->streamId, error);
		rc = ohos_clipboard_send_local_file_contents_response(cliprdr, request, NULL, 0, FALSE);
		goto fail;
	}

	if (wantsSize)
	{
		BYTE sizeData[sizeof(UINT64)] = { 0 };
		for (size_t index = 0; index < sizeof(sizeData); index++)
			sizeData[index] = (BYTE)((fileSize >> (index * 8U)) & 0xFFU);
		rc = ohos_clipboard_send_local_file_contents_response(
		    cliprdr, request, sizeData, (UINT32)sizeof(sizeData), TRUE);
		if (rc == CHANNEL_RC_OK)
			ohos_clipboard_log(clipboard,
			                   "cliprdr local file size response sent: stream=%" PRIu32
			                   " bytes=%" PRIu64 " name=%s",
			                   request->streamId, fileSize, fileName);
		goto fail;
	}

	const UINT64 offset =
	    (((UINT64)request->nPositionHigh) << 32U) | (UINT64)request->nPositionLow;
	if (request->cbRequested == 0U || offset >= fileSize)
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr local file range rejected: stream=%" PRIu32
		                   " offset=%" PRIu64 " requested=%" PRIu32 " size=%" PRIu64
		                   " name=%s",
		                   request->streamId, offset, request->cbRequested, fileSize, fileName);
		rc = ohos_clipboard_send_local_file_contents_response(cliprdr, request, NULL, 0, FALSE);
		goto fail;
	}

	const UINT64 remaining = fileSize - offset;
	UINT32 bytesToRead = request->cbRequested;
	if ((UINT64)bytesToRead > remaining)
		bytesToRead = (UINT32)remaining;

	data = ohos_clipboard_read_local_file_range(path, offset, bytesToRead, &dataSize, error,
	                                            sizeof(error));
	if (!data)
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr local file range read failed: stream=%" PRIu32
		                   " offset=%" PRIu64 " requested=%" PRIu32 " error=%s name=%s",
		                   request->streamId, offset, bytesToRead, error, fileName);
		rc = ohos_clipboard_send_local_file_contents_response(cliprdr, request, NULL, 0, FALSE);
		goto fail;
	}

	rc = ohos_clipboard_send_local_file_contents_response(cliprdr, request, data, dataSize, TRUE);
	if (rc == CHANNEL_RC_OK)
		ohos_clipboard_log(clipboard,
		                   "cliprdr local file contents response sent: stream=%" PRIu32
		                   " offset=%" PRIu64 " bytes=%" PRIu32 " fileBytes=%" PRIu64
		                   " name=%s format=%s",
		                   request->streamId, offset, dataSize, fileSize, fileName,
		                   ohos_clipboard_image_format_name_from_id(fileFormatId)
		                       ? ohos_clipboard_image_format_name_from_id(fileFormatId)
		                       : "none");

fail:
	free(data);
	free(fileName);
	free(path);
	free(uri);
	return rc;
}

static UINT ohos_clipboard_server_file_contents_response(
    CliprdrClientContext* cliprdr, const CLIPRDR_FILE_CONTENTS_RESPONSE* response)
{
	char error[160] = { 0 };
	BYTE* bgra = NULL;
	UINT32 imageWidth = 0;
	UINT32 imageHeight = 0;
	UINT32 expectedBytes = 0;
	UINT32 listIndex = 0;
	char* fileName = NULL;
	char* cachedFileUri = NULL;
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_cliprdr(cliprdr);

	if (!clipboard || !response)
		return ERROR_INVALID_PARAMETER;

	pthread_mutex_lock(&clipboard->lock);
	if (!clipboard->remoteFileTransferActive ||
	    clipboard->remoteFileStreamId != response->streamId)
	{
		pthread_mutex_unlock(&clipboard->lock);
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file content response ignored: stream=%" PRIu32,
		                   response->streamId);
		return CHANNEL_RC_OK;
	}
	expectedBytes = clipboard->remoteFileExpectedBytes;
	listIndex = clipboard->remoteFileListIndex;
	fileName = clipboard->remoteFileName;
	clipboard->remoteFileName = NULL;
	ohos_clipboard_clear_remote_file_transfer_locked(clipboard);
	pthread_mutex_unlock(&clipboard->lock);

	if ((response->common.msgFlags & CB_RESPONSE_FAIL) != 0 || !response->requestedData)
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file content response failed: stream=%" PRIu32
		                   " flags=0x%04" PRIx16 " name=%s",
		                   response->streamId, response->common.msgFlags,
		                   fileName ? fileName : "");
		free(fileName);
		return CHANNEL_RC_OK;
	}

	if (response->cbRequested == 0U ||
	    response->cbRequested > OHOS_CLIPBOARD_MAX_URI_IMAGE_BYTES)
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file content size rejected: stream=%" PRIu32
		                   " bytes=%" PRIu32 " name=%s",
		                   response->streamId, response->cbRequested, fileName ? fileName : "");
		free(fileName);
		return CHANNEL_RC_OK;
	}

	if (expectedBytes != 0U && response->cbRequested != expectedBytes)
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file content size mismatch: stream=%" PRIu32
		                   " expected=%" PRIu32 " got=%" PRIu32 " name=%s",
		                   response->streamId, expectedBytes, response->cbRequested,
		                   fileName ? fileName : "");

	if (!ohos_clipboard_image_buffer_to_bgra(response->requestedData, response->cbRequested,
	                                         &bgra, &imageWidth, &imageHeight, error,
	                                         sizeof(error)) ||
	    !ohos_clipboard_validate_local_image_size(imageWidth, imageHeight, error, sizeof(error)))
	{
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file image decode failed: index=%" PRIu32
		                   " bytes=%" PRIu32 " name=%s error=%s",
		                   listIndex, response->cbRequested, fileName ? fileName : "",
		                   error[0] ? error : "unknown");
		free(bgra);
		free(fileName);
		return CHANNEL_RC_OK;
	}

	error[0] = '\0';
	cachedFileUri = ohos_clipboard_cache_remote_file(clipboard, fileName, response->streamId,
	                                                 response->requestedData,
	                                                 response->cbRequested, error,
	                                                 sizeof(error));
	if (!cachedFileUri)
		ohos_clipboard_log(clipboard,
		                   "cliprdr remote file image sandbox cache failed: name=%s error=%s",
		                   fileName ? fileName : "", error[0] ? error : "unknown");

	error[0] = '\0';
	if (!ohos_clipboard_write_pixelmap_with_uri(clipboard, bgra, imageWidth, imageHeight,
	                                            response->cbRequested, cachedFileUri, error,
	                                            sizeof(error)))
	{
		ohos_clipboard_log(clipboard,
		                   "HarmonyOS Pasteboard remote file pixelmap write failed: %s",
		                   error);
		free(cachedFileUri);
		free(bgra);
		free(fileName);
		return ERROR_INTERNAL_ERROR;
	}

	++clipboard->remoteResponseCount;
	ohos_clipboard_log(clipboard,
	                   "cliprdr remote file image copied to HarmonyOS Pasteboard: %" PRIu32
	                   "x%" PRIu32 " bytes=%" PRIu32 " name=%s sandboxUri=%s",
	                   imageWidth, imageHeight, response->cbRequested, fileName ? fileName : "",
	                   cachedFileUri ? cachedFileUri : "none");
	free(cachedFileUri);
	free(bgra);
	free(fileName);
	return CHANNEL_RC_OK;
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
		ohos_clipboard_clear_remote_file_transfer_locked(clipboard);
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
	UINT32 ignoreLocalChanges = 0;
	UINT64 ignoreLocalChangesUntil = 0;
	pthread_mutex_lock(&clipboard->lock);
	ignoreLocalChanges = clipboard->ignoreLocalChanges;
	ignoreLocalChangesUntil = clipboard->ignoreLocalChangesUntil;
	pthread_mutex_unlock(&clipboard->lock);
	ohos_clipboard_log(clipboard,
	                   "HarmonyOS Pasteboard change observed: type=%d changes=%" PRIu64
	                   " ignoreLocalChanges=%" PRIu32 " ignoreUntil=%" PRIu64,
	                   type, clipboard->pasteboardChangeCount, ignoreLocalChanges,
	                   ignoreLocalChangesUntil);
	ohos_clipboard_note_local_clipboard_changed(clipboard);

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
		ohos_clipboard_log(clipboard,
		                   "HarmonyOS Pasteboard observer subscribed: rc=%d(%s)", rc,
		                   ohos_clipboard_pasteboard_status_name(rc));
	}
	else
	{
		clipboard->lastError = (UINT32)rc;
		ohos_clipboard_log(clipboard, "HarmonyOS Pasteboard subscribe warning: %d(%s)", rc,
		                   ohos_clipboard_pasteboard_status_name(rc));
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
	if (pthread_cond_init(&clipboard->imageCond, NULL) != 0)
	{
		pthread_mutex_destroy(&clipboard->lock);
		free(clipboard);
		return NULL;
	}
	clipboard->imageCondInitialized = TRUE;
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

	ohos_clipboard_stop_image_worker(clipboard);
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
	pthread_mutex_lock(&clipboard->lock);
	ohos_clipboard_clear_remote_file_transfer_locked(clipboard);
	pthread_mutex_unlock(&clipboard->lock);
	++clipboard->unregisterCount;
}

void freerdp_ohos_clipboard_free(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	freerdp_ohos_clipboard_unregister(clipboard);
	if (clipboard->imageCondInitialized)
	{
		pthread_cond_destroy(&clipboard->imageCond);
		clipboard->imageCondInitialized = FALSE;
	}
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
