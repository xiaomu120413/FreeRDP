/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS session settings and standard channel helpers
 */

#include "ohos_session_config.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

#include <freerdp/channels/cliprdr.h>
#include <freerdp/channels/disp.h>
#include <freerdp/channels/rdpgfx.h>
#include <freerdp/client/channels.h>
#include <freerdp/constants.h>
#include <freerdp/settings_keys.h>
#include <winpr/crt.h>

static void ohos_session_format_message(char* message, size_t size, const char* format, ...)
{
	if (!message || size == 0)
		return;

	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, size, format, args);
	va_end(args);
}

static BOOL ohos_session_set_bool(rdpSettings* settings, FreeRDP_Settings_Keys_Bool key,
                                  BOOL value, const char* name, char* message,
                                  size_t messageSize)
{
	if (freerdp_settings_set_bool(settings, key, value))
		return TRUE;
	ohos_session_format_message(message, messageSize, "set %s failed", name);
	return FALSE;
}

static BOOL ohos_session_set_uint32(rdpSettings* settings, FreeRDP_Settings_Keys_UInt32 key,
                                    UINT32 value, const char* name, char* message,
                                    size_t messageSize)
{
	if (freerdp_settings_set_uint32(settings, key, value))
		return TRUE;
	ohos_session_format_message(message, messageSize, "set %s failed", name);
	return FALSE;
}

static BOOL ohos_session_set_string(rdpSettings* settings, FreeRDP_Settings_Keys_String key,
                                    const char* value, const char* name, char* message,
                                    size_t messageSize)
{
	if (freerdp_settings_set_string(settings, key, value ? value : ""))
		return TRUE;
	ohos_session_format_message(message, messageSize, "set %s failed", name);
	return FALSE;
}

static BOOL ohos_session_add_static_channel(rdpSettings* settings, size_t count,
                                            const char* const* params, const char* name,
                                            char* message, size_t messageSize)
{
	if (freerdp_client_add_static_channel(settings, count, params))
		return TRUE;
	ohos_session_format_message(message, messageSize, "set %s static channel failed", name);
	return FALSE;
}

static BOOL ohos_session_add_dynamic_channel(rdpSettings* settings, size_t count,
                                             const char* const* params, const char* name,
                                             char* message, size_t messageSize)
{
	if (freerdp_client_add_dynamic_channel(settings, count, params))
		return TRUE;
	ohos_session_format_message(message, messageSize, "set %s dynamic channel failed", name);
	return FALSE;
}

FREERDP_OHOS_SESSION_CONFIG freerdp_ohos_session_config_default(void)
{
	FREERDP_OHOS_SESSION_CONFIG config = { 0 };
	config.clipboard = TRUE;
	config.displayControl = TRUE;
	config.audioPlayback = TRUE;
	config.audioCapture = TRUE;
	config.audioPlaybackRate = 44100;
	config.audioPlaybackChannels = 2;
	config.audioPlaybackLatencyMs = 100;
	config.audioCaptureRate = 0;
	config.audioCaptureChannels = 0;
	return config;
}

void freerdp_ohos_session_config_from_graphics(const FREERDP_OHOS_GRAPHICS_CONFIG* graphics,
                                               FREERDP_OHOS_SESSION_CONFIG* config)
{
	if (!config)
		return;

	*config = freerdp_ohos_session_config_default();
	if (graphics)
	{
		config->graphicsPipeline = graphics->enabled;
		config->h264 = graphics->enabled && graphics->h264;
	}
}

