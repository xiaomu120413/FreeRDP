#ifndef FREERDP_CLIENT_OHOS_CLIPBOARD_H
#define FREERDP_CLIENT_OHOS_CLIPBOARD_H

#include <stddef.h>

#include <freerdp/api.h>
#include <freerdp/freerdp.h>
#include <winpr/wtypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct freerdp_ohos_clipboard freerdpOhosClipboard;

typedef int (*freerdp_ohos_pubsub_subscribe_fn)(wPubSub* pubSub, const char* eventName, ...);
typedef int (*freerdp_ohos_pubsub_unsubscribe_fn)(wPubSub* pubSub, const char* eventName, ...);
typedef void (*freerdp_ohos_clipboard_log_fn)(void* userData, const char* message);
typedef BOOL (*freerdp_ohos_clipboard_permission_request_fn)(void* userData, UINT32 timeoutMs);

typedef struct
{
	freerdp_ohos_pubsub_subscribe_fn PubSubSubscribe;
	freerdp_ohos_pubsub_unsubscribe_fn PubSubUnsubscribe;
	freerdp_ohos_clipboard_log_fn Log;
	void* logUserData;
	freerdp_ohos_clipboard_permission_request_fn RequestReadPermission;
	void* permissionUserData;
} FREERDP_OHOS_CLIPBOARD_CONFIG;

FREERDP_API freerdpOhosClipboard* freerdp_ohos_clipboard_new(void);
FREERDP_API BOOL freerdp_ohos_clipboard_register(freerdpOhosClipboard* clipboard,
                                                 rdpContext* context,
                                                 const FREERDP_OHOS_CLIPBOARD_CONFIG* config,
                                                 char* errorBuffer, size_t errorBufferSize);
FREERDP_API void freerdp_ohos_clipboard_unregister(freerdpOhosClipboard* clipboard);
FREERDP_API void freerdp_ohos_clipboard_free(freerdpOhosClipboard* clipboard);
FREERDP_API const char* freerdp_ohos_clipboard_get_diagnostics(freerdpOhosClipboard* clipboard);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_CLIPBOARD_H */
