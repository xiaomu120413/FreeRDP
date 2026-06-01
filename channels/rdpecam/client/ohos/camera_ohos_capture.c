/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * MS-RDPECAM Implementation, HarmonyOS capture lifecycle
 */

#include "camera_ohos.h"

static void cam_ohos_release_capture(CamOhosHal* hal)
{
	CamOhosCapture* capture = &hal->capture;

	if (capture->sessionStarted && capture->session)
	{
		(void)OH_CaptureSession_Stop(capture->session);
		capture->sessionStarted = FALSE;
	}
	if (capture->session)
		(void)OH_CaptureSession_Release(capture->session);
	if (capture->previewOutput)
	{
		(void)OH_PreviewOutput_Stop(capture->previewOutput);
		(void)OH_PreviewOutput_Release(capture->previewOutput);
	}
	if (capture->cameraOpen && capture->input)
		(void)OH_CameraInput_Close(capture->input);
	if (capture->input)
		(void)OH_CameraInput_Release(capture->input);
	if (capture->receiver)
		(void)OH_ImageReceiverNative_Release(capture->receiver);
	free(capture->frame);
	if (capture->manager && capture->cameras)
	{
		(void)OH_CameraManager_DeleteSupportedCameras(capture->manager, capture->cameras,
		                                              capture->cameraCount);
	}
	if (capture->manager)
		(void)OH_Camera_DeleteCameraManager(capture->manager);
	memset(capture, 0, sizeof(*capture));
}

static BOOL cam_ohos_find_matching_profile(Camera_Manager* manager, const Camera_Device* camera,
                                           const CAM_MEDIA_TYPE_DESCRIPTION* mediaType,
                                           Camera_Profile* selectedProfile)
{
	Camera_OutputCapability* capability = NULL;
	if (OH_CameraManager_GetSupportedCameraOutputCapability(manager, camera, &capability) !=
	        CAMERA_OK ||
	    !capability)
		return FALSE;

	BOOL found = FALSE;
	for (uint32_t index = 0; index < capability->previewProfilesSize; index++)
	{
		const Camera_Profile* profile = capability->previewProfiles[index];
		if (!profile || !cam_ohos_profile_matches_media_type(profile->format, mediaType->Format))
			continue;
		if ((profile->size.width != mediaType->Width) ||
		    (profile->size.height != mediaType->Height))
			continue;

		*selectedProfile = *profile;
		found = TRUE;
		break;
	}

	(void)OH_CameraManager_DeleteSupportedCameraOutputCapability(manager, capability);
	return found;
}

static CAM_ERROR_CODE cam_ohos_create_receiver(CamOhosHal* hal,
                                               const CAM_MEDIA_TYPE_DESCRIPTION* mediaType)
{
	OH_ImageReceiverOptions* options = NULL;
	if (OH_ImageReceiverOptions_Create(&options) != IMAGE_SUCCESS || !options)
		return CAM_ERROR_CODE_UnexpectedError;

	const Image_Size size = { mediaType->Width, mediaType->Height };
	Image_ErrorCode imageError = OH_ImageReceiverOptions_SetSize(options, size);
	if (imageError == IMAGE_SUCCESS)
		imageError = OH_ImageReceiverOptions_SetCapacity(options, 4);
	if (imageError == IMAGE_SUCCESS)
		imageError = OH_ImageReceiverNative_Create(options, &hal->capture.receiver);
	(void)OH_ImageReceiverOptions_Release(options);

	if (imageError != IMAGE_SUCCESS || !hal->capture.receiver)
		return CAM_ERROR_CODE_UnexpectedError;

	uint64_t surfaceId = 0;
	if (OH_ImageReceiverNative_GetReceivingSurfaceId(hal->capture.receiver, &surfaceId) !=
	    IMAGE_SUCCESS)
		return CAM_ERROR_CODE_UnexpectedError;

	snprintf(hal->capture.surfaceId, sizeof(hal->capture.surfaceId), "%" PRIu64, surfaceId);
	return CAM_ERROR_CODE_None;
}

