#ifndef FREERDP_CODEC_H264_OHOS_AVCODEC_INTERNAL_H
#define FREERDP_CODEC_H264_OHOS_AVCODEC_INTERNAL_H

#include <freerdp/config.h>

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <winpr/crt.h>
#include <winpr/interlocked.h>
#include <winpr/wlog.h>

#include <freerdp/api.h>
#include <freerdp/log.h>

#include "h264.h"

#include <multimedia/player_framework/native_avbuffer.h>
#include <multimedia/player_framework/native_avbuffer_info.h>
#include <multimedia/player_framework/native_avcapability.h>
#include <multimedia/player_framework/native_avcodec_base.h>
#include <multimedia/player_framework/native_avcodec_videoencoder.h>
#include <multimedia/player_framework/native_avcodec_videodecoder.h>
#include <multimedia/player_framework/native_avformat.h>

#define TAG FREERDP_TAG("codec.ohos-avcodec")

#define OHOS_AVCODEC_INPUT_QUEUE_LENGTH 32
#define OHOS_AVCODEC_OUTPUT_QUEUE_LENGTH 32
#define OHOS_AVCODEC_INPUT_WAIT_MS 50
#define OHOS_AVCODEC_OUTPUT_WAIT_MS 250
#define OHOS_AVCODEC_DECODER_OUTPUT_WAIT_MS 50
#define OHOS_AVCODEC_DECODER_INPUT_TIMEOUT_US 8000
#define OHOS_AVCODEC_DECODER_OUTPUT_TIMEOUT_US 8000
#define OHOS_AVCODEC_DECODER_OUTPUT_FOLLOWUP_TIMEOUT_US 4000
#define OHOS_AVCODEC_DECODER_OUTPUT_DEADLINE_US 20000
#define OHOS_AVCODEC_DECODER_OUTPUT_MAX_ATTEMPTS 5

typedef struct
{
	uint32_t index;
	OH_AVBuffer* buffer;
} OHOS_AVCODEC_INPUT_SLOT;

typedef struct
{
	BYTE* data;
	UINT32 size;
	uint32_t flags;
	int64_t pts;
	int32_t pixelFormat;
	UINT32 width;
	UINT32 height;
	UINT32 stride;
	UINT32 sliceHeight;
} OHOS_AVCODEC_OUTPUT_SLOT;

typedef struct
{
	OH_AVCodec* decoder;
	OH_AVCodec* encoder;
	BOOL started;
	BOOL decoderBufferMode;
	BOOL encoderMode;
	BOOL primitivesReady;
	UINT32 width;
	UINT32 height;
	UINT64 decodeCalls;
	UINT64 encodeCalls;
	UINT64 decodedFrames;
	UINT64 encodedFrames;
	UINT64 noOutputFrames;
	UINT64 failedFrames;
	UINT64 inputWaitTimeouts;
	UINT64 outputWaitTimeouts;
	UINT64 droppedOutputFrames;
	UINT64 decoderRefreshes;
	int32_t decoderPixelFormat;
	UINT32 outputWidth;
	UINT32 outputHeight;
	UINT32 outputStride;
	UINT32 outputSliceHeight;
	BOOL inputQueueFullLogged;
	BOOL outputQueueFullLogged;
	BOOL prependCodecConfig;
	BOOL inputFrameLayoutLogged;
	BOOL bitstreamLayoutLogged;
	BOOL bitstreamConversionWarningLogged;
	int32_t asyncError;
	int32_t encoderPixelFormat;
	wLog* log;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	OHOS_AVCODEC_INPUT_SLOT inputQueue[OHOS_AVCODEC_INPUT_QUEUE_LENGTH];
	UINT32 inputHead;
	UINT32 inputTail;
	UINT32 inputCount;
	OHOS_AVCODEC_OUTPUT_SLOT outputQueue[OHOS_AVCODEC_OUTPUT_QUEUE_LENGTH];
	UINT32 outputHead;
	UINT32 outputTail;
	UINT32 outputCount;
	BYTE* codecConfig;
	UINT32 codecConfigSize;
	UINT32 codecConfigCapacity;
	UINT32 nalLengthSize;
	BYTE* encodedData;
	UINT32 encodedSize;
	UINT32 encodedCapacity;
} H264_CONTEXT_OHOS_AVCODEC;

