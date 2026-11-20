# Shared by the desktop apps: SDL3 (window, audio, input) and the Vulkan line renderer.
# Needs the Vulkan SDK (headers, loader and the glslc shader compiler).

find_package(Vulkan REQUIRED)
find_program(VIB_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" REQUIRED)

include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG release-3.2.26
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(SDL3)

# Each GLSL shader becomes a C array of SPIR-V words that line_renderer.cpp includes.
set(shader_out "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${shader_out}")
set(shader_incs)
foreach(name line.vert line.frag)
  set(src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}")
  set(inc "${shader_out}/${name}.inc")
  add_custom_command(OUTPUT "${inc}"
    COMMAND "${VIB_GLSLC}" -mfmt=c -o "${inc}" "${src}"
    DEPENDS "${src}" VERBATIM
    COMMENT "Compiling shader ${name}")
  list(APPEND shader_incs "${inc}")
endforeach()

add_library(vib_render STATIC line_renderer.cpp ${shader_incs})
target_include_directories(vib_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE "${shader_out}")
target_link_libraries(vib_render PUBLIC SDL3::SDL3-static PRIVATE vib_warnings Vulkan::Vulkan)

# The player's own music: MP3 and FLAC (dr_mp3 and dr_flac by David Reid, public
# domain or MIT-0), pinned to one commit.
FetchContent_Declare(dr_libs
  GIT_REPOSITORY https://github.com/mackron/dr_libs.git
  GIT_TAG e381981afa52800b9a3864df548353ff67252fbc
  SOURCE_SUBDIR no-build)  # headers only: fetched, not built
FetchContent_MakeAvailable(dr_libs)
add_library(vib_music STATIC music_file.cpp decoders.cpp)
target_include_directories(vib_music PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(vib_music SYSTEM PRIVATE "${dr_libs_SOURCE_DIR}")
target_link_libraries(vib_music PRIVATE vib_warnings)
# Their code is not ours to warn about.
if(MSVC)
  set_source_files_properties(decoders.cpp PROPERTIES COMPILE_OPTIONS "/W0;/WX-")
else()
  set_source_files_properties(decoders.cpp PROPERTIES COMPILE_OPTIONS "-w;-Wno-error")
endif()

# Headsets through OpenXR (the Khronos loader, built static). On by default on
# Windows, where PC headsets run; the Quest's own build comes later.
if(WIN32)
  set(vib_vr_default ON)
else()
  set(vib_vr_default OFF)
endif()
option(VIB_VR "Build headset support (OpenXR)" ${vib_vr_default})
if(VIB_VR)
  set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
  set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
  set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(OpenXR
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG release-1.1.65
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(OpenXR)
  add_library(vib_xr STATIC xr_display.cpp ${shader_incs})
  target_include_directories(vib_xr PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE "${shader_out}")
  target_link_libraries(vib_xr PUBLIC vib_render PRIVATE vib_warnings Vulkan::Vulkan OpenXR::openxr_loader)
  target_compile_definitions(vib_xr PUBLIC VIB_VR=1)
endif()
