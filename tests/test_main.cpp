#include <exception>

#include "test.h"

int main() {
  int skipped = 0;
  for (const auto& c : vibtest::registry()) {
    const int before = vibtest::failures();
    try {
      c.fn();
      std::printf("%s %s\n", vibtest::failures() == before ? "  ok  " : "  FAIL", c.name);
    } catch (const vibtest::Skip& s) {
      ++skipped;
      std::printf("  skip %s (%s)\n", c.name, s.why.c_str());
    } catch (const std::exception& e) {
      ++vibtest::failures();
      std::printf("  FAIL %s threw: %s\n", c.name, e.what());
    }
  }
  std::printf("%zu tests, %d failed checks, %d skipped\n", vibtest::registry().size(), vibtest::failures(), skipped);
  return vibtest::failures() ? 1 : 0;
}
