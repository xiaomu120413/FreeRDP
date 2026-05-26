/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Input Redirection Virtual Channel - HarmonyOS capturer lifecycle
 */

#ifndef FREERDP_CHANNEL_AUDIN_CLIENT_OHOS_CAPTURER_H
#define FREERDP_CHANNEL_AUDIN_CLIENT_OHOS_CAPTURER_H

#include "audin_ohos_internal.h"

UINT audin_ohos_close(IAudinDevice* device);
UINT audin_ohos_open(IAudinDevice* device, AudinReceive receive, void* userData);

#endif /* FREERDP_CHANNEL_AUDIN_CLIENT_OHOS_CAPTURER_H */
