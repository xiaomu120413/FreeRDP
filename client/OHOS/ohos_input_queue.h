#ifndef FREERDP_CLIENT_OHOS_INPUT_QUEUE_H
#define FREERDP_CLIENT_OHOS_INPUT_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#include <freerdp/api.h>
#include <freerdp/freerdp.h>
#include <winpr/wtypes.h>

#include "ohos_keyboard.h"
#include "ohos_pointer.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct freerdp_ohos_input_queue freerdpOhosInputQueue;

typedef struct
{
	uint32_t depth;
	uint32_t queued;
	uint32_t sent;
	uint32_t dropped;
	uint32_t coalesced;
} FREERDP_OHOS_INPUT_QUEUE_DIAGNOSTICS;

FREERDP_API freerdpOhosInputQueue* freerdp_ohos_input_queue_new(void);
FREERDP_API void freerdp_ohos_input_queue_free(freerdpOhosInputQueue* queue);
FREERDP_API void freerdp_ohos_input_queue_clear(freerdpOhosInputQueue* queue);
FREERDP_API void freerdp_ohos_input_queue_reset(freerdpOhosInputQueue* queue);
FREERDP_API BOOL freerdp_ohos_input_queue_enqueue_pointer(
    freerdpOhosInputQueue* queue, const FREERDP_OHOS_POINTER_VIEWPORT* viewport,
    const FREERDP_OHOS_POINTER_EVENT* event, char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_input_queue_enqueue_pointer_packet(
    freerdpOhosInputQueue* queue, UINT16 flags, UINT16 x, UINT16 y, char* message,
    size_t messageSize);
FREERDP_API BOOL freerdp_ohos_input_queue_enqueue_key_scancode(
    freerdpOhosInputQueue* queue, UINT32 rdpScancode, BOOL down, BOOL repeat, char* message,
    size_t messageSize);
FREERDP_API BOOL freerdp_ohos_input_queue_enqueue_key(
    freerdpOhosInputQueue* queue, const FREERDP_OHOS_KEY_EVENT* event, char* message,
    size_t messageSize);
FREERDP_API BOOL freerdp_ohos_input_queue_enqueue_unicode(
    freerdpOhosInputQueue* queue, UINT32 codeUnit, BOOL down, char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_input_queue_enqueue_text(
    freerdpOhosInputQueue* queue, const uint16_t* text, size_t length, char* message,
    size_t messageSize);
FREERDP_API BOOL freerdp_ohos_input_queue_enqueue_focus_in(
    freerdpOhosInputQueue* queue, UINT16 toggleStates, char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_input_queue_enqueue_release_all_keys(
    freerdpOhosInputQueue* queue, char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_input_queue_drain(
    freerdpOhosInputQueue* queue, rdpContext* context, char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_input_queue_get_diagnostics(
    freerdpOhosInputQueue* queue, FREERDP_OHOS_INPUT_QUEUE_DIAGNOSTICS* diagnostics);
FREERDP_API const char* freerdp_ohos_input_queue_get_diagnostics_text(
    freerdpOhosInputQueue* queue);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_INPUT_QUEUE_H */
