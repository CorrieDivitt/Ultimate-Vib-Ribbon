#pragma once
// Vib-Ribbon PAK archives (GAME/xx_FILES.PAK, TITLE/FILES.PAK, KIOSK/xx_FILES.PAK).
// Layout: u32 count, u32 offsets[count]; at each offset a NUL-terminated path
// padded to a 4-byte boundary, u32 length, then the file's bytes. Uncompressed.

#include <cstdint>
#include <string>
#include <vector>

#include "vib/bytes.h"

namespace vib {

struct PakEntry {
  std::string name;  // as stored, e.g. "CHARA/PEELOO/MODEL.TMD"
  std::uint32_t data_offset = 0;
  std::uint32_t size = 0;
};

class Pak {
 public:
  explicit Pak(Bytes data);

  const std::vector<PakEntry>& entries() const { return entries_; }
  const PakEntry* find(const std::string& name) const;  // case-insensitive, '\' or '/'
  ByteView view(const PakEntry& e) const;
  ByteView view(const std::string& name) const;

  // Builds an archive in the same layout. Used by tests, never to ship game data.
  static Bytes build(const std::vector<std::pair<std::string, Bytes>>& files);

 private:
  Bytes data_;
  std::vector<PakEntry> entries_;
};

}  // namespace vib
