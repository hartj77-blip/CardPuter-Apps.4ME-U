cmake_minimum_required(VERSION 3.24)
project(zeroos-apps LANGUAGES C CXX)

# The installable apps for ZeroOS (README.md). Each app is a folder in apps/
# with an app.cmake that lists it. On the device every app is a module,
# <id>.so, loaded by the OS's ZeroShell; tools/build.sh packs them.
# The simulator and headless builds compile the apps, with the OS's own
# (Apps, Settings, Update, Wi-Fi), into a ZeroShell of our own, for
# development, screenshots and tests.
#
# The OS is the pinned submodule in base/. This mirrors
# base/shell/CMakeLists.txt, so when the pin moves, check that file.
#
# Backends:
#   sim      SDL2 window on a desktop PC (development)
#   device   Linux framebuffer + evdev keyboard (Cardputer Zero)
#   headless no window; renders screens to image files (screenshots, tests)
#   web      WebAssembly for the browser simulator: the OS's web/build.sh with
#            ZS_APPS=<this checkout> (the site builds it this way)
set(ZS_BACKEND "sim" CACHE STRING "sim | device | headless | web")
set_property(CACHE ZS_BACKEND PROPERTY STRINGS sim device headless web)

set(ZEROOS_BASE "${CMAKE_CURRENT_SOURCE_DIR}/base" CACHE PATH "Checkout of the OS (CardPuter-OS.4ME-U)")
set(BASE_SHELL "${ZEROOS_BASE}/shell")
if(NOT EXISTS "${BASE_SHELL}/src/main.cpp")
  message(FATAL_ERROR "The OS was not found at ${ZEROOS_BASE}. Run: git submodule update --init base")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# LVGL and quirc are pinned to the versions the OS uses.
include(FetchContent)
FetchContent_Declare(lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.2.2
  GIT_SHALLOW TRUE)
set(LV_CONF_PATH ${BASE_SHELL}/lv_conf.h CACHE STRING "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lvgl)

FetchContent_Declare(quirc
  GIT_REPOSITORY https://github.com/dlbeer/quirc.git
  GIT_TAG v1.2
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR none)
FetchContent_MakeAvailable(quirc)
add_library(quirc STATIC
  ${quirc_SOURCE_DIR}/lib/decode.c ${quirc_SOURCE_DIR}/lib/identify.c
  ${quirc_SOURCE_DIR}/lib/quirc.c ${quirc_SOURCE_DIR}/lib/version_db.c)
target_include_directories(quirc PUBLIC ${quirc_SOURCE_DIR}/lib)

set(ZS_SDL 0)
set(ZS_DEVICE 0)
if(ZS_BACKEND STREQUAL "sim")
  set(ZS_SDL 1)
elseif(ZS_BACKEND STREQUAL "device")
  set(ZS_DEVICE 1)
endif()
target_compile_definitions(lvgl PUBLIC
  ZS_BACKEND_SDL=${ZS_SDL} ZS_BACKEND_DEVICE=${ZS_DEVICE})

if(ZS_SDL)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED sdl2)
  target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
  target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
endif()

if(ZS_BACKEND STREQUAL "device")
  set(_zs_modules_default ON)
else()
  set(_zs_modules_default OFF)
endif()
option(ZS_APPS_AS_MODULES "Build the apps as installable modules" ${_zs_modules_default})
if(NOT CMAKE_CROSSCOMPILING)
  enable_testing()
endif()

# zs_app_test(<name> SOURCES <file>...): a unit test for an app's code that
# needs no screen, run by ctest. SOURCES are relative to the app's folder.
function(zs_app_test name)
  cmake_parse_arguments(T "" "" "SOURCES" ${ARGN})
  if(CMAKE_CROSSCOMPILING)
    return()
  endif()
  set(srcs "")
  foreach(s ${T_SOURCES})
    list(APPEND srcs "${CMAKE_CURRENT_LIST_DIR}/${s}")
  endforeach()
  add_executable(${name} ${srcs})
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR} ${BASE_SHELL}/src)
  target_compile_options(${name} PRIVATE -Wall -Wextra)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# The OS's own apps first (its apps.cmake), then every app here. An app the
