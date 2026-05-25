/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS input queue, coalescing and worker dispatch
 */

#include "ohos_input_queue.h"

#include "ohos_ime.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <freerdp/input.h>

#define OHOS_INPUT_QUEUE_MAX_EVENTS 4096U
#define OHOS_INPUT_QUEUE_KEY_PACKETS 96U

typedef enum
{
	OHOS_INPUT_POINTER,
	OHOS_INPUT_KEY_SCANCODE,
	OHOS_INPUT_PLATFORM_KEY,
	OHOS_INPUT_PLATFORM_KEY_PACKET,
	OHOS_INPUT_UNICODE,
	OHOS_INPUT_FOCUS_IN
} OHOS_INPUT_EVENT_TYPE;

typedef struct
{
	OHOS_INPUT_EVENT_TYPE type;
	UINT16 flags;
	UINT16 x;
	UINT16 y;
	UINT32 scancode;
	UINT32 keyCode;
	UINT32 vk;
	UINT32 code;
	BOOL ctrl;
	BOOL shift;
	BOOL alt;
	BOOL meta;
	BOOL down;
	BOOL repeat;
	BOOL extended;
	BOOL synthetic;
} OHOS_INPUT_EVENT;

struct freerdp_ohos_input_queue
{
	pthread_mutex_t mutex;
	OHOS_INPUT_EVENT events[OHOS_INPUT_QUEUE_MAX_EVENTS];
	size_t count;
	FREERDP_OHOS_KEYBOARD_STATE* keyboard;
	FREERDP_OHOS_INPUT_QUEUE_DIAGNOSTICS stats;
	char diagnostics[512];
};

static void ohos_input_format(char* message, size_t size, const char* format, ...)
{
	if (!message || size == 0)
		return;

	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, size, format, args);
	va_end(args);
}

static void ohos_input_set_diagnostics(freerdpOhosInputQueue* queue, const char* format, ...)
{
	if (!queue)
		return;

	va_list args;
	va_start(args, format);
	(void)vsnprintf(queue->diagnostics, sizeof(queue->diagnostics), format, args);
	va_end(args);
}

static const char* ohos_input_type_name(const OHOS_INPUT_EVENT* event)
{
	switch (event->type)
	{
		case OHOS_INPUT_POINTER:
			return "pointer";
		case OHOS_INPUT_KEY_SCANCODE:
			return "key";
		case OHOS_INPUT_PLATFORM_KEY:
			return "platform-key";
		case OHOS_INPUT_PLATFORM_KEY_PACKET:
			return "platform-key-packet";
		case OHOS_INPUT_FOCUS_IN:
			return "focus-in";
		default:
			return "unicode";
	}
}

static BOOL ohos_input_is_pointer_wheel(const OHOS_INPUT_EVENT* event)
{
	return event->type == OHOS_INPUT_POINTER &&
	       ((event->flags & (PTR_FLAGS_WHEEL | PTR_FLAGS_HWHEEL)) != 0);
}

static BOOL ohos_input_is_pointer_motion(const OHOS_INPUT_EVENT* event)
{
	return event->type == OHOS_INPUT_POINTER && ((event->flags & PTR_FLAGS_MOVE) != 0) &&
	       !ohos_input_is_pointer_wheel(event);
}

static BOOL ohos_input_same_motion_class(const OHOS_INPUT_EVENT* lhs,
                                         const OHOS_INPUT_EVENT* rhs)
{
	const UINT16 mask = PTR_FLAGS_BUTTON1 | PTR_FLAGS_BUTTON2 | PTR_FLAGS_BUTTON3 | PTR_FLAGS_DOWN;
	return (lhs->flags & mask) == (rhs->flags & mask);
}

static BOOL ohos_input_is_droppable(const OHOS_INPUT_EVENT* event)
{
	return ohos_input_is_pointer_motion(event) || ohos_input_is_pointer_wheel(event);
}

