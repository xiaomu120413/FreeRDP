#include "ohos_keyboard.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define OH_KEYCODE_0 2000u
#define OH_KEYCODE_9 2009u
#define OH_KEYCODE_DPAD_UP 2012u
#define OH_KEYCODE_DPAD_DOWN 2013u
#define OH_KEYCODE_DPAD_LEFT 2014u
#define OH_KEYCODE_DPAD_RIGHT 2015u
#define OH_KEYCODE_A 2017u
#define OH_KEYCODE_Z 2042u
#define OH_KEYCODE_COMMA 2043u
#define OH_KEYCODE_PERIOD 2044u
#define OH_KEYCODE_ALT_LEFT 2045u
#define OH_KEYCODE_ALT_RIGHT 2046u
#define OH_KEYCODE_SHIFT_LEFT 2047u
#define OH_KEYCODE_SHIFT_RIGHT 2048u
#define OH_KEYCODE_TAB 2049u
#define OH_KEYCODE_SPACE 2050u
#define OH_KEYCODE_ENTER 2054u
#define OH_KEYCODE_DEL 2055u
#define OH_KEYCODE_GRAVE 2056u
#define OH_KEYCODE_MINUS 2057u
#define OH_KEYCODE_EQUALS 2058u
#define OH_KEYCODE_LEFT_BRACKET 2059u
#define OH_KEYCODE_RIGHT_BRACKET 2060u
#define OH_KEYCODE_BACKSLASH 2061u
#define OH_KEYCODE_SEMICOLON 2062u
#define OH_KEYCODE_APOSTROPHE 2063u
#define OH_KEYCODE_SLASH 2064u
#define OH_KEYCODE_PAGE_UP 2068u
#define OH_KEYCODE_PAGE_DOWN 2069u
#define OH_KEYCODE_ESCAPE 2070u
#define OH_KEYCODE_FORWARD_DEL 2071u
#define OH_KEYCODE_CTRL_LEFT 2072u
#define OH_KEYCODE_CTRL_RIGHT 2073u
#define OH_KEYCODE_META_LEFT 2076u
#define OH_KEYCODE_META_RIGHT 2077u
#define OH_KEYCODE_MOVE_HOME 2081u
#define OH_KEYCODE_MOVE_END 2082u
#define OH_KEYCODE_INSERT 2083u
#define OH_KEYCODE_F1 2090u
#define OH_KEYCODE_F12 2101u
#define OH_KEYCODE_NUMPAD_0 2103u
#define OH_KEYCODE_NUMPAD_9 2112u
#define OH_KEYCODE_NUMPAD_DIVIDE 2113u
#define OH_KEYCODE_NUMPAD_MULTIPLY 2114u
#define OH_KEYCODE_NUMPAD_SUBTRACT 2115u
#define OH_KEYCODE_NUMPAD_ADD 2116u
#define OH_KEYCODE_NUMPAD_DOT 2117u
#define OH_KEYCODE_NUMPAD_ENTER 2119u

#define VK_BACK 0x08u
#define VK_TAB 0x09u
#define VK_RETURN 0x0Du
#define VK_ESCAPE 0x1Bu
#define VK_SPACE 0x20u
#define VK_PRIOR 0x21u
#define VK_NEXT 0x22u
#define VK_END 0x23u
#define VK_HOME 0x24u
#define VK_LEFT 0x25u
#define VK_UP 0x26u
#define VK_RIGHT 0x27u
#define VK_DOWN 0x28u
#define VK_INSERT 0x2Du
#define VK_DELETE 0x2Eu
#define VK_KEY_0 0x30u
#define VK_KEY_A 0x41u
#define VK_LWIN 0x5Bu
#define VK_RWIN 0x5Cu
#define VK_NUMPAD0 0x60u
#define VK_MULTIPLY 0x6Au
#define VK_ADD 0x6Bu
#define VK_SUBTRACT 0x6Du
#define VK_DECIMAL 0x6Eu
#define VK_DIVIDE 0x6Fu
#define VK_F1 0x70u
#define VK_LSHIFT 0xA0u
#define VK_RSHIFT 0xA1u
#define VK_LCONTROL 0xA2u
#define VK_RCONTROL 0xA3u
#define VK_LMENU 0xA4u
#define VK_RMENU 0xA5u
#define VK_OEM_1 0xBAu
#define VK_OEM_PLUS 0xBBu
#define VK_OEM_COMMA 0xBCu
#define VK_OEM_MINUS 0xBDu
#define VK_OEM_PERIOD 0xBEu
#define VK_OEM_2 0xBFu
#define VK_OEM_3 0xC0u
#define VK_OEM_4 0xDBu
#define VK_OEM_5 0xDCu
#define VK_OEM_6 0xDDu
#define VK_OEM_7 0xDEu

