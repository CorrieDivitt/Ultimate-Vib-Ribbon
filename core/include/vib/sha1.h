#pragma once
// Small streaming SHA-1, used only to identify disc images. Not for security.

#include <array>
#include <cstdint>
#include <string>

#include "vib/bytes.h"

namespace vib {

class Sha1 {
 public:
  Sha1();
  void update(ByteView data);
  std::array<std::uint8_t, 20> finish();
  static std::string to_hex(const std::array<std::uint8_t, 20>& digest);
  static std::string hex_of(ByteView data);

 private:
  void block(const std::uint8_t* p);
  std::uint32_t h_[5];
  std::uint8_t buf_[64];
  std::size_t buf_len_ = 0;
  std::uint64_t total_ = 0;
};

}  // namespace vib
