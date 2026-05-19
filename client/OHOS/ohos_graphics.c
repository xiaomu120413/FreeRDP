/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS rdpgfx/codec policy helpers
 */

#include <freerdp/config.h>

#include "ohos_graphics.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <freerdp/channels/rdpgfx.h>

#define OHOS_H264_DESKTOP_ALIGNMENT 16U
#define OHOS_H264_DESKTOP_MIN_WIDTH 320U
#define OHOS_H264_DESKTOP_MIN_HEIGHT 240U

static const char g_mode_gdi[] = "gdi";
static const char g_mode_rdpgfx[] = "rdpgfx";
static const char g_mode_rdpgfx_h264[] = "rdpgfx-h264";
static const char g_mode_invalid[] = "invalid";

static void ohos_graphics_normalize_mode(const char* requestedMode, char* output,
                                         size_t outputSize)
{
	if (!output || outputSize == 0)
		return;

	output[0] = '\0';
	const char* mode = requestedMode;
	if (!mode)
		return;

	while (*mode && isspace((unsigned char)*mode))
		mode++;

	size_t length = strlen(mode);
	while (length > 0 && isspace((unsigned char)mode[length - 1U]))
		length--;

	const size_t copyLength = length < outputSize - 1U ? length : outputSize - 1U;
	for (size_t index = 0; index < copyLength; index++)
		output[index] = (char)tolower((unsigned char)mode[index]);
	output[copyLength] = '\0';
}

static BOOL ohos_graphics_mode_equals(const char* lhs, const char* rhs)
{
	return lhs && rhs && strcmp(lhs, rhs) == 0;
}

FREERDP_OHOS_GRAPHICS_CONFIG freerdp_ohos_graphics_config_from_mode(const char* requestedMode)
{
	char mode[64] = { 0 };
	FREERDP_OHOS_GRAPHICS_CONFIG config = { 0 };

	ohos_graphics_normalize_mode(requestedMode, mode, sizeof(mode));
	if (ohos_graphics_mode_equals(mode, "rdpgfx") || ohos_graphics_mode_equals(mode, "gfx") ||
	    ohos_graphics_mode_equals(mode, "on"))
	{
		config.mode = FREERDP_OHOS_GRAPHICS_MODE_RDPGFX;
		config.enabled = TRUE;
		config.h264 = FALSE;
		config.modeName = g_mode_rdpgfx;
		return config;
	}

	if (ohos_graphics_mode_equals(mode, "rdpgfx-h264") ||
	    ohos_graphics_mode_equals(mode, "gfx-h264") || ohos_graphics_mode_equals(mode, "h264"))
	{
		config.mode = FREERDP_OHOS_GRAPHICS_MODE_RDPGFX_H264;
		config.enabled = TRUE;
		config.h264 = TRUE;
		config.modeName = g_mode_rdpgfx_h264;
		return config;
	}

	config.mode = FREERDP_OHOS_GRAPHICS_MODE_INVALID;
	config.enabled = FALSE;
	config.h264 = FALSE;
	config.modeName = g_mode_invalid;
	return config;
}

size_t freerdp_ohos_graphics_fallback_modes(const char* requestedMode, const char** modes,
                                            size_t capacity)
{
	const FREERDP_OHOS_GRAPHICS_CONFIG config =
	    freerdp_ohos_graphics_config_from_mode(requestedMode);
	if (!modes || capacity == 0)
		return 0;

	if (config.mode == FREERDP_OHOS_GRAPHICS_MODE_RDPGFX_H264)
	{
		modes[0] = g_mode_rdpgfx_h264;
		if (capacity > 1)
		{
			modes[1] = g_mode_gdi;
			return 2;
		}
		return 1;
	}

	if (config.mode == FREERDP_OHOS_GRAPHICS_MODE_RDPGFX)
	{
		modes[0] = g_mode_rdpgfx;
		if (capacity > 1)
		{
			modes[1] = g_mode_gdi;
			return 2;
		}
		return 1;
	}

	if (config.mode == FREERDP_OHOS_GRAPHICS_MODE_GDI)
	{
		modes[0] = g_mode_gdi;
		return 1;
	}

	return 0;
}