#define OHOS_KEYBOARD_MAX_PRESSED 64u
#define OHOS_KEYBOARD_MAX_SYNTHETIC_MODIFIERS 32u
#define OHOS_KEYBOARD_REPEAT_INITIAL_DELAY_MS 450u
#define OHOS_KEYBOARD_REPEAT_INTERVAL_MS 55u

typedef struct
{
	uint32_t keyCode;
	uint32_t windowsVk;
	int extended;
	int physicalDown;
	uint32_t syntheticCount;
} FREERDP_OHOS_PRESSED_KEY;

typedef struct
{
	uint32_t ownerKeyCode;
	uint32_t modifierKeyCode;
	int active;
} FREERDP_OHOS_SYNTHETIC_MODIFIER;

struct freerdp_ohos_keyboard_state
{
	FREERDP_OHOS_PRESSED_KEY pressed[OHOS_KEYBOARD_MAX_PRESSED];
	size_t pressedCount;
	FREERDP_OHOS_SYNTHETIC_MODIFIER syntheticModifiers[OHOS_KEYBOARD_MAX_SYNTHETIC_MODIFIERS];
	size_t syntheticModifierCount;
	uint32_t repeatKeyCode;
	uint32_t repeatWindowsVk;
	int repeatExtended;
	int repeatActive;
	uint64_t nextRepeatMs;
};

static const char* freerdp_ohos_keyboard_bool_text(int value)
{
	return value ? "true" : "false";
}

static uint64_t freerdp_ohos_keyboard_now_ms(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0;

	return ((uint64_t)now.tv_sec * 1000ull) + ((uint64_t)now.tv_nsec / 1000000ull);
}

static int freerdp_ohos_keyboard_is_modifier_keycode(uint32_t keyCode)
{
	switch (keyCode)
	{
		case OH_KEYCODE_ALT_LEFT:
		case OH_KEYCODE_ALT_RIGHT:
		case OH_KEYCODE_SHIFT_LEFT:
		case OH_KEYCODE_SHIFT_RIGHT:
		case OH_KEYCODE_CTRL_LEFT:
		case OH_KEYCODE_CTRL_RIGHT:
		case OH_KEYCODE_META_LEFT:
		case OH_KEYCODE_META_RIGHT:
			return 1;
		default:
			return 0;
	}
}

static int freerdp_ohos_keyboard_is_key_down(const FREERDP_OHOS_PRESSED_KEY* key)
{
	return key && (key->physicalDown || (key->syntheticCount > 0));
}

static FREERDP_OHOS_PRESSED_KEY*
freerdp_ohos_keyboard_find_pressed(FREERDP_OHOS_KEYBOARD_STATE* state, uint32_t keyCode)
{
	size_t x;

	if (!state)
		return NULL;

	for (x = 0; x < state->pressedCount; x++)
	{
		if (state->pressed[x].keyCode == keyCode)
			return &state->pressed[x];
	}
	return NULL;
}

static FREERDP_OHOS_PRESSED_KEY*
freerdp_ohos_keyboard_add_pressed(FREERDP_OHOS_KEYBOARD_STATE* state, uint32_t keyCode,
                                  uint32_t windowsVk, int extended)
{
	FREERDP_OHOS_PRESSED_KEY* key = NULL;

	if (!state || (state->pressedCount >= OHOS_KEYBOARD_MAX_PRESSED))
		return NULL;

	key = &state->pressed[state->pressedCount++];
	memset(key, 0, sizeof(*key));
	key->keyCode = keyCode;
	key->windowsVk = windowsVk;
	key->extended = extended;
	return key;
}

