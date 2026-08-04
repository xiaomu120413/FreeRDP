#include "ohos_display.h"

#include <freerdp/channels/disp.h>
#include <freerdp/settings_types.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winpr/synch.h>

static uint32_t freerdp_ohos_display_clamp(uint32_t value, uint32_t minimum, uint32_t maximum)
{
	if (value < minimum)
		return minimum;
	if (value > maximum)
		return maximum;
	return value;
}

static uint32_t freerdp_ohos_display_align_down(uint32_t value, uint32_t alignment,
                                                uint32_t minimum)
{
	if (alignment <= 1U)
		return value;

	value -= value % alignment;
	if (value >= minimum)
		return value;
	return minimum + ((alignment - (minimum % alignment)) % alignment);
}

static void freerdp_ohos_display_format_message(char* message, size_t size, const char* format,
                                                ...)
{
	if (!message || size == 0)
		return;

	va_list args;
	va_start(args, format);
	const int rc = vsnprintf(message, size, format, args);
	va_end(args);
	if (rc < 0)
		message[0] = '\0';
	else
		message[size - 1] = '\0';
}

void freerdp_ohos_display_normalize_size(uint32_t width, uint32_t height, uint32_t alignment,
                                         uint32_t* normalizedWidth,
                                         uint32_t* normalizedHeight)
{
	width = freerdp_ohos_display_clamp(width, DISPLAY_CONTROL_MIN_MONITOR_WIDTH,
	                                   DISPLAY_CONTROL_MAX_MONITOR_WIDTH);
	height = freerdp_ohos_display_clamp(height, DISPLAY_CONTROL_MIN_MONITOR_HEIGHT,
	                                    DISPLAY_CONTROL_MAX_MONITOR_HEIGHT);
	width = freerdp_ohos_display_align_down(width, alignment, DISPLAY_CONTROL_MIN_MONITOR_WIDTH);
	height = freerdp_ohos_display_align_down(height, alignment, DISPLAY_CONTROL_MIN_MONITOR_HEIGHT);

	if (normalizedWidth)
		*normalizedWidth = width;
	if (normalizedHeight)
		*normalizedHeight = height;
}

static BOOL freerdp_ohos_display_orientation_valid(uint32_t orientation)
{
	return orientation == ORIENTATION_LANDSCAPE || orientation == ORIENTATION_PORTRAIT ||
	       orientation == ORIENTATION_LANDSCAPE_FLIPPED ||
	       orientation == ORIENTATION_PORTRAIT_FLIPPED;
}

int freerdp_ohos_display_build_monitor_layout_ex(uint32_t width, uint32_t height,
                                                 uint32_t orientation,
                                                 DISPLAY_CONTROL_MONITOR_LAYOUT* layout)
{
	if (!layout || !freerdp_ohos_display_orientation_valid(orientation))
		return 0;

	freerdp_ohos_display_normalize_size(width, height, 1U, &width, &height);
	memset(layout, 0, sizeof(*layout));
	layout->Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
	layout->Left = 0;
	layout->Top = 0;
	layout->Width = width;
	layout->Height = height;
	layout->PhysicalWidth = width;
	layout->PhysicalHeight = height;
	layout->Orientation = orientation;
	layout->DesktopScaleFactor = 100;
	layout->DeviceScaleFactor = 100;
	return 1;
}

struct freerdp_ohos_display_control
{
	CRITICAL_SECTION lock;
	DispClientContext* disp;
	uint32_t alignment;
	BOOL capsReady;
	BOOL hasRequestedSize;
	uint32_t requestedWidth;
	uint32_t requestedHeight;
	uint32_t requestedOrientation;
	BOOL hasSentSize;
	uint32_t lastSentWidth;
	uint32_t lastSentHeight;
	uint32_t lastSentOrientation;
	FREERDP_OHOS_DISPLAY_LOG_CALLBACK logCallback;
	void* logUserData;
};

static const char* freerdp_ohos_display_reason(const char* reason)
{
	return reason && reason[0] != '\0' ? reason : "resize request";
}

static void freerdp_ohos_display_log(freerdpOhosDisplayControl* control, const char* message)
{
	if (control && control->logCallback && message && message[0] != '\0')
		control->logCallback(message, control->logUserData);
}

static void freerdp_ohos_display_resize_result_reset(
    FREERDP_OHOS_DISPLAY_RESIZE_RESULT* result, const freerdpOhosDisplayControl* control)
{
	if (!result)
		return;

	memset(result, 0, sizeof(*result));
	result->status = FREERDP_OHOS_DISPLAY_RESIZE_FAILED;
	if (control && control->hasRequestedSize)
	{
		result->normalizedWidth = control->requestedWidth;
		result->normalizedHeight = control->requestedHeight;
		result->orientation = control->requestedOrientation;
	}
}

