#ifndef FREERDP_CLIENT_OHOS_DISPLAY_H
#define FREERDP_CLIENT_OHOS_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#include <freerdp/api.h>
#include <freerdp/client/disp.h>

#ifdef __cplusplus
extern "C"
{
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_DISPLAY_H */
