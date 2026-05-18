#ifndef FREERDP_CLIENT_OHOS_POINTER_H
#define FREERDP_CLIENT_OHOS_POINTER_H

#include <stddef.h>

#include <freerdp/api.h>
#include <winpr/wtypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define FREERDP_OHOS_POINTER_BUTTON_NONE 0x00U
#define FREERDP_OHOS_POINTER_BUTTON_LEFT 0x01U
#define FREERDP_OHOS_POINTER_BUTTON_RIGHT 0x02U
#define FREERDP_OHOS_POINTER_BUTTON_MIDDLE 0x04U

typedef enum
{
	FREERDP_OHOS_POINTER_ACTION_MOVE = 0,
	FREERDP_OHOS_POINTER_ACTION_BUTTON_DOWN = 1,
	FREERDP_OHOS_POINTER_ACTION_BUTTON_UP = 2,
	FREERDP_OHOS_POINTER_ACTION_WHEEL_VERTICAL = 3,
	FREERDP_OHOS_POINTER_ACTION_WHEEL_HORIZONTAL = 4
} FREERDP_OHOS_POINTER_ACTION;

typedef struct
{
	UINT32 surfaceWidth;
	UINT32 surfaceHeight;
	UINT32 viewportX;
	UINT32 viewportY;
	UINT32 viewportWidth;
	UINT32 viewportHeight;
	UINT32 desktopWidth;
	UINT32 desktopHeight;
} FREERDP_OHOS_POINTER_VIEWPORT;

typedef struct
{
	UINT32 action;
	UINT32 buttons;
	UINT32 x;
	UINT32 y;
	INT32 delta;
	BOOL allowClamp;
} FREERDP_OHOS_POINTER_EVENT;

typedef struct
{
	BOOL ok;
	UINT16 flags;
	UINT16 x;
	UINT16 y;
	UINT32 surfaceX;
	UINT32 surfaceY;
	UINT32 remoteX;
	UINT32 remoteY;
} FREERDP_OHOS_POINTER_PACKET;

FREERDP_API const char* freerdp_ohos_pointer_action_name(UINT32 action);
FREERDP_API BOOL
freerdp_ohos_pointer_is_in_viewport(const FREERDP_OHOS_POINTER_VIEWPORT* viewport,
                                    UINT32 x, UINT32 y);
FREERDP_API BOOL freerdp_ohos_pointer_build_event(
    const FREERDP_OHOS_POINTER_VIEWPORT* viewport, const FREERDP_OHOS_POINTER_EVENT* event,
    FREERDP_OHOS_POINTER_PACKET* packet, char* message, size_t messageSize);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_POINTER_H */
