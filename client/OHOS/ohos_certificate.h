#ifndef FREERDP_CLIENT_OHOS_CERTIFICATE_H
#define FREERDP_CLIENT_OHOS_CERTIFICATE_H

#include <stddef.h>

#include <freerdp/api.h>
#include <winpr/wtypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define FREERDP_OHOS_CERTIFICATE_POLICY_TOFU 0U
#define FREERDP_OHOS_CERTIFICATE_POLICY_STRICT 1U
#define FREERDP_OHOS_CERTIFICATE_POLICY_IGNORE 2U

typedef struct
{
	const char* host;
	UINT16 port;
	const char* commonName;
	const char* subject;
	const char* issuer;
	const char* fingerprint;
	const char* oldSubject;
	const char* oldIssuer;
	const char* oldFingerprint;
	BOOL changed;
} FREERDP_OHOS_CERTIFICATE_VERIFY_INFO;

FREERDP_API UINT32 freerdp_ohos_certificate_policy_from_string(const char* value);
FREERDP_API const char* freerdp_ohos_certificate_policy_name(UINT32 policy);
FREERDP_API DWORD freerdp_ohos_certificate_verify(
    UINT32 policy, const FREERDP_OHOS_CERTIFICATE_VERIFY_INFO* info, char* message,
    size_t messageSize);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_CERTIFICATE_H */
