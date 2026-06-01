/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * MS-RDPECAM Implementation, HarmonyOS CameraKit backend entry
 */

#include "camera_ohos.h"

static UINT cam_ohos_enumerate(WINPR_ATTR_UNUSED ICamHal* ihal, ICamHalEnumCallback callback,
                               CameraPlugin* ecam, GENERIC_CHANNEL_CALLBACK* hchannel)
{
	UINT count = 0;
	CamOhosCameraList list;
	if (!cam_ohos_request_camera_permission("rdpecam device enumeration"))
		return 0;

	if (cam_ohos_open_camera_list(&list))
	{
		for (uint32_t index = 0; index < list.cameraCount; index++)
		{
			const Camera_Device* camera = &list.cameras[index];
			if (!camera->cameraId)
				continue;

			char name[OHOS_CAMERA_MAX_NAME] = { 0 };
			cam_ohos_camera_name(camera, name, sizeof(name));
			IFCALL(callback, ecam, hchannel, camera->cameraId, name);
			count++;
		}
		cam_ohos_close_camera_list(&list);
		if (count > 0)
			return count;
	}

	return 0;
}

static BOOL cam_ohos_activate(WINPR_ATTR_UNUSED ICamHal* ihal,
                              WINPR_ATTR_UNUSED const char* deviceId, CAM_ERROR_CODE* errorCode)
{
	*errorCode = CAM_ERROR_CODE_None;
	return TRUE;
}

static BOOL cam_ohos_deactivate(WINPR_ATTR_UNUSED ICamHal* ihal,
                                WINPR_ATTR_UNUSED const char* deviceId, CAM_ERROR_CODE* errorCode)
{
	*errorCode = CAM_ERROR_CODE_None;
	return TRUE;
}

static CAM_ERROR_CODE cam_ohos_free(ICamHal* ihal)
{
	CamOhosHal* hal = (CamOhosHal*)ihal;
	if (!hal)
		return CAM_ERROR_CODE_None;

	(void)cam_ohos_stop_stream(ihal, NULL, 0);
	if (hal->lockInitialized)
	{
		DeleteCriticalSection(&hal->lock);
		hal->lockInitialized = FALSE;
	}
	free(hal);
	return CAM_ERROR_CODE_None;
}

FREERDP_ENTRY_POINT(UINT VCAPITYPE
                       ohos_freerdp_rdpecam_client_subsystem_entry(
                           PFREERDP_CAMERA_HAL_ENTRY_POINTS pEntryPoints))
{
	if (!pEntryPoints || !pEntryPoints->pRegisterCameraHal)
		return ERROR_INVALID_PARAMETER;

	CamOhosHal* hal = (CamOhosHal*)calloc(1, sizeof(CamOhosHal));
	if (!hal)
		return CHANNEL_RC_NO_MEMORY;

	hal->iHal.Enumerate = cam_ohos_enumerate;
	hal->iHal.Activate = cam_ohos_activate;
	hal->iHal.Deactivate = cam_ohos_deactivate;
	hal->iHal.GetMediaTypeDescriptions = cam_ohos_get_media_type_descriptions;
	hal->iHal.StartStream = cam_ohos_start_stream;
	hal->iHal.StopStream = cam_ohos_stop_stream;
	hal->iHal.Free = cam_ohos_free;

	if (!InitializeCriticalSectionEx(&hal->lock, 0, 0))
	{
		free(hal);
		return CHANNEL_RC_NO_MEMORY;
	}
	hal->lockInitialized = TRUE;

	const UINT error = pEntryPoints->pRegisterCameraHal(pEntryPoints->plugin, &hal->iHal);
	if (error != CHANNEL_RC_OK)
	{
		WLog_ERR(TAG, "RegisterCameraHal failed with error %" PRIu32, error);
		(void)cam_ohos_free(&hal->iHal);
		return error;
	}

	WLog_INFO(TAG, "OHOS rdpecam CameraKit backend registered");
	return CHANNEL_RC_OK;
}
