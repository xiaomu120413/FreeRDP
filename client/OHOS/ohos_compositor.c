/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS compositor state and diagnostics
 */

#include "ohos_compositor.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winpr/crt.h>
#include <winpr/synch.h>

#define OHOS_COMPOSITOR_DIAGNOSTICS_SIZE 1024

struct freerdp_ohos_compositor
{
	CRITICAL_SECTION lock;
	BOOL initialized;
	FREERDP_OHOS_COMPOSITOR_LOG_CALLBACK log;
	void* userData;
	void* outputWindow;
	UINT32 outputWidth;
	UINT32 outputHeight;
	UINT32 outputGeneration;
	UINT32 mode;
	UINT64 outputTargetSets;
	UINT64 outputTargetClears;
	UINT64 avc420Begins;
	UINT64 avc420Ends;
	UINT64 avc444SurfaceSets;
	UINT64 avc444SurfaceClears;
	UINT64 avc444Frames;
	BOOL avc444SurfacesReady;
	UINT32 avc444Width;
	UINT32 avc444Height;
	UINT32 avc444LumaTexture;
	UINT32 avc444ChromaTexture;
	UINT64 avc444LumaSurfaceId;
	UINT64 avc444ChromaSurfaceId;
	UINT32 lastAvc444SurfaceId;
	UINT32 lastAvc444Width;
	UINT32 lastAvc444Height;
	UINT32 lastAvc444Op;
	UINT32 lastAvc444CodecId;
	char diagnostics[OHOS_COMPOSITOR_DIAGNOSTICS_SIZE];
};

static void ohos_compositor_format_message(char* message, size_t size, const char* format, ...)
{
	if (!message || size == 0)
		return;

	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, size, format, args);
	va_end(args);
}

static const char* ohos_compositor_mode_name(UINT32 mode)
{
	switch (mode)
	{
		case FREERDP_OHOS_COMPOSITOR_MODE_RGBA:
			return "rgba";
		case FREERDP_OHOS_COMPOSITOR_MODE_AVC420_SURFACE:
			return "avc420-surface";
		case FREERDP_OHOS_COMPOSITOR_MODE_AVC444_GPU:
			return "avc444-gpu";
		case FREERDP_OHOS_COMPOSITOR_MODE_NONE:
		default:
			return "none";
	}
}

static void ohos_compositor_log(freerdpOhosCompositor* compositor, const char* format, ...)
{
	if (!compositor)
		return;

	FREERDP_OHOS_COMPOSITOR_LOG_CALLBACK log = NULL;
	void* userData = NULL;
	EnterCriticalSection(&compositor->lock);
	log = compositor->log;
	userData = compositor->userData;
	LeaveCriticalSection(&compositor->lock);
	if (!log)
		return;

	char message[384] = { 0 };
	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	log(message, userData);
}

freerdpOhosCompositor* freerdp_ohos_compositor_new(void)
{
	freerdpOhosCompositor* compositor =
	    (freerdpOhosCompositor*)calloc(1, sizeof(freerdpOhosCompositor));
	if (!compositor)
		return NULL;

	InitializeCriticalSection(&compositor->lock);
	compositor->initialized = TRUE;
	compositor->mode = FREERDP_OHOS_COMPOSITOR_MODE_NONE;
	return compositor;
}

void freerdp_ohos_compositor_free(freerdpOhosCompositor* compositor)
{
	if (!compositor)
		return;
	if (compositor->initialized)
		DeleteCriticalSection(&compositor->lock);
	free(compositor);
}

BOOL freerdp_ohos_compositor_configure(freerdpOhosCompositor* compositor,
                                       const FREERDP_OHOS_COMPOSITOR_CONFIG* config,
                                       char* message, size_t messageSize)
{
	if (!compositor || !config)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor configure input invalid");
		return FALSE;
	}

	EnterCriticalSection(&compositor->lock);
	compositor->log = config->log;
	compositor->userData = config->userData;
	LeaveCriticalSection(&compositor->lock);

	ohos_compositor_format_message(message, messageSize,
	                               "OHOS compositor configured: log=%s",
	                               config->log ? "yes" : "no");
	return TRUE;
}

