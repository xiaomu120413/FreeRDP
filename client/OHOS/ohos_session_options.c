/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS session option normalization and storage paths
 */

#include "ohos_session_options.h"

#include "ohos_certificate.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <freerdp/settings_keys.h>

#define OHOS_MIN_DESKTOP_WIDTH 320U
#define OHOS_MIN_DESKTOP_HEIGHT 240U

static void ohos_options_format_message(char* message, size_t size, const char* format, ...)
{
	if (!message || size == 0)
		return;

	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, size, format, args);
	va_end(args);
}

static BOOL ohos_options_copy_trim(const char* input, char* output, size_t outputSize,
                                   const char* name, char* message, size_t messageSize)
{
	if (!output || outputSize == 0)
		return FALSE;

	output[0] = '\0';
	if (!input)
	{
		ohos_options_format_message(message, messageSize, "%s is required", name);
		return FALSE;
	}

	while (*input && isspace((unsigned char)*input))
		input++;

	size_t length = strlen(input);
	while (length > 0 && isspace((unsigned char)input[length - 1U]))
		length--;
	if (length == 0)
	{
		ohos_options_format_message(message, messageSize, "%s is required", name);
		return FALSE;
	}
	if (length >= outputSize)
	{
		ohos_options_format_message(message, messageSize, "%s is too long", name);
		return FALSE;
	}

	memcpy(output, input, length);
	output[length] = '\0';
	return TRUE;
}

static void ohos_options_trim_trailing_slashes(char* value)
{
	if (!value)
		return;

	size_t length = strlen(value);
	while (length > 1 && value[length - 1U] == '/')
		value[--length] = '\0';
}

static BOOL ohos_options_parse_uint32(const char* value, UINT32* parsed)
{
	if (!value || !parsed || value[0] == '\0')
		return FALSE;

	char* end = NULL;
	errno = 0;
	const unsigned long result = strtoul(value, &end, 10);
	if (errno != 0 || end == value || *end != '\0' || result > UINT32_MAX)
		return FALSE;

	*parsed = (UINT32)result;
	return TRUE;
}

static BOOL ohos_options_parse_port(const char* value, UINT32* port)
{
	UINT32 parsed = 0;
	if (!ohos_options_parse_uint32(value, &parsed) || parsed == 0 || parsed > 65535)
		return FALSE;
	*port = parsed;
	return TRUE;
}

static BOOL ohos_options_parse_desktop_size(const char* value, UINT32* width, UINT32* height)
{
	if (!value || !width || !height)
		return FALSE;

	const char* separator = strchr(value, 'x');
	if (!separator)
		separator = strchr(value, 'X');
	if (!separator || separator == value || separator[1] == '\0')
		return FALSE;

	char widthText[32] = { 0 };
	char heightText[32] = { 0 };
	const size_t widthLength = (size_t)(separator - value);
	const size_t heightLength = strlen(separator + 1);
	if (widthLength >= sizeof(widthText) || heightLength >= sizeof(heightText))
		return FALSE;

	memcpy(widthText, value, widthLength);
	memcpy(heightText, separator + 1, heightLength);

	UINT32 parsedWidth = 0;
	UINT32 parsedHeight = 0;
	if (!ohos_options_parse_uint32(widthText, &parsedWidth) ||
	    !ohos_options_parse_uint32(heightText, &parsedHeight) ||
	    parsedWidth < OHOS_MIN_DESKTOP_WIDTH || parsedHeight < OHOS_MIN_DESKTOP_HEIGHT)
		return FALSE;

	*width = parsedWidth;
	*height = parsedHeight;
	return TRUE;
}

static BOOL ohos_options_copy_range(const char* begin, size_t length, char* output,
                                    size_t outputSize)
{
	if (!output || outputSize == 0 || length >= outputSize)
		return FALSE;
	memcpy(output, begin, length);
	output[length] = '\0';
	return TRUE;
}

