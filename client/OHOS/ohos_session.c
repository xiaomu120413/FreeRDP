/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS public session API
 */

#include "ohos_session_private.h"

#include "ohos_certificate.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <freerdp/addin.h>
#include <freerdp/client.h>
#include <freerdp/client/channels.h>
#include <freerdp/error.h>
#include <winpr/collections.h>
#include <winpr/synch.h>

static void ohos_session_format_message(char* message, size_t size, const char* format, ...)
{
	if (!message || size == 0)
		return;

	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, size, format, args);
	va_end(args);
}

void ohos_session_set_diagnostics(freerdpOhosSession* session, const char* format, ...)
{
	if (!session)
		return;

	va_list args;
	va_start(args, format);
	(void)vsnprintf(session->diagnostics, sizeof(session->diagnostics), format, args);
	va_end(args);
}

void ohos_session_copy_diagnostics(freerdpOhosSession* session, char* message,
                                   size_t messageSize)
{
	if (!session)
	{
		ohos_session_format_message(message, messageSize, "OHOS session is null");
		return;
	}
	ohos_session_format_message(message, messageSize, "%s", session->diagnostics);
}

static const char* ohos_session_last_error_text(UINT32 code)
{
	const char* text = freerdp_get_last_error_string(code);
	if (text && text[0] != '\0')
		return text;
	return "";
}

static const char* ohos_session_last_error_name(UINT32 code)
{
	const char* name = freerdp_get_last_error_name(code);
	if (name && name[0] != '\0')
		return name;
	return "UNKNOWN";
}

static void ohos_session_set_last_error(freerdpOhosSession* session, const char* prefix,
                                        UINT32 code)
{
	const char* name = ohos_session_last_error_name(code);
	const char* text = ohos_session_last_error_text(code);
	if (text[0] != '\0')
	{
		ohos_session_set_diagnostics(session, "%s: %s [0x%08" PRIX32 "] %s", prefix, name,
		                             code, text);
		return;
	}
	ohos_session_set_diagnostics(session, "%s: %s [0x%08" PRIX32 "]", prefix, name, code);
}

void ohos_session_emit_log(freerdpOhosSession* session, const char* message)
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

static void ohos_session_location_log(void* userData, const char* message)
{
	ohos_session_emit_log((freerdpOhosSession*)userData, message);
}

static void ohos_session_certificate_log(const char* message, void* userData)
{
	ohos_session_emit_log((freerdpOhosSession*)userData, message);
}

static BOOL ohos_session_should_continue(freerdpOhosSession* session)
{
	if (!session || session->requestedDisconnect)
		return FALSE;
	if (session->callbacks.ShouldContinue &&
	    !session->callbacks.ShouldContinue(session->callbacks.userData))
		return FALSE;
	return TRUE;
}

BOOL ohos_session_require_connected(freerdpOhosSession* session, char* message,
                                    size_t messageSize)
{
	if (!session)
	{
		ohos_session_format_message(message, messageSize, "OHOS session is null");
		return FALSE;
	}
	if (!session->connected || !session->instance || !session->instance->context ||
	    !session->instance->context->input)
	{
		ohos_session_set_diagnostics(session, "OHOS session is not connected");
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}
	return TRUE;
}

static BOOL ohos_session_validate_options(freerdpOhosSession* session,
                                          const FREERDP_OHOS_SESSION_OPTIONS* options,
                                          char* message, size_t messageSize)
{
	if (!options)
	{
		ohos_session_set_diagnostics(session, "OHOS session options are required");
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}
	if (!options->appDataDir || options->appDataDir[0] == '\0')
	{
		ohos_session_set_diagnostics(
		    session, "OHOS session connect validation failed: appDataDir is required");
		ohos_session_emit_error(session, session->diagnostics);
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}
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
	return TRUE;
}

static void ohos_session_cleanup(freerdpOhosSession* session)
{
	if (!session)
		return;

	freerdp* instance = session->instance;
	rdpContext* context = instance ? instance->context : NULL;

	if (session->contextCreated && context)
	{
		freerdp_abort_connect_context(context);
		freerdp_disconnect(instance);
	}

	if (session->teardownPending && session->callbacks.Teardown)
		session->callbacks.Teardown(instance, context, session->callbacks.userData);
	session->teardownPending = FALSE;

	freerdp_ohos_location_unregister(session->location);

	if (instance)
		freerdp_ohos_certificate_unregister_callbacks(instance);

	if (session->contextCreated && context)
		freerdp_context_free(instance);

	if (instance)
		freerdp_free(instance);

	session->instance = NULL;
	session->contextCreated = FALSE;
	session->connected = FALSE;
}

