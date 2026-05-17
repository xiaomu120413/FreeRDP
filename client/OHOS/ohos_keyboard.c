#include "ohos_keyboard.h"

#include <stdio.h>

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

static const char* freerdp_ohos_keyboard_bool_text(int value)
{
	return value ? "true" : "false";
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
