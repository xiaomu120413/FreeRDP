/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * MS-RDPECAM Implementation, HarmonyOS media discovery
 */

#include "camera_ohos.h"

BOOL cam_ohos_open_camera_list(CamOhosCameraList* list)
{
	WINPR_ASSERT(list);
	memset(list, 0, sizeof(*list));

	const Camera_ErrorCode managerError = OH_Camera_GetCameraManager(&list->manager);
	if ((managerError != CAMERA_OK) || !list->manager)
	{
		WLog_WARN(TAG, "OH_Camera_GetCameraManager failed with error %d", (int)managerError);
		return FALSE;
	}

	const Camera_ErrorCode camerasError =
	    OH_CameraManager_GetSupportedCameras(list->manager, &list->cameras, &list->cameraCount);
	if ((camerasError != CAMERA_OK) || !list->cameras || (list->cameraCount == 0))
	{
		WLog_WARN(TAG, "OH_CameraManager_GetSupportedCameras failed or returned no cameras: error=%d count=%" PRIu32,
		          (int)camerasError, list->cameraCount);
		if (list->manager)
			(void)OH_Camera_DeleteCameraManager(list->manager);
		memset(list, 0, sizeof(*list));
		return FALSE;
	}

	return TRUE;
}

void cam_ohos_close_camera_list(CamOhosCameraList* list)
{
	if (!list)
		return;

	if (list->manager && list->cameras)
	{
		(void)OH_CameraManager_DeleteSupportedCameras(list->manager, list->cameras,
		                                              list->cameraCount);
	}
	if (list->manager)
		(void)OH_Camera_DeleteCameraManager(list->manager);
	memset(list, 0, sizeof(*list));
}

const Camera_Device* cam_ohos_find_camera(const CamOhosCameraList* list, const char* deviceId)
{
	if (!list || !deviceId)
		return NULL;

	for (uint32_t index = 0; index < list->cameraCount; index++)
	{
		const Camera_Device* camera = &list->cameras[index];
		if (camera->cameraId && (strcmp(camera->cameraId, deviceId) == 0))
			return camera;
	}
	return NULL;
}

static const char* cam_ohos_position_name(Camera_Position position)
{
	switch (position)
	{
		case CAMERA_POSITION_BACK:
			return "Back";
		case CAMERA_POSITION_FRONT:
			return "Front";
		default:
			return "Unspecified";
	}
}

static const char* cam_ohos_type_name(Camera_Type type)
{
	switch (type)
	{
		case CAMERA_TYPE_WIDE_ANGLE:
			return "wide";
		case CAMERA_TYPE_ULTRA_WIDE:
			return "ultra wide";
		case CAMERA_TYPE_TELEPHOTO:
			return "telephoto";
		case CAMERA_TYPE_TRUE_DEPTH:
			return "true depth";
		default:
			return "default";
	}
}

void cam_ohos_camera_name(const Camera_Device* camera, char* name, size_t nameSize)
{
	WINPR_ASSERT(name);
	if (!camera || (nameSize == 0))
		return;

	snprintf(name, nameSize, "HarmonyOS %s camera (%s)", cam_ohos_position_name(camera->cameraPosition),
	         cam_ohos_type_name(camera->cameraType));
	name[nameSize - 1] = '\0';
}

BOOL cam_ohos_is_supported_preview_format(Camera_Format format)
{
	return (format == CAMERA_FORMAT_RGBA_8888) || (format == CAMERA_FORMAT_YUV_420_SP);
}

BOOL cam_ohos_profile_matches_media_type(Camera_Format cameraFormat, CAM_MEDIA_FORMAT mediaFormat)
{
	switch (mediaFormat)
	{
		case CAM_MEDIA_FORMAT_RGB32:
			return cameraFormat == CAMERA_FORMAT_RGBA_8888;
		case CAM_MEDIA_FORMAT_NV12:
			return cameraFormat == CAMERA_FORMAT_YUV_420_SP;
		default:
			return FALSE;
	}
}

