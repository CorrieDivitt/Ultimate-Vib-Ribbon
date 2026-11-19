add_library(vibcore STATIC
  src/bytes.cpp
  src/sha1.cpp
  src/cue.cpp
  src/disc_image.cpp
  src/iso9660.cpp
  src/pak.cpp
  src/known_discs.cpp
  src/tmd.cpp
  src/motion.cpp
  src/course_script.cpp
  src/pose.cpp
  src/sound_bank.cpp
  src/course_gen.cpp
  src/gameplay.cpp
  src/scene.cpp
)
target_include_directories(vibcore PUBLIC include)
target_link_libraries(vibcore PRIVATE vib_warnings)
