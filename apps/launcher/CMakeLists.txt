# Vib-Ribbon Launcher (task 4.16 I): the Windows download's front door. Dear ImGui
# (MIT, fetched at its release tag) drawn with SDL3's renderer; SDL comes from apps/common.
include(FetchContent)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.9b
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)
add_library(vib_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(vib_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(vib_imgui PUBLIC SDL3::SDL3-static)

# The version shown: the project's and the commit it was built from.
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE vib_commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
set(vib_launcher_version "${PROJECT_VERSION}")
if(vib_commit)
  string(APPEND vib_launcher_version " (${vib_commit})")
endif()

add_executable(vib_launcher WIN32 launcher.cpp launcher.manifest ${PROJECT_SOURCE_DIR}/apps/common/icon/vibribbon.rc)
set_target_properties(vib_launcher PROPERTIES OUTPUT_NAME "Vib-Ribbon Launcher")
target_compile_definitions(vib_launcher PRIVATE "VIB_LAUNCHER_VERSION=\"${vib_launcher_version}\"")
target_link_libraries(vib_launcher PRIVATE vibcore vib_imgui vib_warnings)
