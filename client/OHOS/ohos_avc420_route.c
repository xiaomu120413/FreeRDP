/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS AVC420 surface route state and diagnostics
 */

#include "ohos_avc420_route.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <winpr/synch.h>

#define OHOS_AVC420_ROUTE_DIAGNOSTICS_SIZE 512

struct freerdp_ohos_avc420_route
{
	CRITICAL_SECTION lock;
	BOOL initialized;
	FREERDP_OHOS_AVC420_ROUTE_LOG_CALLBACK log;
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
	char diagnostics[OHOS_AVC420_ROUTE_DIAGNOSTICS_SIZE];
};

static void ohos_avc420_route_format_message(char* message, size_t size, const char* format, ...)
{
	if (!message || size == 0)
		return;

	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, size, format, args);
	va_end(args);
}

static const char* ohos_avc420_route_mode_name(UINT32 mode)
{
	switch (mode)
	{
		case FREERDP_OHOS_AVC420_ROUTE_MODE_AVC420_SURFACE:
			return "avc420-surface";
		case FREERDP_OHOS_AVC420_ROUTE_MODE_NONE:
		default:
			return "none";
	}
}

static void ohos_avc420_route_log(freerdpOhosAvc420Route* route, const char* format, ...)
{
	if (!route)
		return;

	FREERDP_OHOS_AVC420_ROUTE_LOG_CALLBACK log = NULL;
	void* userData = NULL;
	EnterCriticalSection(&route->lock);
	log = route->log;
	userData = route->userData;
	LeaveCriticalSection(&route->lock);
	if (!log)
		return;

	char message[384] = { 0 };
	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	log(message, userData);
}

freerdpOhosAvc420Route* freerdp_ohos_avc420_route_new(void)
{
	freerdpOhosAvc420Route* route =
	    (freerdpOhosAvc420Route*)calloc(1, sizeof(freerdpOhosAvc420Route));
	if (!route)
		return NULL;

	InitializeCriticalSection(&route->lock);
	route->initialized = TRUE;
	route->mode = FREERDP_OHOS_AVC420_ROUTE_MODE_NONE;
	return route;
}

void freerdp_ohos_avc420_route_free(freerdpOhosAvc420Route* route)
{
	if (!route)
		return;
	if (route->initialized)
		DeleteCriticalSection(&route->lock);
	free(route);
}

BOOL freerdp_ohos_avc420_route_configure(freerdpOhosAvc420Route* route,
                                       const FREERDP_OHOS_AVC420_ROUTE_CONFIG* config,
                                       char* message, size_t messageSize)
{
	if (!route || !config)
	{
		ohos_avc420_route_format_message(message, messageSize,
		                               "OHOS AVC420 route configure input invalid");
		return FALSE;
	}

	EnterCriticalSection(&route->lock);
	route->log = config->log;
	route->userData = config->userData;
	LeaveCriticalSection(&route->lock);

	ohos_avc420_route_format_message(message, messageSize,
	                               "OHOS AVC420 route configured: log=%s",
	                               config->log ? "yes" : "no");
	return TRUE;
}

void freerdp_ohos_avc420_route_reset(freerdpOhosAvc420Route* route)
{
	if (!route)
		return;

	EnterCriticalSection(&route->lock);
	route->outputWindow = NULL;
	route->outputWidth = 0;
	route->outputHeight = 0;
	route->outputGeneration = 0;
	route->mode = FREERDP_OHOS_AVC420_ROUTE_MODE_NONE;
	route->outputTargetSets = 0;
	route->outputTargetClears = 0;
	route->avc420Begins = 0;
	route->avc420Ends = 0;
	route->diagnostics[0] = '\0';
	LeaveCriticalSection(&route->lock);
}

