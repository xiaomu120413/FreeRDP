/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS location channel provider
 */

#include "ohos_location.h"

#include <inttypes.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include <LocationKit/oh_location.h>
#include <freerdp/channels/location.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/location.h>
#include <winpr/error.h>
#include <winpr/wtsapi.h>

struct freerdp_ohos_location
{
	rdpContext* context;
	FREERDP_OHOS_LOCATION_CONFIG config;
	LocationClientContext* location;
	BOOL channelConnectedSubscribed;
	BOOL channelDisconnectedSubscribed;
	UINT64 registerCount;
	UINT64 unregisterCount;
	UINT64 channelConnectCount;
	UINT64 channelDisconnectCount;
	UINT64 startCount;
	UINT64 stopCount;
	UINT64 permissionDeniedCount;
	UINT64 sampleRequestCount;
	UINT64 sampleSuccessCount;
	UINT64 sendCount;
	UINT64 errorCount;
	char diagnostics[512];
	struct freerdp_ohos_location* registryNext;
};

typedef struct
{
	BOOL valid;
	double latitude;
	double longitude;
	INT32 altitude;
	double speed;
	double heading;
	double horizontalAccuracy;
	UINT32 source;
} OHOS_LOCATION_SAMPLE;

typedef struct
{
	pthread_mutex_t mutex;
	pthread_cond_t condition;
	BOOL completed;
	OHOS_LOCATION_SAMPLE sample;
} OHOS_LOCATION_NATIVE_REQUEST;

static pthread_mutex_t g_registryMutex = PTHREAD_MUTEX_INITIALIZER;
static freerdpOhosLocation* g_registryHead = NULL;

static pthread_mutex_t g_locationCallbackLock = PTHREAD_MUTEX_INITIALIZER;
static freerdp_ohos_location_permission_request_fn g_permissionRequest = NULL;
static void* g_permissionRequestUserData = NULL;

static void ohos_location_log(freerdpOhosLocation* location, const char* format, ...)
{
	if (!location || !location->config.Log)
		return;

	char message[512] = { 0 };
	va_list ap;
	va_start(ap, format);
	(void)vsnprintf(message, sizeof(message), format, ap);
	va_end(ap);
	location->config.Log(location->config.logUserData, message);
}

static void ohos_location_set_error(freerdpOhosLocation* location, char* errorBuffer,
                                    size_t errorBufferSize, const char* format, ...)
{
	if (location)
		++location->errorCount;

	if (!errorBuffer || errorBufferSize == 0)
		return;

	va_list ap;
	va_start(ap, format);
	(void)vsnprintf(errorBuffer, errorBufferSize, format, ap);
	va_end(ap);
}

static void ohos_location_registry_add(freerdpOhosLocation* location)
{
	if (!location)
		return;

	pthread_mutex_lock(&g_registryMutex);
	location->registryNext = g_registryHead;
	g_registryHead = location;
	pthread_mutex_unlock(&g_registryMutex);
}

static void ohos_location_registry_remove(freerdpOhosLocation* location)
{
	if (!location)
		return;

	pthread_mutex_lock(&g_registryMutex);
	freerdpOhosLocation** current = &g_registryHead;
	while (*current)
	{
		if (*current == location)
		{
			*current = location->registryNext;
			location->registryNext = NULL;
			break;
		}
		current = &(*current)->registryNext;
	}
	pthread_mutex_unlock(&g_registryMutex);
}

static freerdpOhosLocation* ohos_location_from_context(void* context)
{
	rdpContext* rdp_context = (rdpContext*)context;
	freerdpOhosLocation* found = NULL;

	pthread_mutex_lock(&g_registryMutex);
	for (freerdpOhosLocation* current = g_registryHead; current; current = current->registryNext)
	{
		if (current->context == rdp_context)
		{
			found = current;
			break;
		}
	}
	pthread_mutex_unlock(&g_registryMutex);
	return found;
}

static BOOL ohos_location_request_permission(UINT32 timeoutMs)
{
	freerdp_ohos_location_permission_request_fn callback = NULL;
	void* userData = NULL;

	pthread_mutex_lock(&g_locationCallbackLock);
	callback = g_permissionRequest;
	userData = g_permissionRequestUserData;
	pthread_mutex_unlock(&g_locationCallbackLock);

	if (!callback)
		return FALSE;
	return callback(userData, timeoutMs);
}

static BOOL ohos_location_basic_info_valid(const Location_BasicInfo* info)
{
	return info && isfinite(info->latitude) && isfinite(info->longitude) &&
	       info->latitude >= -90.0 && info->latitude <= 90.0 &&
	       info->longitude >= -180.0 && info->longitude <= 180.0;
}

