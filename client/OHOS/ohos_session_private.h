#ifndef FREERDP_CLIENT_OHOS_SESSION_PRIVATE_H
#define FREERDP_CLIENT_OHOS_SESSION_PRIVATE_H

#include "ohos_session.h"

struct freerdp_ohos_session
{
	BOOL connected;
	BOOL contextCreated;
	BOOL teardownPending;
	BOOL requestedDisconnect;
	FREERDP_OHOS_SESSION_CALLBACKS callbacks;
	freerdp* instance;
	FREERDP_OHOS_KEYBOARD_STATE* keyboard;
	char diagnostics[512];
};

void ohos_session_set_diagnostics(freerdpOhosSession* session, const char* format, ...);
void ohos_session_copy_diagnostics(freerdpOhosSession* session, char* message,
                                   size_t messageSize);
BOOL ohos_session_require_connected(freerdpOhosSession* session, char* message,
                                    size_t messageSize);

#endif /* FREERDP_CLIENT_OHOS_SESSION_PRIVATE_H */