static void freerdp_ohos_keyboard_remove_pressed_if_idle(FREERDP_OHOS_KEYBOARD_STATE* state,
                                                        FREERDP_OHOS_PRESSED_KEY* key)
{
	size_t index;

	if (!state || !key || freerdp_ohos_keyboard_is_key_down(key))
		return;

	index = (size_t)(key - state->pressed);
	if (index >= state->pressedCount)
		return;

	if (index + 1 < state->pressedCount)
		memmove(&state->pressed[index], &state->pressed[index + 1],
		        (state->pressedCount - index - 1) * sizeof(state->pressed[0]));
	state->pressedCount--;
}

static int freerdp_ohos_keyboard_push_packet(FREERDP_OHOS_KEY_PACKET* packets, size_t capacity,
                                             size_t* count, uint32_t keyCode, uint32_t windowsVk,
                                             int down, int repeat, int extended, int synthetic)
{
	if (!packets || !count || (*count >= capacity))
		return 0;

	packets[*count].keyCode = keyCode;
	packets[*count].windowsVk = windowsVk;
	packets[*count].down = down;
	packets[*count].repeat = repeat;
	packets[*count].extended = extended;
	packets[*count].synthetic = synthetic;
	(*count)++;
	return 1;
}

static int freerdp_ohos_keyboard_any_key_down(FREERDP_OHOS_KEYBOARD_STATE* state, uint32_t left,
                                              uint32_t right)
{
	FREERDP_OHOS_PRESSED_KEY* key = freerdp_ohos_keyboard_find_pressed(state, left);

	if (freerdp_ohos_keyboard_is_key_down(key))
		return 1;

	key = freerdp_ohos_keyboard_find_pressed(state, right);
	return freerdp_ohos_keyboard_is_key_down(key);
}

static void freerdp_ohos_keyboard_start_repeat(FREERDP_OHOS_KEYBOARD_STATE* state,
                                               uint32_t keyCode, uint32_t windowsVk, int extended,
                                               uint64_t nowMs, int immediate)
{
	if (!state || freerdp_ohos_keyboard_is_modifier_keycode(keyCode))
		return;

	state->repeatKeyCode = keyCode;
	state->repeatWindowsVk = windowsVk;
	state->repeatExtended = extended;
	state->repeatActive = 1;
	state->nextRepeatMs = nowMs + (immediate ? OHOS_KEYBOARD_REPEAT_INTERVAL_MS
	                                        : OHOS_KEYBOARD_REPEAT_INITIAL_DELAY_MS);
}

static void freerdp_ohos_keyboard_stop_repeat(FREERDP_OHOS_KEYBOARD_STATE* state,
                                              uint32_t keyCode)
{
	if (state && state->repeatActive && (state->repeatKeyCode == keyCode))
	{
		state->repeatActive = 0;
		state->repeatKeyCode = 0;
		state->repeatWindowsVk = 0;
		state->repeatExtended = 0;
		state->nextRepeatMs = 0;
	}
}

static int freerdp_ohos_keyboard_press_physical(FREERDP_OHOS_KEYBOARD_STATE* state,
                                                const FREERDP_OHOS_KEY_RESOLVED* resolved,
                                                FREERDP_OHOS_KEY_PACKET* packets,
                                                size_t capacity, size_t* count, uint64_t nowMs)
{
	FREERDP_OHOS_PRESSED_KEY* key = NULL;
	const int modifier = freerdp_ohos_keyboard_is_modifier_keycode(resolved->keyCode);
	int wasDown = 0;
	int repeat = 0;

	if (!state || !resolved)
		return 0;

	key = freerdp_ohos_keyboard_find_pressed(state, resolved->keyCode);
	if (!key)
		key = freerdp_ohos_keyboard_add_pressed(state, resolved->keyCode, resolved->windowsVk,
		                                        resolved->extended);
	if (!key)
		return 0;

	wasDown = freerdp_ohos_keyboard_is_key_down(key);
	repeat = !modifier && (resolved->repeat || key->physicalDown || wasDown);
	key->physicalDown = 1;
	key->windowsVk = resolved->windowsVk;
	key->extended = resolved->extended;

	if (!wasDown || repeat)
	{
		if (!freerdp_ohos_keyboard_push_packet(packets, capacity, count, resolved->keyCode,
		                                       resolved->windowsVk, 1, repeat, resolved->extended,
		                                       0))
			return 0;
	}

	if (!modifier)
		freerdp_ohos_keyboard_start_repeat(state, resolved->keyCode, resolved->windowsVk,
		                                   resolved->extended, nowMs, repeat);
	return 1;
}