static INT32 ohos_location_altitude_to_int32(double altitude)
{
	if (!isfinite(altitude))
		return 0;
	if (altitude > (double)INT32_MAX)
		return INT32_MAX;
	if (altitude < (double)INT32_MIN)
		return INT32_MIN;
	return (INT32)lround(altitude);
}

static UINT32 ohos_location_map_source(Location_SourceType source)
{
	switch (source)
	{
		case LOCATION_SOURCE_TYPE_GNSS:
		case LOCATION_SOURCE_TYPE_RTK:
			return LOCATIONSOURCE_GNSS;
		case LOCATION_SOURCE_TYPE_NETWORK:
			return LOCATIONSOURCE_IP;
		case LOCATION_SOURCE_TYPE_INDOOR:
			return LOCATIONSOURCE_WIFI;
		default:
			return LOCATIONSOURCE_GNSS;
	}
}

static void ohos_location_add_timeout(struct timespec* deadline, UINT32 timeoutMs)
{
	const time_t seconds = (time_t)(timeoutMs / 1000U);
	const long nanoseconds = (long)(timeoutMs % 1000U) * 1000000L;
	deadline->tv_sec += seconds;
	deadline->tv_nsec += nanoseconds;
	if (deadline->tv_nsec >= 1000000000L)
	{
		deadline->tv_sec += 1;
		deadline->tv_nsec -= 1000000000L;
	}
}

static void ohos_location_info_callback(Location_Info* locationInfo, void* userData)
{
	OHOS_LOCATION_NATIVE_REQUEST* request = (OHOS_LOCATION_NATIVE_REQUEST*)userData;
	if (!request || !locationInfo)
		return;

	const Location_BasicInfo basicInfo = OH_LocationInfo_GetBasicInfo(locationInfo);

	pthread_mutex_lock(&request->mutex);
	if (!request->completed)
	{
		request->sample.valid = ohos_location_basic_info_valid(&basicInfo);
		request->sample.latitude = basicInfo.latitude;
		request->sample.longitude = basicInfo.longitude;
		request->sample.altitude = ohos_location_altitude_to_int32(basicInfo.altitude);
		request->sample.speed = isfinite(basicInfo.speed) ? basicInfo.speed : 0.0;
		request->sample.heading = isfinite(basicInfo.direction) ? basicInfo.direction : 0.0;
		request->sample.horizontalAccuracy = isfinite(basicInfo.accuracy) ? basicInfo.accuracy : 0.0;
		request->sample.source = ohos_location_map_source(basicInfo.locationSourceType);
		request->completed = TRUE;
		pthread_cond_signal(&request->condition);
	}
	pthread_mutex_unlock(&request->mutex);
}

static BOOL ohos_location_request_native_sample(freerdpOhosLocation* location, UINT32 timeoutMs,
                                                OHOS_LOCATION_SAMPLE* sample)
{
	if (!sample)
		return FALSE;
	*sample = (OHOS_LOCATION_SAMPLE){ 0 };

	bool enabled = false;
	Location_ResultCode rc = OH_Location_IsLocatingEnabled(&enabled);
	if (rc != LOCATION_SUCCESS)
	{
		ohos_location_log(location, "OHOS native location switch query failed: %d", (int)rc);
		return FALSE;
	}
	if (!enabled)
	{
		ohos_location_log(location, "OHOS native location switch is off");
		return FALSE;
	}

	Location_RequestConfig* config = OH_Location_CreateRequestConfig();
	if (!config)
	{
		ohos_location_log(location, "OHOS native location request config allocation failed");
		return FALSE;
	}

	OHOS_LOCATION_NATIVE_REQUEST request = { 0 };
	if (pthread_mutex_init(&request.mutex, NULL) != 0)
	{
		OH_Location_DestroyRequestConfig(config);
		ohos_location_log(location, "OHOS native location mutex initialization failed");
		return FALSE;
	}
	if (pthread_cond_init(&request.condition, NULL) != 0)
	{
		pthread_mutex_destroy(&request.mutex);
		OH_Location_DestroyRequestConfig(config);
		ohos_location_log(location, "OHOS native location condition initialization failed");
		return FALSE;
	}

	OH_LocationRequestConfig_SetUseScene(config, LOCATION_USE_SCENE_NAVIGATION);
	OH_LocationRequestConfig_SetInterval(config, 1);
	OH_LocationRequestConfig_SetCallback(config, ohos_location_info_callback, &request);

	rc = OH_Location_StartLocating(config);
	if (rc != LOCATION_SUCCESS)
	{
		pthread_cond_destroy(&request.condition);
		pthread_mutex_destroy(&request.mutex);
		OH_Location_DestroyRequestConfig(config);
		ohos_location_log(location, "OHOS native location start failed: %d", (int)rc);
		return FALSE;
	}

	struct timespec deadline = { 0 };
	(void)clock_gettime(CLOCK_REALTIME, &deadline);
	ohos_location_add_timeout(&deadline, timeoutMs == 0 ? 60000U : timeoutMs);

	pthread_mutex_lock(&request.mutex);
	while (!request.completed)
	{
		const int waitRc = pthread_cond_timedwait(&request.condition, &request.mutex, &deadline);
		if (waitRc == ETIMEDOUT)
			break;
	}
	const BOOL completed = request.completed;
	const OHOS_LOCATION_SAMPLE received = request.sample;
	pthread_mutex_unlock(&request.mutex);

	const Location_ResultCode stopRc = OH_Location_StopLocating(config);
	if (stopRc != LOCATION_SUCCESS)
		ohos_location_log(location, "OHOS native location stop failed: %d", (int)stopRc);

	pthread_cond_destroy(&request.condition);
	pthread_mutex_destroy(&request.mutex);
	OH_Location_DestroyRequestConfig(config);

	if (!completed)
	{
		ohos_location_log(location, "OHOS native location sample timed out");
		return FALSE;
	}
	if (!received.valid)
	{
		ohos_location_log(location, "OHOS native location sample is invalid");
		return FALSE;
	}

	*sample = received;
	return TRUE;
}

