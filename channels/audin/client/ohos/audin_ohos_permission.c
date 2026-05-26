/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Input Redirection Virtual Channel - HarmonyOS microphone permission bridge
 */

#include "audin_ohos_permission.h"

#include <pthread.h>

static pthread_mutex_t g_permissionCallbackLock = PTHREAD_MUTEX_INITIALIZER;
static pfnAudinOhosPermissionRequest g_permissionRequest = NULL;
static void* g_permissionRequestUserData = NULL;

FREERDP_API BOOL freerdp_audin_ohos_set_permission_callback(
    pfnAudinOhosPermissionRequest callback, void* userData)
{
	pthread_mutex_lock(&g_permissionCallbackLock);
	g_permissionRequest = callback;
	g_permissionRequestUserData = userData;
	pthread_mutex_unlock(&g_permissionCallbackLock);
	return TRUE;
}

BOOL audin_ohos_request_microphone_permission(AudinOhosDevice* ohos)
{
	pfnAudinOhosPermissionRequest callback = NULL;
	void* userData = NULL;
	BOOL granted = TRUE;

	pthread_mutex_lock(&g_permissionCallbackLock);
	callback = g_permissionRequest;
	userData = g_permissionRequestUserData;
	pthread_mutex_unlock(&g_permissionCallbackLock);

	if (!callback)
	{
		audin_ohos_log(ohos, WLOG_DEBUG,
		               "microphone permission callback is not registered; relying on OHAudio");
		return TRUE;
	}

	++g_permissionRequestCount;
	audin_ohos_log(ohos, WLOG_INFO,
	               "requesting OHOS microphone permission before starting remote audio capture");
	granted = callback(userData, 60000);
	if (granted)
	{
		++g_permissionGrantedCount;
		audin_ohos_log(ohos, WLOG_INFO, "OHOS microphone permission granted");
		return TRUE;
	}

	++g_permissionDeniedCount;
	audin_ohos_log(ohos, WLOG_WARN,
	               "OHOS microphone permission denied or timed out; audin capture open rejected");
	return FALSE;
}
