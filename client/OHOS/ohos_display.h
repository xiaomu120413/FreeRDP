#ifndef FREERDP_CLIENT_OHOS_DISPLAY_H
#define FREERDP_CLIENT_OHOS_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#include <freerdp/api.h>
#include <freerdp/client/disp.h>
#include <freerdp/settings.h>
#include <winpr/wtypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct freerdp_ohos_display_control freerdpOhosDisplayControl;
typedef void (*FREERDP_OHOS_DISPLAY_LOG_CALLBACK)(const char* message, void* userData);

#define FREERDP_OHOS_MONITOR_LAYOUT_VERSION 1U
#define FREERDP_OHOS_MAX_MONITORS 16U

typedef struct
{
	uint32_t structSize;
	uint32_t version;
	int32_t left;
	int32_t top;
	uint32_t width;
	uint32_t height;
	uint32_t physicalWidth;
	uint32_t physicalHeight;
	uint32_t orientation;
	uint32_t desktopScaleFactor;
	uint32_t deviceScaleFactor;
	BOOL primary;
} FREERDP_OHOS_MONITOR_LAYOUT;

typedef struct
{
	uint32_t structSize;
	uint32_t version;
	uint32_t monitorCount;
	const FREERDP_OHOS_MONITOR_LAYOUT* monitors;
} FREERDP_OHOS_MONITOR_LAYOUT_REQUEST;

typedef enum
{
	FREERDP_OHOS_DISPLAY_RESIZE_FAILED = 0,
	FREERDP_OHOS_DISPLAY_RESIZE_SENT = 1,
	FREERDP_OHOS_DISPLAY_RESIZE_DEFERRED = 2,
	FREERDP_OHOS_DISPLAY_RESIZE_UNCHANGED = 3
} FREERDP_OHOS_DISPLAY_RESIZE_STATUS;

typedef struct
{
	FREERDP_OHOS_DISPLAY_RESIZE_STATUS status;
	uint32_t normalizedWidth;
	uint32_t normalizedHeight;
	uint32_t sentWidth;
	uint32_t sentHeight;
	uint32_t orientation;
} FREERDP_OHOS_DISPLAY_RESIZE_RESULT;

FREERDP_API void freerdp_ohos_display_normalize_size(uint32_t width, uint32_t height,
                                                     uint32_t alignment,
                                                     uint32_t* normalizedWidth,
                                                     uint32_t* normalizedHeight);
FREERDP_API int freerdp_ohos_display_build_monitor_layout_ex(
    uint32_t width, uint32_t height, uint32_t orientation,
    DISPLAY_CONTROL_MONITOR_LAYOUT* layout);
FREERDP_API BOOL freerdp_ohos_display_validate_monitor_layout(
    const FREERDP_OHOS_MONITOR_LAYOUT_REQUEST* request,
    DISPLAY_CONTROL_MONITOR_LAYOUT* layouts, char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_display_apply_monitor_settings(
    rdpSettings* settings, const FREERDP_OHOS_MONITOR_LAYOUT_REQUEST* request,
    char* message, size_t messageSize);
FREERDP_API freerdpOhosDisplayControl* freerdp_ohos_display_control_new(void);
FREERDP_API void freerdp_ohos_display_control_free(freerdpOhosDisplayControl* control);
FREERDP_API void freerdp_ohos_display_control_reset(freerdpOhosDisplayControl* control);
FREERDP_API void freerdp_ohos_display_control_set_log_callback(
    freerdpOhosDisplayControl* control, FREERDP_OHOS_DISPLAY_LOG_CALLBACK callback,
    void* userData);
FREERDP_API void freerdp_ohos_display_control_set_alignment(
    freerdpOhosDisplayControl* control, uint32_t alignment);
FREERDP_API BOOL freerdp_ohos_display_control_attach(
    freerdpOhosDisplayControl* control, DispClientContext* disp, char* message,
    size_t messageSize);
FREERDP_API void freerdp_ohos_display_control_detach(
    freerdpOhosDisplayControl* control, DispClientContext* disp);
FREERDP_API BOOL freerdp_ohos_display_control_request_resize_ex(
    freerdpOhosDisplayControl* control, uint32_t width, uint32_t height,
    uint32_t orientation, const char* reason, FREERDP_OHOS_DISPLAY_RESIZE_RESULT* result,
    char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_display_control_request_monitor_layout(
    freerdpOhosDisplayControl* control, const FREERDP_OHOS_MONITOR_LAYOUT_REQUEST* request,
    const char* reason, char* message, size_t messageSize);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_DISPLAY_H */
