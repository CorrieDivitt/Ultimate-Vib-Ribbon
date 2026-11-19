#include "vib/sha1.h"

#include <cstring>

namespace vib {
namespace {
std::uint32_t rol(std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
}  // namespace

Sha1::Sha1() : h_{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u}, buf_{} {}

void Sha1::block(const std::uint8_t* p) {
  std::uint32_t w[80];
  for (int i = 0; i < 16; ++i)
    w[i] = (std::uint32_t(p[i * 4]) << 24) | (std::uint32_t(p[i * 4 + 1]) << 16) |
           (std::uint32_t(p[i * 4 + 2]) << 8) | std::uint32_t(p[i * 4 + 3]);
  for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
  for (int i = 0; i < 80; ++i) {
    std::uint32_t f, k;
    if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
    else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
    else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
    else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
    const std::uint32_t t = rol(a, 5) + f + e + k + w[i];
    e = d; d = c; c = rol(b, 30); b = a; a = t;
  }
  h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e;
}

void Sha1::update(ByteView data) {
  total_ += data.size();
  std::size_t i = 0;
  if (buf_len_) {
    const std::size_t take = std::min<std::size_t>(64 - buf_len_, data.size());
    std::memcpy(buf_ + buf_len_, data.data(), take);
    buf_len_ += take; i = take;
    if (buf_len_ == 64) { block(buf_); buf_len_ = 0; }
  }
  for (; i + 64 <= data.size(); i += 64) block(data.data() + i);
  if (i < data.size()) { std::memcpy(buf_, data.data() + i, data.size() - i); buf_len_ = data.size() - i; }
}

std::array<std::uint8_t, 20> Sha1::finish() {
  const std::uint64_t bits = total_ * 8;
  const std::uint8_t pad = 0x80;
  update(ByteView(&pad, 1));
  const std::uint8_t zero = 0;
  while (buf_len_ != 56) update(ByteView(&zero, 1));
  std::uint8_t len[8];
  for (int i = 0; i < 8; ++i) len[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
  update(ByteView(len, 8));
  std::array<std::uint8_t, 20> out{};
  for (int i = 0; i < 5; ++i)
    for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<std::uint8_t>(h_[i] >> (24 - 8 * j));
  return out;
}

std::string Sha1::to_hex(const std::array<std::uint8_t, 20>& digest) {
  static const char* hex = "0123456789abcdef";
  std::string s;
  for (auto v : digest) { s += hex[v >> 4]; s += hex[v & 15]; }
  return s;
}

std::string Sha1::hex_of(ByteView data) {
  Sha1 s;
  s.update(data);
  return to_hex(s.finish());
}

}  // namespace vib
