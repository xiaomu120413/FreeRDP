/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS session input dispatch API
 */

#include "ohos_session_private.h"

#include <freerdp/input.h>

BOOL freerdp_ohos_session_send_pointer(freerdpOhosSession* session,
                                       const FREERDP_OHOS_POINTER_VIEWPORT* viewport,
                                       const FREERDP_OHOS_POINTER_EVENT* event, char* message,
                                       size_t messageSize)
{
	if (!ohos_session_require_connected(session, message, messageSize))
		return FALSE;

	if (!freerdp_ohos_input_queue_enqueue_pointer(session->inputQueue, viewport, event, message,
	                                              messageSize))
		return FALSE;
	ohos_session_set_diagnostics(session, "%s", message ? message : "pointer event queued");
	ohos_session_copy_diagnostics(session, message, messageSize);
	return TRUE;
}

BOOL freerdp_ohos_session_send_key(freerdpOhosSession* session,
                                   const FREERDP_OHOS_KEY_EVENT* event, char* message,
                                   size_t messageSize)
{
	if (!ohos_session_require_connected(session, message, messageSize))
		return FALSE;

	if (!freerdp_ohos_input_queue_enqueue_key(session->inputQueue, event, message, messageSize))
		return FALSE;
	ohos_session_set_diagnostics(session, "%s", message ? message : "key event queued");
	ohos_session_copy_diagnostics(session, message, messageSize);
	return TRUE;
}

BOOL freerdp_ohos_session_send_text(freerdpOhosSession* session, const uint16_t* text,
                                    size_t length, char* message, size_t messageSize)
{
	if (!ohos_session_require_connected(session, message, messageSize))
		return FALSE;

	if (!freerdp_ohos_input_queue_enqueue_text(session->inputQueue, text, length, message,
	                                           messageSize))
		return FALSE;
	ohos_session_set_diagnostics(session, "%s", message ? message : "committed text queued");
	ohos_session_copy_diagnostics(session, message, messageSize);
	return TRUE;
}

BOOL freerdp_ohos_session_send_focus_in(freerdpOhosSession* session, UINT16 toggleStates,
                                        char* message, size_t messageSize)
{
	if (!ohos_session_require_connected(session, message, messageSize))
		return FALSE;

	if (!freerdp_ohos_input_queue_enqueue_focus_in(session->inputQueue, toggleStates, message,
	                                               messageSize))
		return FALSE;
	ohos_session_set_diagnostics(session, "%s", message ? message : "focus-in event queued");
	ohos_session_copy_diagnostics(session, message, messageSize);
	return TRUE;
}

BOOL freerdp_ohos_session_release_all_keys(freerdpOhosSession* session, char* message,
                                           size_t messageSize)
{
	if (!ohos_session_require_connected(session, message, messageSize))
		return FALSE;

	if (!freerdp_ohos_input_queue_enqueue_release_all_keys(session->inputQueue, message,
	                                                       messageSize))
		return FALSE;
	ohos_session_set_diagnostics(session, "%s", message ? message : "release-all queued");
	ohos_session_copy_diagnostics(session, message, messageSize);
	return TRUE;
}

const char* freerdp_ohos_session_get_diagnostics(freerdpOhosSession* session)
{
	if (!session)
		return "OHOS session diagnostics unavailable: session is null";
	return session->diagnostics;
}