typedef struct
{
	UINT64 decoderAttempts;
	UINT64 decoderActive;
	UINT64 decodeCalls;
	UINT64 decodedFrames;
	UINT64 noOutputFrames;
	UINT64 failedFrames;
	UINT64 inputWaitTimeouts;
	UINT64 droppedOutputFrames;
	UINT64 fallbackRequests;
	UINT64 decoderRefreshes;
	UINT32 lastWidth;
	UINT32 lastHeight;
	int32_t lastAsyncError;
	char lastFallbackReason[160];
} OHOS_AVCODEC_DIAGNOSTICS;

FREERDP_API BOOL freerdp_ohos_avcodec_set_fallback_callback(
    pfnH264OhosAvcodecFallbackCallback callback, void* userData);
FREERDP_API const char* freerdp_ohos_avcodec_get_diagnostics(void);

FREERDP_LOCAL void ohos_avcodec_record_decoder_attempt(void);
FREERDP_LOCAL void ohos_avcodec_record_decoder_active(const H264_CONTEXT_OHOS_AVCODEC* sys);
FREERDP_LOCAL void ohos_avcodec_record_progress(const H264_CONTEXT_OHOS_AVCODEC* sys);
FREERDP_LOCAL void ohos_avcodec_record_fallback(const char* reason);

FREERDP_LOCAL int ohos_avcodec_request_software_fallback(H264_CONTEXT* h264,
                                                         H264_CONTEXT_OHOS_AVCODEC* sys,
                                                         const char* reason);

FREERDP_LOCAL void ohos_avcodec_make_deadline(struct timespec* deadline, UINT32 timeoutMs);
FREERDP_LOCAL UINT64 ohos_avcodec_now_ns(void);
FREERDP_LOCAL BOOL ohos_avcodec_pop_input(H264_CONTEXT_OHOS_AVCODEC* sys, uint32_t* index,
                                          OH_AVBuffer** buffer);
FREERDP_LOCAL BOOL ohos_avcodec_wait_for_input(H264_CONTEXT_OHOS_AVCODEC* sys, uint32_t* index,
                                               OH_AVBuffer** buffer);
FREERDP_LOCAL BOOL ohos_avcodec_pop_output(H264_CONTEXT_OHOS_AVCODEC* sys,
                                           OHOS_AVCODEC_OUTPUT_SLOT* slot);
FREERDP_LOCAL BOOL ohos_avcodec_wait_for_output(H264_CONTEXT_OHOS_AVCODEC* sys,
                                                OHOS_AVCODEC_OUTPUT_SLOT* slot);
FREERDP_LOCAL void ohos_avcodec_clear_output_queue(H264_CONTEXT_OHOS_AVCODEC* sys);
FREERDP_LOCAL void ohos_avcodec_on_error(OH_AVCodec* codec, int32_t errorCode, void* userData);
FREERDP_LOCAL void ohos_avcodec_on_stream_changed(OH_AVCodec* codec, OH_AVFormat* format,
                                                  void* userData);
FREERDP_LOCAL void ohos_avcodec_on_need_input_buffer(OH_AVCodec* codec, uint32_t index,
                                                     OH_AVBuffer* buffer, void* userData);
FREERDP_LOCAL void ohos_avcodec_on_new_output_buffer(OH_AVCodec* codec, uint32_t index,
                                                     OH_AVBuffer* buffer, void* userData);
FREERDP_LOCAL void ohos_avcodec_reset_async_state(H264_CONTEXT_OHOS_AVCODEC* sys);
FREERDP_LOCAL void ohos_avcodec_close_decoder(H264_CONTEXT_OHOS_AVCODEC* sys);
FREERDP_LOCAL void ohos_avcodec_close_encoder(H264_CONTEXT_OHOS_AVCODEC* sys);
FREERDP_LOCAL const char* ohos_avcodec_decoder_pixel_format_name(int32_t pixelFormat);
FREERDP_LOCAL int32_t ohos_avcodec_choose_decoder_pixel_format(OH_AVCapability* capability,
                                                               wLog* log);