static BOOL freerdp_ohos_display_request_locked(
    freerdpOhosDisplayControl* control, const char* reason,
    FREERDP_OHOS_DISPLAY_RESIZE_RESULT* result, char* message, size_t messageSize)
{
	const char* safeReason = freerdp_ohos_display_reason(reason);
	uint32_t sentWidth = 0;
	uint32_t sentHeight = 0;
	UINT channelStatus = CHANNEL_RC_OK;
	DISPLAY_CONTROL_MONITOR_LAYOUT layout;

	freerdp_ohos_display_resize_result_reset(result, control);

	if (!control->hasRequestedSize)
	{
		if (result)
			result->status = FREERDP_OHOS_DISPLAY_RESIZE_DEFERRED;
		freerdp_ohos_display_format_message(message, messageSize,
		                                    "display-control resize has no requested size after %s",
		                                    safeReason);
		return TRUE;
	}

	if (!control->disp || !control->disp->SendMonitorLayout)
	{
		if (result)
			result->status = FREERDP_OHOS_DISPLAY_RESIZE_DEFERRED;
		freerdp_ohos_display_format_message(
		    message, messageSize,
		    "display-control resize pending after %s: channel not ready normalized=%ux%u alignment=%u",
		    safeReason, control->requestedWidth, control->requestedHeight, control->alignment);
		return TRUE;
	}

	if (!control->capsReady)
	{
		if (result)
			result->status = FREERDP_OHOS_DISPLAY_RESIZE_DEFERRED;
		freerdp_ohos_display_format_message(
		    message, messageSize,
		    "display-control resize pending after %s: caps not ready normalized=%ux%u alignment=%u",
		    safeReason, control->requestedWidth, control->requestedHeight, control->alignment);
		return TRUE;
	}

	if (control->hasSentSize && control->lastSentWidth == control->requestedWidth &&
	    control->lastSentHeight == control->requestedHeight &&
	    control->lastSentOrientation == control->requestedOrientation)
	{
		if (result)
		{
			result->status = FREERDP_OHOS_DISPLAY_RESIZE_UNCHANGED;
			result->sentWidth = control->lastSentWidth;
			result->sentHeight = control->lastSentHeight;
		}
		freerdp_ohos_display_format_message(
		    message, messageSize,
		    "display-control resize unchanged after %s: %ux%u orientation=%u alignment=%u",
		    safeReason, control->requestedWidth, control->requestedHeight,
		    control->requestedOrientation, control->alignment);
		return TRUE;
	}

	sentWidth = control->requestedWidth;
	sentHeight = control->requestedHeight;
	if (!freerdp_ohos_display_build_monitor_layout_ex(sentWidth, sentHeight,
	                                                 control->requestedOrientation, &layout))
	{
		freerdp_ohos_display_format_message(message, messageSize,
		                                    "display-control orientation is invalid: %u",
		                                    control->requestedOrientation);
		return FALSE;
	}

	channelStatus = control->disp->SendMonitorLayout(control->disp, 1, &layout);
	if (channelStatus != CHANNEL_RC_OK)
	{
		freerdp_ohos_display_format_message(
		    message, messageSize, "display-control resize failed: %u", channelStatus);
		return FALSE;
	}

	control->hasSentSize = TRUE;
	control->lastSentWidth = sentWidth;
	control->lastSentHeight = sentHeight;
	control->lastSentOrientation = control->requestedOrientation;
	if (result)
	{
		result->status = FREERDP_OHOS_DISPLAY_RESIZE_SENT;
		result->sentWidth = sentWidth;
		result->sentHeight = sentHeight;
	}
	freerdp_ohos_display_format_message(
	    message, messageSize,
	    "display-control resize requested after %s: monitor layout sent %ux%u orientation=%u alignment=%u",
	    safeReason, sentWidth, sentHeight, control->requestedOrientation, control->alignment);
	return TRUE;
}

static UINT freerdp_ohos_display_control_caps(DispClientContext* disp, UINT32 maxNumMonitors,
                                              UINT32 maxMonitorAreaFactorA,
                                              UINT32 maxMonitorAreaFactorB)
{
	freerdpOhosDisplayControl* control =
	    disp ? (freerdpOhosDisplayControl*)disp->custom : NULL;
	char capsMessage[160] = { 0 };
	char resizeMessage[256] = { 0 };

	if (!control)
		return CHANNEL_RC_OK;

	EnterCriticalSection(&control->lock);
	control->capsReady = TRUE;
	control->hasSentSize = FALSE;
	freerdp_ohos_display_format_message(
	    capsMessage, sizeof(capsMessage), "display-control caps: maxMonitors=%u areaFactor=%u/%u",
	    maxNumMonitors, maxMonitorAreaFactorA, maxMonitorAreaFactorB);
	if (control->hasRequestedSize)
		(void)freerdp_ohos_display_request_locked(control, "display-control caps", NULL,
		                                          resizeMessage, sizeof(resizeMessage));
	LeaveCriticalSection(&control->lock);

	freerdp_ohos_display_log(control, capsMessage);
	freerdp_ohos_display_log(control, resizeMessage);
	return CHANNEL_RC_OK;
}

freerdpOhosDisplayControl* freerdp_ohos_display_control_new(void)
{
	freerdpOhosDisplayControl* control =
	    (freerdpOhosDisplayControl*)calloc(1, sizeof(freerdpOhosDisplayControl));
	if (!control)
		return NULL;

	InitializeCriticalSection(&control->lock);
	control->alignment = 1U;
	return control;
}

