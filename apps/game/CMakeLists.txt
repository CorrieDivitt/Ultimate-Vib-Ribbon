# vibplay: the game on a monitor, from your own disc.
add_executable(vibplay main.cpp audio.cpp)
target_link_libraries(vibplay PRIVATE vibcore vib_render vib_warnings)
