#ifndef FREERDP_CLIENT_OHOS_SESSION_H
#define FREERDP_CLIENT_OHOS_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include <freerdp/api.h>
#include <winpr/wtypes.h>

#include "ohos_ime.h"
#include "ohos_keyboard.h"
#include "ohos_pointer.h"
#include "ohos_session_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct freerdp_ohos_session freerdpOhosSession;

typedef void (*FREERDP_OHOS_SESSION_MESSAGE_CALLBACK)(const char* message, void* userData);
typedef void (*FREERDP_OHOS_SESSION_STATE_CALLBACK)(const char* state, void* userData);

typedef struct
{
	FREERDP_OHOS_CONNECTION_CONFIG connection;
	FREERDP_OHOS_SESSION_CONFIG session;
	const char* appDataDir;
	UINT32 certificatePolicy;
} FREERDP_OHOS_SESSION_OPTIONS;

typedef struct
{
	FREERDP_OHOS_SESSION_STATE_CALLBACK StateChanged;
	FREERDP_OHOS_SESSION_MESSAGE_CALLBACK Log;
	FREERDP_OHOS_SESSION_MESSAGE_CALLBACK Error;
	void* userData;
} FREERDP_OHOS_SESSION_CALLBACKS;

FREERDP_API freerdpOhosSession* freerdp_ohos_session_new(void);
FREERDP_API void freerdp_ohos_session_free(freerdpOhosSession* session);
FREERDP_API BOOL freerdp_ohos_session_connect(freerdpOhosSession* session,
                                              const FREERDP_OHOS_SESSION_OPTIONS* options,
                                              const FREERDP_OHOS_SESSION_CALLBACKS* callbacks,
                                              char* message, size_t messageSize);
FREERDP_API void freerdp_ohos_session_disconnect(freerdpOhosSession* session);
FREERDP_API BOOL freerdp_ohos_session_send_pointer(
    freerdpOhosSession* session, const FREERDP_OHOS_POINTER_VIEWPORT* viewport,
    const FREERDP_OHOS_POINTER_EVENT* event, char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_session_send_key(freerdpOhosSession* session,
                                               const FREERDP_OHOS_KEY_EVENT* event,
                                               char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_session_send_text(freerdpOhosSession* session,
                                                const uint16_t* text, size_t length,
                                                char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_session_resize(freerdpOhosSession* session, UINT32 width,
                                             UINT32 height, char* message,
                                             size_t messageSize);
FREERDP_API const char*
freerdp_ohos_session_get_diagnostics(freerdpOhosSession* session);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_SESSION_H */
