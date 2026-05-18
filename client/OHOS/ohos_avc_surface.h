#ifndef FREERDP_CLIENT_OHOS_AVC_SURFACE_H
#define FREERDP_CLIENT_OHOS_AVC_SURFACE_H

#include <stddef.h>

#include <freerdp/api.h>
#include <winpr/wtypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct freerdp_ohos_avc_surface_pool freerdpOhosAvcSurfacePool;

typedef struct
{
	void* lumaWindow;
	void* chromaWindow;
	void* lumaImage;
	void* chromaImage;
	void* eglDisplay;
	void* eglConfig;
	void* eglContext;
	UINT32 width;
	UINT32 height;
	UINT32 lumaTexture;
	UINT32 chromaTexture;
	UINT64 lumaSurfaceId;
	UINT64 chromaSurfaceId;
} FREERDP_OHOS_AVC444_SURFACE_TARGETS;

FREERDP_API freerdpOhosAvcSurfacePool* freerdp_ohos_avc_surface_pool_new(void);
FREERDP_API void freerdp_ohos_avc_surface_pool_free(freerdpOhosAvcSurfacePool* pool);
FREERDP_API void freerdp_ohos_avc_surface_pool_destroy(freerdpOhosAvcSurfacePool* pool);
FREERDP_API BOOL freerdp_ohos_avc_surface_pool_ensure_avc444(
    freerdpOhosAvcSurfacePool* pool, UINT32 width, UINT32 height,
    FREERDP_OHOS_AVC444_SURFACE_TARGETS* targets, char* message, size_t messageSize);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_CLIENT_OHOS_AVC_SURFACE_H */