static int freerdp_ohos_keyboard_release_physical(FREERDP_OHOS_KEYBOARD_STATE* state,
                                                  const FREERDP_OHOS_KEY_RESOLVED* resolved,
                                                  FREERDP_OHOS_KEY_PACKET* packets,
                                                  size_t capacity, size_t* count)
{
	FREERDP_OHOS_PRESSED_KEY* key = NULL;
	int wasDown = 0;
	int sendRelease = 1;

	if (!state || !resolved)
		return 0;

	freerdp_ohos_keyboard_stop_repeat(state, resolved->keyCode);
	key = freerdp_ohos_keyboard_find_pressed(state, resolved->keyCode);
	if (key)
	{
		wasDown = freerdp_ohos_keyboard_is_key_down(key);
		key->physicalDown = 0;
		sendRelease = wasDown && (key->syntheticCount == 0);
	}

	if (sendRelease)
	{
		if (!freerdp_ohos_keyboard_push_packet(packets, capacity, count, resolved->keyCode,
		                                       resolved->windowsVk, 0, 0, resolved->extended, 0))
			return 0;
	}

	if (key)
		freerdp_ohos_keyboard_remove_pressed_if_idle(state, key);
	return 1;
}

static int freerdp_ohos_keyboard_remember_synthetic_modifier(
    FREERDP_OHOS_KEYBOARD_STATE* state, uint32_t ownerKeyCode, uint32_t modifierKeyCode)
{
	size_t x;
	FREERDP_OHOS_SYNTHETIC_MODIFIER* item = NULL;

	if (!state)
		return 0;

	for (x = 0; x < state->syntheticModifierCount; x++)
	{
		item = &state->syntheticModifiers[x];
		if (item->active && (item->ownerKeyCode == ownerKeyCode) &&
		    (item->modifierKeyCode == modifierKeyCode))
			return 2;
	}

	if (state->syntheticModifierCount >= OHOS_KEYBOARD_MAX_SYNTHETIC_MODIFIERS)
		return 0;

	item = &state->syntheticModifiers[state->syntheticModifierCount++];
	item->ownerKeyCode = ownerKeyCode;
	item->modifierKeyCode = modifierKeyCode;
	item->active = 1;
	return 1;
}

static int freerdp_ohos_keyboard_press_synthetic_modifier(
    FREERDP_OHOS_KEYBOARD_STATE* state, uint32_t ownerKeyCode, uint32_t modifierKeyCode,
    FREERDP_OHOS_KEY_PACKET* packets, size_t capacity, size_t* count)
{
	FREERDP_OHOS_PRESSED_KEY* key = NULL;
	uint32_t windowsVk = freerdp_ohos_keyboard_map_keycode_to_windows_vk(modifierKeyCode);
	int extended = freerdp_ohos_keyboard_keycode_requires_extended_scancode(modifierKeyCode);
	int wasDown = 0;

	if (!state || !windowsVk)
		return 0;

	const int remember = freerdp_ohos_keyboard_remember_synthetic_modifier(
	    state, ownerKeyCode, modifierKeyCode);
	if (remember == 0)
		return 0;
	if (remember == 2)
		return 1;

	key = freerdp_ohos_keyboard_find_pressed(state, modifierKeyCode);
	if (!key)
		key = freerdp_ohos_keyboard_add_pressed(state, modifierKeyCode, windowsVk, extended);
	if (!key)
		return 0;

	wasDown = freerdp_ohos_keyboard_is_key_down(key);
	key->syntheticCount++;
	key->windowsVk = windowsVk;
	key->extended = extended;

	if (!wasDown)
		return freerdp_ohos_keyboard_push_packet(packets, capacity, count, modifierKeyCode,
		                                         windowsVk, 1, 0, extended, 1);
	return 1;
}

