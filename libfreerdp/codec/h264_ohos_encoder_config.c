/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS AVCodec H.264 encoder configuration
 *
 * Licensed under the Apache License, Version 2.0.
 */

#include "h264_ohos_avcodec_internal.h"

FREERDP_LOCAL const char* ohos_avcodec_bitrate_mode_name(OH_BitrateMode mode)
{
	switch (mode)
	{
		case BITRATE_MODE_CBR:
			return "CBR";
		case BITRATE_MODE_VBR:
			return "VBR";
		case BITRATE_MODE_CQ:
			return "CQ";
		default:
			return "unknown";
	}
}

FREERDP_LOCAL UINT32 ohos_avcodec_effective_encoder_bitrate(const H264_CONTEXT* h264,
                                                            UINT32 bitrate)
{
	static const struct
	{
		UINT32 height;
		UINT32 bitrate;
	} cameraBitrates[] = {
		{ 1080, 6000000 }, { 720, 3000000 }, { 480, 1500000 }, { 360, 900000 },
		{ 240, 450000 },   { 180, 300000 },  { 0, 200000 },
	};

	if (!h264 || (h264->RateControlMode != H264_RATECONTROL_CQP))
		return bitrate;

	for (size_t i = 0; i < ARRAYSIZE(cameraBitrates); i++)
	{
		if (h264->height >= cameraBitrates[i].height)
			return MAX(bitrate, cameraBitrates[i].bitrate);
	}

	return bitrate;
}

FREERDP_LOCAL OH_BitrateMode ohos_avcodec_select_bitrate_mode(H264_CONTEXT* h264,
                                                              OH_AVCapability* capability)
{
	WINPR_ASSERT(h264);
	WINPR_ASSERT(capability);

	if (h264->RateControlMode == H264_RATECONTROL_VBR)
		if (OH_AVCapability_IsEncoderBitrateModeSupported(capability, BITRATE_MODE_VBR))
			return BITRATE_MODE_VBR;

	return BITRATE_MODE_CBR;
}

static void ohos_avcodec_set_encoder_options(OH_AVFormat* format, const H264_CONTEXT* h264,
                                             UINT32 frameRate, UINT32 bitrate,
                                             int32_t pixelFormat, OH_BitrateMode bitrateMode)
{
	OH_AVFormat_SetIntValue(format, OH_MD_KEY_PIXEL_FORMAT, pixelFormat);
	OH_AVFormat_SetLongValue(format, OH_MD_KEY_BITRATE, bitrate);
	OH_AVFormat_SetDoubleValue(format, OH_MD_KEY_FRAME_RATE, (double)frameRate);
	OH_AVFormat_SetIntValue(format, OH_MD_KEY_I_FRAME_INTERVAL, 1000);
	OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENCODE_BITRATE_MODE, bitrateMode);
	OH_AVFormat_SetIntValue(format, OH_MD_KEY_PROFILE, AVC_PROFILE_BASELINE);
}

FREERDP_LOCAL OH_AVErrCode ohos_avcodec_configure_encoder(
    H264_CONTEXT* h264, H264_CONTEXT_OHOS_AVCODEC* sys, UINT32 frameRate, UINT32 bitrate,
    int32_t pixelFormat, OH_BitrateMode bitrateMode)
{
	OH_AVFormat* format = OH_AVFormat_CreateVideoFormat(
	    OH_AVCODEC_MIMETYPE_VIDEO_AVC, WINPR_ASSERTING_INT_CAST(int32_t, h264->width),
	    WINPR_ASSERTING_INT_CAST(int32_t, h264->height));
	OH_AVErrCode rc = AV_ERR_NO_MEMORY;

	if (!format)
		return AV_ERR_NO_MEMORY;

	ohos_avcodec_set_encoder_options(format, h264, frameRate, bitrate, pixelFormat, bitrateMode);
	rc = OH_VideoEncoder_Configure(sys->encoder, format);
	OH_AVFormat_Destroy(format);
	return rc;
}
