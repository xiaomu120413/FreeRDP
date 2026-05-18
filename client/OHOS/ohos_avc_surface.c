/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS NativeImage decode surface pool for AVC streams
 */

#include "ohos_avc_surface.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <native_image/native_image.h>
#include <native_window/external_window.h>

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

typedef struct
{
	GLuint texture;
	OH_NativeImage* image;
	OHNativeWindow* window;
	UINT64 surfaceId;
} FREERDP_OHOS_DECODE_SURFACE;

struct freerdp_ohos_avc_surface_pool
{
	EGLDisplay display;
	EGLConfig config;
	EGLContext context;
	EGLSurface pbufferSurface;
	UINT32 width;
	UINT32 height;
	FREERDP_OHOS_DECODE_SURFACE luma;
	FREERDP_OHOS_DECODE_SURFACE chroma;
};

static void ohos_avc_surface_format_message(char* message, size_t size, const char* format, ...)
{
	if (!message || size == 0)
		return;

	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, size, format, args);
	va_end(args);
}

static UINT32 ohos_avc_surface_egl_error(void)
{
	return (UINT32)eglGetError();
}

static void ohos_avc_surface_destroy_decode_surface(FREERDP_OHOS_DECODE_SURFACE* surface)
{
	if (!surface)
		return;

	if (surface->image)
		OH_NativeImage_Destroy(&surface->image);
	if (surface->texture != 0)
	{
		GLuint texture = surface->texture;
		glDeleteTextures(1, &texture);
	}

	surface->texture = 0;
	surface->image = NULL;
	surface->window = NULL;
	surface->surfaceId = 0;
}

static BOOL ohos_avc_surface_ensure_context(freerdpOhosAvcSurfacePool* pool, char* message,
                                            size_t messageSize)
{
	if (!pool)
		return FALSE;
	if ((pool->display != EGL_NO_DISPLAY) && (pool->context != EGL_NO_CONTEXT) &&
	    (pool->pbufferSurface != EGL_NO_SURFACE))
		return TRUE;

	freerdp_ohos_avc_surface_pool_destroy(pool);

	pool->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (pool->display == EGL_NO_DISPLAY)
	{
		ohos_avc_surface_format_message(message, messageSize,
		                                "OHOS AVC EGL get display failed: 0x%08" PRIX32,
		                                ohos_avc_surface_egl_error());
		return FALSE;
	}
	if (!eglInitialize(pool->display, NULL, NULL))
	{
		ohos_avc_surface_format_message(message, messageSize,
		                                "OHOS AVC EGL initialize failed: 0x%08" PRIX32,
		                                ohos_avc_surface_egl_error());
		freerdp_ohos_avc_surface_pool_destroy(pool);
		return FALSE;
	}
	if (!eglBindAPI(EGL_OPENGL_ES_API))
	{
		ohos_avc_surface_format_message(message, messageSize,
		                                "OHOS AVC EGL bind GLES API failed: 0x%08" PRIX32,
		                                ohos_avc_surface_egl_error());
		freerdp_ohos_avc_surface_pool_destroy(pool);
		return FALSE;
	}

	const EGLint configAttribs[] = { EGL_RENDERABLE_TYPE,
		                             EGL_OPENGL_ES2_BIT,
		                             EGL_SURFACE_TYPE,
		                             EGL_PBUFFER_BIT | EGL_WINDOW_BIT,
		                             EGL_RED_SIZE,
		                             8,
		                             EGL_GREEN_SIZE,
		                             8,
		                             EGL_BLUE_SIZE,
		                             8,
		                             EGL_ALPHA_SIZE,
		                             8,
		                             EGL_NONE };
	EGLint configCount = 0;
	if (!eglChooseConfig(pool->display, configAttribs, &pool->config, 1, &configCount) ||
	    configCount <= 0)
	{
		ohos_avc_surface_format_message(message, messageSize,
		                                "OHOS AVC EGL choose config failed: 0x%08" PRIX32,
		                                ohos_avc_surface_egl_error());
		freerdp_ohos_avc_surface_pool_destroy(pool);
		return FALSE;
	}

	const EGLint pbufferAttribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
	pool->pbufferSurface = eglCreatePbufferSurface(pool->display, pool->config, pbufferAttribs);
	if (pool->pbufferSurface == EGL_NO_SURFACE)
	{
		ohos_avc_surface_format_message(message, messageSize,
		                                "OHOS AVC EGL create pbuffer failed: 0x%08" PRIX32,
		                                ohos_avc_surface_egl_error());
		freerdp_ohos_avc_surface_pool_destroy(pool);
		return FALSE;
	}

	const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	pool->context = eglCreateContext(pool->display, pool->config, EGL_NO_CONTEXT, contextAttribs);
	if (pool->context == EGL_NO_CONTEXT)
	{
		ohos_avc_surface_format_message(message, messageSize,
		                                "OHOS AVC EGL create context failed: 0x%08" PRIX32,
		                                ohos_avc_surface_egl_error());
		freerdp_ohos_avc_surface_pool_destroy(pool);
		return FALSE;
	}

	return TRUE;
}