static int freerdp_ohos_keyboard_release_synthetic_modifier(
    FREERDP_OHOS_KEYBOARD_STATE* state, uint32_t modifierKeyCode, FREERDP_OHOS_KEY_PACKET* packets,
    size_t capacity, size_t* count)
{
	FREERDP_OHOS_PRESSED_KEY* key = NULL;
	uint32_t windowsVk = freerdp_ohos_keyboard_map_keycode_to_windows_vk(modifierKeyCode);
	int extended = freerdp_ohos_keyboard_keycode_requires_extended_scancode(modifierKeyCode);

	if (!state || !windowsVk)
		return 0;

	key = freerdp_ohos_keyboard_find_pressed(state, modifierKeyCode);
	if (!key)
		return 1;

	if (key->syntheticCount > 0)
		key->syntheticCount--;

	if (!freerdp_ohos_keyboard_is_key_down(key))
	{
		if (!freerdp_ohos_keyboard_push_packet(packets, capacity, count, modifierKeyCode,
		                                       windowsVk, 0, 0, extended, 1))
			return 0;
	}

	freerdp_ohos_keyboard_remove_pressed_if_idle(state, key);
	return 1;
}

static int freerdp_ohos_keyboard_release_synthetic_modifiers_for_owner(
    FREERDP_OHOS_KEYBOARD_STATE* state, uint32_t ownerKeyCode, FREERDP_OHOS_KEY_PACKET* packets,
    size_t capacity, size_t* count)
{
	size_t x = 0;

	if (!state)
		return 0;

	while (x < state->syntheticModifierCount)
	{
		FREERDP_OHOS_SYNTHETIC_MODIFIER item = state->syntheticModifiers[x];
		if (item.active && (item.ownerKeyCode == ownerKeyCode))
		{
			if (x + 1 < state->syntheticModifierCount)
				memmove(&state->syntheticModifiers[x], &state->syntheticModifiers[x + 1],
				        (state->syntheticModifierCount - x - 1) *
				            sizeof(state->syntheticModifiers[0]));
			state->syntheticModifierCount--;
			if (!freerdp_ohos_keyboard_release_synthetic_modifier(
			        state, item.modifierKeyCode, packets, capacity, count))
				return 0;
			continue;
		}
		x++;
	}

	return 1;
}

static int freerdp_ohos_keyboard_ensure_event_modifiers(
    FREERDP_OHOS_KEYBOARD_STATE* state, const FREERDP_OHOS_KEY_RESOLVED* resolved,
    FREERDP_OHOS_KEY_PACKET* packets, size_t capacity, size_t* count)
{
	if (!state || !resolved || freerdp_ohos_keyboard_is_modifier_keycode(resolved->keyCode))
		return 1;

	if (resolved->ctrl &&
	    !freerdp_ohos_keyboard_any_key_down(state, OH_KEYCODE_CTRL_LEFT, OH_KEYCODE_CTRL_RIGHT))
	{
		if (!freerdp_ohos_keyboard_press_synthetic_modifier(
		        state, resolved->keyCode, OH_KEYCODE_CTRL_LEFT, packets, capacity, count))
			return 0;
	}
	if (resolved->shift &&
	    !freerdp_ohos_keyboard_any_key_down(state, OH_KEYCODE_SHIFT_LEFT, OH_KEYCODE_SHIFT_RIGHT))
	{
		if (!freerdp_ohos_keyboard_press_synthetic_modifier(
		        state, resolved->keyCode, OH_KEYCODE_SHIFT_LEFT, packets, capacity, count))
			return 0;
	}
	if (resolved->alt &&
	    !freerdp_ohos_keyboard_any_key_down(state, OH_KEYCODE_ALT_LEFT, OH_KEYCODE_ALT_RIGHT))
	{
		if (!freerdp_ohos_keyboard_press_synthetic_modifier(
		        state, resolved->keyCode, OH_KEYCODE_ALT_LEFT, packets, capacity, count))
			return 0;
	}
	if (resolved->meta &&
	    !freerdp_ohos_keyboard_any_key_down(state, OH_KEYCODE_META_LEFT, OH_KEYCODE_META_RIGHT))
	{
		if (!freerdp_ohos_keyboard_press_synthetic_modifier(
		        state, resolved->keyCode, OH_KEYCODE_META_LEFT, packets, capacity, count))
			return 0;
	}
	return 1;
}

