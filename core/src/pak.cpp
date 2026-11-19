#include "vib/pak.h"

#include <algorithm>
#include <cctype>

namespace vib {
namespace {
std::string norm(std::string s) {
  for (char& c : s) c = c == '\\' ? '/' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
std::size_t padded_name_size(std::size_t name_len) { return (name_len / 4 + 1) * 4; }  // always at least one NUL
}  // namespace

Pak::Pak(Bytes data) : data_(std::move(data)) {
  const ByteView b(data_);
  const std::uint32_t count = read_u32(b, 0);
  require(count < 100000 && 4 + std::size_t(count) * 4 <= b.size(), "PAK header is corrupt");
  entries_.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint32_t off = read_u32(b, 4 + std::size_t(i) * 4);
    require(off < b.size(), "PAK offset out of range");
    const auto nul = std::find(b.begin() + off, b.end(), std::uint8_t(0));
    require(nul != b.end(), "PAK name not terminated");
    PakEntry e;
    e.name.assign(b.begin() + off, nul);
    const std::size_t len_at = off + padded_name_size(e.name.size());
    e.size = read_u32(b, len_at);
    e.data_offset = static_cast<std::uint32_t>(len_at + 4);
    require(std::size_t(e.data_offset) + e.size <= b.size(), "PAK entry " + e.name + " runs past the end");
    entries_.push_back(std::move(e));
  }
}

const PakEntry* Pak::find(const std::string& name) const {
  const std::string want = norm(name);
  for (const auto& e : entries_)
    if (norm(e.name) == want) return &e;
  return nullptr;
}

ByteView Pak::view(const PakEntry& e) const { return ByteView(data_.data() + e.data_offset, e.size); }

ByteView Pak::view(const std::string& name) const {
  const PakEntry* e = find(name);
  require(e != nullptr, "not in PAK: " + name);
  return view(*e);
}

Bytes Pak::build(const std::vector<std::pair<std::string, Bytes>>& files) {
  Bytes out(4 + files.size() * 4, 0);
  auto put32 = [&](std::size_t at, std::uint32_t v) {
    for (int k = 0; k < 4; ++k) out[at + k] = static_cast<std::uint8_t>(v >> (8 * k));
  };
  put32(0, static_cast<std::uint32_t>(files.size()));
  for (std::size_t i = 0; i < files.size(); ++i) {
    put32(4 + i * 4, static_cast<std::uint32_t>(out.size()));
    const auto& [name, bytes] = files[i];
    const std::size_t start = out.size();
    out.resize(start + padded_name_size(name.size()), 0);
    std::copy(name.begin(), name.end(), out.begin() + static_cast<std::ptrdiff_t>(start));
    const std::size_t len_at = out.size();
    out.resize(len_at + 4);
    put32(len_at, static_cast<std::uint32_t>(bytes.size()));
    out.insert(out.end(), bytes.begin(), bytes.end());
  }
  return out;
}

}  // namespace vib