FREERDP_LOCAL BOOL ohos_avcodec_update_decoder_output_description(
    H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys, const char* reason);
FREERDP_LOCAL BOOL ohos_avcodec_configure_decoder(H264_CONTEXT* h264,
                                                  H264_CONTEXT_OHOS_AVCODEC* sys);
FREERDP_LOCAL BOOL ohos_avcodec_open_decoder(H264_CONTEXT* h264,
                                             H264_CONTEXT_OHOS_AVCODEC* sys);
FREERDP_LOCAL int ohos_avcodec_prepare_buffer_decoder(H264_CONTEXT* h264,
                                                      H264_CONTEXT_OHOS_AVCODEC* sys);
FREERDP_LOCAL BOOL ohos_avcodec_copy_output_slot_to_h264(H264_CONTEXT* h264,
                                                         const OHOS_AVCODEC_OUTPUT_SLOT* slot);
FREERDP_LOCAL int ohos_avcodec_decompress_buffer(H264_CONTEXT* h264,
                                                 H264_CONTEXT_OHOS_AVCODEC* sys,
                                                 const BYTE* pSrcData, UINT32 SrcSize);
FREERDP_LOCAL BOOL ohos_avcodec_init_primitives(H264_CONTEXT_OHOS_AVCODEC* sys);
FREERDP_LOCAL void ohos_avcodec_uninit(H264_CONTEXT* h264);
FREERDP_LOCAL BOOL ohos_avcodec_init(H264_CONTEXT* h264);
FREERDP_LOCAL BOOL ohos_avcodec_build_annexb_sample(H264_CONTEXT_OHOS_AVCODEC* sys,
                                                    const BYTE* config, UINT32 configSize,
                                                    const BYTE* frame, UINT32 frameSize,
                                                    BYTE** dst, UINT32* capacity,
                                                    UINT32* dstSize);
FREERDP_LOCAL void ohos_avcodec_return_empty_input(H264_CONTEXT_OHOS_AVCODEC* sys,
                                                   uint32_t inputIndex,
                                                   OH_AVBuffer* inputBuffer);
FREERDP_LOCAL int ohos_avcodec_decompress(H264_CONTEXT* WINPR_RESTRICT h264,
                                           const BYTE* WINPR_RESTRICT pSrcData, UINT32 SrcSize);
FREERDP_LOCAL BOOL ohos_avcodec_open_encoder(H264_CONTEXT* h264,
                                             H264_CONTEXT_OHOS_AVCODEC* sys);
FREERDP_LOCAL const char* ohos_avcodec_bitrate_mode_name(OH_BitrateMode mode);
FREERDP_LOCAL UINT32 ohos_avcodec_effective_encoder_bitrate(const H264_CONTEXT* h264,
                                                            UINT32 bitrate);
FREERDP_LOCAL OH_BitrateMode ohos_avcodec_select_bitrate_mode(H264_CONTEXT* h264,
                                                              OH_AVCapability* capability);
FREERDP_LOCAL OH_AVErrCode ohos_avcodec_configure_encoder(
    H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys, UINT32 frameRate, UINT32 bitrate,
    int32_t pixelFormat, OH_BitrateMode bitrateMode);
FREERDP_LOCAL int ohos_avcodec_compress(H264_CONTEXT* WINPR_RESTRICT h264,
                                        const BYTE** WINPR_RESTRICT ppSrcYuv,
                                        const UINT32* WINPR_RESTRICT pStride,
                                        BYTE** WINPR_RESTRICT ppDstData,
                                        UINT32* WINPR_RESTRICT pDstSize);

#endif /* FREERDP_CODEC_H264_OHOS_AVCODEC_INTERNAL_H */