uint32_t freerdp_ohos_keyboard_map_keycode_to_windows_vk(uint32_t keyCode)
{
	if ((keyCode >= OH_KEYCODE_A) && (keyCode <= OH_KEYCODE_Z))
		return VK_KEY_A + keyCode - OH_KEYCODE_A;
	if ((keyCode >= OH_KEYCODE_0) && (keyCode <= OH_KEYCODE_9))
		return VK_KEY_0 + keyCode - OH_KEYCODE_0;
	if ((keyCode >= OH_KEYCODE_F1) && (keyCode <= OH_KEYCODE_F12))
		return VK_F1 + keyCode - OH_KEYCODE_F1;
	if ((keyCode >= OH_KEYCODE_NUMPAD_0) && (keyCode <= OH_KEYCODE_NUMPAD_9))
		return VK_NUMPAD0 + keyCode - OH_KEYCODE_NUMPAD_0;

	switch (keyCode)
	{
		case OH_KEYCODE_DPAD_UP:
			return VK_UP;
		case OH_KEYCODE_DPAD_DOWN:
			return VK_DOWN;
		case OH_KEYCODE_DPAD_LEFT:
			return VK_LEFT;
		case OH_KEYCODE_DPAD_RIGHT:
			return VK_RIGHT;
		case OH_KEYCODE_COMMA:
			return VK_OEM_COMMA;
		case OH_KEYCODE_PERIOD:
			return VK_OEM_PERIOD;
		case OH_KEYCODE_ALT_LEFT:
			return VK_LMENU;
		case OH_KEYCODE_ALT_RIGHT:
			return VK_RMENU;
		case OH_KEYCODE_SHIFT_LEFT:
			return VK_LSHIFT;
		case OH_KEYCODE_SHIFT_RIGHT:
			return VK_RSHIFT;
		case OH_KEYCODE_TAB:
			return VK_TAB;
		case OH_KEYCODE_SPACE:
			return VK_SPACE;
		case OH_KEYCODE_ENTER:
		case OH_KEYCODE_NUMPAD_ENTER:
			return VK_RETURN;
		case OH_KEYCODE_DEL:
			return VK_BACK;
		case OH_KEYCODE_GRAVE:
			return VK_OEM_3;
		case OH_KEYCODE_MINUS:
			return VK_OEM_MINUS;
		case OH_KEYCODE_EQUALS:
			return VK_OEM_PLUS;
		case OH_KEYCODE_LEFT_BRACKET:
			return VK_OEM_4;
		case OH_KEYCODE_RIGHT_BRACKET:
			return VK_OEM_6;
		case OH_KEYCODE_BACKSLASH:
			return VK_OEM_5;
		case OH_KEYCODE_SEMICOLON:
			return VK_OEM_1;
		case OH_KEYCODE_APOSTROPHE:
			return VK_OEM_7;
		case OH_KEYCODE_SLASH:
			return VK_OEM_2;
		case OH_KEYCODE_PAGE_UP:
			return VK_PRIOR;
		case OH_KEYCODE_PAGE_DOWN:
			return VK_NEXT;
		case OH_KEYCODE_ESCAPE:
			return VK_ESCAPE;
		case OH_KEYCODE_FORWARD_DEL:
			return VK_DELETE;
		case OH_KEYCODE_CTRL_LEFT:
			return VK_LCONTROL;
		case OH_KEYCODE_CTRL_RIGHT:
			return VK_RCONTROL;
		case OH_KEYCODE_META_LEFT:
			return VK_LWIN;
		case OH_KEYCODE_META_RIGHT:
			return VK_RWIN;
		case OH_KEYCODE_MOVE_HOME:
			return VK_HOME;
		case OH_KEYCODE_MOVE_END:
			return VK_END;
		case OH_KEYCODE_INSERT:
			return VK_INSERT;
		case OH_KEYCODE_NUMPAD_DIVIDE:
			return VK_DIVIDE;
		case OH_KEYCODE_NUMPAD_MULTIPLY:
			return VK_MULTIPLY;
		case OH_KEYCODE_NUMPAD_SUBTRACT:
			return VK_SUBTRACT;
		case OH_KEYCODE_NUMPAD_ADD:
			return VK_ADD;
		case OH_KEYCODE_NUMPAD_DOT:
			return VK_DECIMAL;
		default:
			return 0;
	}
}

