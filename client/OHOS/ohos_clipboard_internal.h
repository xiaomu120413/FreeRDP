#ifndef FREERDP_CLIENT_OHOS_CLIPBOARD_INTERNAL_H
#define FREERDP_CLIENT_OHOS_CLIPBOARD_INTERNAL_H

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

#ifdef __cplusplus
extern "C"
{
#endif

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
#define OHOS_CLIPBOARD_PERMISSION_TIMEOUT_MS 60000U
#define OHOS_CLIPBOARD_MAX_LOCAL_IMAGE_PIXELS (4096U * 4096U)
#define OHOS_CLIPBOARD_MAX_LOCAL_IMAGE_BYTES (64U * 1024U * 1024U)
#define OHOS_CLIPBOARD_LCS_SRGB 0x73524742U
#define OHOS_CLIPBOARD_LCS_GM_IMAGES 0x00000004U
#define OHOS_CLIPBOARD_SANDBOX_FILES_DIR "/data/storage/el2/base/files"
#define OHOS_CLIPBOARD_CACHE_DIR_NAME "clipboard-cache"

#define OHOS_CLIPBOARD_HTML_FORMAT_NAME "HTML Format"
#define OHOS_CLIPBOARD_TEXT_HTML_FORMAT_NAME "text/html"
#define OHOS_CLIPBOARD_URIW_FORMAT_NAME "UniformResourceLocatorW"
#define OHOS_CLIPBOARD_URI_FORMAT_NAME "UniformResourceLocator"
#define OHOS_CLIPBOARD_URI_LIST_FORMAT_NAME "text/uri-list"
#define OHOS_CLIPBOARD_IMAGE_BMP_FORMAT_NAME "image/bmp"
#define OHOS_CLIPBOARD_IMAGE_PNG_FORMAT_NAME "image/png"
#define OHOS_CLIPBOARD_IMAGE_JPEG_FORMAT_NAME "image/jpeg"
#define OHOS_CLIPBOARD_IMAGE_WEBP_FORMAT_NAME "image/webp"
#define OHOS_CLIPBOARD_FILE_GROUP_DESCRIPTOR_W_FORMAT_NAME "FileGroupDescriptorW"

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
	BOOL pasteboardReadPermissionGranted;
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

FREERDP_LOCAL void ohos_clipboard_log(freerdpOhosClipboard* clipboard, const char* format, ...);
FREERDP_LOCAL void ohos_clipboard_set_error(freerdpOhosClipboard* clipboard, char* errorBuffer, size_t errorBufferSize, const char* format, ...);
FREERDP_LOCAL const char* ohos_clipboard_pasteboard_status_name(int status);
FREERDP_LOCAL size_t ohos_clipboard_safe_strlen(const char* value);
FREERDP_LOCAL void ohos_clipboard_trace_pasteboard_state(freerdpOhosClipboard* clipboard, const char* reason);
FREERDP_LOCAL void ohos_clipboard_trace_udmf_data(freerdpOhosClipboard* clipboard, const char* reason, OH_UdmfData* data);
FREERDP_LOCAL BOOL ohos_clipboard_ensure_read_permission(freerdpOhosClipboard* clipboard, const char* reason);
FREERDP_LOCAL OH_UdmfData* ohos_clipboard_get_pasteboard_data(freerdpOhosClipboard* clipboard, const char* reason, int* outStatus);
FREERDP_LOCAL void ohos_clipboard_registry_add(freerdpOhosClipboard* clipboard);
FREERDP_LOCAL void ohos_clipboard_registry_remove(freerdpOhosClipboard* clipboard);
FREERDP_LOCAL freerdpOhosClipboard* ohos_clipboard_from_context(void* context);
FREERDP_LOCAL CliprdrClientContext* ohos_clipboard_snapshot_cliprdr(freerdpOhosClipboard* clipboard);
FREERDP_LOCAL char* ohos_clipboard_strdup(const char* value);
FREERDP_LOCAL BOOL ohos_clipboard_equals_ignore_case(const char* left, const char* right);
FREERDP_LOCAL BOOL ohos_clipboard_starts_with_ignore_case(const char* value, const char* prefix);
FREERDP_LOCAL BOOL ohos_clipboard_memory_equals_ignore_case(const char* left, const char* right, size_t length);
FREERDP_LOCAL BOOL ohos_clipboard_ends_with_ignore_case(const char* value, const char* suffix);
FREERDP_LOCAL BOOL ohos_clipboard_is_uri_text(const char* value);
FREERDP_LOCAL int ohos_clipboard_hex_value(char c);
FREERDP_LOCAL char* ohos_clipboard_percent_decode(const char* value);
FREERDP_LOCAL char* ohos_clipboard_uri_to_local_path(const char* uri);
FREERDP_LOCAL const char* ohos_clipboard_uri_kind(const char* uri);
FREERDP_LOCAL BOOL ohos_clipboard_image_signature_supported(const BYTE* data, UINT32 size);
FREERDP_LOCAL UINT32 ohos_clipboard_image_format_id_from_signature(const BYTE* data, UINT32 size);
FREERDP_LOCAL const char* ohos_clipboard_image_format_name_from_id(UINT32 formatId);
FREERDP_LOCAL const char* ohos_clipboard_image_extension_from_id(UINT32 formatId);
FREERDP_LOCAL UINT32 ohos_clipboard_image_format_id_from_uri(const char* uri);
FREERDP_LOCAL BOOL ohos_clipboard_uri_has_image_suffix(const char* uri);
FREERDP_LOCAL char* ohos_clipboard_wchar_to_utf8(const WCHAR* value, size_t maxChars);
FREERDP_LOCAL UINT64 ohos_clipboard_file_descriptor_size(const FILEDESCRIPTORW* file);
FREERDP_LOCAL BYTE* ohos_clipboard_read_local_file(const char* path, UINT32 maxBytes, UINT32* outSize, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BYTE* ohos_clipboard_read_local_file_range(const char* path, UINT64 offset, UINT32 requestedBytes, UINT32* outSize, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_read_local_file_signature(const char* path, BYTE* signature, UINT32 signatureSize, UINT32* outSize);
FREERDP_LOCAL BOOL ohos_clipboard_uri_may_reference_local_image(const char* uri);
FREERDP_LOCAL UINT64 ohos_clipboard_now_ms(void);
FREERDP_LOCAL char* ohos_clipboard_join_path(const char* left, const char* right);
FREERDP_LOCAL BOOL ohos_clipboard_ensure_directory(freerdpOhosClipboard* clipboard, const char* path, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_is_safe_cache_file_char(unsigned char c);
FREERDP_LOCAL char* ohos_clipboard_sanitize_cache_file_name(const char* fileName);
FREERDP_LOCAL char* ohos_clipboard_file_uri_from_path(const char* path);
FREERDP_LOCAL char* ohos_clipboard_cache_remote_file(freerdpOhosClipboard* clipboard, const char* fileName, UINT32 streamId, const BYTE* data, UINT32 dataSize, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL char* ohos_clipboard_local_image_file_name_from_path(const char* path, UINT32 formatId);
FREERDP_LOCAL BOOL ohos_clipboard_get_local_image_file_info_from_uri( const char* uri, char** outPath, char** outFileName, UINT64* outSize, UINT32* outFormatId, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_set_file_descriptor_name(FILEDESCRIPTORW* descriptor, const char* fileName);
FREERDP_LOCAL BYTE* ohos_clipboard_make_local_file_descriptor_data(const char* fileName, UINT64 fileSize, UINT32* outSize, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BYTE* ohos_clipboard_read_local_image_file_data_from_uri( const char* uri, UINT32 requestedFormatId, UINT32* outSize, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BYTE* ohos_clipboard_utf8_to_utf16le(const char* text, UINT32* outSize);
FREERDP_LOCAL void ohos_clipboard_append_utf8(char** text, size_t* count, size_t* capacity, uint32_t cp);
FREERDP_LOCAL char* ohos_clipboard_utf16le_to_utf8(const BYTE* data, UINT32 size);
FREERDP_LOCAL char* ohos_clipboard_bytes_to_string(const BYTE* data, UINT32 size);
FREERDP_LOCAL const char* ohos_clipboard_find_token(const char* text, const char* token);
FREERDP_LOCAL UINT32 ohos_clipboard_parse_html_offset(const char* text, const char* key);
FREERDP_LOCAL char* ohos_clipboard_extract_ms_html(const BYTE* data, UINT32 size);
FREERDP_LOCAL BYTE* ohos_clipboard_html_to_ms_html(const char* html, UINT32* outSize);
FREERDP_LOCAL char* ohos_clipboard_extract_uri_list_first(const BYTE* data, UINT32 size);
FREERDP_LOCAL BYTE* ohos_clipboard_uri_to_uri_list(const char* uri, UINT32* outSize);
FREERDP_LOCAL UINT16 ohos_clipboard_read_le16(const BYTE* data);
FREERDP_LOCAL UINT32 ohos_clipboard_read_le32(const BYTE* data);
FREERDP_LOCAL BYTE* ohos_clipboard_dib_to_bmp(const BYTE* data, UINT32 size, UINT32* outSize, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BYTE* ohos_clipboard_bgra_to_dib(const BYTE* bgra, UINT32 width, UINT32 height, UINT32* outSize, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BYTE* ohos_clipboard_bgra_to_dibv5(const BYTE* bgra, UINT32 width, UINT32 height, UINT32* outSize, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_decoded_image_to_bgra(const wImage* image, BYTE** outBgra, UINT32* outWidth, UINT32* outHeight, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_ohos_image_source_to_bgra(OH_ImageSourceNative* source, BYTE** outBgra, UINT32* outWidth, UINT32* outHeight, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_ohos_image_buffer_to_bgra(const BYTE* data, UINT32 size, BYTE** outBgra, UINT32* outWidth, UINT32* outHeight, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_ohos_image_uri_to_bgra(const char* uri, BYTE** outBgra, UINT32* outWidth, UINT32* outHeight, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_image_buffer_to_bgra(const BYTE* data, UINT32 size, BYTE** outBgra, UINT32* outWidth, UINT32* outHeight, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_bitmap_to_bgra(const BYTE* data, UINT32 size, OHOS_CLIPBOARD_REQUEST_KIND kind, BYTE** outBgra, UINT32* outWidth, UINT32* outHeight, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL UINT32 ohos_clipboard_pixel_format_bytes(int32_t pixelFormat);
FREERDP_LOCAL BOOL ohos_clipboard_pixelmap_native_to_bgra(OH_PixelmapNative* pixelmapNative, BYTE** outBgra, UINT32* outWidth, UINT32* outHeight, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_read_plain_text(freerdpOhosClipboard* clipboard, char** outText, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_read_html(freerdpOhosClipboard* clipboard, char** outHtml, char** outPlain, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_read_uri_from_data(OH_UdmfData* data, char** outUri);
FREERDP_LOCAL BOOL ohos_clipboard_read_uri(freerdpOhosClipboard* clipboard, char** outUri, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_read_uri_image_with_source( freerdpOhosClipboard* clipboard, BYTE** outBgra, UINT32* outWidth, UINT32* outHeight, BYTE** outSourceData, UINT32* outSourceSize, UINT32* outSourceFormatId, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL void ohos_clipboard_clear_image_cache_locked(freerdpOhosClipboard* clipboard);
FREERDP_LOCAL void ohos_clipboard_clear_remote_file_transfer_locked(freerdpOhosClipboard* clipboard);
FREERDP_LOCAL BOOL ohos_clipboard_validate_local_image_size(UINT32 width, UINT32 height, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL void* ohos_clipboard_image_worker_main(void* arg);
FREERDP_LOCAL BOOL ohos_clipboard_start_image_worker_locked(freerdpOhosClipboard* clipboard);
FREERDP_LOCAL BOOL ohos_clipboard_schedule_image_export_locked(freerdpOhosClipboard* clipboard, UINT64 serial);
FREERDP_LOCAL BOOL ohos_clipboard_schedule_image_export(freerdpOhosClipboard* clipboard, const char* reason);
FREERDP_LOCAL void ohos_clipboard_note_local_clipboard_changed(freerdpOhosClipboard* clipboard);
FREERDP_LOCAL void ohos_clipboard_stop_image_worker(freerdpOhosClipboard* clipboard);
FREERDP_LOCAL BOOL ohos_clipboard_make_realtime_deadline(UINT64 timeoutMs, struct timespec* deadline);
FREERDP_LOCAL BYTE* ohos_clipboard_copy_cached_image_locked(freerdpOhosClipboard* clipboard, UINT32 formatId, UINT32* outSize);
FREERDP_LOCAL BOOL ohos_clipboard_get_cached_image_data(freerdpOhosClipboard* clipboard, UINT32 formatId, BYTE** outData, UINT32* outSize, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_write_plain_text(freerdpOhosClipboard* clipboard, const char* text, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_write_html(freerdpOhosClipboard* clipboard, const char* html, const char* plain, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_write_uri(freerdpOhosClipboard* clipboard, const char* uri, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_write_pixelmap_with_uri(freerdpOhosClipboard* clipboard, const BYTE* bgra, UINT32 width, UINT32 height, UINT32 sourceBytes, const char* fileUriValue, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL BOOL ohos_clipboard_write_pixelmap(freerdpOhosClipboard* clipboard, const BYTE* bgra, UINT32 width, UINT32 height, UINT32 sourceBytes, char* errorBuffer, size_t errorBufferSize);
FREERDP_LOCAL UINT ohos_clipboard_send_client_capabilities(CliprdrClientContext* cliprdr);
FREERDP_LOCAL UINT ohos_clipboard_send_format_list_response(CliprdrClientContext* cliprdr, BOOL accepted);
FREERDP_LOCAL UINT ohos_clipboard_send_local_format_list(freerdpOhosClipboard* clipboard, const char* reason);
FREERDP_LOCAL UINT ohos_clipboard_send_format_data_request(freerdpOhosClipboard* clipboard, UINT32 formatId, OHOS_CLIPBOARD_REQUEST_KIND kind);
FREERDP_LOCAL freerdpOhosClipboard* ohos_clipboard_from_cliprdr(CliprdrClientContext* cliprdr);
FREERDP_LOCAL UINT ohos_clipboard_monitor_ready(CliprdrClientContext* cliprdr, const CLIPRDR_MONITOR_READY* monitorReady);
FREERDP_LOCAL UINT ohos_clipboard_server_capabilities(CliprdrClientContext* cliprdr, const CLIPRDR_CAPABILITIES* capabilities);
FREERDP_LOCAL UINT ohos_clipboard_server_format_list(CliprdrClientContext* cliprdr, const CLIPRDR_FORMAT_LIST* formatList);
FREERDP_LOCAL UINT ohos_clipboard_server_format_list_response( CliprdrClientContext* cliprdr, const CLIPRDR_FORMAT_LIST_RESPONSE* response);
FREERDP_LOCAL UINT ohos_clipboard_server_lock_clipboard_data( CliprdrClientContext* cliprdr, const CLIPRDR_LOCK_CLIPBOARD_DATA* lockClipboardData);
FREERDP_LOCAL UINT ohos_clipboard_server_unlock_clipboard_data( CliprdrClientContext* cliprdr, const CLIPRDR_UNLOCK_CLIPBOARD_DATA* unlockClipboardData);
FREERDP_LOCAL UINT ohos_clipboard_server_format_data_request( CliprdrClientContext* cliprdr, const CLIPRDR_FORMAT_DATA_REQUEST* request);
FREERDP_LOCAL UINT ohos_clipboard_request_remote_file_range(freerdpOhosClipboard* clipboard, UINT32 listIndex, UINT64 fileSize, const char* fileName);
FREERDP_LOCAL UINT ohos_clipboard_handle_remote_file_list(freerdpOhosClipboard* clipboard, const BYTE* data, UINT32 dataLen);
FREERDP_LOCAL UINT ohos_clipboard_server_format_data_response( CliprdrClientContext* cliprdr, const CLIPRDR_FORMAT_DATA_RESPONSE* response);
FREERDP_LOCAL UINT ohos_clipboard_send_local_file_contents_response( CliprdrClientContext* cliprdr, const CLIPRDR_FILE_CONTENTS_REQUEST* request, const BYTE* data, UINT32 dataSize, BOOL ok);
FREERDP_LOCAL UINT ohos_clipboard_server_file_contents_request( CliprdrClientContext* cliprdr, const CLIPRDR_FILE_CONTENTS_REQUEST* request);
FREERDP_LOCAL UINT ohos_clipboard_server_file_contents_response( CliprdrClientContext* cliprdr, const CLIPRDR_FILE_CONTENTS_RESPONSE* response);
FREERDP_LOCAL void ohos_clipboard_attach_cliprdr(freerdpOhosClipboard* clipboard, CliprdrClientContext* cliprdr);
FREERDP_LOCAL void ohos_clipboard_detach_cliprdr(freerdpOhosClipboard* clipboard, CliprdrClientContext* cliprdr);
FREERDP_LOCAL void ohos_clipboard_channel_connected(void* context, const ChannelConnectedEventArgs* event);
FREERDP_LOCAL void ohos_clipboard_channel_disconnected(void* context, const ChannelDisconnectedEventArgs* event);
FREERDP_LOCAL void ohos_clipboard_on_pasteboard_finalize(void* context);
FREERDP_LOCAL void ohos_clipboard_handle_pasteboard_changed(freerdpOhosClipboard* clipboard, Pasteboard_NotifyType type);
FREERDP_LOCAL void ohos_clipboard_on_pasteboard_changed(void* context, Pasteboard_NotifyType type);
FREERDP_LOCAL void ohos_clipboard_destroy_pasteboard(freerdpOhosClipboard* clipboard);
FREERDP_LOCAL void ohos_clipboard_create_pasteboard(freerdpOhosClipboard* clipboard);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_CLIPBOARD_INTERNAL_H */
