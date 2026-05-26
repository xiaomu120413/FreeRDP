/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * HarmonyOS cliprdr <-> Pasteboard bridge
 */

#include "ohos_clipboard_internal.h"

static pthread_mutex_t g_registryMutex = PTHREAD_MUTEX_INITIALIZER;
static freerdpOhosClipboard* g_registryHead = NULL;

FREERDP_LOCAL void ohos_clipboard_log(freerdpOhosClipboard* clipboard, const char* format, ...)
{
	if (!clipboard || !clipboard->config.Log)
		return;

	char message[512];
	va_list ap;
	va_start(ap, format);
	(void)vsnprintf(message, sizeof(message), format, ap);
	va_end(ap);
	clipboard->config.Log(clipboard->config.logUserData, message);
}

FREERDP_LOCAL void ohos_clipboard_set_error(freerdpOhosClipboard* clipboard, char* errorBuffer,
                                     size_t errorBufferSize, const char* format, ...)
{
	if (clipboard)
		++clipboard->errorCount;

	if (!errorBuffer || errorBufferSize == 0)
		return;

	va_list ap;
	va_start(ap, format);
	(void)vsnprintf(errorBuffer, errorBufferSize, format, ap);
	va_end(ap);
}

FREERDP_LOCAL void ohos_clipboard_registry_add(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	pthread_mutex_lock(&g_registryMutex);
	clipboard->registryNext = g_registryHead;
	g_registryHead = clipboard;
	pthread_mutex_unlock(&g_registryMutex);
}

FREERDP_LOCAL void ohos_clipboard_registry_remove(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	pthread_mutex_lock(&g_registryMutex);
	freerdpOhosClipboard** current = &g_registryHead;
	while (*current)
	{
		if (*current == clipboard)
		{
			*current = clipboard->registryNext;
			clipboard->registryNext = NULL;
			break;
		}
		current = &(*current)->registryNext;
	}
	pthread_mutex_unlock(&g_registryMutex);
}

