# vibplay: the game on a monitor, from your own disc.

# The version printed at the start of the log: the project's and the commit (task 6.7).
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE vib_game_commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
set(vib_game_version "${PROJECT_VERSION}")
if(vib_game_commit)
  string(APPEND vib_game_version " (${vib_game_commit})")
endif()
add_compile_definitions("VIB_VERSION=\"${vib_game_version}\"")

if(ANDROID)
  # On the headset the game is libmain.so; SDL's activity calls its main().
  add_library(vibplay SHARED main.cpp audio.cpp)
  set_target_properties(vibplay PROPERTIES OUTPUT_NAME main)
  target_link_libraries(vibplay PRIVATE log android)
  # The phone and tablet APK (task 4.20): flat, touch controls, the disc imported in the game.
  option(VIB_ANDROID_FLAT "Build the Android phone and tablet game (flat) instead of the Quest's" OFF)
  if(VIB_ANDROID_FLAT)
    target_compile_definitions(vibplay PRIVATE VIB_ANDROID_FLAT=1)
    target_sources(vibplay PRIVATE android_import.cpp)
  endif()
else()
  add_executable(vibplay main.cpp audio.cpp windows_log.cpp)
  if(WIN32)
    target_sources(vibplay PRIVATE ${PROJECT_SOURCE_DIR}/apps/common/icon/vibribbon.rc)  # (the programs' icon)
  endif()
endif()
target_link_libraries(vibplay PRIVATE vibcore vib_render vib_music vib_warnings)
if(TARGET vib_xr)
  target_link_libraries(vibplay PRIVATE vib_xr)  # vibplay --vr
endif()

# The download (task 4.16 I): two Windows programs from the same code, with no console
# window: "Vib-Ribbon" (the monitor) and "Vib-Ribbon VR" (the headset; --flat for the
# monitor). The package preset also links the C++ runtime in, so nothing else is needed.
option(VIB_PACKAGE "Build the packaged Windows programs (Vib-Ribbon.exe, Vib-Ribbon VR.exe)" OFF)
if(VIB_PACKAGE AND WIN32)
  set_target_properties(vibplay PROPERTIES WIN32_EXECUTABLE ON OUTPUT_NAME "Vib-Ribbon")
  add_executable(vibplay_vr main.cpp audio.cpp windows_log.cpp ${PROJECT_SOURCE_DIR}/apps/common/icon/vibribbon.rc)
  set_target_properties(vibplay_vr PROPERTIES WIN32_EXECUTABLE ON OUTPUT_NAME "Vib-Ribbon VR")
  target_compile_definitions(vibplay_vr PRIVATE VIB_DEFAULT_VR)
  target_link_libraries(vibplay_vr PRIVATE vibcore vib_render vib_music vib_warnings)
  if(TARGET vib_xr)
    target_link_libraries(vibplay_vr PRIVATE vib_xr)
  else()
    message(FATAL_ERROR "the headset program needs VIB_VR")
  endif()
endif()