static BOOL ohos_session_enable_client_channels(freerdpOhosSession* session)
{
	if (!session || !session->instance)
		return FALSE;

	if (freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0) != 0)
	{
		ohos_session_set_diagnostics(session, "freerdp_register_addin_provider failed");
		return FALSE;
	}

	session->instance->LoadChannels = freerdp_client_load_channels;
	return TRUE;
}

static BOOL ohos_session_register_location(freerdpOhosSession* session)
{
	if (!session || !session->location || !session->instance || !session->instance->context)
		return FALSE;

	FREERDP_OHOS_LOCATION_CONFIG config = { 0 };
	config.PubSubSubscribe = PubSub_Subscribe;
	config.PubSubUnsubscribe = PubSub_Unsubscribe;
	config.Log = ohos_session_location_log;
	config.logUserData = session;

	char detail[256] = { 0 };
	if (!freerdp_ohos_location_register(session->location, session->instance->context, &config,
	                                    detail, sizeof(detail)))
	{
		ohos_session_set_diagnostics(
		    session, "%s",
		    detail[0] == '\0' ? "OHOS location bridge registration failed" : detail);
		return FALSE;
	}
	ohos_session_emit_log(session, "OHOS location bridge registered");
	return TRUE;
}

static BOOL ohos_session_apply_options(freerdpOhosSession* session,
                                       const FREERDP_OHOS_SESSION_OPTIONS* options)
{
	rdpContext* context = session->instance ? session->instance->context : NULL;
	rdpSettings* settings = context ? context->settings : NULL;
	char detail[256] = { 0 };

	if (!settings)
	{
		ohos_session_set_diagnostics(session, "FreeRDP settings unavailable");
		return FALSE;
	}

	if (!freerdp_ohos_session_apply_storage_settings(settings, options->appDataDir, detail,
	                                                 sizeof(detail)))
	{
		ohos_session_set_diagnostics(
		    session, "%s",
		    detail[0] == '\0' ? "FreeRDP OHOS storage settings helper failed" : detail);
		return FALSE;
	}
	memset(detail, 0, sizeof(detail));
	if (!freerdp_ohos_session_apply_connection_settings(settings, &options->connection, detail,
	                                                    sizeof(detail)))
	{
		ohos_session_set_diagnostics(session, "%s",
		                             detail[0] == '\0'
		                                 ? "FreeRDP OHOS connection settings helper failed"
		                                 : detail);
		return FALSE;
	}

	memset(detail, 0, sizeof(detail));
	if (!freerdp_ohos_session_apply_settings(settings, &options->session, detail, sizeof(detail)))
	{
		ohos_session_set_diagnostics(
		    session, "%s",
		    detail[0] == '\0' ? "FreeRDP OHOS session settings helper failed" : detail);
		return FALSE;
	}

	memset(detail, 0, sizeof(detail));
	if (!freerdp_ohos_session_add_standard_channels(settings, &options->session, detail,
	                                                sizeof(detail)))
	{
		ohos_session_set_diagnostics(
		    session, "%s",
		    detail[0] == '\0' ? "FreeRDP OHOS standard channel helper failed" : detail);
		return FALSE;
	}
	if (detail[0] != '\0')
		ohos_session_emit_log(session, detail);
	return TRUE;
}

