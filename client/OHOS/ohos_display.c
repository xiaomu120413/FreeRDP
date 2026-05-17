#include "ohos_display.h"

#include <freerdp/settings_types.h>

#include <stdio.h>
#include <string.h>

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
                                                uint32_t first, uint32_t second)
{
	if (!message || size == 0)
		return;

	if (snprintf(message, size, format, first, second) < 0)
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