static CAM_ERROR_CODE cam_ohos_open_capture(CamOhosHal* hal, CameraDevice* dev,
                                            const CAM_MEDIA_TYPE_DESCRIPTION* mediaType)
{
	CamOhosCapture* capture = &hal->capture;

	Camera_ErrorCode cameraError = OH_Camera_GetCameraManager(&capture->manager);
	if (cameraError != CAMERA_OK || !capture->manager)
		return CAM_ERROR_CODE_NotInitialized;

	cameraError = OH_CameraManager_GetSupportedCameras(capture->manager, &capture->cameras,
	                                                  &capture->cameraCount);
	if (cameraError != CAMERA_OK || !capture->cameras || capture->cameraCount == 0)
		return CAM_ERROR_CODE_ItemNotFound;

	const CamOhosCameraList list = { capture->manager, capture->cameras, capture->cameraCount };
	const Camera_Device* camera = cam_ohos_find_camera(&list, dev->deviceId);
	if (!camera)
		return CAM_ERROR_CODE_ItemNotFound;

	if (!cam_ohos_find_matching_profile(capture->manager, camera, mediaType, &capture->profile))
		return CAM_ERROR_CODE_InvalidMediaType;

	CAM_ERROR_CODE error = cam_ohos_create_receiver(hal, mediaType);
	if (error != CAM_ERROR_CODE_None)
		return error;

	cameraError = OH_CameraManager_CreateCameraInput(capture->manager, camera, &capture->input);
	if (cameraError != CAMERA_OK || !capture->input)
		return CAM_ERROR_CODE_UnexpectedError;

	cameraError = OH_CameraInput_Open(capture->input);
	if (cameraError != CAMERA_OK)
		return CAM_ERROR_CODE_OperationNotSupported;
	capture->cameraOpen = TRUE;

	cameraError = OH_CameraManager_CreatePreviewOutput(
	    capture->manager, &capture->profile, capture->surfaceId, &capture->previewOutput);
	if (cameraError != CAMERA_OK || !capture->previewOutput)
		return CAM_ERROR_CODE_UnexpectedError;

	cameraError = OH_CameraManager_CreateCaptureSession(capture->manager, &capture->session);
	if (cameraError != CAMERA_OK || !capture->session)
		return CAM_ERROR_CODE_UnexpectedError;

	if ((OH_CaptureSession_BeginConfig(capture->session) != CAMERA_OK) ||
	    (OH_CaptureSession_AddInput(capture->session, capture->input) != CAMERA_OK) ||
	    (OH_CaptureSession_AddPreviewOutput(capture->session, capture->previewOutput) !=
	     CAMERA_OK) ||
	    (OH_CaptureSession_CommitConfig(capture->session) != CAMERA_OK))
		return CAM_ERROR_CODE_UnexpectedError;

	cameraError = OH_CaptureSession_Start(capture->session);
	if (cameraError != CAMERA_OK)
		return CAM_ERROR_CODE_OperationNotSupported;
	capture->sessionStarted = TRUE;

	WLog_INFO(TAG, "OHOS rdpecam using %s capture profile %" PRIu32 "x%" PRIu32,
	          cam_ohos_preview_format_name(capture->profile.format), capture->profile.size.width,
	          capture->profile.size.height);
	return CAM_ERROR_CODE_None;
}

