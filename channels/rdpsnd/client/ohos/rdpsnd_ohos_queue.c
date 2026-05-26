/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Output Virtual Channel - HarmonyOS playback queue
 */

#include "rdpsnd_ohos_queue.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

size_t rdpsnd_ohos_frame_bytes(const rdpsndOhosPlugin* ohos)
{
	if (!ohos || ohos->blockAlign == 0)
		return 1;

	return ohos->blockAlign;
}

size_t rdpsnd_ohos_bytes_per_second(const rdpsndOhosPlugin* ohos)
{
	return (size_t)ohos->rate * rdpsnd_ohos_frame_bytes(ohos);
}

BYTE rdpsnd_ohos_silence_byte(const rdpsndOhosPlugin* ohos)
{
	return (ohos && ohos->bitsPerSample == 8) ? 0x80 : 0x00;
}

UINT32 rdpsnd_ohos_queued_latency_locked(const rdpsndOhosPlugin* ohos)
{
	const size_t bps = rdpsnd_ohos_bytes_per_second(ohos);
	if ((bps == 0) || (ohos->queueSize == 0))
		return 0;

	return (UINT32)((ohos->queueSize * 1000U) / bps);
}

void rdpsnd_ohos_clear_queue_locked(rdpsndOhosPlugin* ohos)
{
	ohos->queueRead = 0;
	ohos->queueWrite = 0;
	ohos->queueSize = 0;
}

static void rdpsnd_ohos_drop_locked(rdpsndOhosPlugin* ohos, size_t bytes)
{
	const size_t frameBytes = rdpsnd_ohos_frame_bytes(ohos);

	if (frameBytes > 1)
		bytes -= bytes % frameBytes;

	if (bytes >= ohos->queueSize)
	{
		rdpsnd_ohos_clear_queue_locked(ohos);
		return;
	}

	ohos->queueRead = (ohos->queueRead + bytes) % ohos->queueCapacity;
	ohos->queueSize -= bytes;
}

static size_t rdpsnd_ohos_pop_locked(rdpsndOhosPlugin* ohos, BYTE* dst, size_t bytes)
{
	size_t copied = 0;

	while ((copied < bytes) && (ohos->queueSize > 0))
	{
		const size_t chunk = (ohos->queueRead + ohos->queueSize <= ohos->queueCapacity)
		                         ? ohos->queueSize
		                         : (ohos->queueCapacity - ohos->queueRead);
		const size_t todo = (chunk < (bytes - copied)) ? chunk : (bytes - copied);

		memcpy(dst + copied, ohos->queue + ohos->queueRead, todo);
		ohos->queueRead = (ohos->queueRead + todo) % ohos->queueCapacity;
		ohos->queueSize -= todo;
		copied += todo;
	}

	return copied;
}

void rdpsnd_ohos_push_locked(rdpsndOhosPlugin* ohos, const BYTE* data, size_t size)
{
	const size_t frameBytes = rdpsnd_ohos_frame_bytes(ohos);

	if (!ohos->queue || (ohos->queueCapacity == 0) || !data || (size == 0))
		return;

	if (frameBytes > 1)
		size -= size % frameBytes;

	if (size == 0)
		return;

	if (size > ohos->queueCapacity)
	{
		const size_t keep = ohos->queueCapacity - (ohos->queueCapacity % frameBytes);
		data += size - keep;
		size = keep;
		rdpsnd_ohos_clear_queue_locked(ohos);
	}
	else if ((ohos->queueCapacity - ohos->queueSize) < size)
	{
		rdpsnd_ohos_drop_locked(ohos, size - (ohos->queueCapacity - ohos->queueSize));
	}

	while (size > 0)
	{
		const size_t tail = ohos->queueCapacity - ohos->queueWrite;
		const size_t todo = (tail < size) ? tail : size;

		memcpy(ohos->queue + ohos->queueWrite, data, todo);
		ohos->queueWrite = (ohos->queueWrite + todo) % ohos->queueCapacity;
		ohos->queueSize += todo;
		if (ohos->queueSize > g_queuePeakBytes)
			g_queuePeakBytes = (UINT64)ohos->queueSize;
		g_lastQueueBytes = (UINT32)((ohos->queueSize > UINT32_MAX) ? UINT32_MAX : ohos->queueSize);
		data += todo;
		size -= todo;
	}
}

