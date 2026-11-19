# vibview: desktop viewer for the models and animations on your own disc.
add_executable(vibview main.cpp)
target_link_libraries(vibview PRIVATE vibcore vib_render vib_warnings)
