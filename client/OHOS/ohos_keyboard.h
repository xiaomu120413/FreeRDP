#ifndef FREERDP_CLIENT_OHOS_KEYBOARD_H
#define FREERDP_CLIENT_OHOS_KEYBOARD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
	uint32_t keyCode;
	int down;
	int repeat;
	int ctrl;
	int shift;
	int alt;
	int meta;
} FREERDP_OHOS_KEY_EVENT;

typedef struct
{
	uint32_t keyCode;
	uint32_t windowsVk;
	int mapped;
	int extended;
	int down;
	int repeat;
	int ctrl;
	int shift;
	int alt;
	int meta;
} FREERDP_OHOS_KEY_RESOLVED;

typedef struct
{
	uint32_t keyCode;
	uint32_t windowsVk;
	int down;
	int repeat;
	int extended;
	int synthetic;
} FREERDP_OHOS_KEY_PACKET;

typedef struct freerdp_ohos_keyboard_state FREERDP_OHOS_KEYBOARD_STATE;

uint32_t freerdp_ohos_keyboard_map_keycode_to_windows_vk(uint32_t keyCode);
int freerdp_ohos_keyboard_keycode_requires_extended_scancode(uint32_t keyCode);
int freerdp_ohos_keyboard_resolve_event(const FREERDP_OHOS_KEY_EVENT* event,
                                        FREERDP_OHOS_KEY_RESOLVED* resolved);
int freerdp_ohos_keyboard_format_event(const FREERDP_OHOS_KEY_EVENT* event, char* buffer,
                                       size_t size);

FREERDP_OHOS_KEYBOARD_STATE* freerdp_ohos_keyboard_state_new(void);
void freerdp_ohos_keyboard_state_free(FREERDP_OHOS_KEYBOARD_STATE* state);
void freerdp_ohos_keyboard_state_reset(FREERDP_OHOS_KEYBOARD_STATE* state);
int freerdp_ohos_keyboard_state_handle_event(FREERDP_OHOS_KEYBOARD_STATE* state,
                                             const FREERDP_OHOS_KEY_EVENT* event,
                                             FREERDP_OHOS_KEY_PACKET* packets, size_t capacity,
                                             size_t* count);
int freerdp_ohos_keyboard_state_collect_due_repeats(FREERDP_OHOS_KEYBOARD_STATE* state,
                                                    FREERDP_OHOS_KEY_PACKET* packets,
                                                    size_t capacity, size_t* count);
int freerdp_ohos_keyboard_state_release_all(FREERDP_OHOS_KEYBOARD_STATE* state,
                                            FREERDP_OHOS_KEY_PACKET* packets, size_t capacity,
                                            size_t* count);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_KEYBOARD_H */
