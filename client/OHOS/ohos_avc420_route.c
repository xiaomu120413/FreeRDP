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

#define OHOS_AVC420_ROUTE_DIAGNOSTICS_SIZE 768

typedef void (*pfnOhosAvcodecFallbackCallback)(const char* reason, void* userData);

FREERDP_API BOOL freerdp_ohos_avcodec_set_output_surface(void* window, UINT32 width, UINT32 height,
                                                         BOOL enabled);
FREERDP_API BOOL freerdp_ohos_avcodec_set_fallback_callback(pfnOhosAvcodecFallbackCallback callback,
                                                            void* userData);

struct freerdp_ohos_avc420_route
{
	CRITICAL_SECTION lock;
	BOOL initialized;
	BOOL armed;
	BOOL active;
	FREERDP_OHOS_AVC420_ROUTE_LOG_CALLBACK log;
	FREERDP_OHOS_AVC420_ROUTE_GET_TARGET_CALLBACK getOutputTarget;
	FREERDP_OHOS_AVC420_ROUTE_WINDOW_CALLBACK prepareOutputTarget;
	FREERDP_OHOS_AVC420_ROUTE_WINDOW_CALLBACK restoreOutputTarget;
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
	UINT64 refreshes;
	UINT64 fallbackRequests;
	UINT64 surfaceSetFailures;
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

static const char* ohos_avc420_route_reason(const char* reason)
{
	return (reason && reason[0] != '\0') ? reason : "unspecified";
}

static void ohos_avc420_route_snapshot_callbacks(
    freerdpOhosAvc420Route* route, FREERDP_OHOS_AVC420_ROUTE_LOG_CALLBACK* log,
    FREERDP_OHOS_AVC420_ROUTE_GET_TARGET_CALLBACK* getOutputTarget,
    FREERDP_OHOS_AVC420_ROUTE_WINDOW_CALLBACK* prepareOutputTarget,
    FREERDP_OHOS_AVC420_ROUTE_WINDOW_CALLBACK* restoreOutputTarget, void** userData)
{
	if (!route)
		return;

	EnterCriticalSection(&route->lock);
	if (log)
		*log = route->log;
	if (getOutputTarget)
		*getOutputTarget = route->getOutputTarget;
	if (prepareOutputTarget)
		*prepareOutputTarget = route->prepareOutputTarget;
	if (restoreOutputTarget)
		*restoreOutputTarget = route->restoreOutputTarget;
	if (userData)
		*userData = route->userData;
	LeaveCriticalSection(&route->lock);
}

static void ohos_avc420_route_log(freerdpOhosAvc420Route* route, const char* format, ...)
{
	if (!route)
		return;

	FREERDP_OHOS_AVC420_ROUTE_LOG_CALLBACK log = NULL;
	void* userData = NULL;
	ohos_avc420_route_snapshot_callbacks(route, &log, NULL, NULL, NULL, &userData);
	if (!log)
		return;

	char message[512] = { 0 };
	va_list args;
	va_start(args, format);
	(void)vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	log(message, userData);
}

static BOOL ohos_avc420_route_get_target(freerdpOhosAvc420Route* route,
                                         FREERDP_OHOS_AVC420_ROUTE_OUTPUT_TARGET* target,
                                         const char* reason, char* message, size_t messageSize)
{
	FREERDP_OHOS_AVC420_ROUTE_GET_TARGET_CALLBACK getTarget = NULL;
	void* userData = NULL;

	if (!route || !target)
	{
		ohos_avc420_route_format_message(message, messageSize,
		                                 "OHOS AVC420 route target input invalid");
		return FALSE;
	}

	ohos_avc420_route_snapshot_callbacks(route, NULL, &getTarget, NULL, NULL, &userData);
	if (getTarget)
	{
		*target = (FREERDP_OHOS_AVC420_ROUTE_OUTPUT_TARGET){ 0 };
		if (!getTarget(target, userData))
		{
			ohos_avc420_route_format_message(
			    message, messageSize,
			    "OHOS AVC420 route target unavailable after %s: target callback failed",
			    ohos_avc420_route_reason(reason));
			return FALSE;
		}
	}
	else
	{
		EnterCriticalSection(&route->lock);
		target->window = route->outputWindow;
		target->width = route->outputWidth;
		target->height = route->outputHeight;
		LeaveCriticalSection(&route->lock);
	}

	if (!target->window || (target->width == 0) || (target->height == 0))
	{
		ohos_avc420_route_format_message(
		    message, messageSize,
		    "OHOS AVC420 route target unavailable after %s: window=%p size=%ux%u",
		    ohos_avc420_route_reason(reason), target->window, target->width, target->height);
		return FALSE;
	}
	return TRUE;
}

static void ohos_avc420_route_disable_surface(freerdpOhosAvc420Route* route, const char* reason,
                                              BOOL fallback, char* message, size_t messageSize)
{
	void* window = NULL;
	UINT64 fallbackCount = 0;
	FREERDP_OHOS_AVC420_ROUTE_WINDOW_CALLBACK restore = NULL;
	void* userData = NULL;

	if (!route)
		return;

	EnterCriticalSection(&route->lock);
	window = route->outputWindow;
	route->armed = FALSE;
	if (route->active || (route->mode == FREERDP_OHOS_AVC420_ROUTE_MODE_AVC420_SURFACE))
		route->avc420Ends++;
	route->active = FALSE;
	route->mode = FREERDP_OHOS_AVC420_ROUTE_MODE_NONE;
	if (fallback)
		fallbackCount = ++route->fallbackRequests;
	LeaveCriticalSection(&route->lock);

	(void)freerdp_ohos_avcodec_set_output_surface(NULL, 0, 0, FALSE);
	ohos_avc420_route_snapshot_callbacks(route, NULL, NULL, NULL, &restore, &userData);
	if (restore && window)
		restore(window, ohos_avc420_route_reason(reason), userData);

	ohos_avc420_route_format_message(
	    message, messageSize,
	    fallback ? "OHOS AVC420 route disabled after AVCodec fallback: %s fallbacks=%" PRIu64
	             : "OHOS AVC420 route disabled: %s",
	    ohos_avc420_route_reason(reason), fallbackCount);
	ohos_avc420_route_log(route, "%s", message ? message : "OHOS AVC420 route disabled");
}

static BOOL ohos_avc420_route_activate_surface(freerdpOhosAvc420Route* route, const char* reason,
                                               char* message, size_t messageSize)
{
	FREERDP_OHOS_AVC420_ROUTE_OUTPUT_TARGET target = { 0 };
	FREERDP_OHOS_AVC420_ROUTE_WINDOW_CALLBACK prepare = NULL;
	void* userData = NULL;
	UINT32 generation = 0;
	BOOL wasActive = FALSE;

	if (!route)
	{
		ohos_avc420_route_format_message(message, messageSize, "OHOS AVC420 route input invalid");
		return FALSE;
	}

	EnterCriticalSection(&route->lock);
	const BOOL armed = route->armed;
	LeaveCriticalSection(&route->lock);
	if (!armed)
	{
		ohos_avc420_route_format_message(message, messageSize,
		                                 "OHOS AVC420 route unavailable: not armed");
		return FALSE;
	}

	if (!ohos_avc420_route_get_target(route, &target, reason, message, messageSize))
		return FALSE;

	ohos_avc420_route_snapshot_callbacks(route, NULL, NULL, &prepare, NULL, &userData);
	if (prepare)
		prepare(target.window, ohos_avc420_route_reason(reason), userData);

	if (!freerdp_ohos_avcodec_set_output_surface(target.window, target.width, target.height, TRUE))
	{
		EnterCriticalSection(&route->lock);
		route->surfaceSetFailures++;
		route->active = FALSE;
		route->mode = FREERDP_OHOS_AVC420_ROUTE_MODE_NONE;
		LeaveCriticalSection(&route->lock);
		ohos_avc420_route_format_message(
		    message, messageSize,
		    "OHOS AVC420 route failed after %s: set AVCodec output surface failed",
		    ohos_avc420_route_reason(reason));
		return FALSE;
	}

	EnterCriticalSection(&route->lock);
	wasActive = route->active;
	route->active = TRUE;
	route->mode = FREERDP_OHOS_AVC420_ROUTE_MODE_AVC420_SURFACE;
	route->outputWindow = target.window;
	route->outputWidth = target.width;
	route->outputHeight = target.height;
	generation = ++route->outputGeneration;
	route->outputTargetSets++;
	route->avc420Begins++;
	LeaveCriticalSection(&route->lock);

	ohos_avc420_route_format_message(
	    message, messageSize,
	    wasActive ? "OHOS AVC420 surface route refreshed: target=%ux%u generation=%" PRIu32
	              : "OHOS AVC420 surface route active: target=%ux%u generation=%" PRIu32
	                " appRendererReleaseRequired=yes",
	    target.width, target.height, generation);
	ohos_avc420_route_log(route, "%s", message ? message : "OHOS AVC420 route active");
	return TRUE;
}

static void ohos_avc420_route_avcodec_fallback(const char* reason, void* userData)
{
	freerdpOhosAvc420Route* route = (freerdpOhosAvc420Route*)userData;
	char message[256] = { 0 };
	ohos_avc420_route_disable_surface(route, reason, TRUE, message, sizeof(message));
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
	(void)freerdp_ohos_avcodec_set_fallback_callback(NULL, NULL);
	ohos_avc420_route_disable_surface(route, "route free", FALSE, NULL, 0);
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
	route->getOutputTarget = config->getOutputTarget;
	route->prepareOutputTarget = config->prepareOutputTarget;
	route->restoreOutputTarget = config->restoreOutputTarget;
	route->userData = config->userData;
	LeaveCriticalSection(&route->lock);
	(void)freerdp_ohos_avcodec_set_fallback_callback(ohos_avc420_route_avcodec_fallback, route);

	ohos_avc420_route_format_message(
	    message, messageSize,
	    "OHOS AVC420 route configured: log=%s targetCallback=%s fallback=route-owned",
	    config->log ? "yes" : "no", config->getOutputTarget ? "yes" : "no");
	return TRUE;
}

void freerdp_ohos_avc420_route_reset(freerdpOhosAvc420Route* route)
{
	if (!route)
		return;

	ohos_avc420_route_disable_surface(route, "route reset", FALSE, NULL, 0);
	EnterCriticalSection(&route->lock);
	route->outputWindow = NULL;
	route->outputWidth = 0;
	route->outputHeight = 0;
	route->outputGeneration = 0;
	route->outputTargetSets = 0;
	route->outputTargetClears = 0;
	route->avc420Begins = 0;
	route->avc420Ends = 0;
	route->refreshes = 0;
	route->fallbackRequests = 0;
	route->surfaceSetFailures = 0;
	route->diagnostics[0] = '\0';
	LeaveCriticalSection(&route->lock);
}

BOOL freerdp_ohos_avc420_route_set_armed(freerdpOhosAvc420Route* route, BOOL armed, char* message,
                                         size_t messageSize)
{
	if (!route)
	{
		ohos_avc420_route_format_message(message, messageSize,
		                                 "OHOS AVC420 route arm input invalid");
		return FALSE;
	}

	if (!armed)
	{
		ohos_avc420_route_disable_surface(route, "route disarmed", FALSE, message, messageSize);
		return TRUE;
	}

	EnterCriticalSection(&route->lock);
	route->armed = TRUE;
	LeaveCriticalSection(&route->lock);
	ohos_avc420_route_format_message(
	    message, messageSize,
	    "OHOS AVC420 route armed: activation deferred until AVC420 surface command");
	return TRUE;
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

	ohos_avc420_route_format_message(
	    message, messageSize, "OHOS AVC420 route output target set: %ux%u generation=%" PRIu32,
	    target->width, target->height, generation);
	return TRUE;
}

BOOL freerdp_ohos_avc420_route_clear_output_target(freerdpOhosAvc420Route* route, char* message,
                                                   size_t messageSize)
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
	route->active = FALSE;
	route->outputTargetClears++;
	LeaveCriticalSection(&route->lock);
	(void)freerdp_ohos_avcodec_set_output_surface(NULL, 0, 0, FALSE);