static BOOL ohos_input_drop_oldest_pointer(freerdpOhosInputQueue* queue)
{
	for (size_t index = 0; index < queue->count; ++index)
	{
		if (!ohos_input_is_droppable(&queue->events[index]))
			continue;
		if (index + 1 < queue->count)
		{
			memmove(&queue->events[index], &queue->events[index + 1],
			        (queue->count - index - 1) * sizeof(queue->events[0]));
		}
		--queue->count;
		queue->stats.depth = (uint32_t)queue->count;
		return TRUE;
	}
	return FALSE;
}

static BOOL ohos_input_append_pending(OHOS_INPUT_EVENT** events, size_t* count,
                                      size_t* capacity, const OHOS_INPUT_EVENT* event)
{
	if (*count >= *capacity)
	{
		const size_t next = *capacity == 0 ? 64U : *capacity * 2U;
		void* data = realloc(*events, next * sizeof(**events));
		if (!data)
			return FALSE;
		*events = (OHOS_INPUT_EVENT*)data;
		*capacity = next;
	}
	(*events)[(*count)++] = *event;
	return TRUE;
}

static BOOL ohos_input_enqueue_locked(freerdpOhosInputQueue* queue,
                                      const OHOS_INPUT_EVENT* event, const char* okMessage,
                                      char* message, size_t messageSize)
{
	if (ohos_input_is_pointer_motion(event) && queue->count > 0 &&
	    ohos_input_is_pointer_motion(&queue->events[queue->count - 1]) &&
	    ohos_input_same_motion_class(event, &queue->events[queue->count - 1]))
	{
		queue->events[queue->count - 1] = *event;
		++queue->stats.queued;
		++queue->stats.coalesced;
		ohos_input_format(message, messageSize, "%s", okMessage);
		return TRUE;
	}

	if (queue->count >= OHOS_INPUT_QUEUE_MAX_EVENTS)
	{
		const BOOL protect = event->type != OHOS_INPUT_POINTER || !ohos_input_is_droppable(event);
		if (protect && ohos_input_drop_oldest_pointer(queue))
			++queue->stats.dropped;
	}
	if (queue->count >= OHOS_INPUT_QUEUE_MAX_EVENTS)
	{
		++queue->stats.dropped;
		ohos_input_format(message, messageSize,
		                  "FreeRDP input queue is full; dropped %s event",
		                  ohos_input_type_name(event));
		ohos_input_set_diagnostics(queue, "%s", message ? message : "");
		return FALSE;
	}

	queue->events[queue->count++] = *event;
	queue->stats.depth = (uint32_t)queue->count;
	++queue->stats.queued;
	ohos_input_format(message, messageSize, "%s", okMessage);
	return TRUE;
}

static BOOL ohos_input_enqueue(freerdpOhosInputQueue* queue, const OHOS_INPUT_EVENT* event,
                               const char* okMessage, char* message, size_t messageSize)
{
	if (!queue)
	{
		ohos_input_format(message, messageSize, "OHOS input queue is null");
		return FALSE;
	}

	pthread_mutex_lock(&queue->mutex);
	const BOOL ok = ohos_input_enqueue_locked(queue, event, okMessage, message, messageSize);
	pthread_mutex_unlock(&queue->mutex);
	return ok;
}

freerdpOhosInputQueue* freerdp_ohos_input_queue_new(void)
{
	freerdpOhosInputQueue* queue = (freerdpOhosInputQueue*)calloc(1, sizeof(*queue));
	if (!queue)
		return NULL;
	if (pthread_mutex_init(&queue->mutex, NULL) != 0)
	{
		free(queue);
		return NULL;
	}
	queue->keyboard = freerdp_ohos_keyboard_state_new();
	if (!queue->keyboard)
	{
		pthread_mutex_destroy(&queue->mutex);
		free(queue);
		return NULL;
	}
	ohos_input_set_diagnostics(queue, "OHOS input queue created");
	return queue;
}

