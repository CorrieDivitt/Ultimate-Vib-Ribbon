# vibplay: the game on a monitor, from your own disc.
if(ANDROID)
  # On the headset the game is libmain.so; SDL's activity calls its main().
  add_library(vibplay SHARED main.cpp audio.cpp)
  set_target_properties(vibplay PROPERTIES OUTPUT_NAME main)
  target_link_libraries(vibplay PRIVATE log android)
else()
  add_executable(vibplay main.cpp audio.cpp)
endif()
target_link_libraries(vibplay PRIVATE vibcore vib_render vib_music vib_warnings)
if(TARGET vib_xr)
  target_link_libraries(vibplay PRIVATE vib_xr)  # vibplay --vr
endif()