static BOOL ohos_options_split_domain_username(const char* value, char* domain,
                                               size_t domainSize, char* username,
                                               size_t usernameSize, char* message,
                                               size_t messageSize)
{
	char trimmed[256] = { 0 };
	if (!ohos_options_copy_trim(value, trimmed, sizeof(trimmed), "username", message,
	                            messageSize))
		return FALSE;

	const char* separator = strchr(trimmed, '\\');
	size_t separatorLength = 1;
	const char* ideographicSeparator = strstr(trimmed, "\xE3\x80\x81");
	if (!separator || (ideographicSeparator && ideographicSeparator < separator))
	{
		separator = ideographicSeparator;
		separatorLength = 3;
	}

	if (!separator || separator == trimmed || separator[separatorLength] == '\0')
	{
		domain[0] = '\0';
		return ohos_options_copy_range(trimmed, strlen(trimmed), username, usernameSize);
	}

	const size_t domainLength = (size_t)(separator - trimmed);
	const char* userBegin = separator + separatorLength;
	if (!ohos_options_copy_range(trimmed, domainLength, domain, domainSize) ||
	    !ohos_options_copy_range(userBegin, strlen(userBegin), username, usernameSize))
	{
		ohos_options_format_message(message, messageSize, "domain or username is too long");
		return FALSE;
	}
	return TRUE;
}

static BOOL ohos_options_join_path(const char* base, const char* child, char* output,
                                   size_t outputSize)
{
	const int rc = snprintf(output, outputSize, "%s/%s", base, child);
	return rc > 0 && (size_t)rc < outputSize;
}

static BOOL ohos_options_ensure_directory(const char* path, char* message, size_t messageSize)
{
	if (!path || path[0] == '\0')
	{
		ohos_options_format_message(message, messageSize, "empty directory path");
		return FALSE;
	}

	char current[768] = { 0 };
	size_t currentLength = 0;
	size_t index = 0;
	if (path[0] == '/')
	{
		current[0] = '/';
		current[1] = '\0';
		currentLength = 1;
		index = 1;
	}

	while (path[index] != '\0')
	{
		while (path[index] == '/')
			index++;
		const size_t start = index;
		while (path[index] != '\0' && path[index] != '/')
			index++;
		const size_t partLength = index - start;
		if (partLength == 0)
			continue;

		if (currentLength > 1 && current[currentLength - 1U] != '/')
			current[currentLength++] = '/';
		if (currentLength + partLength >= sizeof(current))
		{
			ohos_options_format_message(message, messageSize, "directory path is too long");
			return FALSE;
		}
		memcpy(current + currentLength, path + start, partLength);
		currentLength += partLength;
		current[currentLength] = '\0';

		if (mkdir(current, 0700) != 0 && errno != EEXIST)
		{
			ohos_options_format_message(message, messageSize, "mkdir %s failed: errno=%d",
			                            current, errno);
			return FALSE;
		}
	}
	return TRUE;
}