FREERDP_LOCAL freerdpOhosClipboard* ohos_clipboard_from_context(void* context)
{
	rdpContext* rdp_context = (rdpContext*)context;
	freerdpOhosClipboard* found = NULL;

	pthread_mutex_lock(&g_registryMutex);
	for (freerdpOhosClipboard* current = g_registryHead; current; current = current->registryNext)
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

FREERDP_LOCAL CliprdrClientContext* ohos_clipboard_snapshot_cliprdr(freerdpOhosClipboard* clipboard)
{
	CliprdrClientContext* cliprdr = NULL;

	if (!clipboard || !clipboard->lockInitialized)
		return NULL;

	pthread_mutex_lock(&clipboard->lock);
	cliprdr = clipboard->cliprdr;
	pthread_mutex_unlock(&clipboard->lock);
	return cliprdr;
}

FREERDP_LOCAL void ohos_clipboard_attach_cliprdr(freerdpOhosClipboard* clipboard,
                                          CliprdrClientContext* cliprdr)
{
	if (!clipboard || !cliprdr)
		return;

	pthread_mutex_lock(&clipboard->lock);
	clipboard->cliprdr = cliprdr;
	cliprdr->custom = clipboard;
	cliprdr->MonitorReady = ohos_clipboard_monitor_ready;
	cliprdr->ServerCapabilities = ohos_clipboard_server_capabilities;
	cliprdr->ServerFormatList = ohos_clipboard_server_format_list;
	cliprdr->ServerFormatListResponse = ohos_clipboard_server_format_list_response;
	cliprdr->ServerLockClipboardData = ohos_clipboard_server_lock_clipboard_data;
	cliprdr->ServerUnlockClipboardData = ohos_clipboard_server_unlock_clipboard_data;
	cliprdr->ServerFormatDataRequest = ohos_clipboard_server_format_data_request;
	cliprdr->ServerFormatDataResponse = ohos_clipboard_server_format_data_response;
	cliprdr->ServerFileContentsRequest = ohos_clipboard_server_file_contents_request;
	cliprdr->ServerFileContentsResponse = ohos_clipboard_server_file_contents_response;
	pthread_mutex_unlock(&clipboard->lock);

	++clipboard->channelConnectCount;
	ohos_clipboard_log(clipboard, "cliprdr connected to HarmonyOS Pasteboard backend");
}

FREERDP_LOCAL void ohos_clipboard_detach_cliprdr(freerdpOhosClipboard* clipboard,
                                          CliprdrClientContext* cliprdr)
{
	if (!clipboard)
		return;

	pthread_mutex_lock(&clipboard->lock);
	if (clipboard->cliprdr == cliprdr)
	{
		if (clipboard->cliprdr)
			clipboard->cliprdr->custom = NULL;
		clipboard->cliprdr = NULL;
		clipboard->requestedFormatId = 0;
		clipboard->requestedFormatKind = OHOS_CLIPBOARD_REQUEST_NONE;
		ohos_clipboard_clear_remote_file_transfer_locked(clipboard);
	}
	pthread_mutex_unlock(&clipboard->lock);

	++clipboard->channelDisconnectCount;
	ohos_clipboard_log(clipboard, "cliprdr disconnected from HarmonyOS Pasteboard backend");
}

FREERDP_LOCAL void ohos_clipboard_channel_connected(void* context, const ChannelConnectedEventArgs* event)
{
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_context(context);

	if (!clipboard || !event || !event->name)
		return;
	if (strcmp(event->name, CLIPRDR_SVC_CHANNEL_NAME) == 0)
		ohos_clipboard_attach_cliprdr(clipboard, (CliprdrClientContext*)event->pInterface);
}

FREERDP_LOCAL void ohos_clipboard_channel_disconnected(void* context,
                                                const ChannelDisconnectedEventArgs* event)
{
	freerdpOhosClipboard* clipboard = ohos_clipboard_from_context(context);

	if (!clipboard || !event || !event->name)
		return;
	if (strcmp(event->name, CLIPRDR_SVC_CHANNEL_NAME) == 0)
		ohos_clipboard_detach_cliprdr(clipboard, (CliprdrClientContext*)event->pInterface);
}

freerdpOhosClipboard* freerdp_ohos_clipboard_new(void)
{
	freerdpOhosClipboard* clipboard = (freerdpOhosClipboard*)calloc(1, sizeof(freerdpOhosClipboard));
	if (!clipboard)
		return NULL;

	if (pthread_mutex_init(&clipboard->lock, NULL) != 0)
	{
		free(clipboard);
		return NULL;
	}
	clipboard->lockInitialized = TRUE;
	if (pthread_cond_init(&clipboard->imageCond, NULL) != 0)
	{
		pthread_mutex_destroy(&clipboard->lock);
		free(clipboard);
		return NULL;
	}
	clipboard->imageCondInitialized = TRUE;
	return clipboard;
}

BOOL freerdp_ohos_clipboard_register(freerdpOhosClipboard* clipboard, rdpContext* context,
                                     const FREERDP_OHOS_CLIPBOARD_CONFIG* config,
                                     char* errorBuffer, size_t errorBufferSize)
{
	if (errorBuffer && errorBufferSize > 0)
		errorBuffer[0] = '\0';
	if (!clipboard || !context || !context->pubSub || !config || !config->PubSubSubscribe ||
	    !config->PubSubUnsubscribe)
	{
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "invalid OHOS clipboard registration arguments");
		return FALSE;
	}

	clipboard->context = context;
	clipboard->config = *config;

	ohos_clipboard_registry_add(clipboard);

	int rc = clipboard->config.PubSubSubscribe(context->pubSub, "ChannelConnected",
	                                           ohos_clipboard_channel_connected);
	if (rc < 0)
	{
		ohos_clipboard_registry_remove(clipboard);
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "subscribe ChannelConnected for clipboard failed: %d", rc);
		return FALSE;
	}
	clipboard->channelConnectedSubscribed = TRUE;

	rc = clipboard->config.PubSubSubscribe(context->pubSub, "ChannelDisconnected",
	                                       ohos_clipboard_channel_disconnected);
	if (rc < 0)
	{
		freerdp_ohos_clipboard_unregister(clipboard);
		ohos_clipboard_set_error(clipboard, errorBuffer, errorBufferSize,
		                         "subscribe ChannelDisconnected for clipboard failed: %d", rc);
		return FALSE;
	}
	clipboard->channelDisconnectedSubscribed = TRUE;

	ohos_clipboard_create_pasteboard(clipboard);
	++clipboard->registerCount;
	ohos_clipboard_log(clipboard, "cliprdr bridge subscribed to FreeRDP channel events");
	return TRUE;
}

