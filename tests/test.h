#pragma once
// A deliberately tiny test harness: no dependencies, works on every target.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace vibtest {

struct Case {
  const char* name;
  std::function<void()> fn;
};

inline std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}
inline int& failures() {
  static int f = 0;
  return f;
}
struct Register {
  Register(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};
struct Skip {
  std::string why;
};

}  // namespace vibtest

#define VIB_CAT2(a, b) a##b
#define VIB_CAT(a, b) VIB_CAT2(a, b)
#define TEST(name)                                                              \
  static void VIB_CAT(test_, name)();                                            \
  static vibtest::Register VIB_CAT(reg_, name)(#name, VIB_CAT(test_, name));     \
  static void VIB_CAT(test_, name)()

#define CHECK(cond)                                                                       \
  do {                                                                                    \
    if (!(cond)) {                                                                        \
      std::printf("    FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
      ++vibtest::failures();                                                              \
    }                                                                                     \
  } while (0)

#define CHECK_THROWS(expr)                                                               \
  do {                                                                                   \
    bool threw_ = false;                                                                 \
    try { (void)(expr); } catch (...) { threw_ = true; }                                 \
    if (!threw_) {                                                                       \
      std::printf("    FAILED %s:%d: expected throw: %s\n", __FILE__, __LINE__, #expr);  \
      ++vibtest::failures();                                                             \
    }                                                                                    \
  } while (0)

#define SKIP(why) throw vibtest::Skip{why}
