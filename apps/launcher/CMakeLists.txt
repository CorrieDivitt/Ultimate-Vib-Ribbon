# Vib-Ribbon Launcher (task 4.16 I): the Windows download's front door. Plain Win32.
add_executable(vib_launcher WIN32 launcher.cpp launcher.manifest)
set_target_properties(vib_launcher PROPERTIES OUTPUT_NAME "Vib-Ribbon Launcher")
target_compile_definitions(vib_launcher PRIVATE UNICODE _UNICODE)
target_link_libraries(vib_launcher PRIVATE vibcore vib_warnings comdlg32 shell32 ole32 uuid user32 gdi32)