int freerdp_ohos_keyboard_keycode_requires_extended_scancode(uint32_t keyCode)
{
	switch (keyCode)
	{
		case OH_KEYCODE_DPAD_UP:
		case OH_KEYCODE_DPAD_DOWN:
		case OH_KEYCODE_DPAD_LEFT:
		case OH_KEYCODE_DPAD_RIGHT:
		case OH_KEYCODE_ALT_RIGHT:
		case OH_KEYCODE_PAGE_UP:
		case OH_KEYCODE_PAGE_DOWN:
		case OH_KEYCODE_FORWARD_DEL:
		case OH_KEYCODE_CTRL_RIGHT:
		case OH_KEYCODE_META_LEFT:
		case OH_KEYCODE_META_RIGHT:
		case OH_KEYCODE_MOVE_HOME:
		case OH_KEYCODE_MOVE_END:
		case OH_KEYCODE_INSERT:
		case OH_KEYCODE_NUMPAD_DIVIDE:
		case OH_KEYCODE_NUMPAD_ENTER:
			return 1;
		default:
			return 0;
	}
}

int freerdp_ohos_keyboard_resolve_event(const FREERDP_OHOS_KEY_EVENT* event,
                                        FREERDP_OHOS_KEY_RESOLVED* resolved)
{
	if (!event || !resolved)
		return 0;

	resolved->keyCode = event->keyCode;
	resolved->windowsVk = freerdp_ohos_keyboard_map_keycode_to_windows_vk(event->keyCode);
	resolved->mapped = resolved->windowsVk != 0;
	resolved->extended = freerdp_ohos_keyboard_keycode_requires_extended_scancode(event->keyCode);
	resolved->down = event->down;
	resolved->repeat = event->repeat;
	resolved->ctrl = event->ctrl;
	resolved->shift = event->shift;
	resolved->alt = event->alt;
	resolved->meta = event->meta;
	return resolved->mapped;
}

int freerdp_ohos_keyboard_format_event(const FREERDP_OHOS_KEY_EVENT* event, char* buffer,
                                       size_t size)
{
	FREERDP_OHOS_KEY_RESOLVED resolved;

	if (!event || !buffer || (size == 0))
		return 0;

	(void)freerdp_ohos_keyboard_resolve_event(event, &resolved);
	return snprintf(buffer, size,
	                "ohos.key keyCode=%u vk=0x%X mapped=%s extended=%s down=%s repeat=%s "
	                "ctrl=%s shift=%s alt=%s meta=%s",
	                event->keyCode, resolved.windowsVk,
	                freerdp_ohos_keyboard_bool_text(resolved.mapped),
	                freerdp_ohos_keyboard_bool_text(resolved.extended),
	                freerdp_ohos_keyboard_bool_text(event->down),
	                freerdp_ohos_keyboard_bool_text(event->repeat),
	                freerdp_ohos_keyboard_bool_text(event->ctrl),
	                freerdp_ohos_keyboard_bool_text(event->shift),
	                freerdp_ohos_keyboard_bool_text(event->alt),
	                freerdp_ohos_keyboard_bool_text(event->meta)) > 0;
}