static BOOL ohos_session_configure(freerdpOhosSession* session,
                                   const FREERDP_OHOS_SESSION_OPTIONS* options)
{
	char detail[256] = { 0 };

	session->instance = freerdp_new();
	if (!session->instance)
	{
		ohos_session_set_diagnostics(session, "freerdp_new failed");
		return FALSE;
	}

	if (!freerdp_context_new(session->instance))
	{
		ohos_session_set_diagnostics(session, "freerdp_context_new failed");
		return FALSE;
	}
	session->contextCreated = TRUE;

	if (options->session.location && !ohos_session_register_location(session))
		return FALSE;
	if (!ohos_session_enable_client_channels(session) || !ohos_session_apply_options(session, options))
		return FALSE;

	if (!freerdp_ohos_certificate_register_callbacks(
	        session->instance, options->certificatePolicy, ohos_session_certificate_log, session,
	        detail, sizeof(detail)))
	{
		ohos_session_set_diagnostics(
		    session, "%s",
		                     detail[0] == '\0' ? "OHOS certificate callback registration failed" : detail);
		return FALSE;
	}

	session->teardownPending = TRUE;
	if (session->callbacks.Configure &&
	    !session->callbacks.Configure(session->instance, session->instance->context, options,
	                                  detail, sizeof(detail), session->callbacks.userData))
	{
		ohos_session_set_diagnostics(
		    session, "%s",
		    detail[0] == '\0' ? "OHOS session configure callback failed" : detail);
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
	session->inputQueue = freerdp_ohos_input_queue_new();
	session->displayControl = freerdp_ohos_display_control_new();
	session->location = freerdp_ohos_location_new();
	if (!session->keyboard || !session->inputQueue || !session->displayControl || !session->location)
	{
		freerdp_ohos_keyboard_state_free(session->keyboard);
		freerdp_ohos_input_queue_free(session->inputQueue);
		freerdp_ohos_display_control_free(session->displayControl);
		freerdp_ohos_location_free(session->location);
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

	if (session->instance)
		freerdp_ohos_session_disconnect(session);
	ohos_session_cleanup(session);
	freerdp_ohos_keyboard_state_free(session->keyboard);
	freerdp_ohos_input_queue_free(session->inputQueue);
	freerdp_ohos_display_control_free(session->displayControl);
	freerdp_ohos_location_free(session->location);
	free(session);
}

BOOL freerdp_ohos_session_connect(freerdpOhosSession* session,
                                  const FREERDP_OHOS_SESSION_OPTIONS* options,
                                  const FREERDP_OHOS_SESSION_CALLBACKS* callbacks,
                                  char* message, size_t messageSize)
{
	BOOL success = FALSE;

	if (!session)
	{
		ohos_session_format_message(message, messageSize, "OHOS session is null");
		return FALSE;
	}
	if (session->instance)
	{
		ohos_session_set_diagnostics(session, "OHOS session is already running");
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}

	if (callbacks)
		session->callbacks = *callbacks;
	else
		memset(&session->callbacks, 0, sizeof(session->callbacks));

	session->requestedDisconnect = FALSE;
	session->connected = FALSE;
	freerdp_ohos_keyboard_state_reset(session->keyboard);
	freerdp_ohos_input_queue_reset(session->inputQueue);

	if (!ohos_session_validate_options(session, options, message, messageSize))
		return FALSE;
	ohos_session_prepare_display_control(session, options->session.h264);

	ohos_session_emit_state(session, "Configuring");
	if (!ohos_session_configure(session, options))
	{
		ohos_session_emit_error(session, session->diagnostics);
		ohos_session_copy_diagnostics(session, message, messageSize);
		ohos_session_cleanup(session);
		return FALSE;
	}

	ohos_session_emit_state(session, "Connecting");
	const BOOL rc = freerdp_connect(session->instance);
	UINT32 lastError = session->instance && session->instance->context
	                       ? freerdp_get_last_error(session->instance->context)
	                       : UINT32_MAX;

	if (!ohos_session_should_continue(session))
	{
		ohos_session_set_diagnostics(session, "FreeRDP connect cancelled");
		ohos_session_copy_diagnostics(session, message, messageSize);
		ohos_session_cleanup(session);
		return FALSE;
	}

	if (!rc)
	{
		ohos_session_set_last_error(session, "FreeRDP connect failed", lastError);
		ohos_session_emit_error(session, session->diagnostics);
		ohos_session_copy_diagnostics(session, message, messageSize);
		ohos_session_cleanup(session);
		return FALSE;
	}

	session->connected = TRUE;
	ohos_session_set_diagnostics(session, "FreeRDP session connected");
	ohos_session_emit_state(session, "Connected");
	if (session->callbacks.Connected)
		session->callbacks.Connected(session->instance, session->instance->context,
		                             session->callbacks.userData);

	while (ohos_session_should_continue(session) &&
	       !freerdp_shall_disconnect_context(session->instance->context))
	{
		char inputDetail[256] = { 0 };
		(void)freerdp_ohos_input_queue_drain(session->inputQueue,
		                                     session->instance->context, inputDetail,
		                                     sizeof(inputDetail));

		if (session->callbacks.Pump &&
		    !session->callbacks.Pump(session->instance, session->instance->context,
		                             session->callbacks.userData))
		{
			ohos_session_set_diagnostics(session, "OHOS session pump callback failed");
			break;
		}

		HANDLE handles[MAXIMUM_WAIT_OBJECTS] = { 0 };
		DWORD count =
		    freerdp_get_event_handles(session->instance->context, handles, MAXIMUM_WAIT_OBJECTS);
		if (count == 0)
		{
			lastError = freerdp_get_last_error(session->instance->context);
			ohos_session_set_last_error(session, "freerdp_get_event_handles failed", lastError);
			break;
		}

		const DWORD waitStatus = WaitForMultipleObjects(count, handles, FALSE, 5);
		if (!ohos_session_should_continue(session))
		{
			ohos_session_set_diagnostics(session, "FreeRDP session cancelled");
			break;
		}

		if (waitStatus == WAIT_TIMEOUT)
			continue;

		if (waitStatus == WAIT_FAILED)
		{
			ohos_session_set_diagnostics(session, "WaitForMultipleObjects failed: 0x%08" PRIX32,
			                             (UINT32)waitStatus);
			break;
		}

		if (!freerdp_check_event_handles(session->instance->context))
		{
			lastError = freerdp_get_last_error(session->instance->context);
			if (lastError == FREERDP_ERROR_SUCCESS)
			{
				ohos_session_set_diagnostics(session, "FreeRDP event loop stopped without error");
				success = TRUE;
			}
			else if (options->session.h264 && lastError == ERROR_NOT_SUPPORTED)
			{
				ohos_session_set_diagnostics(
				    session,
				    "FreeRDP graphics negotiation failed: server did not confirm requested RDPGFX H264 mode");
			}
			else
			{
				ohos_session_set_last_error(session, "FreeRDP event loop failed", lastError);
			}
			break;
		}

		if (session->callbacks.Pump)
			(void)session->callbacks.Pump(session->instance, session->instance->context,
			                              session->callbacks.userData);
	}

	if (!ohos_session_should_continue(session))
		ohos_session_set_diagnostics(session, "FreeRDP session cancelled");

	if (ohos_session_should_continue(session) && session->connected && session->instance &&
	    session->instance->context && freerdp_shall_disconnect_context(session->instance->context))
	{
		lastError = freerdp_get_last_error(session->instance->context);
		if (lastError != FREERDP_ERROR_SUCCESS)
			ohos_session_set_last_error(session, "FreeRDP session disconnected", lastError);
		else if (session->diagnostics[0] == '\0' ||
		         strncmp(session->diagnostics, "display-control resize pending", 30) == 0)
		{
			ohos_session_set_diagnostics(session, "FreeRDP session disconnected");
		}
	}

	if (ohos_session_should_continue(session) && session->connected &&
	    session->diagnostics[0] != '\0' &&
	    strcmp(session->diagnostics, "FreeRDP session connected") == 0)
	{
		ohos_session_set_diagnostics(session, "FreeRDP session ended");
		success = TRUE;
	}

	ohos_session_copy_diagnostics(session, message, messageSize);
	if (!success && ohos_session_should_continue(session))
		ohos_session_emit_error(session, session->diagnostics);

	ohos_session_cleanup(session);
	ohos_session_emit_state(session, "Disconnected");
	return success;
}

void freerdp_ohos_session_disconnect(freerdpOhosSession* session)
{
	if (!session)
		return;

	session->requestedDisconnect = TRUE;
	session->connected = FALSE;
	freerdp_ohos_keyboard_state_reset(session->keyboard);
	freerdp_ohos_input_queue_clear(session->inputQueue);
	freerdp_ohos_display_control_reset(session->displayControl);
	if (session->instance && session->instance->context)
		freerdp_abort_connect_context(session->instance->context);
	ohos_session_set_diagnostics(session, "OHOS session disconnected");
	ohos_session_emit_state(session, "Disconnected");
}
