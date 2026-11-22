// The packaged Windows programs have no console (task 4.16 I): what the game prints
// goes to a log file beside save.txt instead, for reporting problems.
#ifdef _WIN32
#include <windows.h>
#include <io.h>

#include <cstdio>
#include <string>

void log_to_file_without_console(const char* path_utf8) {
  if (GetConsoleWindow() || !path_utf8) return;  // (started from a console: it shows there)
  const int n = MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, nullptr, 0);
  if (n <= 0) return;
  std::wstring wide(std::size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, wide.data(), n);
  FILE* f = nullptr;
  if (_wfreopen_s(&f, wide.c_str(), L"w", stdout) != 0 || !f) return;
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // (every line reaches the file, even if the game stops)
  _dup2(_fileno(stdout), _fileno(stderr));
  std::setvbuf(stderr, nullptr, _IONBF, 0);
}
#endif
