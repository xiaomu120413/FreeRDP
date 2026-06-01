/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * MS-RDPECAM Implementation, HarmonyOS permission bridge
 */

#include "camera_ohos.h"

static pthread_mutex_t g_permissionCallbackLock = PTHREAD_MUTEX_INITIALIZER;
static pfnRdpecamOhosPermissionRequest g_permissionRequest = NULL;
static void* g_permissionRequestUserData = NULL;

FREERDP_API BOOL freerdp_rdpecam_ohos_set_permission_callback(
    pfnRdpecamOhosPermissionRequest callback, void* userData)
{
	pthread_mutex_lock(&g_permissionCallbackLock);
	g_permissionRequest = callback;
	g_permissionRequestUserData = userData;
	pthread_mutex_unlock(&g_permissionCallbackLock);
	return TRUE;
}

BOOL cam_ohos_request_camera_permission(const char* reason)
{
	pfnRdpecamOhosPermissionRequest callback = NULL;
	void* userData = NULL;

	pthread_mutex_lock(&g_permissionCallbackLock);
	callback = g_permissionRequest;
	userData = g_permissionRequestUserData;
	pthread_mutex_unlock(&g_permissionCallbackLock);

	if (!callback)
	{
		WLog_DBG(TAG, "camera permission callback is not registered for %s; relying on OHOS camera APIs",
		         reason ? reason : "rdpecam");
		return TRUE;
	}

	WLog_INFO(TAG, "requesting OHOS camera permission for %s", reason ? reason : "rdpecam");
	if (callback(userData, OHOS_CAMERA_PERMISSION_TIMEOUT_MS))
	{
		WLog_INFO(TAG, "OHOS camera permission granted");
		return TRUE;
	}

	WLog_WARN(TAG, "OHOS camera permission denied or timed out; rdpecam stream rejected");
	return FALSE;
}