	ohos_avc420_route_format_message(message, messageSize,
	                                 "OHOS AVC420 route output target cleared");
	return TRUE;
}

BOOL freerdp_ohos_avc420_route_begin_surface(freerdpOhosAvc420Route* route, char* message,
                                             size_t messageSize)
{
	return ohos_avc420_route_activate_surface(route, "RDPGFX AVC420 surface command", message,
	                                          messageSize);
}

BOOL freerdp_ohos_avc420_route_refresh_output_target(freerdpOhosAvc420Route* route,
                                                     const char* reason, char* message,
                                                     size_t messageSize)
{
	BOOL active = FALSE;
	if (!route)
	{
		ohos_avc420_route_format_message(message, messageSize,
		                                 "OHOS AVC420 route refresh input invalid");
		return FALSE;
	}

	EnterCriticalSection(&route->lock);
	active = route->active;
	if (active)
		route->refreshes++;
	LeaveCriticalSection(&route->lock);
	if (!active)
	{
		ohos_avc420_route_format_message(message, messageSize,
		                                 "OHOS AVC420 route refresh skipped after %s: inactive",
		                                 ohos_avc420_route_reason(reason));
		return TRUE;
	}

	if (!ohos_avc420_route_activate_surface(route, reason, message, messageSize))
	{
		void* window = NULL;
		FREERDP_OHOS_AVC420_ROUTE_WINDOW_CALLBACK restore = NULL;
		void* userData = NULL;
		EnterCriticalSection(&route->lock);
		window = route->outputWindow;
		if (route->active || (route->mode == FREERDP_OHOS_AVC420_ROUTE_MODE_AVC420_SURFACE))
			route->avc420Ends++;
		route->active = FALSE;
		route->mode = FREERDP_OHOS_AVC420_ROUTE_MODE_NONE;
		LeaveCriticalSection(&route->lock);
		(void)freerdp_ohos_avcodec_set_output_surface(NULL, 0, 0, FALSE);
		ohos_avc420_route_snapshot_callbacks(route, NULL, NULL, NULL, &restore, &userData);
		if (restore && window)
			restore(window, message, userData);
		return FALSE;
	}
	return TRUE;
}

