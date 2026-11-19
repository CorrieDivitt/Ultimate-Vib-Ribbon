#include "test.h"
#include "vib/pak.h"

using namespace vib;

TEST(pak_round_trip) {
  // Name lengths 3, 4 and 7 exercise every padding case.
  const Bytes a = {1, 2, 3};
  const Bytes b = {};
  const Bytes c(1000, 0xAB);
  const Bytes raw = Pak::build({{"A/B", a}, {"ABCD", b}, {"X/Y.TMD", c}});
  const Pak pak{Bytes(raw)};
  CHECK(pak.entries().size() == 3);
  CHECK(pak.entries()[0].name == "A/B");
  CHECK(pak.view("a\\b").size() == 3 && pak.view("A/B")[2] == 3);
  CHECK(pak.view("ABCD").empty());
  CHECK(pak.view("x/y.tmd").size() == 1000 && pak.view("X/Y.TMD")[999] == 0xAB);
  CHECK(pak.find("missing") == nullptr);
}

TEST(pak_rejects_truncation) {
  Bytes raw = Pak::build({{"FILE.BIN", Bytes(64, 1)}});
  raw.resize(raw.size() - 1);
  CHECK_THROWS(Pak{Bytes(raw)});
  CHECK_THROWS((Pak{Bytes{1, 0}}));
}
