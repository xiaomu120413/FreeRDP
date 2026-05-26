#ifndef FREERDP_CLIENT_OHOS_DISPLAY_H
#define FREERDP_CLIENT_OHOS_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#include <freerdp/api.h>
#include <freerdp/client/disp.h>
#include <winpr/wtypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct freerdp_ohos_display_control freerdpOhosDisplayControl;
typedef void (*FREERDP_OHOS_DISPLAY_LOG_CALLBACK)(const char* message, void* userData);

FREERDP_API void freerdp_ohos_display_normalize_size(uint32_t width, uint32_t height,
                                                     uint32_t alignment,
                                                     uint32_t* normalizedWidth,
                                                     uint32_t* normalizedHeight);
FREERDP_API int
freerdp_ohos_display_build_monitor_layout(uint32_t width, uint32_t height,
                                          DISPLAY_CONTROL_MONITOR_LAYOUT* layout);
FREERDP_API int freerdp_ohos_display_send_monitor_layout(
    DispClientContext* disp, uint32_t width, uint32_t height, uint32_t alignment,
    uint32_t* sentWidth, uint32_t* sentHeight, uint32_t* channelStatus, char* message,
    size_t messageSize);
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
FREERDP_API BOOL freerdp_ohos_display_control_request_resize(
    freerdpOhosDisplayControl* control, uint32_t width, uint32_t height, const char* reason,
    char* message, size_t messageSize);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_DISPLAY_H */
