add_library(vibcore STATIC
  src/bytes.cpp
  src/sha1.cpp
  src/cue.cpp
  src/disc_image.cpp
  src/iso9660.cpp
  src/pak.cpp
  src/known_discs.cpp
)
target_include_directories(vibcore PUBLIC include)
target_link_libraries(vibcore PRIVATE vib_warnings)
