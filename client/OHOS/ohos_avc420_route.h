#ifndef FREERDP_CLIENT_OHOS_AVC420_ROUTE_H
#define FREERDP_CLIENT_OHOS_AVC420_ROUTE_H

#include <stddef.h>

#include <freerdp/api.h>
#include <freerdp/types.h>
#include <winpr/wtypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct freerdp_ohos_avc420_route freerdpOhosAvc420Route;

typedef enum
{
	FREERDP_OHOS_AVC420_ROUTE_MODE_NONE = 0,
	FREERDP_OHOS_AVC420_ROUTE_MODE_AVC420_SURFACE = 1
} FREERDP_OHOS_AVC420_ROUTE_MODE;

typedef struct
{
	void* window;
	UINT32 width;
	UINT32 height;
} FREERDP_OHOS_AVC420_ROUTE_OUTPUT_TARGET;

typedef void (*FREERDP_OHOS_AVC420_ROUTE_LOG_CALLBACK)(const char* message, void* userData);
typedef BOOL (*FREERDP_OHOS_AVC420_ROUTE_GET_TARGET_CALLBACK)(
    FREERDP_OHOS_AVC420_ROUTE_OUTPUT_TARGET* target, void* userData);
typedef void (*FREERDP_OHOS_AVC420_ROUTE_WINDOW_CALLBACK)(void* window, const char* reason,
                                                          void* userData);

typedef struct
{
	FREERDP_OHOS_AVC420_ROUTE_LOG_CALLBACK log;
	FREERDP_OHOS_AVC420_ROUTE_GET_TARGET_CALLBACK getOutputTarget;
	FREERDP_OHOS_AVC420_ROUTE_WINDOW_CALLBACK prepareOutputTarget;
	FREERDP_OHOS_AVC420_ROUTE_WINDOW_CALLBACK restoreOutputTarget;
	void* userData;
} FREERDP_OHOS_AVC420_ROUTE_CONFIG;

FREERDP_API freerdpOhosAvc420Route* freerdp_ohos_avc420_route_new(void);
FREERDP_API void freerdp_ohos_avc420_route_free(freerdpOhosAvc420Route* route);
FREERDP_API BOOL freerdp_ohos_avc420_route_configure(
    freerdpOhosAvc420Route* route, const FREERDP_OHOS_AVC420_ROUTE_CONFIG* config,
    char* message, size_t messageSize);
FREERDP_API void freerdp_ohos_avc420_route_reset(freerdpOhosAvc420Route* route);
FREERDP_API BOOL freerdp_ohos_avc420_route_set_armed(freerdpOhosAvc420Route* route, BOOL armed,
                                                     char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_avc420_route_set_output_target(
    freerdpOhosAvc420Route* route, const FREERDP_OHOS_AVC420_ROUTE_OUTPUT_TARGET* target,
    char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_avc420_route_clear_output_target(freerdpOhosAvc420Route* route,
                                                               char* message,
                                                               size_t messageSize);
FREERDP_API BOOL freerdp_ohos_avc420_route_begin_surface(freerdpOhosAvc420Route* route,
                                                         char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_avc420_route_refresh_output_target(freerdpOhosAvc420Route* route,
                                                                 const char* reason,
                                                                 char* message,
                                                                 size_t messageSize);
FREERDP_API void freerdp_ohos_avc420_route_end_surface(freerdpOhosAvc420Route* route);
FREERDP_API BOOL freerdp_ohos_avc420_route_is_surface_active(freerdpOhosAvc420Route* route);
FREERDP_API const char*
freerdp_ohos_avc420_route_get_diagnostics(freerdpOhosAvc420Route* route);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_AVC420_ROUTE_H */
