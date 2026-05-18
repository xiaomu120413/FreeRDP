/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS certificate policy helpers
 */

#include "ohos_certificate.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void ohos_certificate_format_message(char* message, size_t size, const char* format, ...)
{
	if (!message || size == 0)
		return;

	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, size, format, args);
	va_end(args);
}

static const char* ohos_certificate_safe_string(const char* value)
{
	return value ? value : "";
}

static int ohos_certificate_token_is(const char* token, const char* expected)
{
	if (!token || !expected)
		return 0;

	while (*token && *expected)
	{
		if (tolower((unsigned char)*token) != tolower((unsigned char)*expected))
			return 0;
		token++;
		expected++;
	}
	return *token == '\0' && *expected == '\0';
}

static void ohos_certificate_normalize_token(const char* value, char* token, size_t tokenSize)
{
	if (!token || tokenSize == 0)
		return;

	token[0] = '\0';
	if (!value)
		return;

	while (*value && isspace((unsigned char)*value))
		value++;

	size_t index = 0;
	while (*value && index + 1 < tokenSize)
	{
		if (isspace((unsigned char)*value))
			break;
		token[index++] = (char)tolower((unsigned char)*value);
		value++;
	}
	token[index] = '\0';
}

UINT32 freerdp_ohos_certificate_policy_from_string(const char* value)
{
	char token[32] = { 0 };
	ohos_certificate_normalize_token(value, token, sizeof(token));

	if (ohos_certificate_token_is(token, "strict") ||
	    ohos_certificate_token_is(token, "verify") ||
	    ohos_certificate_token_is(token, "valid-ca") ||
	    ohos_certificate_token_is(token, "deny") ||
	    ohos_certificate_token_is(token, "reject"))
		return FREERDP_OHOS_CERTIFICATE_POLICY_STRICT;

	if (ohos_certificate_token_is(token, "ignore") ||
	    ohos_certificate_token_is(token, "accept") ||
	    ohos_certificate_token_is(token, "insecure"))
		return FREERDP_OHOS_CERTIFICATE_POLICY_IGNORE;

	return FREERDP_OHOS_CERTIFICATE_POLICY_TOFU;
}

const char* freerdp_ohos_certificate_policy_name(UINT32 policy)
{
	switch (policy)
	{
		case FREERDP_OHOS_CERTIFICATE_POLICY_STRICT:
			return "strict";
		case FREERDP_OHOS_CERTIFICATE_POLICY_IGNORE:
			return "ignore";
		case FREERDP_OHOS_CERTIFICATE_POLICY_TOFU:
		default:
			return "tofu";
	}
}

DWORD freerdp_ohos_certificate_verify(UINT32 policy,
                                      const FREERDP_OHOS_CERTIFICATE_VERIFY_INFO* info,
                                      char* message, size_t messageSize)
{
	if (!info)
	{
		ohos_certificate_format_message(message, messageSize,
		                                "certificate callback input invalid");
		return 0;
	}

	const char* target = ohos_certificate_safe_string(info->host);
	if (policy == FREERDP_OHOS_CERTIFICATE_POLICY_IGNORE)
	{
		ohos_certificate_format_message(
		    message, messageSize, "%s certificate accepted for current session by ignore policy: %s:%u",
		    info->changed ? "Changed" : "Server", target, (unsigned int)info->port);
		return 2;
	}

	if (!info->changed && policy == FREERDP_OHOS_CERTIFICATE_POLICY_TOFU)
	{
		ohos_certificate_format_message(
		    message, messageSize,
		    "Certificate accepted by TOFU policy and requested for FreeRDP store: %s:%u cn=%s",
		    target, (unsigned int)info->port, ohos_certificate_safe_string(info->commonName));
		return 1;
	}

	if (info->changed)
	{
		ohos_certificate_format_message(
		    message, messageSize,
		    "Changed certificate rejected by %s policy: %s:%u cn=%s subject=[%s] oldSubject=[%s]",
		    freerdp_ohos_certificate_policy_name(policy), target, (unsigned int)info->port,
		    ohos_certificate_safe_string(info->commonName),
		    ohos_certificate_safe_string(info->subject),
		    ohos_certificate_safe_string(info->oldSubject));
		return 0;
	}

	ohos_certificate_format_message(
	    message, messageSize, "Certificate rejected by strict policy: %s:%u cn=%s issuer=%s",
	    target, (unsigned int)info->port, ohos_certificate_safe_string(info->commonName),
	    ohos_certificate_safe_string(info->issuer));
	return 0;
}
