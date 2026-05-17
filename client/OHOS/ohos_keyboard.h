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

uint32_t freerdp_ohos_keyboard_map_keycode_to_windows_vk(uint32_t keyCode);
int freerdp_ohos_keyboard_keycode_requires_extended_scancode(uint32_t keyCode);
int freerdp_ohos_keyboard_resolve_event(const FREERDP_OHOS_KEY_EVENT* event,
                                        FREERDP_OHOS_KEY_RESOLVED* resolved);
int freerdp_ohos_keyboard_format_event(const FREERDP_OHOS_KEY_EVENT* event, char* buffer,
                                       size_t size);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_KEYBOARD_H */
