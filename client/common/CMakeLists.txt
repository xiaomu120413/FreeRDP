# FreeRDP: A Remote Desktop Protocol Implementation
# FreeRDP Client Common
#
# Copyright 2012 Marc-Andre Moreau <marcandre.moreau@gmail.com>
# Copyright 2025 Siemens
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set(MODULE_NAME "freerdp-client")
set(MODULE_PREFIX "FREERDP_CLIENT")

set(PRIV_LIBS "")
set(PUB_LIBS "")

set(SRCS
    client.c
    cmdline.c
    cmdline.h
    file.c
    client_cliprdr_file.c
    geometry.c
    smartcard_cli.c
)

if(OHOS OR CMAKE_SYSTEM_NAME STREQUAL "OHOS")
  find_library(OHOS_EGL_LIBRARY EGL REQUIRED)
  find_library(OHOS_GLESV3_LIBRARY GLESv3 REQUIRED)
  find_library(OHOS_NATIVE_IMAGE_LIBRARY native_image REQUIRED)
  list(APPEND SRCS
       ${CMAKE_CURRENT_SOURCE_DIR}/../OHOS/ohos_avc_surface.c
       ${CMAKE_CURRENT_SOURCE_DIR}/../OHOS/ohos_certificate.c
       ${CMAKE_CURRENT_SOURCE_DIR}/../OHOS/ohos_compositor.c
       ${CMAKE_CURRENT_SOURCE_DIR}/../OHOS/ohos_display.c
       ${CMAKE_CURRENT_SOURCE_DIR}/../OHOS/ohos_graphics.c
       ${CMAKE_CURRENT_SOURCE_DIR}/../OHOS/ohos_ime.c
       ${CMAKE_CURRENT_SOURCE_DIR}/../OHOS/ohos_keyboard.c
       ${CMAKE_CURRENT_SOURCE_DIR}/../OHOS/ohos_pointer.c
       ${CMAKE_CURRENT_SOURCE_DIR}/../OHOS/ohos_rdpgfx.c
       ${CMAKE_CURRENT_SOURCE_DIR}/../OHOS/ohos_session_config.c)
  list(APPEND PRIV_LIBS ${OHOS_EGL_LIBRARY} ${OHOS_GLESV3_LIBRARY}
       ${OHOS_NATIVE_IMAGE_LIBRARY})
endif()

if(WITH_OHOS_PASTEBOARD)
  find_library(OHOS_PASTEBOARD_LIBRARY pasteboard REQUIRED)
  find_library(OHOS_UDMF_LIBRARY udmf REQUIRED)
  find_library(OHOS_FILE_URI_LIBRARY ohfileuri REQUIRED)
  find_library(OHOS_IMAGE_NDK_LIBRARY image_ndk.z REQUIRED)
  find_library(OHOS_IMAGE_SOURCE_LIBRARY image_source REQUIRED)
  find_library(OHOS_PIXELMAP_NDK_LIBRARY pixelmap_ndk.z REQUIRED)
  find_library(OHOS_PIXELMAP_LIBRARY pixelmap REQUIRED)
  list(APPEND SRCS ${CMAKE_CURRENT_SOURCE_DIR}/../OHOS/ohos_clipboard.c)
  list(APPEND PRIV_LIBS ${OHOS_PASTEBOARD_LIBRARY} ${OHOS_UDMF_LIBRARY}
       ${OHOS_FILE_URI_LIBRARY} ${OHOS_IMAGE_NDK_LIBRARY} ${OHOS_IMAGE_SOURCE_LIBRARY}
       ${OHOS_PIXELMAP_NDK_LIBRARY} ${OHOS_PIXELMAP_LIBRARY})
endif()

foreach(FREERDP_CHANNELS_CLIENT_SRC ${FREERDP_CHANNELS_CLIENT_SRCS})
  get_filename_component(NINC ${FREERDP_CHANNELS_CLIENT_SRC} PATH)
  include_directories(${NINC})
  list(APPEND SRCS "${FREERDP_CHANNELS_CLIENT_SRC}")
endforeach()

if(UNIX)
  option(WITH_SSO_MIB "Build with sso-mib support" OFF)
else()
  set(WITH_SSO_MIB OFF CACHE INTERNAL "unsupported platform")
endif()

if(NOT APPLE AND NOT WIN32 AND NOT ANDROID)
  set(OPT_FUSE_DEFAULT ON)
else()
  set(OPT_FUSE_DEFAULT OFF)
endif()

option(WITH_FUSE "Build clipboard with FUSE file copy support" ${OPT_FUSE_DEFAULT})
if(WITH_FUSE)
  find_package(PkgConfig REQUIRED)

  pkg_check_modules(FUSE3 REQUIRED fuse3)
  freerdp_client_pc_add_requires_private("fuse3")

  include_directories(SYSTEM ${FUSE3_INCLUDE_DIRS})
  add_compile_definitions(WITH_FUSE)
  list(APPEND PRIV_LIBS ${FUSE3_LIBRARIES})

  add_compile_definitions(_FILE_OFFSET_BITS=64)
endif()

include_directories(SYSTEM ${OPENSSL_INCLUDE_DIR})

if(WITH_SSO_MIB)
  list(APPEND SRCS sso_mib_tokens.c)
endif()

addtargetwithresourcefile(${MODULE_NAME} FALSE "${FREERDP_VERSION}" SRCS)

if(WITH_SSO_MIB)
  if(SSO_MIB_EXTERNAL_DIR)
    add_dependencies(${MODULE_NAME} sso-mib-external)
  endif()

  find_package(sso-mib REQUIRED)
  add_compile_definitions(WITH_SSO_MIB)
  list(APPEND PRIV_LIBS sso-mib)
endif()

list(APPEND PUB_LIBS freerdp winpr)

include(CheckLibraryExists)
check_library_exists(m lround "" HAVE_LIB_M)
if(HAVE_LIB_M)
  list(APPEND PUB_LIBS m)
endif()

target_include_directories(${MODULE_NAME} INTERFACE $<INSTALL_INTERFACE:include/freerdp${FREERDP_VERSION_MAJOR}>)
target_link_libraries(${MODULE_NAME} PRIVATE ${FREERDP_CHANNELS_CLIENT_LIBS})
target_link_libraries(${MODULE_NAME} PRIVATE ${PRIV_LIBS})
target_link_libraries(${MODULE_NAME} PUBLIC ${PUB_LIBS})

installwithrpath(
  TARGETS
  ${MODULE_NAME}
  COMPONENT
  libraries
  EXPORT
  FreeRDP-ClientTargets
  ARCHIVE
  DESTINATION
  ${CMAKE_INSTALL_LIBDIR}
  LIBRARY
  DESTINATION
  ${CMAKE_INSTALL_LIBDIR}
  RUNTIME
  DESTINATION
  ${CMAKE_INSTALL_BINDIR}
)

set_property(TARGET ${MODULE_NAME} PROPERTY FOLDER "Client/Common")

if(BUILD_TESTING_INTERNAL OR BUILD_TESTING)
  add_subdirectory(test)
endif()

if(WITH_MANPAGES)
  add_subdirectory(man)
endif()
