#ifndef FREERDP_CLIENT_OHOS_LOCATION_H
#define FREERDP_CLIENT_OHOS_LOCATION_H

#include <stddef.h>

#include <freerdp/api.h>
#include <freerdp/freerdp.h>
#include <winpr/wtypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct freerdp_ohos_location freerdpOhosLocation;

typedef int (*freerdp_ohos_location_pubsub_subscribe_fn)(wPubSub* pubSub,
                                                         const char* eventName, ...);
typedef int (*freerdp_ohos_location_pubsub_unsubscribe_fn)(wPubSub* pubSub,
                                                           const char* eventName, ...);
typedef void (*freerdp_ohos_location_log_fn)(void* userData, const char* message);
typedef BOOL (*freerdp_ohos_location_permission_request_fn)(void* userData, UINT32 timeoutMs);

typedef struct
{
	freerdp_ohos_location_pubsub_subscribe_fn PubSubSubscribe;
	freerdp_ohos_location_pubsub_unsubscribe_fn PubSubUnsubscribe;
	freerdp_ohos_location_log_fn Log;
	void* logUserData;
} FREERDP_OHOS_LOCATION_CONFIG;

FREERDP_API freerdpOhosLocation* freerdp_ohos_location_new(void);
FREERDP_API BOOL freerdp_ohos_location_register(freerdpOhosLocation* location,
                                                rdpContext* context,
                                                const FREERDP_OHOS_LOCATION_CONFIG* config,
                                                char* errorBuffer, size_t errorBufferSize);
FREERDP_API void freerdp_ohos_location_unregister(freerdpOhosLocation* location);
FREERDP_API void freerdp_ohos_location_free(freerdpOhosLocation* location);
FREERDP_API const char* freerdp_ohos_location_get_diagnostics(
    freerdpOhosLocation* location);
FREERDP_API BOOL freerdp_ohos_location_set_permission_callback(
    freerdp_ohos_location_permission_request_fn callback, void* userData);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_LOCATION_H */
