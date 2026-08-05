#include "ohos_pen.h"

#include <math.h>
#include <stdio.h>

#include <freerdp/channels/rdpei.h>
#include <freerdp/channels/channels.h>
#include <winpr/error.h>

static void ohos_pen_message(char* message, size_t size, const char* text)
{
	if (message && size > 0)
		(void)snprintf(message, size, "%s", text ? text : "");
}

BOOL freerdp_ohos_pen_validate(const FREERDP_OHOS_PEN_EVENT* event, char* message,
                               size_t messageSize)
{
	if (!event || event->structSize < sizeof(*event) ||
	    event->version != FREERDP_OHOS_PEN_EVENT_VERSION)
	{
		ohos_pen_message(message, messageSize, "OHOS pen event version is invalid");
		return FALSE;
	}
	if (event->action < FREERDP_OHOS_PEN_ACTION_DOWN ||
	    event->action > FREERDP_OHOS_PEN_ACTION_CANCEL || !isfinite(event->pressure) ||
	    event->pressure < 0.0f || event->pressure > 1.0f || event->tiltX < -90 ||
	    event->tiltX > 90 || event->tiltY < -90 || event->tiltY > 90)
	{
		ohos_pen_message(message, messageSize, "OHOS pen event fields are invalid");
		return FALSE;
	}
	return TRUE;
}

BOOL freerdp_ohos_pen_dispatch(RdpeiClientContext* rdpei,
                               const FREERDP_OHOS_PEN_EVENT* event, char* message,
                               size_t messageSize)
{
	if (!freerdp_ohos_pen_validate(event, message, messageSize))
		return FALSE;
	if (!rdpei)
	{
		ohos_pen_message(message, messageSize, "OHOS pen RDPEI channel is unavailable");
		return FALSE;
	}

	UINT32 penFlags = 0;
	if ((event->flags & FREERDP_OHOS_PEN_FLAG_ERASER) != 0)
		penFlags |= RDPINPUT_PEN_FLAG_ERASER_PRESSED;
	if ((event->flags & FREERDP_OHOS_PEN_FLAG_INVERTED) != 0)
		penFlags |= RDPINPUT_PEN_FLAG_INVERTED;
	if ((event->flags & FREERDP_OHOS_PEN_FLAG_BARREL) != 0)
		penFlags |= RDPINPUT_PEN_FLAG_BARREL_PRESSED;

	const UINT32 fields = RDPINPUT_PEN_CONTACT_PENFLAGS_PRESENT |
	                      RDPINPUT_PEN_CONTACT_PRESSURE_PRESENT |
	                      RDPINPUT_PEN_CONTACT_TILTX_PRESENT |
	                      RDPINPUT_PEN_CONTACT_TILTY_PRESENT;
	const UINT32 pressure = (UINT32)lroundf(event->pressure * 1024.0f);
	UINT status = ERROR_INVALID_FUNCTION;
	switch (event->action)
	{
		case FREERDP_OHOS_PEN_ACTION_DOWN:
			if (rdpei->PenBegin)
				status = rdpei->PenBegin(rdpei, event->deviceId, fields, (INT32)event->x,
				                         (INT32)event->y, penFlags, pressure,
				                         (INT32)event->tiltX, (INT32)event->tiltY);
			break;
		case FREERDP_OHOS_PEN_ACTION_MOVE:
			if (rdpei->PenUpdate)
				status = rdpei->PenUpdate(rdpei, event->deviceId, fields, (INT32)event->x,
				                          (INT32)event->y, penFlags, pressure,
				                          (INT32)event->tiltX, (INT32)event->tiltY);
			break;
		case FREERDP_OHOS_PEN_ACTION_UP:
			if (rdpei->PenEnd)
				status = rdpei->PenEnd(rdpei, event->deviceId, fields, (INT32)event->x,
				                       (INT32)event->y, penFlags, pressure,
				                       (INT32)event->tiltX, (INT32)event->tiltY);
			break;
		case FREERDP_OHOS_PEN_ACTION_CANCEL:
			if (rdpei->PenCancel)
				status = rdpei->PenCancel(rdpei, event->deviceId, fields, (INT32)event->x,
				                          (INT32)event->y, penFlags, pressure,
				                          (INT32)event->tiltX, (INT32)event->tiltY);
			break;
		default:
			break;
	}
	if (status != CHANNEL_RC_OK)
	{
		if (message && messageSize > 0)
			(void)snprintf(message, messageSize, "OHOS pen RDPEI dispatch failed: %u", status);
		return FALSE;
	}
	ohos_pen_message(message, messageSize, "OHOS pen RDPEI event sent");
	return TRUE;
}