static UINT ohos_location_start(LocationClientContext* context, UINT32 version, UINT32 flags)
{
	(void)version;
	(void)flags;

	freerdpOhosLocation* location = context ? (freerdpOhosLocation*)context->custom : NULL;
	if (!location)
		return CHANNEL_RC_OK;

	++location->startCount;
	ohos_location_log(location, "RDP location channel requested HarmonyOS location sample");

	if (!ohos_location_request_permission(60000))
	{
		++location->permissionDeniedCount;
		ohos_location_log(location, "HarmonyOS location permission denied or unavailable");
		return CHANNEL_RC_OK;
	}

	OHOS_LOCATION_SAMPLE sample = { 0 };
	++location->sampleRequestCount;
	if (!ohos_location_request_native_sample(location, 60000, &sample) || !sample.valid)
	{
		++location->errorCount;
		ohos_location_log(location, "HarmonyOS current location sample unavailable");
		return CHANNEL_RC_OK;
	}
	++location->sampleSuccessCount;

	if (!context->LocationSend)
	{
		++location->errorCount;
		ohos_location_log(location, "RDP location channel send callback is unavailable");
		return ERROR_INTERNAL_ERROR;
	}

	const UINT32 source = sample.source <= LOCATIONSOURCE_GNSS ? sample.source : LOCATIONSOURCE_GNSS;
	const UINT rc = context->LocationSend(context, PDUTYPE_BASE_LOCATION3D, 7,
	                                      sample.latitude, sample.longitude, sample.altitude,
	                                      sample.speed, sample.heading,
	                                      sample.horizontalAccuracy, (int)source);
	if (rc == CHANNEL_RC_OK)
	{
		++location->sendCount;
		ohos_location_log(location, "HarmonyOS location sample sent to RDP location channel");
	}
	else
	{
		++location->errorCount;
		ohos_location_log(location, "RDP location sample send failed: %" PRIu32, rc);
	}
	return rc;
}

static UINT ohos_location_stop(LocationClientContext* context)
{
	freerdpOhosLocation* location = context ? (freerdpOhosLocation*)context->custom : NULL;
	if (location)
	{
		++location->stopCount;
		ohos_location_log(location, "RDP location channel stopped");
	}
	return CHANNEL_RC_OK;
}

static void ohos_location_attach(freerdpOhosLocation* location,
                                 LocationClientContext* locationContext)
{
	if (!location || !locationContext)
		return;

	location->location = locationContext;
	locationContext->custom = location;
	locationContext->LocationStart = ohos_location_start;
	locationContext->LocationStop = ohos_location_stop;
	++location->channelConnectCount;
	ohos_location_log(location, "RDP location channel connected to HarmonyOS provider");
}

static void ohos_location_detach(freerdpOhosLocation* location,
                                 LocationClientContext* locationContext)
{
	if (!location)
		return;

	if (location->location == locationContext)
	{
		if (location->location)
		{
			location->location->custom = NULL;
			location->location->LocationStart = NULL;
			location->location->LocationStop = NULL;
		}
		location->location = NULL;
	}
	++location->channelDisconnectCount;
	ohos_location_log(location, "RDP location channel disconnected from HarmonyOS provider");
}

static void ohos_location_channel_connected(void* context,
                                            const ChannelConnectedEventArgs* event)
{
	freerdpOhosLocation* location = ohos_location_from_context(context);

	if (!location || !event || !event->name)
		return;
	if (strcmp(event->name, LOCATION_DVC_CHANNEL_NAME) == 0)
		ohos_location_attach(location, (LocationClientContext*)event->pInterface);
}

