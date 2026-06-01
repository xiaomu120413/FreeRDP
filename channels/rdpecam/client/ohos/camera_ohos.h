/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * MS-RDPECAM Implementation, HarmonyOS CameraKit backend
 */

#pragma once

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <freerdp/api.h>
#include <winpr/assert.h>
#include <winpr/synch.h>
#include <winpr/wlog.h>
#include <winpr/wtypes.h>

#include <ohcamera/camera.h>
#include <multimedia/image_framework/image/image_receiver_native.h>
#include <native_buffer/native_buffer.h>

#include "../camera.h"

#define TAG CHANNELS_TAG("rdpecam-ohos.client")

#define OHOS_CAMERA_FPS 30U
#define OHOS_CAMERA_PERMISSION_TIMEOUT_MS 60000U
#define OHOS_CAMERA_MAX_NAME 128U
#define OHOS_CAMERA_MAX_SURFACE_ID 32U
#define OHOS_CAMERA_MAX_PIXELS (1280U * 720U)
#define OHOS_IMAGE_COMPONENT_Y 1U
#define OHOS_IMAGE_COMPONENT_U 2U
#define OHOS_IMAGE_COMPONENT_V 3U

typedef BOOL (*pfnRdpecamOhosPermissionRequest)(void* userData, UINT32 timeoutMs);

typedef struct Camera_Input Camera_Input;
typedef struct Camera_PreviewOutput Camera_PreviewOutput;
typedef struct Camera_CaptureSession Camera_CaptureSession;

Camera_ErrorCode OH_CameraManager_GetSupportedCameras(Camera_Manager* cameraManager,
                                                      Camera_Device** cameras, uint32_t* size);
Camera_ErrorCode OH_CameraManager_DeleteSupportedCameras(Camera_Manager* cameraManager,
                                                        Camera_Device* cameras, uint32_t size);
Camera_ErrorCode OH_CameraManager_GetSupportedCameraOutputCapability(
    Camera_Manager* cameraManager, const Camera_Device* camera,
    Camera_OutputCapability** cameraOutputCapability);
Camera_ErrorCode OH_CameraManager_DeleteSupportedCameraOutputCapability(
    Camera_Manager* cameraManager, Camera_OutputCapability* cameraOutputCapability);
Camera_ErrorCode OH_CameraManager_CreateCameraInput(Camera_Manager* cameraManager,
                                                    const Camera_Device* camera,
                                                    Camera_Input** cameraInput);
Camera_ErrorCode OH_CameraManager_CreatePreviewOutput(Camera_Manager* cameraManager,
                                                      const Camera_Profile* profile,
                                                      const char* surfaceId,
                                                      Camera_PreviewOutput** previewOutput);
Camera_ErrorCode OH_CameraManager_CreateCaptureSession(Camera_Manager* cameraManager,
                                                       Camera_CaptureSession** captureSession);
Camera_ErrorCode OH_CameraInput_Open(Camera_Input* cameraInput);
Camera_ErrorCode OH_CameraInput_Close(Camera_Input* cameraInput);
Camera_ErrorCode OH_CameraInput_Release(Camera_Input* cameraInput);
Camera_ErrorCode OH_CaptureSession_BeginConfig(Camera_CaptureSession* session);
Camera_ErrorCode OH_CaptureSession_AddInput(Camera_CaptureSession* session,
                                            Camera_Input* cameraInput);
Camera_ErrorCode OH_CaptureSession_AddPreviewOutput(Camera_CaptureSession* session,
                                                    Camera_PreviewOutput* previewOutput);
Camera_ErrorCode OH_CaptureSession_CommitConfig(Camera_CaptureSession* session);
Camera_ErrorCode OH_CaptureSession_Start(Camera_CaptureSession* session);
Camera_ErrorCode OH_CaptureSession_Stop(Camera_CaptureSession* session);
Camera_ErrorCode OH_CaptureSession_Release(Camera_CaptureSession* session);
Camera_ErrorCode OH_PreviewOutput_Stop(Camera_PreviewOutput* previewOutput);
Camera_ErrorCode OH_PreviewOutput_Release(Camera_PreviewOutput* previewOutput);

typedef struct
{
	Camera_Manager* manager;
	Camera_Device* cameras;
	uint32_t cameraCount;
	Camera_Input* input;
	Camera_PreviewOutput* previewOutput;
	Camera_CaptureSession* session;
	OH_ImageReceiverNative* receiver;
	Camera_Profile profile;
	BOOL cameraOpen;
	BOOL sessionStarted;
	BYTE* frame;
	size_t frameSize;
	char surfaceId[OHOS_CAMERA_MAX_SURFACE_ID];
} CamOhosCapture;

typedef struct
{
	ICamHal iHal;
	CRITICAL_SECTION lock;
	BOOL lockInitialized;
	BOOL streaming;
	HANDLE captureThread;
	CameraDevice* dev;
	size_t streamIndex;
	ICamHalSampleCapturedCallback sampleCallback;
	CAM_MEDIA_TYPE_DESCRIPTION mediaType;
	CamOhosCapture capture;
} CamOhosHal;

typedef struct
{
	Camera_Manager* manager;
	Camera_Device* cameras;
	uint32_t cameraCount;
} CamOhosCameraList;

typedef struct
{
	OH_NativeBuffer* buffer;
	void* mapped;
	size_t bufferSize;
	int32_t rowStride;
	int32_t pixelStride;
	OH_NativeBuffer_Config config;
} CamOhosMappedPlane;

FREERDP_API BOOL freerdp_rdpecam_ohos_set_permission_callback(
    pfnRdpecamOhosPermissionRequest callback, void* userData);

BOOL cam_ohos_request_camera_permission(const char* reason);
BOOL cam_ohos_open_camera_list(CamOhosCameraList* list);
void cam_ohos_close_camera_list(CamOhosCameraList* list);
const Camera_Device* cam_ohos_find_camera(const CamOhosCameraList* list, const char* deviceId);
void cam_ohos_camera_name(const Camera_Device* camera, char* name, size_t nameSize);
BOOL cam_ohos_is_supported_preview_format(Camera_Format format);
BOOL cam_ohos_profile_matches_media_type(Camera_Format cameraFormat, CAM_MEDIA_FORMAT mediaFormat);
const char* cam_ohos_preview_format_name(Camera_Format format);
size_t cam_ohos_frame_size(const CAM_MEDIA_TYPE_DESCRIPTION* mediaType);
BOOL cam_ohos_read_latest_frame(CamOhosHal* hal);
CAM_ERROR_CODE cam_ohos_start_stream(ICamHal* ihal, CameraDevice* dev, size_t streamIndex,
                                     const CAM_MEDIA_TYPE_DESCRIPTION* mediaType,
                                     ICamHalSampleCapturedCallback callback);
CAM_ERROR_CODE cam_ohos_stop_stream(ICamHal* ihal, const char* deviceId, size_t streamIndex);
INT16 cam_ohos_get_media_type_descriptions(ICamHal* ihal, const char* deviceId,
                                           size_t streamIndex,
                                           const CAM_MEDIA_FORMAT_INFO* supportedFormats,
                                           size_t nSupportedFormats,
                                           CAM_MEDIA_TYPE_DESCRIPTION* mediaTypes,
                                           size_t* nMediaTypes);
