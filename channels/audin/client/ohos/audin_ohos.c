/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Input Redirection Virtual Channel - HarmonyOS OHAudio backend
 */

#include "audin_ohos_internal.h"

#include "audin_ohos_capturer.h"
#include "audin_ohos_format.h"

#include <stdlib.h>

#include <winpr/error.h>

static UINT audin_ohos_free(IAudinDevice* device)
{
	AudinOhosDevice* ohos = (AudinOhosDevice*)device;

	if (!ohos)
		return ERROR_INVALID_PARAMETER;

	audin_ohos_close(device);
	if (ohos->lockInitialized)
	{
		DeleteCriticalSection(&ohos->lock);
		ohos->lockInitialized = FALSE;
	}
	free(ohos);
	return CHANNEL_RC_OK;
}

FREERDP_ENTRY_POINT(UINT VCAPITYPE
                       ohos_freerdp_audin_client_subsystem_entry(
                           PFREERDP_AUDIN_DEVICE_ENTRY_POINTS pEntryPoints))
{
	UINT error = CHANNEL_RC_OK;
	AudinOhosDevice* ohos = NULL;

	if (!pEntryPoints || !pEntryPoints->pRegisterAudinDevice)
		return ERROR_INVALID_PARAMETER;

	ohos = (AudinOhosDevice*)calloc(1, sizeof(AudinOhosDevice));
	if (!ohos)
		return CHANNEL_RC_NO_MEMORY;

	ohos->log = WLog_Get(TAG);
	ohos->iface.Open = audin_ohos_open;
	ohos->iface.FormatSupported = audin_ohos_format_supported;
	ohos->iface.SetFormat = audin_ohos_set_format;
	ohos->iface.Close = audin_ohos_close;
	ohos->iface.Free = audin_ohos_free;
	ohos->rdpcontext = pEntryPoints->rdpcontext;
	InitializeCriticalSection(&ohos->lock);
	ohos->lockInitialized = TRUE;

	error = pEntryPoints->pRegisterAudinDevice(pEntryPoints->plugin, (IAudinDevice*)ohos);
	if (error != CHANNEL_RC_OK)
	{
		audin_ohos_log(ohos, WLOG_ERROR, "RegisterAudinDevice failed with error %" PRIu32,
		               error);
		audin_ohos_free((IAudinDevice*)ohos);
		return error;
	}

	++g_registeredCount;
	audin_ohos_log(ohos, WLOG_INFO, "OHOS audin backend registered");
	return CHANNEL_RC_OK;
}
