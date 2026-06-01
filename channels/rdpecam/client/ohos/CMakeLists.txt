# FreeRDP: A Remote Desktop Protocol Implementation
# FreeRDP cmake build script

define_channel_client_subsystem("rdpecam" "ohos" "")

set(${MODULE_PREFIX}_SRCS
	camera_ohos.c
	camera_ohos_capture.c
	camera_ohos_frame.c
	camera_ohos_media.c
	camera_ohos_permission.c)

set(${MODULE_PREFIX}_LIBS winpr freerdp)

include_directories(..)

add_channel_client_subsystem_library(${MODULE_PREFIX} ${MODULE_NAME} ${CHANNEL_NAME} "" TRUE "")
