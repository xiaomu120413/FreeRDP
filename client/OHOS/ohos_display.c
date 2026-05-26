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

int freerdp_ohos_display_build_monitor_layout(uint32_t width, uint32_t height,
                                              DISPLAY_CONTROL_MONITOR_LAYOUT* layout)
{
	if (!layout)
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
	layout->Orientation = ORIENTATION_LANDSCAPE;
	layout->DesktopScaleFactor = 100;
	layout->DeviceScaleFactor = 100;
	return 1;
}

int freerdp_ohos_display_send_monitor_layout(DispClientContext* disp, uint32_t width,
                                             uint32_t height, uint32_t alignment,
                                             uint32_t* sentWidth, uint32_t* sentHeight,
                                             uint32_t* channelStatus, char* message,
                                             size_t messageSize)
{
	DISPLAY_CONTROL_MONITOR_LAYOUT layout;
	UINT status = 0;

	if (sentWidth)
		*sentWidth = 0;
	if (sentHeight)
		*sentHeight = 0;
	if (channelStatus)
		*channelStatus = 0;

	if (!disp || !disp->SendMonitorLayout)
	{
		freerdp_ohos_display_format_message(message, messageSize,
		                                    "display-control channel is not ready (%u/%u)", 0, 0);
		return 0;
	}

	freerdp_ohos_display_normalize_size(width, height, alignment, &width, &height);
	if (!freerdp_ohos_display_build_monitor_layout(width, height, &layout))
	{
		freerdp_ohos_display_format_message(message, messageSize,
		                                    "display-control monitor layout build failed (%u/%u)",
		                                    width, height);
		return 0;
	}

	status = disp->SendMonitorLayout(disp, 1, &layout);
	if (channelStatus)
		*channelStatus = status;
	if (status != 0)
	{
		if (message && messageSize > 0)
		{
			if (snprintf(message, messageSize, "display-control resize failed: %u for %ux%u",
			             status, width, height) < 0)
				message[0] = '\0';
			else
				message[messageSize - 1] = '\0';
		}
		return 0;
	}

	if (sentWidth)
		*sentWidth = width;
	if (sentHeight)
		*sentHeight = height;
	freerdp_ohos_display_format_message(message, messageSize,
	                                    "display-control monitor layout sent: %ux%u", width,
	                                    height);
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
	BOOL hasSentSize;
	uint32_t lastSentWidth;
	uint32_t lastSentHeight;
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

static BOOL freerdp_ohos_display_request_locked(freerdpOhosDisplayControl* control,
                                                const char* reason, char* message,
                                                size_t messageSize)
{
	const char* safeReason = freerdp_ohos_display_reason(reason);
	uint32_t sentWidth = 0;
	uint32_t sentHeight = 0;
	uint32_t channelStatus = 0;
	char detail[192] = { 0 };

	if (!control->hasRequestedSize)
	{
		freerdp_ohos_display_format_message(message, messageSize,
		                                    "display-control resize has no requested size after %s",
		                                    safeReason);
		return TRUE;
	}

	if (!control->disp || !control->disp->SendMonitorLayout)
	{
		freerdp_ohos_display_format_message(
		    message, messageSize,
		    "display-control resize pending after %s: channel not ready normalized=%ux%u alignment=%u",
		    safeReason, control->requestedWidth, control->requestedHeight, control->alignment);
		return TRUE;
	}

	if (!control->capsReady)
	{
		freerdp_ohos_display_format_message(
		    message, messageSize,
		    "display-control resize pending after %s: caps not ready normalized=%ux%u alignment=%u",
		    safeReason, control->requestedWidth, control->requestedHeight, control->alignment);
		return TRUE;
	}

	if (control->hasSentSize && control->lastSentWidth == control->requestedWidth &&
	    control->lastSentHeight == control->requestedHeight)
	{
		freerdp_ohos_display_format_message(
		    message, messageSize,
		    "display-control resize unchanged after %s: %ux%u alignment=%u", safeReason,
		    control->requestedWidth, control->requestedHeight, control->alignment);
		return TRUE;
	}

	if (!freerdp_ohos_display_send_monitor_layout(control->disp, control->requestedWidth,
	                                             control->requestedHeight, 1U, &sentWidth,
	                                             &sentHeight, &channelStatus, detail,
	                                             sizeof(detail)))
	{
		if (detail[0] != '\0')
			freerdp_ohos_display_format_message(message, messageSize, "%s", detail);
		else
			freerdp_ohos_display_format_message(
			    message, messageSize, "display-control resize failed: %u", channelStatus);
		return FALSE;
	}

	control->hasSentSize = TRUE;
	control->lastSentWidth = sentWidth;
	control->lastSentHeight = sentHeight;
	freerdp_ohos_display_format_message(
	    message, messageSize, "display-control resize requested after %s: %s alignment=%u",
	    safeReason, detail[0] != '\0' ? detail : "monitor layout sent", control->alignment);
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
		(void)freerdp_ohos_display_request_locked(control, "display-control caps", resizeMessage,
		                                          sizeof(resizeMessage));
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
	control->hasSentSize = FALSE;
	control->lastSentWidth = 0;
	control->lastSentHeight = 0;
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

BOOL freerdp_ohos_display_control_request_resize(freerdpOhosDisplayControl* control,
                                                 uint32_t width, uint32_t height,
                                                 const char* reason, char* message,
                                                 size_t messageSize)
{
	uint32_t requestedWidth = width;
	uint32_t requestedHeight = height;
	BOOL ok = FALSE;

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

	EnterCriticalSection(&control->lock);
	freerdp_ohos_display_normalize_size(width, height, control->alignment, &width, &height);
	control->hasRequestedSize = TRUE;
	control->requestedWidth = width;
	control->requestedHeight = height;
	ok = freerdp_ohos_display_request_locked(control, reason, message, messageSize);
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