void freerdp_ohos_compositor_reset(freerdpOhosCompositor* compositor)
{
	if (!compositor)
		return;

	EnterCriticalSection(&compositor->lock);
	compositor->outputWindow = NULL;
	compositor->outputWidth = 0;
	compositor->outputHeight = 0;
	compositor->outputGeneration = 0;
	compositor->mode = FREERDP_OHOS_COMPOSITOR_MODE_NONE;
	compositor->outputTargetSets = 0;
	compositor->outputTargetClears = 0;
	compositor->avc420Begins = 0;
	compositor->avc420Ends = 0;
	compositor->avc444SurfaceSets = 0;
	compositor->avc444SurfaceClears = 0;
	compositor->avc444Frames = 0;
	compositor->avc444SurfacesReady = FALSE;
	compositor->avc444Width = 0;
	compositor->avc444Height = 0;
	compositor->avc444LumaTexture = 0;
	compositor->avc444ChromaTexture = 0;
	compositor->avc444LumaSurfaceId = 0;
	compositor->avc444ChromaSurfaceId = 0;
	compositor->lastAvc444SurfaceId = 0;
	compositor->lastAvc444Width = 0;
	compositor->lastAvc444Height = 0;
	compositor->lastAvc444Op = 0;
	compositor->lastAvc444CodecId = 0;
	compositor->diagnostics[0] = '\0';
	LeaveCriticalSection(&compositor->lock);
}

BOOL freerdp_ohos_compositor_set_output_target(
    freerdpOhosCompositor* compositor, const FREERDP_OHOS_COMPOSITOR_OUTPUT_TARGET* target,
    char* message, size_t messageSize)
{
	if (!compositor || !target)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor output target input invalid");
		return FALSE;
	}
	if (!target->window || (target->width == 0) || (target->height == 0))
	{
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor output target is not ready");
		return FALSE;
	}

	UINT32 generation = 0;
	EnterCriticalSection(&compositor->lock);
	compositor->outputWindow = target->window;
	compositor->outputWidth = target->width;
	compositor->outputHeight = target->height;
	generation = ++compositor->outputGeneration;
	compositor->outputTargetSets++;
	LeaveCriticalSection(&compositor->lock);

	ohos_compositor_format_message(message, messageSize,
	                               "OHOS compositor output target set: %ux%u generation=%" PRIu32,
	                               target->width, target->height, generation);
	return TRUE;
}

BOOL freerdp_ohos_compositor_clear_output_target(freerdpOhosCompositor* compositor,
                                                 char* message, size_t messageSize)
{
	if (!compositor)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor output target input invalid");
		return FALSE;
	}

	EnterCriticalSection(&compositor->lock);
	compositor->outputWindow = NULL;
	compositor->outputWidth = 0;
	compositor->outputHeight = 0;
	compositor->mode = FREERDP_OHOS_COMPOSITOR_MODE_NONE;
	compositor->outputTargetClears++;
	LeaveCriticalSection(&compositor->lock);

	ohos_compositor_format_message(message, messageSize,
	                               "OHOS compositor output target cleared");
	return TRUE;
}

BOOL freerdp_ohos_compositor_begin_avc420_surface(freerdpOhosCompositor* compositor,
                                                  char* message, size_t messageSize)
{
	if (!compositor)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor AVC420 input invalid");
		return FALSE;
	}

	UINT32 width = 0;
	UINT32 height = 0;
	UINT32 generation = 0;
	EnterCriticalSection(&compositor->lock);
	if (!compositor->outputWindow || (compositor->outputWidth == 0) ||
	    (compositor->outputHeight == 0))
	{
		LeaveCriticalSection(&compositor->lock);
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor AVC420 route unavailable: no output target");
		return FALSE;
	}
	compositor->mode = FREERDP_OHOS_COMPOSITOR_MODE_AVC420_SURFACE;
	compositor->avc420Begins++;
	width = compositor->outputWidth;
	height = compositor->outputHeight;
	generation = compositor->outputGeneration;
	LeaveCriticalSection(&compositor->lock);

	ohos_compositor_format_message(
	    message, messageSize,
	    "OHOS compositor AVC420 surface route ready: target=%ux%u generation=%" PRIu32
	    " appRendererReleaseRequired=yes",
	    width, height, generation);
	ohos_compositor_log(compositor, "%s", message ? message : "OHOS compositor AVC420 ready");
	return TRUE;
}

void freerdp_ohos_compositor_end_avc420_surface(freerdpOhosCompositor* compositor)
{
	if (!compositor)
		return;

	EnterCriticalSection(&compositor->lock);
	if (compositor->mode == FREERDP_OHOS_COMPOSITOR_MODE_AVC420_SURFACE)
		compositor->mode = FREERDP_OHOS_COMPOSITOR_MODE_NONE;
	compositor->avc420Ends++;
	LeaveCriticalSection(&compositor->lock);
}