BOOL freerdp_ohos_session_apply_connection_settings(
    rdpSettings* settings, const FREERDP_OHOS_CONNECTION_CONFIG* config, char* message,
    size_t messageSize)
{
	if (!settings || !config)
	{
		ohos_session_format_message(message, messageSize,
		                            "OHOS connection settings input invalid");
		return FALSE;
	}

	if (!config->serverHostname || config->serverHostname[0] == '\0')
	{
		ohos_session_format_message(message, messageSize, "server hostname is required");
		return FALSE;
	}
	if (!config->username || config->username[0] == '\0')
	{
		ohos_session_format_message(message, messageSize, "username is required");
		return FALSE;
	}
	if (config->serverPort == 0 || config->desktopWidth == 0 || config->desktopHeight == 0)
	{
		ohos_session_format_message(
		    message, messageSize,
		    "server port and desktop dimensions are required: port=%" PRIu32 " desktop=%" PRIu32
		    "x%" PRIu32,
		    config->serverPort, config->desktopWidth, config->desktopHeight);
		return FALSE;
	}

	const UINT32 colorDepth = config->colorDepth > 0 ? config->colorDepth : 32;
	const UINT32 tcpConnectTimeoutMs =
	    config->tcpConnectTimeoutMs > 0 ? config->tcpConnectTimeoutMs : 5000;

	if (!ohos_session_set_string(settings, FreeRDP_ServerHostname, config->serverHostname,
	                             "ServerHostname", message, messageSize) ||
	    !ohos_session_set_uint32(settings, FreeRDP_ServerPort, config->serverPort,
	                             "ServerPort", message, messageSize) ||
	    !ohos_session_set_string(settings, FreeRDP_Username, config->username, "Username",
	                             message, messageSize) ||
	    !ohos_session_set_string(settings, FreeRDP_Password, config->password, "Password",
	                             message, messageSize) ||
	    !ohos_session_set_uint32(settings, FreeRDP_DesktopWidth, config->desktopWidth,
	                             "DesktopWidth", message, messageSize) ||
	    !ohos_session_set_uint32(settings, FreeRDP_DesktopHeight, config->desktopHeight,
	                             "DesktopHeight", message, messageSize) ||
	    !ohos_session_set_uint32(settings, FreeRDP_ColorDepth, colorDepth, "ColorDepth", message,
	                             messageSize) ||
	    !ohos_session_set_uint32(settings, FreeRDP_TcpConnectTimeout, tcpConnectTimeoutMs,
	                             "TcpConnectTimeout", message, messageSize) ||
	    !ohos_session_set_uint32(settings, FreeRDP_OsMajorType, OSMAJORTYPE_UNIX,
	                             "OsMajorType", message, messageSize) ||
	    !ohos_session_set_uint32(settings, FreeRDP_OsMinorType, OSMINORTYPE_NATIVE_WAYLAND,
	                             "OsMinorType", message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_AuthenticationOnly, FALSE,
	                           "AuthenticationOnly", message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_Authentication, TRUE, "Authentication", message,
	                           messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_SoftwareGdi, TRUE, "SoftwareGdi", message,
	                           messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_NegotiateSecurityLayer, TRUE,
	                           "NegotiateSecurityLayer", message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_CertificateCallbackPreferPEM, TRUE,
	                           "CertificateCallbackPreferPEM", message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_IgnoreCertificate, config->ignoreCertificate,
	                           "IgnoreCertificate", message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_AutoAcceptCertificate, FALSE,
	                           "AutoAcceptCertificate", message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_AutoDenyCertificate, FALSE,
	                           "AutoDenyCertificate", message, messageSize))
		return FALSE;

	if (config->domain && config->domain[0] != '\0' &&
	    !ohos_session_set_string(settings, FreeRDP_Domain, config->domain, "Domain", message,
	                             messageSize))
		return FALSE;

	ohos_session_format_message(
	    message, messageSize,
	    "OHOS FreeRDP connection settings applied: target=%s:%" PRIu32
	    " desktop=%" PRIu32 "x%" PRIu32 " colorDepth=%" PRIu32
	    " timeoutMs=%" PRIu32 " ignoreCertificate=%d domain=%s",
	    config->serverHostname, config->serverPort, config->desktopWidth, config->desktopHeight,
	    colorDepth, tcpConnectTimeoutMs, config->ignoreCertificate ? 1 : 0,
	    (config->domain && config->domain[0] != '\0') ? "set" : "none");
	return TRUE;
}

BOOL freerdp_ohos_session_apply_settings(rdpSettings* settings,
                                         const FREERDP_OHOS_SESSION_CONFIG* config,
                                         char* message, size_t messageSize)
{
	if (!settings || !config)
	{
		ohos_session_format_message(message, messageSize, "OHOS session settings input invalid");
		return FALSE;
	}

	const BOOL h264 = config->graphicsPipeline && config->h264;
	const BOOL avc420 = h264;
	const BOOL avc444 = h264;
	if (!ohos_session_set_bool(settings, FreeRDP_SupportDynamicChannels, TRUE,
	                           "SupportDynamicChannels", message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_SupportDisplayControl, config->displayControl,
	                           "SupportDisplayControl", message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_DynamicResolutionUpdate, config->displayControl,
	                           "DynamicResolutionUpdate", message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_SupportGraphicsPipeline, config->graphicsPipeline,
	                           "SupportGraphicsPipeline", message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_GfxH264, h264, "GfxH264", message,
	                           messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_GfxAVC444, avc444, "GfxAVC444", message,
	                           messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_GfxAVC444v2, avc444, "GfxAVC444v2", message,
	                           messageSize) ||
	    !ohos_session_set_uint32(settings, FreeRDP_GfxCapsFilter, 0, "GfxCapsFilter", message,
	                             messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_RemoteFxCodec, FALSE, "RemoteFxCodec", message,
	                           messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_NSCodec, h264, "NSCodec", message,
	                           messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_GfxProgressive, FALSE, "GfxProgressive",
	                           message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_GfxProgressiveV2, FALSE, "GfxProgressiveV2",
	                           message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_GfxSmallCache, TRUE, "GfxSmallCache", message,
	                           messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_FastPathOutput, TRUE, "FastPathOutput", message,
	                           messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_FrameMarkerCommandEnabled, TRUE,
	                           "FrameMarkerCommandEnabled", message, messageSize) ||
	    !ohos_session_set_uint32(settings, FreeRDP_FrameAcknowledge, 2, "FrameAcknowledge",
	                             message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_RedirectClipboard, config->clipboard,
	                           "RedirectClipboard", message, messageSize) ||
	    !ohos_session_set_uint32(settings, FreeRDP_ClipboardFeatureMask,
	                             CLIPRDR_FLAG_LOCAL_TO_REMOTE | CLIPRDR_FLAG_REMOTE_TO_LOCAL,
	                             "ClipboardFeatureMask", message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_DeviceRedirection, TRUE, "DeviceRedirection",
	                           message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_AudioPlayback, config->audioPlayback,
	                           "AudioPlayback", message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_AudioCapture, config->audioCapture,
	                           "AudioCapture", message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_RemoteConsoleAudio, FALSE, "RemoteConsoleAudio",
	                           message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_RedirectDrives, FALSE, "RedirectDrives", message,
	                           messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_RedirectPrinters, FALSE, "RedirectPrinters",
	                           message, messageSize) ||
	    !ohos_session_set_bool(settings, FreeRDP_RedirectSmartCards, FALSE, "RedirectSmartCards",
	                           message, messageSize))
		return FALSE;

	ohos_session_format_message(
	    message, messageSize,
	    "OHOS FreeRDP settings applied: cliprdr=%d disp=%d rdpsnd=%d audin=%d gfx=%d "
	    "h264=%d avc420=%d avc444=%d",
	    config->clipboard, config->displayControl, config->audioPlayback, config->audioCapture,
	    config->graphicsPipeline, h264, avc420, avc444);
	return TRUE;
}

