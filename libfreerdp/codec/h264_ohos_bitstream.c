/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS AVCodec H.264 bitstream helpers
 *
 * Licensed under the Apache License, Version 2.0.
 */

#include "h264_ohos_avcodec_internal.h"

static BOOL ohos_avcodec_starts_annexb(const BYTE* data, UINT32 size)
{
	if (!data || (size < 3))
		return FALSE;
	if ((data[0] == 0x00) && (data[1] == 0x00) && (data[2] == 0x01))
		return TRUE;
	return (size >= 4) && (data[0] == 0x00) && (data[1] == 0x00) && (data[2] == 0x00) &&
	       (data[3] == 0x01);
}

static BOOL ohos_avcodec_is_avcc_config(const BYTE* data, UINT32 size)
{
	return data && (size >= 7) && (data[0] == 0x01);
}

static UINT32 ohos_avcodec_read_be(const BYTE* data, UINT32 size)
{
	UINT32 value = 0;

	for (UINT32 x = 0; x < size; x++)
		value = (value << 8) | data[x];
	return value;
}

static BOOL ohos_avcodec_reserve_annexb(BYTE** dst, UINT32* capacity, UINT32 needed)
{
	BYTE* tmp = NULL;
	UINT32 next = 0;

	if (!dst || !capacity)
		return FALSE;
	if (needed <= *capacity)
		return TRUE;

	next = *capacity ? *capacity : 4096;
	while (next < needed)
	{
		if (next > (UINT32_MAX / 2))
		{
			next = needed;
			break;
		}
		next *= 2;
	}

	tmp = (BYTE*)realloc(*dst, next);
	if (!tmp)
		return FALSE;

	*dst = tmp;
	*capacity = next;
	return TRUE;
}

static BOOL ohos_avcodec_append(BYTE** dst, UINT32* capacity, UINT32* size, const BYTE* data,
                                UINT32 length)
{
	if (!dst || !capacity || !size || (!data && (length > 0)))
		return FALSE;
	if (length > (UINT32_MAX - *size))
		return FALSE;
	if (!ohos_avcodec_reserve_annexb(dst, capacity, *size + length))
		return FALSE;
	if (length > 0)
		CopyMemory(&(*dst)[*size], data, length);
	*size += length;
	return TRUE;
}

static BOOL ohos_avcodec_append_nal(BYTE** dst, UINT32* capacity, UINT32* size, const BYTE* data,
                                    UINT32 length)
{
	static const BYTE startCode[] = { 0x00, 0x00, 0x00, 0x01 };

	return ohos_avcodec_append(dst, capacity, size, startCode, ARRAYSIZE(startCode)) &&
	       ohos_avcodec_append(dst, capacity, size, data, length);
}

static BOOL ohos_avcodec_convert_avcc_config(H264_CONTEXT_OHOS_AVCODEC* sys, const BYTE* data,
                                             UINT32 dataSize, BYTE** dst, UINT32* capacity,
                                             UINT32* dstSize)
{
	UINT32 offset = 6;
	UINT32 nalCount = 0;
	const UINT32 mark = *dstSize;
	const UINT32 spsCount = data[5] & 0x1F;

	if (!ohos_avcodec_is_avcc_config(data, dataSize))
		return FALSE;

	sys->nalLengthSize = (data[4] & 0x03) + 1;
	for (UINT32 x = 0; x < spsCount; x++)
	{
		UINT32 nalSize = 0;

		if ((dataSize - offset) < 2)
			goto fail;
		nalSize = ohos_avcodec_read_be(&data[offset], 2);
		offset += 2;
		if ((nalSize == 0) || (nalSize > (dataSize - offset)))
			goto fail;
		if (!ohos_avcodec_append_nal(dst, capacity, dstSize, &data[offset], nalSize))
			goto fail;
		offset += nalSize;
		nalCount++;
	}

	if (offset >= dataSize)
		goto fail;

	const UINT32 ppsCount = data[offset++];
	for (UINT32 x = 0; x < ppsCount; x++)
	{
		UINT32 nalSize = 0;

		if ((dataSize - offset) < 2)
			goto fail;
		nalSize = ohos_avcodec_read_be(&data[offset], 2);
		offset += 2;
		if ((nalSize == 0) || (nalSize > (dataSize - offset)))
			goto fail;
		if (!ohos_avcodec_append_nal(dst, capacity, dstSize, &data[offset], nalSize))
			goto fail;
		offset += nalSize;
		nalCount++;
	}

	if (nalCount == 0)
		goto fail;
	return TRUE;

fail:
	*dstSize = mark;
	return FALSE;
}

