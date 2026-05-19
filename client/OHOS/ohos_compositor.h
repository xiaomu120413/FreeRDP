#ifndef FREERDP_CLIENT_OHOS_COMPOSITOR_H
#define FREERDP_CLIENT_OHOS_COMPOSITOR_H

#include <stddef.h>

#include <freerdp/api.h>
#include <freerdp/types.h>
#include <winpr/wtypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct freerdp_ohos_compositor freerdpOhosCompositor;

typedef void (*FREERDP_OHOS_COMPOSITOR_LOG_CALLBACK)(const char* message, void* userData);

typedef enum
{
	FREERDP_OHOS_COMPOSITOR_MODE_NONE = 0,
	FREERDP_OHOS_COMPOSITOR_MODE_RGBA = 1,
	FREERDP_OHOS_COMPOSITOR_MODE_AVC420_SURFACE = 2
} FREERDP_OHOS_COMPOSITOR_MODE;

typedef struct
{
	FREERDP_OHOS_COMPOSITOR_LOG_CALLBACK log;
	void* userData;
} FREERDP_OHOS_COMPOSITOR_CONFIG;

typedef struct
{
	void* window;
	UINT32 width;
	UINT32 height;
} FREERDP_OHOS_COMPOSITOR_OUTPUT_TARGET;

FREERDP_API freerdpOhosCompositor* freerdp_ohos_compositor_new(void);
FREERDP_API void freerdp_ohos_compositor_free(freerdpOhosCompositor* compositor);
FREERDP_API BOOL freerdp_ohos_compositor_configure(
    freerdpOhosCompositor* compositor, const FREERDP_OHOS_COMPOSITOR_CONFIG* config,
    char* message, size_t messageSize);
FREERDP_API void freerdp_ohos_compositor_reset(freerdpOhosCompositor* compositor);
FREERDP_API BOOL freerdp_ohos_compositor_set_output_target(
    freerdpOhosCompositor* compositor, const FREERDP_OHOS_COMPOSITOR_OUTPUT_TARGET* target,
    char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_compositor_clear_output_target(
    freerdpOhosCompositor* compositor, char* message, size_t messageSize);
FREERDP_API BOOL freerdp_ohos_compositor_begin_avc420_surface(
    freerdpOhosCompositor* compositor, char* message, size_t messageSize);
FREERDP_API void freerdp_ohos_compositor_end_avc420_surface(
    freerdpOhosCompositor* compositor);
FREERDP_API const char* freerdp_ohos_compositor_get_diagnostics(
    freerdpOhosCompositor* compositor);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_COMPOSITOR_H */