void freerdp_ohos_input_queue_free(freerdpOhosInputQueue* queue)
{
	if (!queue)
		return;
	freerdp_ohos_keyboard_state_free(queue->keyboard);
	pthread_mutex_destroy(&queue->mutex);
	free(queue);
}

void freerdp_ohos_input_queue_clear(freerdpOhosInputQueue* queue)
{
	if (!queue)
		return;
	pthread_mutex_lock(&queue->mutex);
	queue->count = 0;
	queue->stats.depth = 0;
	freerdp_ohos_keyboard_state_reset(queue->keyboard);
	ohos_input_set_diagnostics(queue, "OHOS input queue cleared");
	pthread_mutex_unlock(&queue->mutex);
}

void freerdp_ohos_input_queue_reset(freerdpOhosInputQueue* queue)
{
	if (!queue)
		return;
	pthread_mutex_lock(&queue->mutex);
	queue->count = 0;
	memset(&queue->stats, 0, sizeof(queue->stats));
	freerdp_ohos_keyboard_state_reset(queue->keyboard);
	ohos_input_set_diagnostics(queue, "OHOS input queue reset");
	pthread_mutex_unlock(&queue->mutex);
}

BOOL freerdp_ohos_input_queue_enqueue_pointer_packet(freerdpOhosInputQueue* queue,
                                                     UINT16 flags, UINT16 x, UINT16 y,
                                                     char* message, size_t messageSize)
{
	OHOS_INPUT_EVENT event = { 0 };
	event.type = OHOS_INPUT_POINTER;
	event.flags = flags;
	event.x = x;
	event.y = y;
	return ohos_input_enqueue(queue, &event, "pointer event queued", message, messageSize);
}

BOOL freerdp_ohos_input_queue_enqueue_pointer(freerdpOhosInputQueue* queue,
                                              const FREERDP_OHOS_POINTER_VIEWPORT* viewport,
                                              const FREERDP_OHOS_POINTER_EVENT* pointer,
                                              char* message, size_t messageSize)
{
	FREERDP_OHOS_POINTER_PACKET packet = { 0 };
	if (!freerdp_ohos_pointer_build_event(viewport, pointer, &packet, message, messageSize))
		return FALSE;
	return freerdp_ohos_input_queue_enqueue_pointer_packet(queue, packet.flags, packet.x,
	                                                       packet.y, message, messageSize);
}

BOOL freerdp_ohos_input_queue_enqueue_key_scancode(freerdpOhosInputQueue* queue,
                                                   UINT32 rdpScancode, BOOL down, BOOL repeat,
                                                   char* message, size_t messageSize)
{
	OHOS_INPUT_EVENT event = { 0 };
	event.type = OHOS_INPUT_KEY_SCANCODE;
	event.scancode = rdpScancode;
	event.down = down ? TRUE : FALSE;
	event.repeat = repeat ? TRUE : FALSE;
	return ohos_input_enqueue(queue, &event,
	                          down ? (repeat ? "key down queued repeat" : "key down queued")
	                               : "key up queued",
	                          message, messageSize);
}

BOOL freerdp_ohos_input_queue_enqueue_key(freerdpOhosInputQueue* queue,
                                          const FREERDP_OHOS_KEY_EVENT* key, char* message,
                                          size_t messageSize)
{
	FREERDP_OHOS_KEY_RESOLVED resolved = { 0 };
	if (!key || !freerdp_ohos_keyboard_resolve_event(key, &resolved) || !resolved.mapped)
	{
		char detail[160] = { 0 };
		if (key)
			(void)freerdp_ohos_keyboard_format_event(key, detail, sizeof(detail));
		ohos_input_format(message, messageSize, "%s not mapped",
		                  detail[0] == '\0' ? "ohos.key" : detail);
		return FALSE;
	}

	OHOS_INPUT_EVENT event = { 0 };
	event.type = OHOS_INPUT_PLATFORM_KEY;
	event.keyCode = key->keyCode;
	event.vk = resolved.windowsVk;
	event.down = key->down ? TRUE : FALSE;
	event.repeat = key->repeat ? TRUE : FALSE;
	event.ctrl = key->ctrl ? TRUE : FALSE;
	event.shift = key->shift ? TRUE : FALSE;
	event.alt = key->alt ? TRUE : FALSE;
	event.meta = key->meta ? TRUE : FALSE;
	event.extended = resolved.extended ? TRUE : FALSE;
	return ohos_input_enqueue(queue, &event,
	                          key->down ? (key->repeat ? "platform key down queued repeat"
	                                                    : "platform key down queued")
	                                    : "platform key up queued",
	                          message, messageSize);
}