static DWORD WINAPI cam_ohos_capture_thread(LPVOID param)
{
	CamOhosHal* hal = (CamOhosHal*)param;
	WINPR_ASSERT(hal);

	const DWORD frameDelayMs = 1000U / OHOS_CAMERA_FPS;

	while (hal->streaming)
	{
		Sleep(frameDelayMs);

		EnterCriticalSection(&hal->lock);
		const BOOL streaming = hal->streaming;
		CameraDevice* dev = hal->dev;
		const size_t streamIndex = hal->streamIndex;
		ICamHalSampleCapturedCallback callback = hal->sampleCallback;

		if (streaming && dev && callback && hal->capture.frame && cam_ohos_read_latest_frame(hal))
		{
			const UINT error =
			    callback(dev, streamIndex, hal->capture.frame, hal->capture.frameSize);
			if (error != CHANNEL_RC_OK)
				WLog_ERR(TAG, "sample callback failed with error %" PRIu32, error);
		}
		LeaveCriticalSection(&hal->lock);
	}

	return CHANNEL_RC_OK;
}

CAM_ERROR_CODE cam_ohos_stop_stream(ICamHal* ihal, WINPR_ATTR_UNUSED const char* deviceId,
                                    WINPR_ATTR_UNUSED size_t streamIndex)
{
	CamOhosHal* hal = (CamOhosHal*)ihal;
	if (!hal)
		return CAM_ERROR_CODE_NotInitialized;

	if (hal->streaming)
		hal->streaming = FALSE;

	if (hal->captureThread)
	{
		(void)WaitForSingleObject(hal->captureThread, INFINITE);
		(void)CloseHandle(hal->captureThread);
		hal->captureThread = NULL;
	}

	EnterCriticalSection(&hal->lock);
	hal->dev = NULL;
	hal->sampleCallback = NULL;
	LeaveCriticalSection(&hal->lock);
	cam_ohos_release_capture(hal);

	return CAM_ERROR_CODE_None;
}

CAM_ERROR_CODE cam_ohos_start_stream(ICamHal* ihal, CameraDevice* dev, size_t streamIndex,
                                     const CAM_MEDIA_TYPE_DESCRIPTION* mediaType,
                                     ICamHalSampleCapturedCallback callback)
{
	CamOhosHal* hal = (CamOhosHal*)ihal;
	if (!hal || !dev || !mediaType || !callback)
		return CAM_ERROR_CODE_UnexpectedError;

	if ((mediaType->Format != CAM_MEDIA_FORMAT_RGB32) && (mediaType->Format != CAM_MEDIA_FORMAT_NV12))
		return CAM_ERROR_CODE_InvalidMediaType;
	if (hal->streaming)
		return CAM_ERROR_CODE_UnexpectedError;
	if (!cam_ohos_request_camera_permission("rdpecam stream start"))
		return CAM_ERROR_CODE_OperationNotSupported;

	const size_t frameSize = cam_ohos_frame_size(mediaType);
	if (frameSize == 0)
		return CAM_ERROR_CODE_InvalidMediaType;

	BYTE* frame = (BYTE*)calloc(1, frameSize);
	if (!frame)
		return CAM_ERROR_CODE_OutOfMemory;

	hal->capture.frame = frame;
	hal->capture.frameSize = frameSize;
	CAM_ERROR_CODE captureError = cam_ohos_open_capture(hal, dev, mediaType);
	if (captureError != CAM_ERROR_CODE_None)
	{
		cam_ohos_release_capture(hal);
		return captureError;
	}

	EnterCriticalSection(&hal->lock);
	hal->mediaType = *mediaType;
	hal->dev = dev;
	hal->streamIndex = streamIndex;
	hal->sampleCallback = callback;
	hal->streaming = TRUE;
	LeaveCriticalSection(&hal->lock);

	hal->captureThread = CreateThread(NULL, 0, cam_ohos_capture_thread, hal, 0, NULL);
	if (!hal->captureThread)
	{
		(void)cam_ohos_stop_stream(ihal, dev->deviceId, streamIndex);
		return CAM_ERROR_CODE_UnexpectedError;
	}

	WLog_INFO(TAG, "OHOS rdpecam ImageReceiver stream started: %ux%u@%u format=%u",
	          mediaType->Width, mediaType->Height, OHOS_CAMERA_FPS, mediaType->Format);
	return CAM_ERROR_CODE_None;
}
