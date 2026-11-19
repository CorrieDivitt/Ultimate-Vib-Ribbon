add_executable(vibcore_tests
  test_main.cpp
  test_sha1.cpp
  test_cue.cpp
  test_pak.cpp
  test_disc.cpp
  test_formats.cpp
  test_course_gen.cpp
  test_gameplay.cpp
)
target_link_libraries(vibcore_tests PRIVATE vibcore vib_warnings)
add_test(NAME vibcore_tests COMMAND vibcore_tests)
# test_disc.cpp runs only when VIB_TEST_CUE points at your own SCES-02873 CUE file.