# OS repository still lists as well is taken from here.
include(${BASE_SHELL}/cmake/ZeroShellApps.cmake)
include(${BASE_SHELL}/apps.cmake)
file(GLOB _app_files CONFIGURE_DEPENDS apps/*/app.cmake)
foreach(_f ${_app_files})
  include(${_f})
endforeach()
get_property(_ids GLOBAL PROPERTY ZS_APP_IDS)
list(REMOVE_DUPLICATES _ids)
# An app here that takes over another (REPLACES= in its .pkg: a renamed app,
# or one folded into another, that the pinned OS may still list) leaves that
# one out, as tools/build.sh does when it packs.
file(GLOB _pkgs CONFIGURE_DEPENDS apps/*/*.pkg)
foreach(_p ${_pkgs})
  file(STRINGS ${_p} _r REGEX "^REPLACES=")
  if(_r)
    string(REGEX REPLACE "^REPLACES=\"([^\"]*)\".*" "\\1" _r "${_r}")
    separate_arguments(_r)
    list(REMOVE_ITEM _ids ${_r})
  endif()
endforeach()
set_property(GLOBAL PROPERTY ZS_APP_IDS ${_ids})

# The shell: the OS's src/ without any app (zs_apps_build adds the apps).
file(GLOB_RECURSE BASE_SOURCES CONFIGURE_DEPENDS ${BASE_SHELL}/src/*.cpp ${BASE_SHELL}/src/fonts/*.c)
list(FILTER BASE_SOURCES EXCLUDE REGEX "^${BASE_SHELL}/src/apps/")
list(APPEND BASE_SOURCES ${BASE_SHELL}/src/apps/common.cpp ${BASE_SHELL}/src/apps/registry.cpp)
# ZeroOS 0.9.3 and newer: on the device the shell carries a small preload
# library for M5Stack's launcher apps (base/shell/CMakeLists.txt). An older
# OS has no src/platform/m5_drm_shim.c, and its shell doesn't need one.
if(ZS_DEVICE AND EXISTS ${BASE_SHELL}/src/platform/m5_drm_shim.c)
  add_library(zs_m5_drm_shim MODULE ${BASE_SHELL}/src/platform/m5_drm_shim.c)
  set_target_properties(zs_m5_drm_shim PROPERTIES PREFIX "" OUTPUT_NAME m5-drm-shim)
  target_compile_options(zs_m5_drm_shim PRIVATE -Wall -Wextra -Os -g0)
  target_link_options(zs_m5_drm_shim PRIVATE -s)
  target_link_libraries(zs_m5_drm_shim PRIVATE ${CMAKE_DL_LIBS})
  set(_zs_shim_c ${CMAKE_CURRENT_BINARY_DIR}/m5_drm_shim_blob.c)
  add_custom_command(OUTPUT ${_zs_shim_c}
    COMMAND ${CMAKE_COMMAND} -DIN=$<TARGET_FILE:zs_m5_drm_shim> -DOUT=${_zs_shim_c} -DNAME=zs_m5_drm_shim
            -P ${BASE_SHELL}/cmake/embed.cmake
    DEPENDS zs_m5_drm_shim ${BASE_SHELL}/cmake/embed.cmake
    VERBATIM)
  list(APPEND BASE_SOURCES ${_zs_shim_c})
endif()

add_executable(zeroshell ${BASE_SOURCES})
# This repository first: "apps/<id>/x.h" is the copy here, never the OS's.
target_include_directories(zeroshell PRIVATE ${CMAKE_SOURCE_DIR} ${BASE_SHELL}/src)
target_compile_definitions(zeroshell PRIVATE
  ZS_VERSION="apps"
  ZS_BACKEND_NAME="${ZS_BACKEND}")
target_compile_options(zeroshell PRIVATE -Wall -Wextra -Wno-unused-parameter)
zs_link_lvgl(zeroshell)
target_link_libraries(zeroshell PRIVATE quirc pthread m)
zs_apps_build(zeroshell ${BASE_SHELL}/src)
foreach(_t ${ZS_APP_MODULES})
  target_include_directories(${_t} BEFORE PRIVATE ${CMAKE_SOURCE_DIR})
endforeach()

# The browser simulator (needs an OS with zs_web_link: newer than 0.6.0).
if(ZS_BACKEND STREQUAL "web")
  if(NOT COMMAND zs_web_link)
    message(FATAL_ERROR "ZS_BACKEND=web: the OS at ${ZEROOS_BASE} can't build for the browser yet")
  endif()
  zs_web_link(zeroshell)
endif()