static void ohos_location_channel_disconnected(void* context,
                                               const ChannelDisconnectedEventArgs* event)
{
	freerdpOhosLocation* location = ohos_location_from_context(context);

	if (!location || !event || !event->name)
		return;
	if (strcmp(event->name, LOCATION_DVC_CHANNEL_NAME) == 0)
		ohos_location_detach(location, (LocationClientContext*)event->pInterface);
}

freerdpOhosLocation* freerdp_ohos_location_new(void)
{
	return (freerdpOhosLocation*)calloc(1, sizeof(freerdpOhosLocation));
}

BOOL freerdp_ohos_location_register(freerdpOhosLocation* location, rdpContext* context,
                                    const FREERDP_OHOS_LOCATION_CONFIG* config,
                                    char* errorBuffer, size_t errorBufferSize)
{
	if (errorBuffer && errorBufferSize > 0)
		errorBuffer[0] = '\0';
	if (!location || !context || !context->pubSub || !config || !config->PubSubSubscribe ||
	    !config->PubSubUnsubscribe)
	{
		ohos_location_set_error(location, errorBuffer, errorBufferSize,
		                        "invalid OHOS location registration arguments");
		return FALSE;
	}

	freerdp_ohos_location_unregister(location);
	location->context = context;
	location->config = *config;
	ohos_location_registry_add(location);

	int rc = location->config.PubSubSubscribe(context->pubSub, "ChannelConnected",
	                                          ohos_location_channel_connected);
	if (rc < 0)
	{
		ohos_location_registry_remove(location);
		ohos_location_set_error(location, errorBuffer, errorBufferSize,
		                        "subscribe ChannelConnected for location failed: %d", rc);
		return FALSE;
	}
	location->channelConnectedSubscribed = TRUE;

	rc = location->config.PubSubSubscribe(context->pubSub, "ChannelDisconnected",
	                                      ohos_location_channel_disconnected);
	if (rc < 0)
	{
		freerdp_ohos_location_unregister(location);
		ohos_location_set_error(location, errorBuffer, errorBufferSize,
		                        "subscribe ChannelDisconnected for location failed: %d", rc);
		return FALSE;
	}
	location->channelDisconnectedSubscribed = TRUE;

	++location->registerCount;
	ohos_location_log(location, "RDP location bridge subscribed to FreeRDP channel events");
	return TRUE;
}

void freerdp_ohos_location_unregister(freerdpOhosLocation* location)
{
	if (!location)
		return;

	if (location->location)
		ohos_location_detach(location, location->location);

	if (location->config.PubSubUnsubscribe && location->context && location->context->pubSub)
	{
		if (location->channelConnectedSubscribed)
		{
			(void)location->config.PubSubUnsubscribe(location->context->pubSub,
			                                         "ChannelConnected",
			                                         ohos_location_channel_connected);
			location->channelConnectedSubscribed = FALSE;
		}
		if (location->channelDisconnectedSubscribed)
		{
			(void)location->config.PubSubUnsubscribe(location->context->pubSub,
			                                         "ChannelDisconnected",
			                                         ohos_location_channel_disconnected);
			location->channelDisconnectedSubscribed = FALSE;
		}
	}

	ohos_location_registry_remove(location);
	location->context = NULL;
	++location->unregisterCount;
}

void freerdp_ohos_location_free(freerdpOhosLocation* location)
{
	if (!location)
		return;

	freerdp_ohos_location_unregister(location);
	free(location);
}

const char* freerdp_ohos_location_get_diagnostics(freerdpOhosLocation* location)
{
	if (!location)
		return "OHOS location stats: unavailable";

	(void)snprintf(location->diagnostics, sizeof(location->diagnostics),
	               "OHOS location stats: registered=%" PRIu64
	               " unregistered=%" PRIu64 " channelConnect=%" PRIu64
	               " channelDisconnect=%" PRIu64 " start=%" PRIu64 " stop=%" PRIu64
	               " permissionDenied=%" PRIu64 " sampleRequests=%" PRIu64
	               " sampleSuccess=%" PRIu64 " sent=%" PRIu64 " errors=%" PRIu64,
	               location->registerCount, location->unregisterCount,
	               location->channelConnectCount, location->channelDisconnectCount,
	               location->startCount, location->stopCount,
	               location->permissionDeniedCount, location->sampleRequestCount,
	               location->sampleSuccessCount, location->sendCount, location->errorCount);
	return location->diagnostics;
}

BOOL freerdp_ohos_location_set_permission_callback(
    freerdp_ohos_location_permission_request_fn callback, void* userData)
{
	pthread_mutex_lock(&g_locationCallbackLock);
	g_permissionRequest = callback;
	g_permissionRequestUserData = userData;
	pthread_mutex_unlock(&g_locationCallbackLock);
	return TRUE;
}