BOOL freerdp_ohos_input_queue_enqueue_unicode(freerdpOhosInputQueue* queue, UINT32 codeUnit,
                                              BOOL down, char* message, size_t messageSize)
{
	if (codeUnit == 0 || codeUnit > 0xFFFFU)
	{
		ohos_input_format(message, messageSize, "unicode input requires a BMP UTF-16 code unit");
		return FALSE;
	}
	OHOS_INPUT_EVENT event = { 0 };
	event.type = OHOS_INPUT_UNICODE;
	event.code = codeUnit;
	event.down = down ? TRUE : FALSE;
	return ohos_input_enqueue(queue, &event, down ? "unicode key down queued" : "unicode key up queued",
	                          message, messageSize);
}

BOOL freerdp_ohos_input_queue_enqueue_text(freerdpOhosInputQueue* queue, const uint16_t* text,
                                           size_t length, char* message, size_t messageSize)
{
	if (!queue)
	{
		ohos_input_format(message, messageSize, "OHOS input queue is null");
		return FALSE;
	}
	if (!text || length == 0)
	{
		ohos_input_format(message, messageSize, "committed text input is empty");
		return FALSE;
	}
	FREERDP_OHOS_IME_PACKET* packets =
	    (FREERDP_OHOS_IME_PACKET*)calloc(length * 2U, sizeof(*packets));
	if (!packets)
	{
		ohos_input_format(message, messageSize, "OHOS IME packet allocation failed");
		return FALSE;
	}

	size_t packetCount = 0;
	size_t skipped = 0;
	const BOOL built = freerdp_ohos_ime_build_committed_text_packets(
	    text, length, packets, length * 2U, &packetCount, &skipped);
	(void)freerdp_ohos_ime_format_committed_text_result(length, packetCount, skipped, message,
	                                                    messageSize);
	if (!built || packetCount == 0)
	{
		free(packets);
		return skipped == 0 ? TRUE : FALSE;
	}

	BOOL ok = TRUE;
	pthread_mutex_lock(&queue->mutex);
	for (size_t index = 0; index < packetCount; ++index)
	{
		OHOS_INPUT_EVENT event = { 0 };
		event.type = OHOS_INPUT_UNICODE;
		event.code = packets[index].codeUnit;
		event.down = packets[index].down ? TRUE : FALSE;
		char packetMessage[96] = { 0 };
		if (!ohos_input_enqueue_locked(queue, &event, "unicode text packet queued",
		                               packetMessage, sizeof(packetMessage)))
		{
			ohos_input_format(message, messageSize, "%s", packetMessage);
			ok = FALSE;
			break;
		}
	}
	pthread_mutex_unlock(&queue->mutex);
	free(packets);
	return ok;
}

BOOL freerdp_ohos_input_queue_enqueue_focus_in(freerdpOhosInputQueue* queue,
                                               UINT16 toggleStates, char* message,
                                               size_t messageSize)
{
	OHOS_INPUT_EVENT event = { 0 };
	event.type = OHOS_INPUT_FOCUS_IN;
	event.flags = toggleStates;
	return ohos_input_enqueue(queue, &event, "focus-in event queued", message, messageSize);
}

