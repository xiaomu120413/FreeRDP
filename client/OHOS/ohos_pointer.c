/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS pointer and viewport mapping helper
 */

#include "ohos_pointer.h"

#include <inttypes.h>
#include <stdio.h>

#include <freerdp/input.h>

#define OHOS_RDP_WHEEL_DELTA 0x0078U
#define OHOS_RDP_WHEEL_DELTA_NEGATIVE 0x0088U

static void ohos_pointer_format_message(char* message, size_t size, const char* text)
{
	if (!message || size == 0)
		return;
	(void)snprintf(message, size, "%s", text ? text : "");
}

static UINT32 ohos_pointer_clamp_u32(UINT32 value, UINT32 minValue, UINT32 maxValue)
{
	if (value < minValue)
		return minValue;
	if (value > maxValue)
		return maxValue;
	return value;
}

static UINT16 ohos_pointer_clamp_u16(UINT32 value)
{
	return (UINT16)ohos_pointer_clamp_u32(value, 0, 0xFFFFU);
}

static UINT32 ohos_pointer_viewport_width(const FREERDP_OHOS_POINTER_VIEWPORT* viewport)
{
	if (!viewport)
		return 0;
	if (viewport->viewportWidth > 0)
		return viewport->viewportWidth;
	return viewport->surfaceWidth;
}

static UINT32 ohos_pointer_viewport_height(const FREERDP_OHOS_POINTER_VIEWPORT* viewport)
{
	if (!viewport)
		return 0;
	if (viewport->viewportHeight > 0)
		return viewport->viewportHeight;
	return viewport->surfaceHeight;
}

static UINT32 ohos_pointer_desktop_width(const FREERDP_OHOS_POINTER_VIEWPORT* viewport)
{
	const UINT32 desktopWidth = viewport ? viewport->desktopWidth : 0;
	return desktopWidth > 0 ? desktopWidth : ohos_pointer_viewport_width(viewport);
}

static UINT32 ohos_pointer_desktop_height(const FREERDP_OHOS_POINTER_VIEWPORT* viewport)
{
	const UINT32 desktopHeight = viewport ? viewport->desktopHeight : 0;
	return desktopHeight > 0 ? desktopHeight : ohos_pointer_viewport_height(viewport);
}

static UINT16 ohos_pointer_button_flags(UINT32 buttons)
{
	UINT16 flags = 0;
	if ((buttons & FREERDP_OHOS_POINTER_BUTTON_LEFT) != 0)
		flags |= PTR_FLAGS_BUTTON1;
	if ((buttons & FREERDP_OHOS_POINTER_BUTTON_RIGHT) != 0)
		flags |= PTR_FLAGS_BUTTON2;
	if ((buttons & FREERDP_OHOS_POINTER_BUTTON_MIDDLE) != 0)
		flags |= PTR_FLAGS_BUTTON3;
	return flags;
}

const char* freerdp_ohos_pointer_action_name(UINT32 action)
{
	switch (action)
	{
		case FREERDP_OHOS_POINTER_ACTION_MOVE:
			return "move";
		case FREERDP_OHOS_POINTER_ACTION_BUTTON_DOWN:
			return "buttonDown";
		case FREERDP_OHOS_POINTER_ACTION_BUTTON_UP:
			return "buttonUp";
		case FREERDP_OHOS_POINTER_ACTION_WHEEL_VERTICAL:
			return "wheelVertical";
		case FREERDP_OHOS_POINTER_ACTION_WHEEL_HORIZONTAL:
			return "wheelHorizontal";
		default:
			return "unknown";
	}
}

BOOL freerdp_ohos_pointer_is_in_viewport(const FREERDP_OHOS_POINTER_VIEWPORT* viewport,
                                         UINT32 x, UINT32 y)
{
	if (!viewport)
		return FALSE;

	const UINT32 viewportWidth = ohos_pointer_viewport_width(viewport);
	const UINT32 viewportHeight = ohos_pointer_viewport_height(viewport);
	if (viewportWidth == 0 || viewportHeight == 0)
		return FALSE;

	return x >= viewport->viewportX && y >= viewport->viewportY &&
	       x < viewport->viewportX + viewportWidth && y < viewport->viewportY + viewportHeight;
}

