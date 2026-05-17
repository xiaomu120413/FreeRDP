# FreeRDP: A Remote Desktop Protocol Implementation
# FreeRDP cmake build script

define_channel_client_subsystem("audin" "ohos" "")

set(${MODULE_PREFIX}_SRCS audin_ohos.c)

set(${MODULE_PREFIX}_LIBS winpr freerdp ohaudio)

include_directories(..)

add_channel_client_subsystem_library(${MODULE_PREFIX} ${MODULE_NAME} ${CHANNEL_NAME} "" TRUE "")
