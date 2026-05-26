/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Output Virtual Channel - HarmonyOS OHAudio backend
 */

#include "rdpsnd_ohos_internal.h"

#include "rdpsnd_ohos_format.h"
#include "rdpsnd_ohos_queue.h"
#include "rdpsnd_ohos_renderer.h"

#include <stdlib.h>

#include <winpr/assert.h>

static UINT rdpsnd_ohos_play(rdpsndDevicePlugin* device, const BYTE* data, size_t size)
{
	rdpsndOhosPlugin* ohos = (rdpsndOhosPlugin*)device;
	UINT32 latency = 0;
	UINT32 peak = 0;

	if (!ohos || !ohos->renderer || !data || (size == 0))
		return 0;

	peak = rdpsnd_ohos_peak_sample(ohos, data, size);
	g_lastPeakSample = peak;
	if (peak > g_maxPeakSample)
		g_maxPeakSample = peak;
	if (peak > 0)
		g_nonSilentPlayCount++;
	else
		g_silentPlayCount++;

	EnterCriticalSection(&ohos->lock);
	if (!ohos->primed)
	{
		rdpsnd_ohos_push_silence_locked(ohos, 120U);
		ohos->primed = TRUE;
	}
	rdpsnd_ohos_push_locked(ohos, data, size);
	latency = rdpsnd_ohos_queued_latency_locked(ohos);
	LeaveCriticalSection(&ohos->lock);

	if (!ohos->started)
	{
		const OH_AudioStream_Result rc = OH_AudioRenderer_Start(ohos->renderer);
		if (rc != AUDIOSTREAM_SUCCESS)
		{
			g_lastOhosResult = (UINT32)rc;
			rdpsnd_ohos_log(WLOG_ERROR, "renderer delayed start failed result=%" PRIu32,
			                (UINT32)rc);
			rdpsnd_ohos_release_renderer(ohos);
			return 0;
		}
		ohos->started = TRUE;
		g_rendererStartCount++;
		rdpsnd_ohos_log(WLOG_INFO, "renderer delayed start ok queue=%" PRIu32
		                          " starts=%" PRIu64 " peak=%" PRIu32,
		                g_lastQueueBytes, g_rendererStartCount, peak);
	}

	g_playCount++;
	g_playBytes += size;
	return latency;
}

static void rdpsnd_ohos_start(WINPR_ATTR_UNUSED rdpsndDevicePlugin* device)
{
}

static void rdpsnd_ohos_close(rdpsndDevicePlugin* device)
{
	rdpsndOhosPlugin* ohos = (rdpsndOhosPlugin*)device;

	if (!ohos)
		return;

	rdpsnd_ohos_log(WLOG_INFO, "closing renderer queue=%" PRIu32 " playCalls=%" PRIu64,
	                g_lastQueueBytes, g_playCount);
	rdpsnd_ohos_release_renderer(ohos);
	g_closeCount++;

	EnterCriticalSection(&ohos->lock);
	rdpsnd_ohos_clear_queue_locked(ohos);
	LeaveCriticalSection(&ohos->lock);
}

static void rdpsnd_ohos_free(rdpsndDevicePlugin* device)
{
	rdpsndOhosPlugin* ohos = (rdpsndOhosPlugin*)device;

	if (!ohos)
		return;

	rdpsnd_ohos_release_renderer(ohos);

	if (ohos->lockInitialized)
		DeleteCriticalSection(&ohos->lock);

	free(ohos->queue);
	free(ohos);
}

FREERDP_ENTRY_POINT(UINT VCAPITYPE ohos_freerdp_rdpsnd_client_subsystem_entry(
    PFREERDP_RDPSND_DEVICE_ENTRY_POINTS pEntryPoints))
{
	rdpsndOhosPlugin* ohos = (rdpsndOhosPlugin*)calloc(1, sizeof(rdpsndOhosPlugin));

	if (!ohos)
		return CHANNEL_RC_NO_MEMORY;

	InitializeCriticalSection(&ohos->lock);
	ohos->lockInitialized = TRUE;
	ohos->volume = 0xFFFFFFFFU;
	ohos->rate = 44100U;
	ohos->channels = 2U;
	ohos->bitsPerSample = 16U;
	ohos->blockAlign = 4U;

	ohos->device.Open = rdpsnd_ohos_open;
	ohos->device.FormatSupported = rdpsnd_ohos_format_supported;
	ohos->device.DefaultFormat = rdpsnd_ohos_default_format;
	ohos->device.GetVolume = rdpsnd_ohos_get_volume;
	ohos->device.SetVolume = rdpsnd_ohos_set_volume;
	ohos->device.Start = rdpsnd_ohos_start;
	ohos->device.Play = rdpsnd_ohos_play;
	ohos->device.Close = rdpsnd_ohos_close;
	ohos->device.Free = rdpsnd_ohos_free;
	ohos->device.ServerFormatAnnounce = rdpsnd_ohos_server_format_announce;

	pEntryPoints->pRegisterRdpsndDevice(pEntryPoints->rdpsnd, &ohos->device);
	g_registeredCount++;
	rdpsnd_ohos_log(WLOG_INFO, "rdpsnd OHAudio device registered count=%" PRIu64,
	                g_registeredCount);
	return CHANNEL_RC_OK;
}
