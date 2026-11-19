# vibview: desktop viewer for the models and animations on your own disc.
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

# Compile each GLSL shader to a C array of SPIR-V words that main.cpp includes.
set(shader_out "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${shader_out}")
set(shader_incs)
foreach(name line.vert line.frag fill.vert fill.frag)
  set(src "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}")
  set(inc "${shader_out}/${name}.inc")
  add_custom_command(OUTPUT "${inc}"
    COMMAND "${VIB_GLSLC}" -mfmt=c -o "${inc}" "${src}"
    DEPENDS "${src}" VERBATIM
    COMMENT "Compiling shader ${name}")
  list(APPEND shader_incs "${inc}")
endforeach()

add_executable(vibview main.cpp ${shader_incs})
target_include_directories(vibview PRIVATE "${shader_out}")
target_link_libraries(vibview PRIVATE vibcore vib_warnings Vulkan::Vulkan SDL3::SDL3-static)
