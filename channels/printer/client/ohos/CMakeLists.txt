# FreeRDP: A Remote Desktop Protocol Implementation
# FreeRDP cmake build script

define_channel_client_subsystem("printer" "ohos" "")

set(${MODULE_PREFIX}_SRCS
    printer_ohos.c
    printer_ohos_common.c
    printer_ohos_job.c)

set(${MODULE_PREFIX}_LIBS winpr freerdp ohprint)

include_directories(..)

add_channel_client_subsystem_library(${MODULE_PREFIX} ${MODULE_NAME} ${CHANNEL_NAME} "" TRUE "")
