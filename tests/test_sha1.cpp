#include <string>

#include "test.h"
#include "vib/sha1.h"

using namespace vib;

static ByteView text(const std::string& s) { return ByteView(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); }

TEST(sha1_known_vectors) {
  CHECK(Sha1::hex_of(text("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  CHECK(Sha1::hex_of(text("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d");
  CHECK(Sha1::hex_of(text("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
        "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

TEST(sha1_streaming_matches_one_shot) {
  std::string s(1000, 'a');
  Sha1 h;
  for (std::size_t i = 0; i < s.size(); i += 37) h.update(text(s.substr(i, 37)));
  CHECK(Sha1::to_hex(h.finish()) == Sha1::hex_of(text(s)));
}
