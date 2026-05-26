/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS display-control session API
 */

#include "ohos_session_private.h"

#include <inttypes.h>

static void ohos_session_display_log(const char* message, void* userData)
{
	ohos_session_emit_log((freerdpOhosSession*)userData, message);
}

void ohos_session_prepare_display_control(freerdpOhosSession* session, BOOL h264)
{
	if (!session || !session->displayControl)
		return;

	freerdp_ohos_display_control_reset(session->displayControl);
	freerdp_ohos_display_control_set_log_callback(session->displayControl,
	                                              ohos_session_display_log, session);
	freerdp_ohos_display_control_set_alignment(session->displayControl, h264 ? 16U : 1U);
}

BOOL freerdp_ohos_session_attach_display_control(freerdpOhosSession* session,
                                                 DispClientContext* disp, char* message,
                                                 size_t messageSize)
{
	if (!session)
	{
		ohos_session_set_diagnostics(session, "OHOS session is null");
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}
	if (!session->displayControl)
	{
		ohos_session_set_diagnostics(session, "OHOS display-control manager is not available");
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}

	if (!freerdp_ohos_display_control_attach(session->displayControl, disp, message,
	                                         messageSize))
	{
		ohos_session_set_diagnostics(session, "%s",
		                             message && message[0] != '\0'
		                                 ? message
		                                 : "display-control attach failed");
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}

	ohos_session_set_diagnostics(session, "%s",
	                             message && message[0] != '\0'
	                                 ? message
	                                 : "display-control attached");
	ohos_session_copy_diagnostics(session, message, messageSize);
	return TRUE;
}

void freerdp_ohos_session_detach_display_control(freerdpOhosSession* session,
                                                 DispClientContext* disp)
{
	if (!session || !session->displayControl)
		return;

	freerdp_ohos_display_control_detach(session->displayControl, disp);
	ohos_session_set_diagnostics(session,
	                             "display-control disconnected from FreeRDP OHOS resize manager");
}

BOOL freerdp_ohos_session_resize(freerdpOhosSession* session, UINT32 width, UINT32 height,
                                 char* message, size_t messageSize)
{
	if (!ohos_session_require_connected(session, message, messageSize))
		return FALSE;
	if (!session->displayControl)
	{
		ohos_session_set_diagnostics(session, "OHOS display-control manager is not available");
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}
	if (width == 0 || height == 0)
	{
		ohos_session_set_diagnostics(session, "OHOS session resize dimensions are invalid");
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}

	if (!freerdp_ohos_display_control_request_resize(session->displayControl, width, height,
	                                                 "session resize", message, messageSize))
	{
		ohos_session_set_diagnostics(session, "%s",
		                             message && message[0] != '\0'
		                                 ? message
		                                 : "display-control resize failed");
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}

	ohos_session_set_diagnostics(session, "%s",
	                             message && message[0] != '\0'
	                                 ? message
	                                 : "display-control resize accepted");
	ohos_session_copy_diagnostics(session, message, messageSize);
	return TRUE;
}
