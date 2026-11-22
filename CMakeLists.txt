cmake_minimum_required(VERSION 3.24)
project(VibRibbonVR VERSION 0.0.1 LANGUAGES CXX)

# One codebase for every target: Windows x64 (PCVR), Android ARM64 (Quest 3),
# and later Linux ARM64 (Steam Frame stretch goal). Keep vibcore free of any
# platform, graphics or audio-device code so it builds everywhere.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VIB_BUILD_TOOLS "Build developer command-line tools (vibtool)" ON)
option(VIB_BUILD_TESTS "Build unit tests" ON)
if(WIN32)
  set(_vib_viewer_default ON)
else()
  set(_vib_viewer_default OFF)
endif()
option(VIB_BUILD_VIEWER "Build the desktop apps: vibview (model viewer) and vibplay (the game on a monitor). Needs the Vulkan SDK" ${_vib_viewer_default})
option(VIB_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Shared warning settings, applied per target.
add_library(vib_warnings INTERFACE)
if(MSVC)
  target_compile_options(vib_warnings INTERFACE /W4 /permissive- /utf-8)
  target_compile_definitions(vib_warnings INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  if(VIB_WARNINGS_AS_ERRORS)
    target_compile_options(vib_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(vib_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion -Wno-missing-field-initializers)
  if(VIB_WARNINGS_AS_ERRORS)
    target_compile_options(vib_warnings INTERFACE -Werror)
  endif()
endif()

add_subdirectory(core)

if(VIB_BUILD_TOOLS AND NOT ANDROID)
  add_subdirectory(tools/vibtool)
endif()

if(ANDROID)
  # The Quest 3 standalone build (task 5.1): the game as libmain.so, which the
  # Android project in android/ packages with SDL's Java side into an APK.
  add_subdirectory(apps/common)
  add_subdirectory(apps/game)
elseif(VIB_BUILD_VIEWER)
  add_subdirectory(apps/common)
  add_subdirectory(apps/viewer)
  add_subdirectory(apps/game)
  if(WIN32)
    add_subdirectory(apps/launcher)  # the Windows download's launcher (task 4.16 I)
  endif()
endif()

if(VIB_BUILD_TESTS AND NOT ANDROID)
  enable_testing()
  add_subdirectory(tests)
endif()
