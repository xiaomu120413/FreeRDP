#ifndef FREERDP_CLIENT_OHOS_PEN_H
#define FREERDP_CLIENT_OHOS_PEN_H

#include <stddef.h>
#include <stdint.h>

#include <freerdp/api.h>
#include <freerdp/client/rdpei.h>
#include <winpr/wtypes.h>

#include "ohos_pointer.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define FREERDP_OHOS_PEN_EVENT_VERSION 1U
#define FREERDP_OHOS_PEN_FLAG_ERASER 0x0001U
#define FREERDP_OHOS_PEN_FLAG_INVERTED 0x0002U
#define FREERDP_OHOS_PEN_FLAG_BARREL 0x0004U

typedef enum
{
	FREERDP_OHOS_PEN_ACTION_DOWN = 1,
	FREERDP_OHOS_PEN_ACTION_MOVE = 2,
	FREERDP_OHOS_PEN_ACTION_UP = 3,
	FREERDP_OHOS_PEN_ACTION_CANCEL = 4
} FREERDP_OHOS_PEN_ACTION;

typedef struct
{
	uint32_t structSize;
	uint32_t version;
	uint32_t action;
	int32_t deviceId;
	uint32_t x;
	uint32_t y;
	float pressure;
	int16_t tiltX;
	int16_t tiltY;
	uint16_t reserved;
	uint32_t flags;
	BOOL allowClamp;
} FREERDP_OHOS_PEN_EVENT;

FREERDP_API BOOL freerdp_ohos_pen_validate(const FREERDP_OHOS_PEN_EVENT* event,
                                            char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_pen_dispatch(RdpeiClientContext* rdpei,
                                            const FREERDP_OHOS_PEN_EVENT* event,
                                            char* message, size_t messageSize);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_PEN_H */