BOOL freerdp_ohos_session_add_standard_channels(rdpSettings* settings,
                                                const FREERDP_OHOS_SESSION_CONFIG* config,
                                                char* message, size_t messageSize)
{
	if (!settings || !config)
	{
		ohos_session_format_message(message, messageSize, "OHOS session channel input invalid");
		return FALSE;
	}

	if (config->clipboard)
	{
		const char* params[] = { "cliprdr" };
		if (!ohos_session_add_static_channel(settings, ARRAYSIZE(params), params, "cliprdr",
		                                     message, messageSize))
			return FALSE;
	}

	if (config->displayControl)
	{
		const char* params[] = { DISP_CHANNEL_NAME };
		if (!ohos_session_add_dynamic_channel(settings, ARRAYSIZE(params), params, "disp",
		                                      message, messageSize))
			return FALSE;
	}

	if (config->graphicsPipeline)
	{
		const char* params[] = { RDPGFX_CHANNEL_NAME };
		if (!ohos_session_add_dynamic_channel(settings, ARRAYSIZE(params), params, "rdpgfx",
		                                      message, messageSize))
			return FALSE;
	}

	if (config->audioPlayback)
	{
		char rate[32] = { 0 };
		char channels[32] = { 0 };
		char latency[32] = { 0 };
		(void)snprintf(rate, sizeof(rate), "rate:%" PRIu32, config->audioPlaybackRate);
		(void)snprintf(channels, sizeof(channels), "channel:%" PRIu32,
		               config->audioPlaybackChannels);
		(void)snprintf(latency, sizeof(latency), "latency:%" PRIu32,
		               config->audioPlaybackLatencyMs);
		const char* params[] = { "rdpsnd", "sys:ohos", "format:1", rate, channels, latency,
			                     "quality:high" };
		if (!ohos_session_add_static_channel(settings, ARRAYSIZE(params), params, "rdpsnd",
		                                     message, messageSize) ||
		    !ohos_session_add_dynamic_channel(settings, ARRAYSIZE(params), params, "rdpsnd",
		                                      message, messageSize))
			return FALSE;
	}

	if (config->audioCapture)
	{
		char rate[32] = { 0 };
		char channels[32] = { 0 };
		const char* params[4] = { "audin", "sys:ohos" };
		size_t paramCount = 2;
		if (config->audioCaptureRate > 0)
		{
			(void)snprintf(rate, sizeof(rate), "rate:%" PRIu32, config->audioCaptureRate);
			params[paramCount++] = rate;
		}
		if (config->audioCaptureChannels > 0)
		{
			(void)snprintf(channels, sizeof(channels), "channel:%" PRIu32,
			               config->audioCaptureChannels);
			params[paramCount++] = channels;
		}
		if (!ohos_session_add_dynamic_channel(settings, paramCount, params, "audin",
		                                      message, messageSize))
			return FALSE;
	}

	ohos_session_format_message(
	    message, messageSize,
	    "OHOS FreeRDP channels added: cliprdr=%d disp=%d rdpgfx=%d rdpsnd=%d audin=%d "
	    "audinCapture=%s",
	    config->clipboard, config->displayControl, config->graphicsPipeline,
	    config->audioPlayback, config->audioCapture,
	    (config->audioCaptureRate == 0 && config->audioCaptureChannels == 0) ? "negotiated"
	                                                                         : "fixed");
	return TRUE;
}
