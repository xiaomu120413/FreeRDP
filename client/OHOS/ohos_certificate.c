/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS certificate policy helpers
 */

#include "ohos_certificate.h"

#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define OHOS_CERTIFICATE_MAX_REGISTRATIONS 16U

typedef struct
{
	freerdp* instance;
	UINT32 policy;
	FREERDP_OHOS_CERTIFICATE_MESSAGE_CALLBACK logCallback;
	void* userData;
} FREERDP_OHOS_CERTIFICATE_REGISTRATION;

static pthread_mutex_t g_ohos_certificate_lock = PTHREAD_MUTEX_INITIALIZER;
static FREERDP_OHOS_CERTIFICATE_REGISTRATION
    g_ohos_certificate_registrations[OHOS_CERTIFICATE_MAX_REGISTRATIONS];

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

static BOOL ohos_certificate_policy_is_valid(UINT32 policy)
{
	return policy == FREERDP_OHOS_CERTIFICATE_POLICY_TOFU ||
	       policy == FREERDP_OHOS_CERTIFICATE_POLICY_STRICT ||
	       policy == FREERDP_OHOS_CERTIFICATE_POLICY_IGNORE;
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

	if (policy == FREERDP_OHOS_CERTIFICATE_POLICY_IGNORE)
	{
		ohos_certificate_format_message(
		    message, messageSize,
		    "%s certificate accepted for current session by ignore policy: target=<redacted>:%u",
		    info->changed ? "Changed" : "Server", (unsigned int)info->port);
		return 2;
	}

	if (!info->changed && policy == FREERDP_OHOS_CERTIFICATE_POLICY_TOFU)
	{
		ohos_certificate_format_message(
		    message, messageSize,
		    "Certificate accepted by TOFU policy and requested for FreeRDP store: target=<redacted>:%u cn=%s",
		    (unsigned int)info->port, ohos_certificate_safe_string(info->commonName));
		return 1;
	}

	if (info->changed)
	{
		ohos_certificate_format_message(
		    message, messageSize,
		    "Changed certificate rejected by %s policy: target=<redacted>:%u cn=%s subject=[%s] oldSubject=[%s]",
		    freerdp_ohos_certificate_policy_name(policy), (unsigned int)info->port,
		    ohos_certificate_safe_string(info->commonName), ohos_certificate_safe_string(info->subject),
		    ohos_certificate_safe_string(info->oldSubject));
		return 0;
	}

	ohos_certificate_format_message(
	    message, messageSize,
	    "Certificate rejected by strict policy: target=<redacted>:%u cn=%s issuer=%s",
	    (unsigned int)info->port, ohos_certificate_safe_string(info->commonName),
	    ohos_certificate_safe_string(info->issuer));
	return 0;
}

static BOOL ohos_certificate_find_registration_locked(
    freerdp* instance, FREERDP_OHOS_CERTIFICATE_REGISTRATION* registration)
{
	if (!instance)
		return FALSE;

	for (size_t index = 0; index < OHOS_CERTIFICATE_MAX_REGISTRATIONS; ++index)
	{
		if (g_ohos_certificate_registrations[index].instance == instance)
		{
			if (registration)
				*registration = g_ohos_certificate_registrations[index];
			return TRUE;
		}
	}
	return FALSE;
}

static void ohos_certificate_emit_log(const FREERDP_OHOS_CERTIFICATE_REGISTRATION* registration,
                                      const char* message)
{
	if (registration && registration->logCallback && message && message[0] != '\0')
		registration->logCallback(message, registration->userData);
}

static DWORD ohos_certificate_verify_common(
    freerdp* instance, BOOL changed, const char* host, UINT16 port, const char* commonName,
    const char* subject, const char* issuer, const char* fingerprint, const char* oldSubject,
    const char* oldIssuer, const char* oldFingerprint)
{
	FREERDP_OHOS_CERTIFICATE_REGISTRATION registration = { 0 };
	pthread_mutex_lock(&g_ohos_certificate_lock);
	const BOOL found = ohos_certificate_find_registration_locked(instance, &registration);
	pthread_mutex_unlock(&g_ohos_certificate_lock);

	if (!found)
		return 0;

	FREERDP_OHOS_CERTIFICATE_VERIFY_INFO info = { 0 };
	info.host = host;
	info.port = port;
	info.commonName = commonName;
	info.subject = subject;
	info.issuer = issuer;
	info.fingerprint = fingerprint;
	info.oldSubject = oldSubject;
	info.oldIssuer = oldIssuer;
	info.oldFingerprint = oldFingerprint;
	info.changed = changed;

	char message[512] = { 0 };
	const DWORD rc = freerdp_ohos_certificate_verify(registration.policy, &info, message,
	                                                 sizeof(message));
	ohos_certificate_emit_log(&registration, message);
	return rc;
}