const char* cam_ohos_preview_format_name(Camera_Format format)
{
	switch (format)
	{
		case CAMERA_FORMAT_RGBA_8888:
			return "RGBA_8888";
		case CAMERA_FORMAT_YUV_420_SP:
			return "YUV_420_SP";
		default:
			return "unsupported";
	}
}

size_t cam_ohos_frame_size(const CAM_MEDIA_TYPE_DESCRIPTION* mediaType)
{
	WINPR_ASSERT(mediaType);

	switch (mediaType->Format)
	{
		case CAM_MEDIA_FORMAT_RGB32:
			return (size_t)mediaType->Width * (size_t)mediaType->Height * 4U;
		case CAM_MEDIA_FORMAT_NV12:
			return (size_t)mediaType->Width * (size_t)mediaType->Height * 3U / 2U;
		default:
			return 0;
	}
}

INT16 cam_ohos_get_media_type_descriptions(
    WINPR_ATTR_UNUSED ICamHal* ihal, WINPR_ATTR_UNUSED const char* deviceId,
    WINPR_ATTR_UNUSED size_t streamIndex, const CAM_MEDIA_FORMAT_INFO* supportedFormats,
    size_t nSupportedFormats, CAM_MEDIA_TYPE_DESCRIPTION* mediaTypes, size_t* nMediaTypes)
{
	if (!supportedFormats || !mediaTypes || !nMediaTypes)
		return -1;

	for (size_t formatIndex = 0; formatIndex < nSupportedFormats; formatIndex++)
	{
		const CAM_MEDIA_FORMAT inputFormat = supportedFormats[formatIndex].inputFormat;
		if ((inputFormat != CAM_MEDIA_FORMAT_NV12) && (inputFormat != CAM_MEDIA_FORMAT_RGB32))
			continue;

		CamOhosCameraList list;
		size_t written = 0;
		if (cam_ohos_open_camera_list(&list))
		{
			const Camera_Device* camera = cam_ohos_find_camera(&list, deviceId);
			Camera_OutputCapability* capability = NULL;
			if (!camera)
			{
				WLog_WARN(TAG, "OHOS rdpecam camera id '%s' was not found",
				          deviceId ? deviceId : "");
			}
			else if ((OH_CameraManager_GetSupportedCameraOutputCapability(
			              list.manager, camera, &capability) == CAMERA_OK) &&
			         capability)
			{
				const size_t capacity = *nMediaTypes;
				for (uint32_t profileIndex = 0;
				     (profileIndex < capability->previewProfilesSize) && (written < capacity);
				     profileIndex++)
				{
					const Camera_Profile* profile = capability->previewProfiles[profileIndex];
					if (!profile || !cam_ohos_is_supported_preview_format(profile->format))
						continue;
					if (!cam_ohos_profile_matches_media_type(profile->format, inputFormat))
						continue;
					if ((profile->size.width * profile->size.height) > OHOS_CAMERA_MAX_PIXELS)
						continue;

					mediaTypes[written].Format = inputFormat;
					mediaTypes[written].Width = profile->size.width;
					mediaTypes[written].Height = profile->size.height;
					mediaTypes[written].FrameRateNumerator = OHOS_CAMERA_FPS;
					mediaTypes[written].FrameRateDenominator = 1;
					mediaTypes[written].PixelAspectRatioNumerator = 1;
					mediaTypes[written].PixelAspectRatioDenominator = 1;
					WLog_INFO(TAG, "OHOS rdpecam exposing %s profile %" PRIu32 "x%" PRIu32,
					          cam_ohos_preview_format_name(profile->format),
					          profile->size.width, profile->size.height);
					written++;
				}
				(void)OH_CameraManager_DeleteSupportedCameraOutputCapability(list.manager,
				                                                             capability);
			}
			cam_ohos_close_camera_list(&list);
		}

		if (written > 0)
		{
			*nMediaTypes = written;
			return (INT16)formatIndex;
		}

		*nMediaTypes = 0;
		WLog_WARN(TAG, "OHOS rdpecam found no compatible RGBA_8888/YUV_420_SP preview profiles for camera '%s'",
		          deviceId ? deviceId : "");
		return -1;
	}

	*nMediaTypes = 0;
	return -1;
}
