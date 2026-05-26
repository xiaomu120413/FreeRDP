/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Input Redirection Virtual Channel - HarmonyOS format negotiation
 */

#ifndef FREERDP_CHANNEL_AUDIN_CLIENT_OHOS_FORMAT_H
#define FREERDP_CHANNEL_AUDIN_CLIENT_OHOS_FORMAT_H

#include "audin_ohos_internal.h"

BOOL audin_ohos_format_supported(IAudinDevice* device, const AUDIO_FORMAT* format);
UINT audin_ohos_set_format(IAudinDevice* device, const AUDIO_FORMAT* format,
                           UINT32 framesPerPacket);
OH_AudioStream_SampleFormat audin_ohos_sample_format(UINT16 bitsPerSample);
UINT32 audin_ohos_pcm16_peak(const void* buffer, int32_t length);

#endif /* FREERDP_CHANNEL_AUDIN_CLIENT_OHOS_FORMAT_H */