static DWORD ohos_certificate_verify_certificate_ex(
    freerdp* instance, const char* host, UINT16 port, const char* commonName,
    const char* subject, const char* issuer, const char* fingerprint, DWORD flags)
{
	(void)flags;
	return ohos_certificate_verify_common(instance, FALSE, host, port, commonName, subject,
	                                      issuer, fingerprint, NULL, NULL, NULL);
}

static DWORD ohos_certificate_verify_changed_certificate_ex(
    freerdp* instance, const char* host, UINT16 port, const char* commonName,
    const char* subject, const char* issuer, const char* fingerprint, const char* oldSubject,
    const char* oldIssuer, const char* oldFingerprint, DWORD flags)
{
	(void)flags;
	return ohos_certificate_verify_common(instance, TRUE, host, port, commonName, subject,
	                                      issuer, fingerprint, oldSubject, oldIssuer,
	                                      oldFingerprint);
}

BOOL freerdp_ohos_certificate_register_callbacks(
    freerdp* instance, UINT32 policy, FREERDP_OHOS_CERTIFICATE_MESSAGE_CALLBACK logCallback,
    void* userData, char* message, size_t messageSize)
{
	if (!instance)
	{
		ohos_certificate_format_message(message, messageSize,
		                                "certificate callback registration needs an instance");
		return FALSE;
	}
	if (!ohos_certificate_policy_is_valid(policy))
	{
		ohos_certificate_format_message(message, messageSize,
		                                "certificate callback registration got invalid policy");
		return FALSE;
	}

	pthread_mutex_lock(&g_ohos_certificate_lock);
	size_t slot = OHOS_CERTIFICATE_MAX_REGISTRATIONS;
	for (size_t index = 0; index < OHOS_CERTIFICATE_MAX_REGISTRATIONS; ++index)
	{
		if (g_ohos_certificate_registrations[index].instance == instance)
		{
			slot = index;
			break;
		}
		if (slot == OHOS_CERTIFICATE_MAX_REGISTRATIONS &&
		    g_ohos_certificate_registrations[index].instance == NULL)
		{
			slot = index;
		}
	}

	if (slot == OHOS_CERTIFICATE_MAX_REGISTRATIONS)
	{
		pthread_mutex_unlock(&g_ohos_certificate_lock);
		ohos_certificate_format_message(message, messageSize,
		                                "certificate callback registration table is full");
		return FALSE;
	}

	g_ohos_certificate_registrations[slot].instance = instance;
	g_ohos_certificate_registrations[slot].policy = policy;
	g_ohos_certificate_registrations[slot].logCallback = logCallback;
	g_ohos_certificate_registrations[slot].userData = userData;
	pthread_mutex_unlock(&g_ohos_certificate_lock);

	instance->VerifyCertificateEx = ohos_certificate_verify_certificate_ex;
	instance->VerifyChangedCertificateEx = ohos_certificate_verify_changed_certificate_ex;
	ohos_certificate_format_message(message, messageSize,
	                                "OHOS certificate callbacks registered: policy=%s",
	                                freerdp_ohos_certificate_policy_name(policy));
	return TRUE;
}

void freerdp_ohos_certificate_unregister_callbacks(freerdp* instance)
{
	if (!instance)
		return;

	pthread_mutex_lock(&g_ohos_certificate_lock);
	for (size_t index = 0; index < OHOS_CERTIFICATE_MAX_REGISTRATIONS; ++index)
	{
		if (g_ohos_certificate_registrations[index].instance == instance)
		{
			memset(&g_ohos_certificate_registrations[index], 0,
			       sizeof(g_ohos_certificate_registrations[index]));
			break;
		}
	}
	pthread_mutex_unlock(&g_ohos_certificate_lock);

	instance->VerifyCertificateEx = NULL;
	instance->VerifyChangedCertificateEx = NULL;
}