static BOOL ohos_avc_surface_create_decode_surface(const char* name,
                                                   FREERDP_OHOS_DECODE_SURFACE* surface,
                                                   char* message, size_t messageSize)
{
	if (!surface)
		return FALSE;

	glGenTextures(1, &surface->texture);
	if (surface->texture == 0)
	{
		ohos_avc_surface_format_message(message, messageSize,
		                                "OHOS AVC %s texture allocation failed", name);
		return FALSE;
	}

	glBindTexture(GL_TEXTURE_EXTERNAL_OES, surface->texture);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	const GLenum glError = glGetError();
	if (glError != GL_NO_ERROR)
	{
		ohos_avc_surface_format_message(message, messageSize,
		                                "OHOS AVC %s external texture setup failed: 0x%08" PRIX32,
		                                name, (UINT32)glError);
		ohos_avc_surface_destroy_decode_surface(surface);
		return FALSE;
	}

	surface->image = OH_NativeImage_Create(surface->texture, GL_TEXTURE_EXTERNAL_OES);
	if (!surface->image)
	{
		ohos_avc_surface_format_message(message, messageSize,
		                                "OHOS AVC %s NativeImage create failed", name);
		ohos_avc_surface_destroy_decode_surface(surface);
		return FALSE;
	}

	(void)OH_NativeImage_SetDropBufferMode(surface->image, true);
	surface->window = OH_NativeImage_AcquireNativeWindow(surface->image);
	if (!surface->window)
	{
		ohos_avc_surface_format_message(message, messageSize,
		                                "OHOS AVC %s NativeImage window acquire failed", name);
		ohos_avc_surface_destroy_decode_surface(surface);
		return FALSE;
	}

	(void)OH_NativeImage_GetSurfaceId(surface->image, &surface->surfaceId);
	return TRUE;
}

freerdpOhosAvcSurfacePool* freerdp_ohos_avc_surface_pool_new(void)
{
	freerdpOhosAvcSurfacePool* pool =
	    (freerdpOhosAvcSurfacePool*)calloc(1, sizeof(freerdpOhosAvcSurfacePool));
	if (!pool)
		return NULL;

	pool->display = EGL_NO_DISPLAY;
	pool->context = EGL_NO_CONTEXT;
	pool->pbufferSurface = EGL_NO_SURFACE;
	return pool;
}