void rdpsnd_ohos_push_silence_locked(rdpsndOhosPlugin* ohos, UINT32 durationMs)
{
	const size_t bytesPerSecond = rdpsnd_ohos_bytes_per_second(ohos);
	const size_t frameBytes = rdpsnd_ohos_frame_bytes(ohos);
	BYTE silence[4096] = { 0 };
	size_t remaining = 0;
	size_t pushed = 0;

	if (!ohos || !ohos->queue || (bytesPerSecond == 0) || (frameBytes == 0) || (durationMs == 0))
		return;

	remaining = (bytesPerSecond * durationMs) / 1000U;
	remaining -= remaining % frameBytes;
	if (remaining == 0)
		return;

	memset(silence, rdpsnd_ohos_silence_byte(ohos), sizeof(silence));
	while (remaining > 0)
	{
		size_t chunk = (remaining < sizeof(silence)) ? remaining : sizeof(silence);
		chunk -= chunk % frameBytes;
		if (chunk == 0)
			break;
		rdpsnd_ohos_push_locked(ohos, silence, chunk);
		remaining -= chunk;
		pushed += chunk;
	}

	if (pushed > 0)
	{
		g_rendererPrimeCount++;
		g_rendererPrimeBytes += pushed;
	}
}

size_t rdpsnd_ohos_fill_audio_buffer(rdpsndOhosPlugin* ohos, void* audioData,
                                     int32_t audioDataSize, BOOL fillSilence)
{
	BYTE* dst = (BYTE*)audioData;
	size_t copied = 0;

	if (!ohos || !dst || (audioDataSize <= 0))
		return 0;

	EnterCriticalSection(&ohos->lock);
	copied = rdpsnd_ohos_pop_locked(ohos, dst, (size_t)audioDataSize);
	g_lastQueueBytes = (UINT32)((ohos->queueSize > UINT32_MAX) ? UINT32_MAX : ohos->queueSize);
	LeaveCriticalSection(&ohos->lock);

	g_callbackCount++;
	g_lastCallbackSize = (UINT32)audioDataSize;
	g_lastCallbackCopied = (UINT32)((copied > UINT32_MAX) ? UINT32_MAX : copied);
	g_renderedBytes += copied;
	if (copied == 0)
		g_emptyCallbackCount++;
	if (copied < (size_t)audioDataSize)
	{
		g_underrunBytes += (UINT64)((size_t)audioDataSize - copied);
		if (fillSilence)
			memset(dst + copied, rdpsnd_ohos_silence_byte(ohos), (size_t)audioDataSize - copied);
	}

	return copied;
}

BOOL rdpsnd_ohos_allocate_queue(rdpsndOhosPlugin* ohos, UINT32 latency)
{
	const size_t bytesPerSecond = rdpsnd_ohos_bytes_per_second(ohos);
	UINT32 queueMs = latency ? latency * 2U : 300U;
	size_t capacity = 0;
	BYTE* queue = NULL;

	if (queueMs < 400U)
		queueMs = 400U;
	if (queueMs > 1000U)
		queueMs = 1000U;

	capacity = (bytesPerSecond * queueMs) / 1000U;
	if (capacity < 32768U)
		capacity = 32768U;

	capacity -= capacity % rdpsnd_ohos_frame_bytes(ohos);
	if (capacity == 0)
		return FALSE;

	queue = (BYTE*)calloc(1, capacity);
	if (!queue)
		return FALSE;

	free(ohos->queue);
	ohos->queue = queue;
	ohos->queueCapacity = capacity;
	rdpsnd_ohos_clear_queue_locked(ohos);
	ohos->latencyMs = queueMs;
	return TRUE;
}