BOOL freerdp_ohos_compositor_set_avc444_decode_surfaces(
    freerdpOhosCompositor* compositor, const FREERDP_OHOS_AVC444_SURFACE_TARGETS* targets,
    BOOL enabled, char* message, size_t messageSize)
{
	if (!compositor)
	{
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor AVC444 surface input invalid");
		return FALSE;
	}

	if (!enabled)
	{
		EnterCriticalSection(&compositor->lock);
		compositor->avc444SurfacesReady = FALSE;
		compositor->avc444Width = 0;
		compositor->avc444Height = 0;
		compositor->avc444LumaTexture = 0;
		compositor->avc444ChromaTexture = 0;
		compositor->avc444LumaSurfaceId = 0;
		compositor->avc444ChromaSurfaceId = 0;
		compositor->avc444SurfaceClears++;
		LeaveCriticalSection(&compositor->lock);
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor AVC444 decode surfaces cleared");
		return TRUE;
	}

	if (!targets || !targets->lumaWindow || !targets->chromaWindow || (targets->width == 0) ||
	    (targets->height == 0))
	{
		ohos_compositor_format_message(message, messageSize,
		                               "OHOS compositor AVC444 decode surfaces are not ready");
		return FALSE;
	}

	EnterCriticalSection(&compositor->lock);
	compositor->avc444SurfacesReady = TRUE;
	compositor->avc444Width = targets->width;
	compositor->avc444Height = targets->height;
	compositor->avc444LumaTexture = targets->lumaTexture;
	compositor->avc444ChromaTexture = targets->chromaTexture;
	compositor->avc444LumaSurfaceId = targets->lumaSurfaceId;
	compositor->avc444ChromaSurfaceId = targets->chromaSurfaceId;
	compositor->avc444SurfaceSets++;
	LeaveCriticalSection(&compositor->lock);

	ohos_compositor_format_message(
	    message, messageSize,
	    "OHOS compositor AVC444 decode surfaces set: %ux%u lumaTex=%" PRIu32
	    " chromaTex=%" PRIu32 " lumaSurface=%" PRIu64 " chromaSurface=%" PRIu64
	    " route=disabled-until-gpu-shader",
	    targets->width, targets->height, targets->lumaTexture, targets->chromaTexture,
	    targets->lumaSurfaceId, targets->chromaSurfaceId);
	return TRUE;
}

void freerdp_ohos_compositor_notify_avc444_frame(freerdpOhosCompositor* compositor,
                                                 UINT32 surfaceId, UINT32 width, UINT32 height,
                                                 UINT32 op, UINT32 codecId)
{
	if (!compositor)
		return;

	EnterCriticalSection(&compositor->lock);
	compositor->avc444Frames++;
	compositor->lastAvc444SurfaceId = surfaceId;
	compositor->lastAvc444Width = width;
	compositor->lastAvc444Height = height;
	compositor->lastAvc444Op = op;
	compositor->lastAvc444CodecId = codecId;
	LeaveCriticalSection(&compositor->lock);
}

const char* freerdp_ohos_compositor_get_diagnostics(freerdpOhosCompositor* compositor)
{
	if (!compositor)
		return "ohos compositor: unavailable";

	EnterCriticalSection(&compositor->lock);
	(void)snprintf(
	    compositor->diagnostics, sizeof(compositor->diagnostics),
	    "ohos compositor: mode=%s output=%s:%ux%u generation=%" PRIu32
	    " target=set:%" PRIu64 ",clear:%" PRIu64
	    " avc420=begin:%" PRIu64 ",end:%" PRIu64
	    " avc444Surfaces=%s:%ux%u set:%" PRIu64 ",clear:%" PRIu64
	    " lumaTex=%" PRIu32 " chromaTex=%" PRIu32 " lumaSurface=%" PRIu64
	    " chromaSurface=%" PRIu64
	    " avc444Frames=%" PRIu64 " lastAvc444=surface:%" PRIu32
	    " size:%" PRIu32 "x%" PRIu32 " op:%" PRIu32 " codec:%" PRIu32,
	    ohos_compositor_mode_name(compositor->mode),
	    compositor->outputWindow ? "ready" : "none", compositor->outputWidth,
	    compositor->outputHeight, compositor->outputGeneration, compositor->outputTargetSets,
	    compositor->outputTargetClears, compositor->avc420Begins, compositor->avc420Ends,
	    compositor->avc444SurfacesReady ? "ready" : "none", compositor->avc444Width,
	    compositor->avc444Height, compositor->avc444SurfaceSets,
	    compositor->avc444SurfaceClears, compositor->avc444LumaTexture,
	    compositor->avc444ChromaTexture, compositor->avc444LumaSurfaceId,
	    compositor->avc444ChromaSurfaceId, compositor->avc444Frames,
	    compositor->lastAvc444SurfaceId, compositor->lastAvc444Width,
	    compositor->lastAvc444Height, compositor->lastAvc444Op,
	    compositor->lastAvc444CodecId);
	LeaveCriticalSection(&compositor->lock);
	return compositor->diagnostics;
}