FREERDP_OHOS_KEYBOARD_STATE* freerdp_ohos_keyboard_state_new(void)
{
	return (FREERDP_OHOS_KEYBOARD_STATE*)calloc(1, sizeof(FREERDP_OHOS_KEYBOARD_STATE));
}

void freerdp_ohos_keyboard_state_free(FREERDP_OHOS_KEYBOARD_STATE* state)
{
	free(state);
}

void freerdp_ohos_keyboard_state_reset(FREERDP_OHOS_KEYBOARD_STATE* state)
{
	if (state)
		memset(state, 0, sizeof(*state));
}

int freerdp_ohos_keyboard_state_handle_event(FREERDP_OHOS_KEYBOARD_STATE* state,
                                             const FREERDP_OHOS_KEY_EVENT* event,
                                             FREERDP_OHOS_KEY_PACKET* packets, size_t capacity,
                                             size_t* count)
{
	FREERDP_OHOS_KEY_RESOLVED resolved;
	uint64_t nowMs;

	if (count)
		*count = 0;
	if (!state || !event || !packets || !count)
		return 0;
	if (!freerdp_ohos_keyboard_resolve_event(event, &resolved))
		return 0;

	nowMs = freerdp_ohos_keyboard_now_ms();
	if (resolved.down)
	{
		if (!freerdp_ohos_keyboard_ensure_event_modifiers(state, &resolved, packets, capacity,
		                                                  count))
			return 0;
		return freerdp_ohos_keyboard_press_physical(state, &resolved, packets, capacity, count,
		                                            nowMs);
	}

	if (!freerdp_ohos_keyboard_release_physical(state, &resolved, packets, capacity, count))
		return 0;
	return freerdp_ohos_keyboard_release_synthetic_modifiers_for_owner(
	    state, resolved.keyCode, packets, capacity, count);
}

int freerdp_ohos_keyboard_state_collect_due_repeats(FREERDP_OHOS_KEYBOARD_STATE* state,
                                                    FREERDP_OHOS_KEY_PACKET* packets,
                                                    size_t capacity, size_t* count)
{
	uint64_t nowMs;
	FREERDP_OHOS_PRESSED_KEY* key = NULL;

	if (count)
		*count = 0;
	if (!state || !packets || !count || !state->repeatActive)
		return 1;

	key = freerdp_ohos_keyboard_find_pressed(state, state->repeatKeyCode);
	if (!key || !key->physicalDown)
	{
		freerdp_ohos_keyboard_stop_repeat(state, state->repeatKeyCode);
		return 1;
	}

	nowMs = freerdp_ohos_keyboard_now_ms();
	if ((nowMs == 0) || (nowMs < state->nextRepeatMs))
		return 1;

	if (!freerdp_ohos_keyboard_push_packet(packets, capacity, count, state->repeatKeyCode,
	                                       state->repeatWindowsVk, 1, 1,
	                                       state->repeatExtended, 0))
		return 0;

	state->nextRepeatMs = nowMs + OHOS_KEYBOARD_REPEAT_INTERVAL_MS;
	return 1;
}

int freerdp_ohos_keyboard_state_release_all(FREERDP_OHOS_KEYBOARD_STATE* state,
                                            FREERDP_OHOS_KEY_PACKET* packets, size_t capacity,
                                            size_t* count)
{
	if (count)
		*count = 0;
	if (!state || !packets || !count)
		return 0;

	while (state->pressedCount > 0)
	{
		FREERDP_OHOS_PRESSED_KEY key = state->pressed[state->pressedCount - 1];
		if (freerdp_ohos_keyboard_is_key_down(&key))
		{
			if (!freerdp_ohos_keyboard_push_packet(packets, capacity, count, key.keyCode,
			                                       key.windowsVk, 0, 0, key.extended, 1))
				return 0;
		}
		state->pressedCount--;
	}

	state->syntheticModifierCount = 0;
	state->repeatActive = 0;
	state->repeatKeyCode = 0;
	state->repeatWindowsVk = 0;
	state->repeatExtended = 0;
	state->nextRepeatMs = 0;
	return 1;
}