void freerdp_ohos_avc_surface_pool_destroy(freerdpOhosAvcSurfacePool* pool)
{
	if (!pool)
		return;

	if (pool->display != EGL_NO_DISPLAY)
	{
		if ((pool->context != EGL_NO_CONTEXT) && (pool->pbufferSurface != EGL_NO_SURFACE))
		{
			(void)eglMakeCurrent(pool->display, pool->pbufferSurface, pool->pbufferSurface,
			                     pool->context);
			ohos_avc_surface_destroy_decode_surface(&pool->luma);
			ohos_avc_surface_destroy_decode_surface(&pool->chroma);
			(void)eglMakeCurrent(pool->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		}
		else
		{
			ohos_avc_surface_destroy_decode_surface(&pool->luma);
			ohos_avc_surface_destroy_decode_surface(&pool->chroma);
		}

		if (pool->context != EGL_NO_CONTEXT)
			(void)eglDestroyContext(pool->display, pool->context);
		if (pool->pbufferSurface != EGL_NO_SURFACE)
			(void)eglDestroySurface(pool->display, pool->pbufferSurface);
		(void)eglTerminate(pool->display);
	}

	pool->display = EGL_NO_DISPLAY;
	pool->config = NULL;
	pool->context = EGL_NO_CONTEXT;
	pool->pbufferSurface = EGL_NO_SURFACE;
	pool->width = 0;
	pool->height = 0;
	memset(&pool->luma, 0, sizeof(pool->luma));
	memset(&pool->chroma, 0, sizeof(pool->chroma));
}

void freerdp_ohos_avc_surface_pool_free(freerdpOhosAvcSurfacePool* pool)
{
	if (!pool)
		return;
	freerdp_ohos_avc_surface_pool_destroy(pool);
	free(pool);
}

BOOL freerdp_ohos_avc_surface_pool_ensure_avc444(
    freerdpOhosAvcSurfacePool* pool, UINT32 width, UINT32 height,
    FREERDP_OHOS_AVC444_SURFACE_TARGETS* targets, char* message, size_t messageSize)
{
	if (!pool || !targets)
		return FALSE;
	memset(targets, 0, sizeof(*targets));

	if ((width == 0) || (height == 0))
	{
		ohos_avc_surface_format_message(message, messageSize,
		                                "OHOS AVC decode surface size is invalid");
		return FALSE;
	}
	if (!ohos_avc_surface_ensure_context(pool, message, messageSize))
		return FALSE;
	if (!eglMakeCurrent(pool->display, pool->pbufferSurface, pool->pbufferSurface, pool->context))
	{
		ohos_avc_surface_format_message(message, messageSize,
		                                "OHOS AVC pbuffer make current failed: 0x%08" PRIX32,
		                                ohos_avc_surface_egl_error());
		freerdp_ohos_avc_surface_pool_destroy(pool);
		return FALSE;
	}

	if ((pool->width != width) || (pool->height != height) || !pool->luma.window ||
	    !pool->chroma.window)
	{
		ohos_avc_surface_destroy_decode_surface(&pool->luma);
		ohos_avc_surface_destroy_decode_surface(&pool->chroma);
		pool->width = 0;
		pool->height = 0;

		if (!ohos_avc_surface_create_decode_surface("luma", &pool->luma, message,
		                                            messageSize) ||
		    !ohos_avc_surface_create_decode_surface("chroma", &pool->chroma, message,
		                                            messageSize))
		{
			(void)eglMakeCurrent(pool->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
			freerdp_ohos_avc_surface_pool_destroy(pool);
			return FALSE;
		}
		pool->width = width;
		pool->height = height;
	}

	(void)eglMakeCurrent(pool->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	targets->lumaWindow = pool->luma.window;
	targets->chromaWindow = pool->chroma.window;
	targets->width = pool->width;
	targets->height = pool->height;
	targets->lumaTexture = pool->luma.texture;
	targets->chromaTexture = pool->chroma.texture;
	targets->lumaSurfaceId = pool->luma.surfaceId;
	targets->chromaSurfaceId = pool->chroma.surfaceId;
	ohos_avc_surface_format_message(message, messageSize,
	                                "OHOS AVC444 decode surfaces ready: %ux%u lumaSurface=%" PRIu64
	                                " chromaSurface=%" PRIu64,
	                                width, height, targets->lumaSurfaceId,
	                                targets->chromaSurfaceId);
	return TRUE;
}