BOOL freerdp_ohos_avc420_route_set_output_target(
    freerdpOhosAvc420Route* route, const FREERDP_OHOS_AVC420_ROUTE_OUTPUT_TARGET* target,
    char* message, size_t messageSize)
{
	if (!route || !target)
	{
		ohos_avc420_route_format_message(message, messageSize,
		                               "OHOS AVC420 route output target input invalid");
		return FALSE;
	}
	if (!target->window || (target->width == 0) || (target->height == 0))
	{
		ohos_avc420_route_format_message(message, messageSize,
		                               "OHOS AVC420 route output target is not ready");
		return FALSE;
	}

	UINT32 generation = 0;
	EnterCriticalSection(&route->lock);
	route->outputWindow = target->window;
	route->outputWidth = target->width;
	route->outputHeight = target->height;
	generation = ++route->outputGeneration;
	route->outputTargetSets++;
	LeaveCriticalSection(&route->lock);

	ohos_avc420_route_format_message(message, messageSize,
	                               "OHOS AVC420 route output target set: %ux%u generation=%" PRIu32,
	                               target->width, target->height, generation);
	return TRUE;
}

BOOL freerdp_ohos_avc420_route_clear_output_target(freerdpOhosAvc420Route* route,
                                                 char* message, size_t messageSize)
{
	if (!route)
	{
		ohos_avc420_route_format_message(message, messageSize,
		                               "OHOS AVC420 route output target input invalid");
		return FALSE;
	}

	EnterCriticalSection(&route->lock);
	route->outputWindow = NULL;
	route->outputWidth = 0;
	route->outputHeight = 0;
	route->mode = FREERDP_OHOS_AVC420_ROUTE_MODE_NONE;
	route->outputTargetClears++;
	LeaveCriticalSection(&route->lock);

	ohos_avc420_route_format_message(message, messageSize,
	                               "OHOS AVC420 route output target cleared");
	return TRUE;
}

BOOL freerdp_ohos_avc420_route_begin_surface(freerdpOhosAvc420Route* route, char* message,
                                             size_t messageSize)
{
	if (!route)
	{
		ohos_avc420_route_format_message(message, messageSize,
		                               "OHOS AVC420 route input invalid");
		return FALSE;
	}

	UINT32 width = 0;
	UINT32 height = 0;
	UINT32 generation = 0;
	EnterCriticalSection(&route->lock);
	if (!route->outputWindow || (route->outputWidth == 0) ||
	    (route->outputHeight == 0))
	{
		LeaveCriticalSection(&route->lock);
		ohos_avc420_route_format_message(message, messageSize,
		                               "OHOS AVC420 route unavailable: no output target");
		return FALSE;
	}
	route->mode = FREERDP_OHOS_AVC420_ROUTE_MODE_AVC420_SURFACE;
	route->avc420Begins++;
	width = route->outputWidth;
	height = route->outputHeight;
	generation = route->outputGeneration;
	LeaveCriticalSection(&route->lock);

	ohos_avc420_route_format_message(
	    message, messageSize,
	    "OHOS AVC420 surface route ready: target=%ux%u generation=%" PRIu32
	    " appRendererReleaseRequired=yes",
	    width, height, generation);
	ohos_avc420_route_log(route, "%s", message ? message : "OHOS AVC420 route ready");
	return TRUE;
}

void freerdp_ohos_avc420_route_end_surface(freerdpOhosAvc420Route* route)
{
	if (!route)
		return;

	EnterCriticalSection(&route->lock);
	if (route->mode == FREERDP_OHOS_AVC420_ROUTE_MODE_AVC420_SURFACE)
		route->mode = FREERDP_OHOS_AVC420_ROUTE_MODE_NONE;
	route->avc420Ends++;
	LeaveCriticalSection(&route->lock);
}

const char* freerdp_ohos_avc420_route_get_diagnostics(freerdpOhosAvc420Route* route)
{
	if (!route)
		return "ohos avc420 route: unavailable";

	EnterCriticalSection(&route->lock);
	(void)snprintf(
	    route->diagnostics, sizeof(route->diagnostics),
	    "ohos avc420 route: mode=%s output=%s:%ux%u generation=%" PRIu32
	    " target=set:%" PRIu64 ",clear:%" PRIu64
	    " avc420=begin:%" PRIu64 ",end:%" PRIu64,
	    ohos_avc420_route_mode_name(route->mode),
	    route->outputWindow ? "ready" : "none", route->outputWidth,
	    route->outputHeight, route->outputGeneration, route->outputTargetSets,
	    route->outputTargetClears, route->avc420Begins, route->avc420Ends);
	LeaveCriticalSection(&route->lock);
	return route->diagnostics;
}