void freerdp_ohos_display_control_free(freerdpOhosDisplayControl* control)
{
	if (!control)
		return;

	freerdp_ohos_display_control_reset(control);
	DeleteCriticalSection(&control->lock);
	free(control);
}

void freerdp_ohos_display_control_reset(freerdpOhosDisplayControl* control)
{
	if (!control)
		return;

	EnterCriticalSection(&control->lock);
	if (control->disp && control->disp->custom == control)
		control->disp->custom = NULL;
	control->disp = NULL;
	control->alignment = 1U;
	control->capsReady = FALSE;
	control->hasRequestedSize = FALSE;
	control->requestedWidth = 0;
	control->requestedHeight = 0;
	control->requestedOrientation = ORIENTATION_LANDSCAPE;
	control->hasSentSize = FALSE;
	control->lastSentWidth = 0;
	control->lastSentHeight = 0;
	control->lastSentOrientation = ORIENTATION_LANDSCAPE;
	LeaveCriticalSection(&control->lock);
}

void freerdp_ohos_display_control_set_log_callback(
    freerdpOhosDisplayControl* control, FREERDP_OHOS_DISPLAY_LOG_CALLBACK callback,
    void* userData)
{
	if (!control)
		return;

	EnterCriticalSection(&control->lock);
	control->logCallback = callback;
	control->logUserData = userData;
	LeaveCriticalSection(&control->lock);
}

void freerdp_ohos_display_control_set_alignment(freerdpOhosDisplayControl* control,
                                                uint32_t alignment)
{
	if (!control)
		return;

	EnterCriticalSection(&control->lock);
	control->alignment = alignment > 1U ? alignment : 1U;
	control->hasSentSize = FALSE;
	LeaveCriticalSection(&control->lock);
}

BOOL freerdp_ohos_display_control_attach(freerdpOhosDisplayControl* control,
                                         DispClientContext* disp, char* message,
                                         size_t messageSize)
{
	if (!control || !disp)
	{
		freerdp_ohos_display_format_message(message, messageSize,
		                                    "display-control attach arguments are invalid");
		return FALSE;
	}

	EnterCriticalSection(&control->lock);
	if (control->disp && control->disp != disp && control->disp->custom == control)
		control->disp->custom = NULL;
	control->disp = disp;
	control->disp->custom = control;
	control->disp->DisplayControlCaps = freerdp_ohos_display_control_caps;
	control->capsReady = FALSE;
	control->hasSentSize = FALSE;
	freerdp_ohos_display_format_message(
	    message, messageSize, "display-control connected to FreeRDP OHOS resize manager");
	LeaveCriticalSection(&control->lock);
	return TRUE;
}

void freerdp_ohos_display_control_detach(freerdpOhosDisplayControl* control,
                                         DispClientContext* disp)
{
	if (!control)
		return;

	EnterCriticalSection(&control->lock);
	if (control->disp && (!disp || control->disp == disp))
	{
		if (control->disp->custom == control)
			control->disp->custom = NULL;
		control->disp = NULL;
		control->capsReady = FALSE;
		control->hasSentSize = FALSE;
	}
	LeaveCriticalSection(&control->lock);
}

BOOL freerdp_ohos_display_control_request_resize_ex(
    freerdpOhosDisplayControl* control, uint32_t width, uint32_t height,
    uint32_t orientation, const char* reason, FREERDP_OHOS_DISPLAY_RESIZE_RESULT* result,
    char* message, size_t messageSize)
{
	uint32_t requestedWidth = width;
	uint32_t requestedHeight = height;
	BOOL ok = FALSE;
	freerdp_ohos_display_resize_result_reset(result, NULL);

	if (!control)
	{
		freerdp_ohos_display_format_message(message, messageSize,
		                                    "display-control resize manager is null");
		return FALSE;
	}
	if (width == 0 || height == 0)
	{
		freerdp_ohos_display_format_message(message, messageSize,
		                                    "display-control resize dimensions are invalid");
		return FALSE;
	}
	if (!freerdp_ohos_display_orientation_valid(orientation))
	{
		freerdp_ohos_display_format_message(message, messageSize,
		                                    "display-control orientation is invalid: %u",
		                                    orientation);
		return FALSE;
	}

	EnterCriticalSection(&control->lock);
	freerdp_ohos_display_normalize_size(width, height, control->alignment, &width, &height);
	control->hasRequestedSize = TRUE;
	control->requestedWidth = width;
	control->requestedHeight = height;
	control->requestedOrientation = orientation;
	ok = freerdp_ohos_display_request_locked(control, reason, result, message, messageSize);
	if (ok && message && messageSize > 0)
	{
		const size_t used = strlen(message);
		if (used < messageSize - 1)
			(void)snprintf(message + used, messageSize - used, " requested=%ux%u", requestedWidth,
			               requestedHeight);
		message[messageSize - 1] = '\0';
	}
	LeaveCriticalSection(&control->lock);
	return ok;
}