static BOOL ohos_avcodec_convert_length_prefixed_frame(const BYTE* data, UINT32 dataSize,
                                                       UINT32 lengthSize, BYTE** dst,
                                                       UINT32* capacity, UINT32* dstSize)
{
	UINT32 offset = 0;
	UINT32 nalCount = 0;
	const UINT32 mark = *dstSize;

	if (!data || (dataSize == 0) || (lengthSize == 0) || (lengthSize > 4))
		return FALSE;

	while (offset < dataSize)
	{
		UINT32 nalSize = 0;

		if ((dataSize - offset) < lengthSize)
			goto fail;
		nalSize = ohos_avcodec_read_be(&data[offset], lengthSize);
		offset += lengthSize;
		if (nalSize == 0)
			continue;
		if (nalSize > (dataSize - offset))
			goto fail;
		if (!ohos_avcodec_append_nal(dst, capacity, dstSize, &data[offset], nalSize))
			goto fail;
		offset += nalSize;
		nalCount++;
	}

	if (nalCount == 0)
		goto fail;
	return TRUE;

fail:
	*dstSize = mark;
	return FALSE;
}

static BOOL ohos_avcodec_try_convert_frame(H264_CONTEXT_OHOS_AVCODEC* sys, const BYTE* data,
                                           UINT32 dataSize, BYTE** dst, UINT32* capacity,
                                           UINT32* dstSize)
{
	const UINT32 candidates[] = { sys ? sys->nalLengthSize : 0, 4, 3, 2, 1 };

	for (UINT32 x = 0; x < ARRAYSIZE(candidates); x++)
	{
		BOOL duplicate = FALSE;
		const UINT32 lengthSize = candidates[x];

		if ((lengthSize == 0) || (lengthSize > 4))
			continue;
		for (UINT32 y = 0; y < x; y++)
		{
			if (candidates[y] == lengthSize)
				duplicate = TRUE;
		}
		if (duplicate)
			continue;

		if (ohos_avcodec_convert_length_prefixed_frame(data, dataSize, lengthSize, dst, capacity,
		                                               dstSize))
		{
			if (sys)
				sys->nalLengthSize = lengthSize;
			return TRUE;
		}
	}

	return FALSE;
}

FREERDP_LOCAL BOOL ohos_avcodec_build_annexb_sample(H264_CONTEXT_OHOS_AVCODEC* sys,
                                                    const BYTE* config, UINT32 configSize,
                                                    const BYTE* frame, UINT32 frameSize,
                                                    BYTE** dst, UINT32* capacity,
                                                    UINT32* dstSize)
{
	const char* configLayout = "none";
	const char* frameLayout = "none";

	if (!sys || !dst || !capacity || !dstSize || (!frame && (frameSize > 0)))
		return FALSE;

	*dstSize = 0;
	if (config && (configSize > 0))
	{
		if (ohos_avcodec_starts_annexb(config, configSize))
		{
			configLayout = "annexb";
			if (!ohos_avcodec_append(dst, capacity, dstSize, config, configSize))
				return FALSE;
		}
		else if (ohos_avcodec_convert_avcc_config(sys, config, configSize, dst, capacity, dstSize))
			configLayout = "avcc";
		else
		{
			configLayout = "raw";
			if (!ohos_avcodec_append(dst, capacity, dstSize, config, configSize))
				return FALSE;
		}
	}

	if (frame && (frameSize > 0))
	{
		if (ohos_avcodec_starts_annexb(frame, frameSize))
		{
			frameLayout = "annexb";
			if (!ohos_avcodec_append(dst, capacity, dstSize, frame, frameSize))
				return FALSE;
		}
		else if (ohos_avcodec_try_convert_frame(sys, frame, frameSize, dst, capacity, dstSize))
			frameLayout = "length-prefixed";
		else
		{
			frameLayout = "raw";
			if (!ohos_avcodec_append(dst, capacity, dstSize, frame, frameSize))
				return FALSE;
		}
	}

	if (!sys->bitstreamLayoutLogged)
	{
		WLog_Print(sys->log, WLOG_INFO,
		           "OHOS AVCodec encoder H264 output: config=%s(%u) frame=%s(%u) nalLength=%u "
		           "annexbSize=%u",
		           configLayout, configSize, frameLayout, frameSize, sys->nalLengthSize, *dstSize);
		sys->bitstreamLayoutLogged = TRUE;
	}

	if (!sys->bitstreamConversionWarningLogged &&
	    ((strcmp(configLayout, "raw") == 0) || (strcmp(frameLayout, "raw") == 0)))
	{
		WLog_Print(sys->log, WLOG_WARN,
		           "OHOS AVCodec encoder emitted unknown H264 layout; forwarding raw bytes");
		sys->bitstreamConversionWarningLogged = TRUE;
	}

	return TRUE;
}
