/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS public session API skeleton
 */

#include "ohos_session.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <freerdp/input.h>

struct freerdp_ohos_session
{
	BOOL connected;
	BOOL connectAttempted;
	FREERDP_OHOS_SESSION_CALLBACKS callbacks;
	FREERDP_OHOS_KEYBOARD_STATE* keyboard;
	char diagnostics[512];
};

static void ohos_session_format_message(char* message, size_t size, const char* format, ...)
{
	if (!message || size == 0)
		return;

	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, size, format, args);
	va_end(args);
}

static void ohos_session_set_diagnostics(freerdpOhosSession* session, const char* format, ...)
{
	if (!session)
		return;

	va_list args;
	va_start(args, format);
	(void)vsnprintf(session->diagnostics, sizeof(session->diagnostics), format, args);
	va_end(args);
}

static void ohos_session_copy_diagnostics(freerdpOhosSession* session, char* message,
                                          size_t messageSize)
{
	if (!session)
	{
		ohos_session_format_message(message, messageSize, "OHOS session is null");
		return;
	}
	ohos_session_format_message(message, messageSize, "%s", session->diagnostics);
}

static void ohos_session_emit_log(freerdpOhosSession* session, const char* message)
{
	if (session && session->callbacks.Log)
		session->callbacks.Log(message, session->callbacks.userData);
}

static void ohos_session_emit_error(freerdpOhosSession* session, const char* message)
{
	if (session && session->callbacks.Error)
		session->callbacks.Error(message, session->callbacks.userData);
}

static void ohos_session_emit_state(freerdpOhosSession* session, const char* state)
{
	if (session && session->callbacks.StateChanged)
		session->callbacks.StateChanged(state, session->callbacks.userData);
}

static BOOL ohos_session_require_connected(freerdpOhosSession* session, char* message,
                                           size_t messageSize)
{
	if (!session)
	{
		ohos_session_format_message(message, messageSize, "OHOS session is null");
		return FALSE;
	}
	if (!session->connected)
	{
		ohos_session_set_diagnostics(session, "OHOS session is not connected");
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}
	return TRUE;
}

freerdpOhosSession* freerdp_ohos_session_new(void)
{
	freerdpOhosSession* session = (freerdpOhosSession*)calloc(1, sizeof(freerdpOhosSession));
	if (!session)
		return NULL;

	session->keyboard = freerdp_ohos_keyboard_state_new();
	if (!session->keyboard)
	{
		free(session);
		return NULL;
	}

	ohos_session_set_diagnostics(session, "OHOS session created");
	return session;
}

void freerdp_ohos_session_free(freerdpOhosSession* session)
{
	if (!session)
		return;

	freerdp_ohos_session_disconnect(session);
	freerdp_ohos_keyboard_state_free(session->keyboard);
	free(session);
}

BOOL freerdp_ohos_session_connect(freerdpOhosSession* session,
                                  const FREERDP_OHOS_SESSION_OPTIONS* options,
                                  const FREERDP_OHOS_SESSION_CALLBACKS* callbacks,
                                  char* message, size_t messageSize)
{
	if (!session)
	{
		ohos_session_format_message(message, messageSize, "OHOS session is null");
		return FALSE;
	}
	if (!options)
	{
		ohos_session_set_diagnostics(session, "OHOS session options are required");
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}

	if (callbacks)
		session->callbacks = *callbacks;
	else
		memset(&session->callbacks, 0, sizeof(session->callbacks));

	session->connectAttempted = TRUE;
	session->connected = FALSE;
	freerdp_ohos_keyboard_state_reset(session->keyboard);

	if (!options->connection.serverHostname || options->connection.serverHostname[0] == '\0' ||
	    !options->connection.username || options->connection.username[0] == '\0')
	{
		ohos_session_set_diagnostics(session,
		                             "OHOS session connect validation failed: host and username "
		                             "are required");
		ohos_session_emit_error(session, session->diagnostics);
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}

	ohos_session_set_diagnostics(
	    session,
	    "OHOS session API skeleton is present; HAP still owns the FreeRDP event loop until T03");
	ohos_session_emit_state(session, "Idle");
	ohos_session_emit_log(session, session->diagnostics);
	ohos_session_copy_diagnostics(session, message, messageSize);
	return FALSE;
}

