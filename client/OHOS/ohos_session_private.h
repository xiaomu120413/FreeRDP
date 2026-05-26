#ifndef FREERDP_CLIENT_OHOS_SESSION_PRIVATE_H
#define FREERDP_CLIENT_OHOS_SESSION_PRIVATE_H

#include "ohos_session.h"
#include "ohos_display.h"
#include "ohos_input_queue.h"

struct freerdp_ohos_session
{
	BOOL connected;
	BOOL contextCreated;
	BOOL teardownPending;
	BOOL requestedDisconnect;
	FREERDP_OHOS_SESSION_CALLBACKS callbacks;
	freerdp* instance;
	FREERDP_OHOS_KEYBOARD_STATE* keyboard;
	freerdpOhosInputQueue* inputQueue;
	freerdpOhosDisplayControl* displayControl;
	char diagnostics[512];
};

void ohos_session_emit_log(freerdpOhosSession* session, const char* message);
void ohos_session_set_diagnostics(freerdpOhosSession* session, const char* format, ...);
void ohos_session_copy_diagnostics(freerdpOhosSession* session, char* message,
                                   size_t messageSize);
BOOL ohos_session_require_connected(freerdpOhosSession* session, char* message,
                                    size_t messageSize);
void ohos_session_prepare_display_control(freerdpOhosSession* session, BOOL h264);

#endif /* FREERDP_CLIENT_OHOS_SESSION_PRIVATE_H */
