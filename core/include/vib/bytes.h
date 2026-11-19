#pragma once
// Little-endian readers with bounds checks. All PlayStation data is little-endian.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace vib {

using Bytes = std::vector<std::uint8_t>;
using ByteView = std::span<const std::uint8_t>;

struct FormatError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

inline void require(bool ok, const std::string& what) {
  if (!ok) throw FormatError(what);
}

inline std::uint16_t read_u16(ByteView b, std::size_t off) {
  require(off + 2 <= b.size(), "read_u16 out of range");
  return static_cast<std::uint16_t>(b[off] | (b[off + 1] << 8));
}

inline std::int16_t read_i16(ByteView b, std::size_t off) {
  return static_cast<std::int16_t>(read_u16(b, off));
}

inline std::uint32_t read_u32(ByteView b, std::size_t off) {
  require(off + 4 <= b.size(), "read_u32 out of range");
  return static_cast<std::uint32_t>(b[off]) | (static_cast<std::uint32_t>(b[off + 1]) << 8) |
         (static_cast<std::uint32_t>(b[off + 2]) << 16) | (static_cast<std::uint32_t>(b[off + 3]) << 24);
}

inline std::int32_t read_i32(ByteView b, std::size_t off) {
  return static_cast<std::int32_t>(read_u32(b, off));
}

// UTF-8 text to a filesystem path, correct on Windows for non-ASCII names.
inline std::filesystem::path path_from_utf8(std::string_view s) {
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

Bytes read_file(const std::filesystem::path& path);
void write_file(const std::filesystem::path& path, ByteView data);

}  // namespace vib