static BOOL ohos_pointer_map_to_desktop(const FREERDP_OHOS_POINTER_VIEWPORT* viewport,
                                        const FREERDP_OHOS_POINTER_EVENT* event,
                                        FREERDP_OHOS_POINTER_PACKET* packet, char* message,
                                        size_t messageSize)
{
	if (!viewport || !event || !packet)
	{
		ohos_pointer_format_message(message, messageSize, "OHOS pointer input is invalid");
		return FALSE;
	}

	const UINT32 viewportWidth = ohos_pointer_viewport_width(viewport);
	const UINT32 viewportHeight = ohos_pointer_viewport_height(viewport);
	const UINT32 desktopWidth = ohos_pointer_desktop_width(viewport);
	const UINT32 desktopHeight = ohos_pointer_desktop_height(viewport);
	if (viewportWidth == 0 || viewportHeight == 0 || desktopWidth == 0 || desktopHeight == 0)
	{
		ohos_pointer_format_message(message, messageSize,
		                            "OHOS pointer viewport or desktop size is not ready");
		return FALSE;
	}

	const BOOL inside = freerdp_ohos_pointer_is_in_viewport(viewport, event->x, event->y);
	if (!inside && !event->allowClamp)
	{
		ohos_pointer_format_message(message, messageSize,
		                            "OHOS pointer point is outside rendered remote viewport");
		return FALSE;
	}

	const UINT32 maxLocalX = viewportWidth > 0 ? viewportWidth - 1U : 0;
	const UINT32 maxLocalY = viewportHeight > 0 ? viewportHeight - 1U : 0;
	const UINT32 localX = event->x > viewport->viewportX ? event->x - viewport->viewportX : 0;
	const UINT32 localY = event->y > viewport->viewportY ? event->y - viewport->viewportY : 0;
	const UINT32 clampedX = ohos_pointer_clamp_u32(localX, 0, maxLocalX);
	const UINT32 clampedY = ohos_pointer_clamp_u32(localY, 0, maxLocalY);

	const UINT32 remoteX =
	    ohos_pointer_clamp_u32((UINT32)(((UINT64)clampedX * desktopWidth) / viewportWidth), 0,
	                           desktopWidth - 1U);
	const UINT32 remoteY =
	    ohos_pointer_clamp_u32((UINT32)(((UINT64)clampedY * desktopHeight) / viewportHeight), 0,
	                           desktopHeight - 1U);

	packet->surfaceX = event->x;
	packet->surfaceY = event->y;
	packet->remoteX = remoteX;
	packet->remoteY = remoteY;
	packet->x = ohos_pointer_clamp_u16(remoteX);
	packet->y = ohos_pointer_clamp_u16(remoteY);
	return TRUE;
}

BOOL freerdp_ohos_pointer_build_event(const FREERDP_OHOS_POINTER_VIEWPORT* viewport,
                                      const FREERDP_OHOS_POINTER_EVENT* event,
                                      FREERDP_OHOS_POINTER_PACKET* packet, char* message,
                                      size_t messageSize)
{
	if (!packet)
		return FALSE;
	*packet = (FREERDP_OHOS_POINTER_PACKET){ 0 };

	if (!ohos_pointer_map_to_desktop(viewport, event, packet, message, messageSize))
		return FALSE;

	switch (event->action)
	{
		case FREERDP_OHOS_POINTER_ACTION_MOVE:
			packet->flags = PTR_FLAGS_MOVE;
			if (event->buttons != FREERDP_OHOS_POINTER_BUTTON_NONE)
				packet->flags |= ohos_pointer_button_flags(event->buttons) | PTR_FLAGS_DOWN;
			break;
		case FREERDP_OHOS_POINTER_ACTION_BUTTON_DOWN:
			packet->flags = ohos_pointer_button_flags(event->buttons) | PTR_FLAGS_DOWN;
			break;
		case FREERDP_OHOS_POINTER_ACTION_BUTTON_UP:
			packet->flags = ohos_pointer_button_flags(event->buttons);
			break;
		case FREERDP_OHOS_POINTER_ACTION_WHEEL_VERTICAL:
			packet->flags = PTR_FLAGS_WHEEL |
			                ((event->delta > 0) ? (PTR_FLAGS_WHEEL_NEGATIVE |
			                                       OHOS_RDP_WHEEL_DELTA_NEGATIVE)
			                                    : OHOS_RDP_WHEEL_DELTA);
			break;
		case FREERDP_OHOS_POINTER_ACTION_WHEEL_HORIZONTAL:
			packet->flags = PTR_FLAGS_HWHEEL |
			                ((event->delta > 0) ? (PTR_FLAGS_WHEEL_NEGATIVE |
			                                       OHOS_RDP_WHEEL_DELTA_NEGATIVE)
			                                    : OHOS_RDP_WHEEL_DELTA);
			break;
		default:
			ohos_pointer_format_message(message, messageSize, "OHOS pointer action is unsupported");
			return FALSE;
	}

	if ((event->action == FREERDP_OHOS_POINTER_ACTION_BUTTON_DOWN ||
	     event->action == FREERDP_OHOS_POINTER_ACTION_BUTTON_UP) &&
	    ohos_pointer_button_flags(event->buttons) == 0)
	{
		ohos_pointer_format_message(message, messageSize, "OHOS pointer button is invalid");
		return FALSE;
	}

	packet->ok = TRUE;
	if (message && messageSize > 0)
	{
		(void)snprintf(message, messageSize,
		               "OHOS pointer %s mapped surface=%" PRIu32 ",%" PRIu32
		               " remote=%" PRIu32 ",%" PRIu32 " flags=0x%04" PRIX16,
		               freerdp_ohos_pointer_action_name(event->action), packet->surfaceX,
		               packet->surfaceY, packet->remoteX, packet->remoteY, packet->flags);
	}
	return TRUE;
}