BOOL freerdp_ohos_input_queue_enqueue_release_all_keys(freerdpOhosInputQueue* queue,
                                                       char* message, size_t messageSize)
{
	OHOS_INPUT_EVENT event = { 0 };
	event.type = OHOS_INPUT_PLATFORM_KEY;
	event.synthetic = TRUE;
	return ohos_input_enqueue(queue, &event, "platform key release-all queued", message,
	                          messageSize);
}

static BOOL ohos_input_append_key_packets(freerdpOhosInputQueue* queue,
                                          const OHOS_INPUT_EVENT* event,
                                          OHOS_INPUT_EVENT** pending, size_t* count,
                                          size_t* capacity)
{
	FREERDP_OHOS_KEY_PACKET packets[OHOS_INPUT_QUEUE_KEY_PACKETS] = { 0 };
	size_t packetCount = 0;
	BOOL ok = FALSE;
	if (event->synthetic)
	{
		ok = freerdp_ohos_keyboard_state_release_all(queue->keyboard, packets,
		                                             OHOS_INPUT_QUEUE_KEY_PACKETS,
		                                             &packetCount)
		     ? TRUE
		     : FALSE;
	}
	else
	{
		FREERDP_OHOS_KEY_EVENT key = { event->keyCode, event->down, event->repeat,
			                           event->ctrl, event->shift, event->alt, event->meta };
		ok = freerdp_ohos_keyboard_state_handle_event(queue->keyboard, &key, packets,
		                                              OHOS_INPUT_QUEUE_KEY_PACKETS,
		                                              &packetCount)
		     ? TRUE
		     : FALSE;
	}
	if (!ok)
		return FALSE;

	for (size_t index = 0; index < packetCount; ++index)
	{
		OHOS_INPUT_EVENT packet = { 0 };
		packet.type = OHOS_INPUT_PLATFORM_KEY_PACKET;
		packet.keyCode = packets[index].keyCode;
		packet.vk = packets[index].windowsVk;
		packet.scancode = packets[index].rdpScancode;
		packet.down = packets[index].down ? TRUE : FALSE;
		packet.repeat = packets[index].repeat ? TRUE : FALSE;
		packet.extended = packets[index].extended ? TRUE : FALSE;
		packet.synthetic = packets[index].synthetic ? TRUE : FALSE;
		if (!ohos_input_append_pending(pending, count, capacity, &packet))
			return FALSE;
	}
	return TRUE;
}

static BOOL ohos_input_append_due_repeats(freerdpOhosInputQueue* queue,
                                          OHOS_INPUT_EVENT** pending, size_t* count,
                                          size_t* capacity)
{
	OHOS_INPUT_EVENT repeat = { 0 };
	repeat.type = OHOS_INPUT_PLATFORM_KEY;
	FREERDP_OHOS_KEY_PACKET packets[OHOS_INPUT_QUEUE_KEY_PACKETS] = { 0 };
	size_t packetCount = 0;
	if (!freerdp_ohos_keyboard_state_collect_due_repeats(
	        queue->keyboard, packets, OHOS_INPUT_QUEUE_KEY_PACKETS, &packetCount))
		return FALSE;
	for (size_t index = 0; index < packetCount; ++index)
	{
		repeat.type = OHOS_INPUT_PLATFORM_KEY_PACKET;
		repeat.keyCode = packets[index].keyCode;
		repeat.vk = packets[index].windowsVk;
		repeat.scancode = packets[index].rdpScancode;
		repeat.down = packets[index].down ? TRUE : FALSE;
		repeat.repeat = packets[index].repeat ? TRUE : FALSE;
		repeat.extended = packets[index].extended ? TRUE : FALSE;
		repeat.synthetic = packets[index].synthetic ? TRUE : FALSE;
		if (!ohos_input_append_pending(pending, count, capacity, &repeat))
			return FALSE;
	}
	return TRUE;
}