void freerdp_ohos_avc420_route_end_surface(freerdpOhosAvc420Route* route)
{
	if (!route)
		return;

	EnterCriticalSection(&route->lock);
	if (route->mode == FREERDP_OHOS_AVC420_ROUTE_MODE_AVC420_SURFACE)
		route->mode = FREERDP_OHOS_AVC420_ROUTE_MODE_NONE;
	route->active = FALSE;
	route->avc420Ends++;
	LeaveCriticalSection(&route->lock);
	(void)freerdp_ohos_avcodec_set_output_surface(NULL, 0, 0, FALSE);
}

BOOL freerdp_ohos_avc420_route_is_surface_active(freerdpOhosAvc420Route* route)
{
	if (!route)
		return FALSE;

	BOOL active = FALSE;
	EnterCriticalSection(&route->lock);
	active = route->active;
	LeaveCriticalSection(&route->lock);
	return active;
}

const char* freerdp_ohos_avc420_route_get_diagnostics(freerdpOhosAvc420Route* route)
{
	if (!route)
		return "ohos avc420 route: unavailable";

	EnterCriticalSection(&route->lock);
	(void)snprintf(
	    route->diagnostics, sizeof(route->diagnostics),
	    "ohos avc420 route: armed=%s active=%s mode=%s output=%s:%ux%u generation=%" PRIu32
	    " target=set:%" PRIu64 ",clear:%" PRIu64 " avc420=begin:%" PRIu64 ",end:%" PRIu64
	    " refresh=%" PRIu64 " fallbacks=%" PRIu64 " surfaceSetFailures=%" PRIu64,
	    route->armed ? "yes" : "no", route->active ? "yes" : "no",
	    ohos_avc420_route_mode_name(route->mode), route->outputWindow ? "ready" : "none",
	    route->outputWidth, route->outputHeight, route->outputGeneration, route->outputTargetSets,
	    route->outputTargetClears, route->avc420Begins, route->avc420Ends, route->refreshes,
	    route->fallbackRequests, route->surfaceSetFailures);
	LeaveCriticalSection(&route->lock);
	return route->diagnostics;
}
