#ifndef FREERDP_CLIENT_OHOS_SESSION_OPTIONS_H
#define FREERDP_CLIENT_OHOS_SESSION_OPTIONS_H

#include <stddef.h>
#include <stdint.h>

#include <freerdp/api.h>
#include <freerdp/settings.h>
#include <winpr/wtypes.h>

#include "ohos_graphics.h"
#include "ohos_session_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct freerdp_ohos_session_options
{
	FREERDP_OHOS_CONNECTION_CONFIG connection;
	FREERDP_OHOS_SESSION_CONFIG session;
	const char* appDataDir;
	UINT32 certificatePolicy;
} FREERDP_OHOS_SESSION_OPTIONS;

typedef struct
{
	const char* serverHostname;
	const char* serverPort;
	const char* username;
	const char* password;
	const char* desktopSize;
	const char* graphicsMode;
	const char* appDataDir;
	const char* certificatePolicy;
	UINT32 colorDepth;
	UINT32 tcpConnectTimeoutMs;
} FREERDP_OHOS_SESSION_INPUT;

typedef struct
{
	FREERDP_OHOS_SESSION_OPTIONS options;
	FREERDP_OHOS_GRAPHICS_CONFIG graphics;
	char serverHostname[256];
	char username[256];
	char domain[256];
	char appDataDir[512];
} FREERDP_OHOS_SESSION_PREPARED_OPTIONS;

FREERDP_API BOOL freerdp_ohos_session_prepare_options(
    const FREERDP_OHOS_SESSION_INPUT* input, FREERDP_OHOS_SESSION_PREPARED_OPTIONS* prepared,
    char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_session_apply_storage_settings(
    rdpSettings* settings, const char* appDataDir, char* message, size_t messageSize);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_SESSION_OPTIONS_H */