static BOOL ohos_graphics_contains_ci(const char* haystack, const char* needle)
{
	if (!haystack || !needle || needle[0] == '\0')
		return FALSE;

	const size_t needleLength = strlen(needle);
	for (const char* pos = haystack; *pos; pos++)
	{
		size_t matched = 0;
		while (matched < needleLength && pos[matched] &&
		       tolower((unsigned char)pos[matched]) ==
		           tolower((unsigned char)needle[matched]))
		{
			matched++;
		}
		if (matched == needleLength)
			return TRUE;
	}
	return FALSE;
}

BOOL freerdp_ohos_graphics_should_retry_fallback(BOOL sessionFailed, BOOL attemptConnected,
                                                 const char* failedMode, size_t attemptIndex,
                                                 size_t attemptCount, const char* message)
{
	(void)attemptConnected;

	if (!sessionFailed || attemptIndex + 1U >= attemptCount)
		return FALSE;
	if (ohos_graphics_contains_ci(failedMode, g_mode_gdi))
		return FALSE;

	return ohos_graphics_contains_ci(message, "graphics") ||
	       ohos_graphics_contains_ci(message, "rdpgfx") ||
	       ohos_graphics_contains_ci(message, "gfx") ||
	       ohos_graphics_contains_ci(message, "dynamic channel") ||
	       ohos_graphics_contains_ci(message, "h264") ||
	       ohos_graphics_contains_ci(message, "surface");
}

UINT32 freerdp_ohos_graphics_align_down_to_multiple(UINT32 value, UINT32 alignment,
                                                    UINT32 minimum)
{
	if (alignment == 0)
		return value;
	value -= value % alignment;
	if (value >= minimum)
		return value;
	return minimum + ((alignment - (minimum % alignment)) % alignment);
}

void freerdp_ohos_graphics_align_h264_desktop_size(
    const FREERDP_OHOS_GRAPHICS_CONFIG* config, UINT32* width, UINT32* height)
{
	if (!config || !width || !height || !config->enabled || !config->h264)
		return;

	*width = freerdp_ohos_graphics_align_down_to_multiple(
	    *width, OHOS_H264_DESKTOP_ALIGNMENT, OHOS_H264_DESKTOP_MIN_WIDTH);
	*height = freerdp_ohos_graphics_align_down_to_multiple(
	    *height, OHOS_H264_DESKTOP_ALIGNMENT, OHOS_H264_DESKTOP_MIN_HEIGHT);
}

BOOL freerdp_ohos_rdpgfx_caps_confirm_is_avc420(UINT32 version, UINT32 flags)
{
	return version == RDPGFX_CAPVERSION_81 && (flags & RDPGFX_CAPS_FLAG_AVC420_ENABLED) != 0;
}

BOOL freerdp_ohos_rdpgfx_caps_confirm_is_avc444(UINT32 version, UINT32 flags)
{
	return version == RDPGFX_CAPVERSION_101 ||
	       (version >= RDPGFX_CAPVERSION_10 && (flags & RDPGFX_CAPS_FLAG_AVC_DISABLED) == 0);
}

BOOL freerdp_ohos_rdpgfx_codec_is_h264(UINT32 codecId)
{
	return codecId == RDPGFX_CODECID_AVC420 || codecId == RDPGFX_CODECID_AVC444 ||
	       codecId == RDPGFX_CODECID_AVC444v2;
}

BOOL freerdp_ohos_rdpgfx_surface_command_is_full_window(UINT32 left, UINT32 top, UINT32 width,
                                                        UINT32 height, UINT32 targetWidth,
                                                        UINT32 targetHeight)
{
	return (left == 0) && (top == 0) && (width > 0) && (height > 0) &&
	       (targetWidth > 0) && (targetHeight > 0) && (width == targetWidth) &&
	       (height == targetHeight);
}
