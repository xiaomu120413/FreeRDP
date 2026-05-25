/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS session input dispatch API
 */

#include "ohos_session_private.h"

#include <inttypes.h>

#include <freerdp/input.h>

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

	rdpInput* input = session->instance->context->input;
	if (!freerdp_input_send_mouse_event(input, packet.flags, packet.x, packet.y))
	{
		ohos_session_set_diagnostics(session, "OHOS session pointer dispatch failed");
		ohos_session_copy_diagnostics(session, message, messageSize);
		return FALSE;
	}

	ohos_session_set_diagnostics(session, "OHOS session pointer dispatched");
	ohos_session_copy_diagnostics(session, message, messageSize);
	return TRUE;
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

	rdpInput* input = session->instance->context->input;
	size_t sent = 0;
	for (size_t index = 0; index < count; ++index)
	{
		const FREERDP_OHOS_KEY_PACKET* packet = &packets[index];
		if (packet->rdpScancode == 0)
			continue;
		if (freerdp_input_send_keyboard_event_ex(input, packet->down ? TRUE : FALSE,
		                                         packet->repeat ? TRUE : FALSE,
		                                         packet->rdpScancode))
			++sent;
	}

	ohos_session_set_diagnostics(session, "OHOS session key packets dispatched: sent=%zu/%zu",
	                             sent, count);
	ohos_session_copy_diagnostics(session, message, messageSize);
	return sent == count ? TRUE : FALSE;
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

	rdpInput* input = session->instance->context->input;
	size_t sent = 0;
	for (size_t index = 0; index < count; ++index)
	{
		const UINT16 flags = packets[index].down ? 0 : KBD_FLAGS_RELEASE;
		if (freerdp_input_send_unicode_keyboard_event(input, flags,
		                                              (UINT16)packets[index].codeUnit))
			++sent;
	}

	(void)freerdp_ohos_ime_format_committed_text_result(length, sent, skipped,
	                                                    session->diagnostics,
	                                                    sizeof(session->diagnostics));
	ohos_session_copy_diagnostics(session, message, messageSize);
	return sent == count ? TRUE : FALSE;
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
	    "; display-control dispatch is owned by the caller until T10",
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