void freerdp_ohos_session_disconnect(freerdpOhosSession* session)
{
	if (!session)
		return;

	session->connected = FALSE;
	freerdp_ohos_keyboard_state_reset(session->keyboard);
	ohos_session_set_diagnostics(session, "OHOS session disconnected");
	ohos_session_emit_state(session, "Disconnected");
}

BOOL freerdp_ohos_session_send_pointer(freerdpOhosSession* session,
                                       const FREERDP_OHOS_POINTER_VIEWPORT* viewport,
                                       const FREERDP_OHOS_POINTER_EVENT* event, char* message,
                                       size_t messageSize)
{
	if (!ohos_session_require_connected(session, message, messageSize))
		return FALSE;

	FREERDP_OHOS_POINTER_PACKET packet = { 0 };
	if (!freerdp_ohos_pointer_build_event(viewport, event, &packet, message, messageSize))
		return FALSE;

	ohos_session_set_diagnostics(session,
	                             "OHOS session pointer packet built; FreeRDP dispatch pending T03");
	ohos_session_copy_diagnostics(session, message, messageSize);
	return FALSE;
}

BOOL freerdp_ohos_session_send_key(freerdpOhosSession* session,
                                   const FREERDP_OHOS_KEY_EVENT* event, char* message,
                                   size_t messageSize)
{
	if (!ohos_session_require_connected(session, message, messageSize))
		return FALSE;

	FREERDP_OHOS_KEY_PACKET packets[16] = { 0 };
	size_t count = 0;
	if (!freerdp_ohos_keyboard_state_handle_event(session->keyboard, event, packets,
	                                              sizeof(packets) / sizeof(packets[0]),
	                                              &count))
	{
		ohos_session_set_diagnostics(session, "OHOS session key event mapping failed");
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}

	ohos_session_set_diagnostics(
	    session, "OHOS session key packets built: count=%zu; FreeRDP dispatch pending T03",
	    count);
	ohos_session_copy_diagnostics(session, message, messageSize);
	return FALSE;
}

BOOL freerdp_ohos_session_send_text(freerdpOhosSession* session, const uint16_t* text,
                                    size_t length, char* message, size_t messageSize)
{
	if (!ohos_session_require_connected(session, message, messageSize))
		return FALSE;

	FREERDP_OHOS_IME_PACKET packets[64] = { 0 };
	size_t count = 0;
	size_t skipped = 0;
	if (!freerdp_ohos_ime_build_committed_text_packets(text, length, packets,
	                                                   sizeof(packets) / sizeof(packets[0]),
	                                                   &count, &skipped))
	{
		ohos_session_set_diagnostics(session, "OHOS session committed text mapping failed");
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}

	(void)freerdp_ohos_ime_format_committed_text_result(length, count, skipped,
	                                                    session->diagnostics,
	                                                    sizeof(session->diagnostics));
	ohos_session_copy_diagnostics(session, message, messageSize);
	return FALSE;
}

BOOL freerdp_ohos_session_resize(freerdpOhosSession* session, UINT32 width, UINT32 height,
                                 char* message, size_t messageSize)
{
	if (!ohos_session_require_connected(session, message, messageSize))
		return FALSE;
	if (width == 0 || height == 0)
	{
		ohos_session_set_diagnostics(session, "OHOS session resize dimensions are invalid");
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}

	ohos_session_set_diagnostics(
	    session,
	    "OHOS session resize requested: %" PRIu32 "x%" PRIu32
	    "; display-control dispatch pending T03",
	    width, height);
	ohos_session_copy_diagnostics(session, message, messageSize);
	return FALSE;
}

const char* freerdp_ohos_session_get_diagnostics(freerdpOhosSession* session)
{
	if (!session)
		return "OHOS session diagnostics unavailable: session is null";
	return session->diagnostics;
}
