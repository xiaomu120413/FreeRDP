/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS display-control session API
 */

#include "ohos_session_private.h"

#include <inttypes.h>
#include <stddef.h>
#include <string.h>

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

static uint32_t ohos_session_resize_status(FREERDP_OHOS_DISPLAY_RESIZE_STATUS status)
{
	switch (status)
	{
		case FREERDP_OHOS_DISPLAY_RESIZE_SENT:
			return FREERDP_OHOS_SESSION_RESIZE_SENT;
		case FREERDP_OHOS_DISPLAY_RESIZE_DEFERRED:
			return FREERDP_OHOS_SESSION_RESIZE_DEFERRED;
		case FREERDP_OHOS_DISPLAY_RESIZE_UNCHANGED:
			return FREERDP_OHOS_SESSION_RESIZE_UNCHANGED;
		case FREERDP_OHOS_DISPLAY_RESIZE_FAILED:
		default:
			return FREERDP_OHOS_SESSION_RESIZE_FAILED;
	}
}

BOOL freerdp_ohos_session_set_monitor_layout(
    freerdpOhosSession* session, const FREERDP_OHOS_MONITOR_LAYOUT_REQUEST* request,
    char* message, size_t messageSize)
{
	DISPLAY_CONTROL_MONITOR_LAYOUT validated[FREERDP_OHOS_MAX_MONITORS] = { 0 };
	if (!session || !freerdp_ohos_display_validate_monitor_layout(request, validated, message,
	                                                              messageSize))
		return FALSE;
	if (!freerdp_ohos_display_control_request_monitor_layout(
	        session->displayControl, request, "session monitor layout", message, messageSize))
		return FALSE;
	session->monitorCount = request->monitorCount;
	if (request->monitorCount > 0)
		memcpy(session->monitors, request->monitors,
		       request->monitorCount * sizeof(session->monitors[0]));
	return TRUE;
}

BOOL freerdp_ohos_session_resize_ex(
    freerdpOhosSession* session, const FREERDP_OHOS_SESSION_RESIZE_REQUEST* request,
    FREERDP_OHOS_SESSION_RESIZE_RESULT* result, char* message, size_t messageSize)
{
	const size_t requestMinimum = offsetof(FREERDP_OHOS_SESSION_RESIZE_REQUEST, orientation) +
	                              sizeof(request->orientation);
	const size_t resultMinimum = offsetof(FREERDP_OHOS_SESSION_RESIZE_RESULT, orientation) +
	                             sizeof(result->orientation);
	if (!request || !result || request->structSize < requestMinimum ||
	    result->structSize < resultMinimum ||
	    request->version != FREERDP_OHOS_SESSION_RESIZE_VERSION)
	{
		if (session)
		{
			ohos_session_set_diagnostics(session, "OHOS session resize_ex arguments are invalid");
			ohos_session_copy_diagnostics(session, message, messageSize);
		}
		return FALSE;
	}

	const uint32_t callerResultSize = result->structSize;
	FREERDP_OHOS_SESSION_RESIZE_RESULT local = {
		0,
	};
	local.structSize = sizeof(local);
	local.version = FREERDP_OHOS_SESSION_RESIZE_VERSION;
	local.status = FREERDP_OHOS_SESSION_RESIZE_FAILED;
	local.orientation = request->orientation;

	if (!ohos_session_require_connected(session, message, messageSize))
	{
		memcpy(result, &local, callerResultSize < sizeof(local) ? callerResultSize : sizeof(local));
		return TRUE;
	}
	if (!session->displayControl)
	{
		local.status = FREERDP_OHOS_SESSION_RESIZE_UNSUPPORTED;
		ohos_session_set_diagnostics(session, "OHOS display-control manager is not available");
		ohos_session_copy_diagnostics(session, message, messageSize);
		memcpy(result, &local, callerResultSize < sizeof(local) ? callerResultSize : sizeof(local));
		return TRUE;
	}

	FREERDP_OHOS_DISPLAY_RESIZE_RESULT displayResult = {
		0,
	};
	const size_t metricsMinimum = offsetof(FREERDP_OHOS_SESSION_RESIZE_REQUEST,
	                                      deviceScaleFactor) +
	                              sizeof(request->deviceScaleFactor);
	const BOOL hasMetrics = request->structSize >= metricsMinimum;
	const BOOL accepted = freerdp_ohos_display_control_request_resize_layout_ex(
	    session->displayControl, request->width, request->height,
	    hasMetrics ? request->physicalWidth : 0,
	    hasMetrics ? request->physicalHeight : 0, request->orientation,
	    hasMetrics ? request->desktopScaleFactor : 100,
	    hasMetrics ? request->deviceScaleFactor : 100, "session resize", &displayResult,
	    message, messageSize);
	local.status = ohos_session_resize_status(displayResult.status);
	local.normalizedWidth = displayResult.normalizedWidth;
	local.normalizedHeight = displayResult.normalizedHeight;
	local.sentWidth = displayResult.sentWidth;
	local.sentHeight = displayResult.sentHeight;
	local.orientation = displayResult.orientation;
	if (!accepted)
		local.status = FREERDP_OHOS_SESSION_RESIZE_FAILED;

	ohos_session_set_diagnostics(session, "%s",
	                             message && message[0] != '\0'
	                                 ? message
	                                 : "display-control resize_ex completed");
	ohos_session_copy_diagnostics(session, message, messageSize);
	memcpy(result, &local, callerResultSize < sizeof(local) ? callerResultSize : sizeof(local));
	return TRUE;
}