void freerdp_ohos_clipboard_unregister(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	if (clipboard->cliprdr)
		ohos_clipboard_detach_cliprdr(clipboard, clipboard->cliprdr);

	ohos_clipboard_stop_image_worker(clipboard);
	ohos_clipboard_destroy_pasteboard(clipboard);

	if (clipboard->config.PubSubUnsubscribe && clipboard->context && clipboard->context->pubSub)
	{
		if (clipboard->channelConnectedSubscribed)
		{
			(void)clipboard->config.PubSubUnsubscribe(clipboard->context->pubSub,
			                                          "ChannelConnected",
			                                          ohos_clipboard_channel_connected);
			clipboard->channelConnectedSubscribed = FALSE;
		}
		if (clipboard->channelDisconnectedSubscribed)
		{
			(void)clipboard->config.PubSubUnsubscribe(clipboard->context->pubSub,
			                                          "ChannelDisconnected",
			                                          ohos_clipboard_channel_disconnected);
			clipboard->channelDisconnectedSubscribed = FALSE;
		}
	}

	ohos_clipboard_registry_remove(clipboard);
	clipboard->context = NULL;
	clipboard->requestedFormatId = 0;
	clipboard->requestedFormatKind = OHOS_CLIPBOARD_REQUEST_NONE;
	pthread_mutex_lock(&clipboard->lock);
	ohos_clipboard_clear_remote_file_transfer_locked(clipboard);
	pthread_mutex_unlock(&clipboard->lock);
	++clipboard->unregisterCount;
}

void freerdp_ohos_clipboard_free(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return;

	freerdp_ohos_clipboard_unregister(clipboard);
	if (clipboard->imageCondInitialized)
	{
		pthread_cond_destroy(&clipboard->imageCond);
		clipboard->imageCondInitialized = FALSE;
	}
	if (clipboard->lockInitialized)
	{
		pthread_mutex_destroy(&clipboard->lock);
		clipboard->lockInitialized = FALSE;
	}
	free(clipboard);
}

const char* freerdp_ohos_clipboard_get_diagnostics(freerdpOhosClipboard* clipboard)
{
	if (!clipboard)
		return "OHOS clipboard stats: unavailable";

	(void)snprintf(clipboard->diagnostics, sizeof(clipboard->diagnostics),
	               "OHOS clipboard stats: registered=%" PRIu64 " unregistered=%" PRIu64
	               " channelConnect=%" PRIu64 " channelDisconnect=%" PRIu64
	               " monitorReady=%" PRIu64 " localFormatList=%" PRIu64
	               " remoteFormatList=%" PRIu64 " localRequests=%" PRIu64
	               " remoteResponses=%" PRIu64 " pasteboardRead=%" PRIu64
	               " pasteboardWrite=%" PRIu64 " pasteboardChanges=%" PRIu64
	               " suppressedChanges=%" PRIu64 " errors=%" PRIu64
	               " lastError=%" PRIu32 " lastRequestedFormat=%" PRIu32
	               " lastRemoteFormats=%" PRIu32 " lastLocalFormats=%" PRIu32
	               " lastTextBytes=%" PRIu32 " lastHtmlBytes=%" PRIu32
	               " lastUriBytes=%" PRIu32 " lastImageBytes=%" PRIu32,
	               clipboard->registerCount, clipboard->unregisterCount,
	               clipboard->channelConnectCount, clipboard->channelDisconnectCount,
	               clipboard->monitorReadyCount, clipboard->localFormatListCount,
	               clipboard->remoteFormatListCount, clipboard->localRequestCount,
	               clipboard->remoteResponseCount, clipboard->pasteboardReadCount,
	               clipboard->pasteboardWriteCount, clipboard->pasteboardChangeCount,
	               clipboard->suppressedChangeCount, clipboard->errorCount,
	               clipboard->lastError, clipboard->lastRequestedFormat,
	               clipboard->lastRemoteFormatCount, clipboard->lastLocalFormatCount,
	               clipboard->lastTextBytes, clipboard->lastHtmlBytes, clipboard->lastUriBytes,
	               clipboard->lastImageBytes);
	return clipboard->diagnostics;
}
