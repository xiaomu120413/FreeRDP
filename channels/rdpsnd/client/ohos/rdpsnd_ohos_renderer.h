/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Output Virtual Channel - HarmonyOS renderer lifecycle
 */

#ifndef FREERDP_CHANNEL_RDPSND_CLIENT_OHOS_RENDERER_H
#define FREERDP_CHANNEL_RDPSND_CLIENT_OHOS_RENDERER_H

#include "rdpsnd_ohos_internal.h"

BOOL rdpsnd_ohos_set_volume(rdpsndDevicePlugin* device, UINT32 value);
UINT32 rdpsnd_ohos_get_volume(rdpsndDevicePlugin* device);
BOOL rdpsnd_ohos_open(rdpsndDevicePlugin* device, const AUDIO_FORMAT* format, UINT32 latency);
void rdpsnd_ohos_release_renderer(rdpsndOhosPlugin* ohos);

#endif /* FREERDP_CHANNEL_RDPSND_CLIENT_OHOS_RENDERER_H */
