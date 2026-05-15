/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS AVCodec H.264 decoder probe
 *
 * Licensed under the Apache License, Version 2.0.
 */

#include <freerdp/config.h>

#include <stdbool.h>
#include <stdlib.h>

#include <winpr/crt.h>
#include <winpr/interlocked.h>
#include <winpr/wlog.h>

#include "h264.h"

#if defined(WITH_OHOS_AVCODEC)
#include <multimedia/player_framework/native_avcodec_base.h>
#include <multimedia/player_framework/native_avcodec_videodecoder.h>
#endif

#define TAG FREERDP_TAG("codec.ohos-avcodec")

typedef struct
{
	OH_AVCodec* decoder;
} H264_CONTEXT_OHOS_AVCODEC;

static volatile LONG g_ohos_avcodec_probe_done = 0;

static void ohos_avcodec_uninit(H264_CONTEXT* h264)
{
	H264_CONTEXT_OHOS_AVCODEC* sys = NULL;

	if (!h264)
		return;

	sys = (H264_CONTEXT_OHOS_AVCODEC*)h264->pSystemData;
	if (!sys)
		return;

	if (sys->decoder)
		OH_VideoDecoder_Destroy(sys->decoder);

	free(sys);
	h264->pSystemData = NULL;
	h264->numSystemData = 0;
}

static BOOL ohos_avcodec_init(H264_CONTEXT* h264)
{
	bool isValid = false;
	OH_AVErrCode rc = AV_ERR_OK;
	H264_CONTEXT_OHOS_AVCODEC* sys = NULL;

	if (!h264 || h264->Compressor)
		return FALSE;

	if (InterlockedCompareExchange(&g_ohos_avcodec_probe_done, 1, 0) != 0)
		return FALSE;

	sys = (H264_CONTEXT_OHOS_AVCODEC*)calloc(1, sizeof(H264_CONTEXT_OHOS_AVCODEC));
	if (!sys)
		return FALSE;

	sys->decoder = OH_VideoDecoder_CreateByMime(OH_AVCODEC_MIMETYPE_VIDEO_AVC);
	if (!sys->decoder)
	{
		WLog_Print(h264->log, WLOG_WARN,
		           "OHOS AVCodec H264 decoder unavailable; falling back to software H264");
		free(sys);
		return FALSE;
	}

	h264->pSystemData = sys;
	h264->numSystemData = 1;

	rc = OH_VideoDecoder_IsValid(sys->decoder, &isValid);
	WLog_Print(h264->log, WLOG_INFO,
	           "OHOS AVCodec H264 decoder probe ok: valid=%d rc=%d; software fallback remains active",
	           isValid ? 1 : 0, (int)rc);

	ohos_avcodec_uninit(h264);
	return FALSE;
}

static int ohos_avcodec_decompress(H264_CONTEXT* WINPR_RESTRICT h264,
                                   const BYTE* WINPR_RESTRICT pSrcData, UINT32 SrcSize)
{
	WINPR_UNUSED(h264);
	WINPR_UNUSED(pSrcData);
	WINPR_UNUSED(SrcSize);
	return -1;
}

static int ohos_avcodec_compress(H264_CONTEXT* WINPR_RESTRICT h264,
                                 const BYTE** WINPR_RESTRICT ppSrcYuv,
                                 const UINT32* WINPR_RESTRICT pStride,
                                 BYTE** WINPR_RESTRICT ppDstData,
                                 UINT32* WINPR_RESTRICT pDstSize)
{
	WINPR_UNUSED(h264);
	WINPR_UNUSED(ppSrcYuv);
	WINPR_UNUSED(pStride);
	WINPR_UNUSED(ppDstData);
	WINPR_UNUSED(pDstSize);
	return -1;
}

const H264_CONTEXT_SUBSYSTEM g_Subsystem_OHOS_AVCodec = {
	"OHOS-AVCodec", ohos_avcodec_init, ohos_avcodec_uninit, ohos_avcodec_decompress,
	ohos_avcodec_compress
};