BOOL freerdp_ohos_input_queue_drain(freerdpOhosInputQueue* queue, rdpContext* context,
                                    char* message, size_t messageSize)
{
	if (!queue || !context || !context->input)
	{
		ohos_input_format(message, messageSize, "FreeRDP input context is not ready");
		return FALSE;
	}

	OHOS_INPUT_EVENT* pending = NULL;
	size_t pendingCount = 0;
	size_t pendingCapacity = 0;
	pthread_mutex_lock(&queue->mutex);
	for (size_t index = 0; index < queue->count; ++index)
	{
		const OHOS_INPUT_EVENT* event = &queue->events[index];
		const BOOL appended =
		    event->type == OHOS_INPUT_PLATFORM_KEY
		        ? ohos_input_append_key_packets(queue, event, &pending, &pendingCount,
		                                        &pendingCapacity)
		        : ohos_input_append_pending(&pending, &pendingCount, &pendingCapacity, event);
		if (!appended)
			++queue->stats.dropped;
	}
	(void)ohos_input_append_due_repeats(queue, &pending, &pendingCount, &pendingCapacity);
	queue->count = 0;
	queue->stats.depth = 0;
	pthread_mutex_unlock(&queue->mutex);

	if (pendingCount == 0)
	{
		free(pending);
		ohos_input_format(message, messageSize, "");
		return TRUE;
	}

	uint32_t sent = 0;
	for (size_t index = 0; index < pendingCount; ++index)
	{
		const OHOS_INPUT_EVENT* event = &pending[index];
		BOOL ok = FALSE;
		if (event->type == OHOS_INPUT_POINTER)
			ok = freerdp_input_send_mouse_event(context->input, event->flags, event->x, event->y);
		else if (event->type == OHOS_INPUT_KEY_SCANCODE)
			ok = freerdp_input_send_keyboard_event_ex(context->input, event->down, event->repeat,
			                                          event->scancode);
		else if (event->type == OHOS_INPUT_PLATFORM_KEY_PACKET && event->scancode != 0)
			ok = freerdp_input_send_keyboard_event_ex(context->input, event->down, event->repeat,
			                                          event->scancode);
		else if (event->type == OHOS_INPUT_FOCUS_IN)
			ok = freerdp_input_send_focus_in_event(context->input, event->flags);
		else if (event->type == OHOS_INPUT_UNICODE)
			ok = freerdp_input_send_unicode_keyboard_event(
			    context->input, event->down ? 0 : KBD_FLAGS_RELEASE, (UINT16)event->code);

		if (ok)
			++sent;
	}
	free(pending);

	pthread_mutex_lock(&queue->mutex);
	queue->stats.sent += sent;
	queue->stats.dropped += (uint32_t)(pendingCount - sent);
	ohos_input_set_diagnostics(
	    queue,
	    "FreeRDP input dispatched on worker thread: %" PRIu32
	    " event(s), total=%" PRIu32 " dropped=%" PRIu32 " coalesced=%" PRIu32,
	    sent, queue->stats.sent, queue->stats.dropped, queue->stats.coalesced);
	ohos_input_format(message, messageSize, "%s", queue->diagnostics);
	pthread_mutex_unlock(&queue->mutex);
	return sent == pendingCount ? TRUE : FALSE;
}

BOOL freerdp_ohos_input_queue_get_diagnostics(
    freerdpOhosInputQueue* queue, FREERDP_OHOS_INPUT_QUEUE_DIAGNOSTICS* diagnostics)
{
	if (!queue || !diagnostics)
		return FALSE;
	pthread_mutex_lock(&queue->mutex);
	*diagnostics = queue->stats;
	pthread_mutex_unlock(&queue->mutex);
	return TRUE;
}

const char* freerdp_ohos_input_queue_get_diagnostics_text(freerdpOhosInputQueue* queue)
{
	if (!queue)
		return "OHOS input queue diagnostics unavailable: queue is null";
	return queue->diagnostics;
}