BOOL freerdp_ohos_session_prepare_options(
    const FREERDP_OHOS_SESSION_INPUT* input, FREERDP_OHOS_SESSION_PREPARED_OPTIONS* prepared,
    char* message, size_t messageSize)
{
	if (!input || !prepared)
	{
		ohos_options_format_message(message, messageSize, "OHOS session input is required");
		return FALSE;
	}

	memset(prepared, 0, sizeof(*prepared));
	if (!ohos_options_copy_trim(input->serverHostname, prepared->serverHostname,
	                            sizeof(prepared->serverHostname), "server hostname", message,
	                            messageSize) ||
	    !ohos_options_split_domain_username(input->username, prepared->domain,
	                                        sizeof(prepared->domain), prepared->username,
	                                        sizeof(prepared->username), message, messageSize) ||
	    !ohos_options_copy_trim(input->appDataDir, prepared->appDataDir,
	                            sizeof(prepared->appDataDir), "appDataDir", message,
	                            messageSize))
		return FALSE;
	ohos_options_trim_trailing_slashes(prepared->appDataDir);

	UINT32 port = 0;
	if (!ohos_options_parse_port(input->serverPort, &port))
	{
		ohos_options_format_message(message, messageSize, "invalid RDP port: %s",
		                            input->serverPort ? input->serverPort : "");
		return FALSE;
	}

	UINT32 width = 0;
	UINT32 height = 0;
	if (!ohos_options_parse_desktop_size(input->desktopSize, &width, &height))
	{
		ohos_options_format_message(message, messageSize, "invalid RDP desktop resolution: %s",
		                            input->desktopSize ? input->desktopSize : "");
		return FALSE;
	}

	prepared->graphics = freerdp_ohos_graphics_config_from_mode(input->graphicsMode);
	if (prepared->graphics.mode == FREERDP_OHOS_GRAPHICS_MODE_INVALID)
	{
		ohos_options_format_message(message, messageSize, "invalid graphicsMode: %s",
		                            input->graphicsMode ? input->graphicsMode : "");
		return FALSE;
	}

	const UINT32 requestedWidth = width;
	const UINT32 requestedHeight = height;
	freerdp_ohos_graphics_align_h264_desktop_size(&prepared->graphics, &width, &height);

	FREERDP_OHOS_SESSION_CONFIG sessionConfig = { 0 };
	freerdp_ohos_session_config_from_graphics(&prepared->graphics, &sessionConfig);
	const UINT32 certificatePolicy =
	    freerdp_ohos_certificate_policy_from_string(input->certificatePolicy);

	prepared->options.connection.serverHostname = prepared->serverHostname;
	prepared->options.connection.serverPort = port;
	prepared->options.connection.username = prepared->username;
	prepared->options.connection.password = input->password ? input->password : "";
	prepared->options.connection.domain =
	    prepared->domain[0] == '\0' ? NULL : prepared->domain;
	prepared->options.connection.desktopWidth = width;
	prepared->options.connection.desktopHeight = height;
	prepared->options.connection.colorDepth = input->colorDepth > 0 ? input->colorDepth : 32;
	prepared->options.connection.tcpConnectTimeoutMs =
	    input->tcpConnectTimeoutMs > 0 ? input->tcpConnectTimeoutMs : 5000;
	prepared->options.connection.ignoreCertificate =
	    certificatePolicy == FREERDP_OHOS_CERTIFICATE_POLICY_IGNORE ? TRUE : FALSE;
	prepared->options.session = sessionConfig;
	prepared->options.appDataDir = prepared->appDataDir;
	prepared->options.certificatePolicy = certificatePolicy;

	ohos_options_format_message(
	    message, messageSize,
	    "OHOS session options prepared: target=<redacted>:%" PRIu32
	    " desktop=%" PRIu32 "x%" PRIu32 " mode=%s policy=%s domain=%s appDataDir=set"
	    " h264Align=%" PRIu32 "x%" PRIu32 "->%" PRIu32 "x%" PRIu32,
	    port, width, height,
	    prepared->graphics.modeName ? prepared->graphics.modeName : "unknown",
	    freerdp_ohos_certificate_policy_name(certificatePolicy),
	    prepared->domain[0] == '\0' ? "none" : "set", requestedWidth, requestedHeight, width,
	    height);
	return TRUE;
}

BOOL freerdp_ohos_session_apply_storage_settings(rdpSettings* settings, const char* appDataDir,
                                                 char* message, size_t messageSize)
{
	if (!settings)
	{
		ohos_options_format_message(message, messageSize, "FreeRDP settings unavailable");
		return FALSE;
	}

	char filesDir[512] = { 0 };
	if (!ohos_options_copy_trim(appDataDir, filesDir, sizeof(filesDir), "appDataDir", message,
	                            messageSize))
		return FALSE;
	ohos_options_trim_trailing_slashes(filesDir);

	char configPath[768] = { 0 };
	char certsPath[768] = { 0 };
	char serverPath[768] = { 0 };
	if (!ohos_options_join_path(filesDir, "freerdp", configPath, sizeof(configPath)) ||
	    !ohos_options_join_path(configPath, "certs", certsPath, sizeof(certsPath)) ||
	    !ohos_options_join_path(configPath, "server", serverPath, sizeof(serverPath)))
	{
		ohos_options_format_message(message, messageSize, "FreeRDP storage path is too long");
		return FALSE;
	}

	if (!ohos_options_ensure_directory(configPath, message, messageSize) ||
	    !ohos_options_ensure_directory(certsPath, message, messageSize) ||
	    !ohos_options_ensure_directory(serverPath, message, messageSize))
		return FALSE;

	setenv("HOME", filesDir, 1);
	setenv("XDG_CONFIG_HOME", filesDir, 1);
	if (!freerdp_settings_set_string(settings, FreeRDP_HomePath, filesDir) ||
	    !freerdp_settings_set_string(settings, FreeRDP_ConfigPath, configPath))
	{
		ohos_options_format_message(message, messageSize,
		                            "set FreeRDP storage settings failed");
		return FALSE;
	}

	ohos_options_format_message(message, messageSize,
	                            "OHOS FreeRDP storage path applied: appDataDir=set");
	return TRUE;
}
