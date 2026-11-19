#include "vib/iso9660.h"

#include <algorithm>
#include <cctype>

namespace vib {
namespace {
std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  std::replace(s.begin(), s.end(), '\\', '/');
  while (!s.empty() && s.front() == '/') s.erase(s.begin());
  return s;
}
}  // namespace

Iso9660::Iso9660(DiscImage& disc) : disc_(disc) {
  const Bytes pvd = disc_.read_data_sector(16);
  require(pvd.size() >= 2048 && pvd[0] == 1 && std::string(pvd.begin() + 1, pvd.begin() + 6) == "CD001",
          "no ISO 9660 primary volume descriptor at sector 16");
  system_id_ = std::string(pvd.begin() + 8, pvd.begin() + 40);
  while (!system_id_.empty() && system_id_.back() == ' ') system_id_.pop_back();
  const ByteView root(pvd.data() + 156, 34);
  walk(read_u32(root, 2), read_u32(root, 10), "", 0);
}

void Iso9660::walk(std::uint32_t lba, std::uint32_t size, const std::string& prefix, int depth) {
  require(depth < 16, "ISO directory tree too deep");
  const Bytes dir = disc_.read_data(lba, size);
  std::size_t i = 0;
  while (i < dir.size()) {
    const std::uint8_t len = dir[i];
    if (len == 0) {  // records never cross a sector boundary; skip padding
      i = (i / 2048 + 1) * 2048;
      continue;
    }
    require(i + len <= dir.size() && len >= 34, "corrupt ISO directory record");
    const ByteView rec(dir.data() + i, len);
    i += len;
    const std::uint8_t name_len = rec[32];
    require(33u + name_len <= len, "corrupt ISO name length");
    if (name_len == 1 && (rec[33] == 0 || rec[33] == 1)) continue;  // "." and ".."
    std::string name(reinterpret_cast<const char*>(rec.data() + 33), name_len);
    if (auto semi = name.find(';'); semi != std::string::npos) name.resize(semi);
    IsoEntry e;
    e.path = upper(prefix.empty() ? name : prefix + "/" + name);
    e.lba = read_u32(rec, 2);
    e.size = read_u32(rec, 10);
    e.directory = (rec[25] & 2) != 0;
    entries_.push_back(e);
    if (e.directory) walk(e.lba, e.size, e.path, depth + 1);
  }
}

const IsoEntry* Iso9660::find(std::string path) const {
  path = upper(std::move(path));
  if (auto semi = path.find(';'); semi != std::string::npos) path.resize(semi);
  for (const auto& e : entries_)
    if (e.path == path) return &e;
  return nullptr;
}

Bytes Iso9660::read(const IsoEntry& file) {
  require(!file.directory, file.path + " is a directory");
  return disc_.read_data(file.lba, file.size);
}

Bytes Iso9660::read(const std::string& path) {
  const IsoEntry* e = find(path);
  require(e != nullptr, "file not on disc: " + path);
  return read(*e);
}

}  // namespace vib
