/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Output Virtual Channel - HarmonyOS playback queue
 */

#ifndef FREERDP_CHANNEL_RDPSND_CLIENT_OHOS_QUEUE_H
#define FREERDP_CHANNEL_RDPSND_CLIENT_OHOS_QUEUE_H

#include "rdpsnd_ohos_internal.h"

size_t rdpsnd_ohos_frame_bytes(const rdpsndOhosPlugin* ohos);
size_t rdpsnd_ohos_bytes_per_second(const rdpsndOhosPlugin* ohos);
BYTE rdpsnd_ohos_silence_byte(const rdpsndOhosPlugin* ohos);
UINT32 rdpsnd_ohos_queued_latency_locked(const rdpsndOhosPlugin* ohos);
void rdpsnd_ohos_clear_queue_locked(rdpsndOhosPlugin* ohos);
void rdpsnd_ohos_push_locked(rdpsndOhosPlugin* ohos, const BYTE* data, size_t size);
void rdpsnd_ohos_push_silence_locked(rdpsndOhosPlugin* ohos, UINT32 durationMs);
size_t rdpsnd_ohos_fill_audio_buffer(rdpsndOhosPlugin* ohos, void* audioData,
                                     int32_t audioDataSize, BOOL fillSilence);
BOOL rdpsnd_ohos_allocate_queue(rdpsndOhosPlugin* ohos, UINT32 latency);

#endif /* FREERDP_CHANNEL_RDPSND_CLIENT_OHOS_QUEUE_H */
