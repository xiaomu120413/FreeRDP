/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Output Virtual Channel - HarmonyOS format negotiation
 */

#ifndef FREERDP_CHANNEL_RDPSND_CLIENT_OHOS_FORMAT_H
#define FREERDP_CHANNEL_RDPSND_CLIENT_OHOS_FORMAT_H

#include "rdpsnd_ohos_internal.h"

OH_AudioStream_SampleFormat rdpsnd_ohos_sample_format(UINT16 bitsPerSample);
UINT32 rdpsnd_ohos_peak_sample(const rdpsndOhosPlugin* ohos, const BYTE* data, size_t size);
BOOL rdpsnd_ohos_format_supported(rdpsndDevicePlugin* device, const AUDIO_FORMAT* format);
BOOL rdpsnd_ohos_default_format(rdpsndDevicePlugin* device, const AUDIO_FORMAT* sourceFormat,
                                AUDIO_FORMAT* defaultFormat);
UINT rdpsnd_ohos_server_format_announce(rdpsndDevicePlugin* device, const AUDIO_FORMAT* formats,
                                        size_t count);

#endif /* FREERDP_CHANNEL_RDPSND_CLIENT_OHOS_FORMAT_H */
