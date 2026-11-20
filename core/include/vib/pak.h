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

// The original's languages by number (0 German, 1 English, 2 Spanish, 3 French,
// 4 Italian, 5 Japanese) and the bit its files are tagged with for each:
// German 04, English 02, Spanish 08, French 10, Italian 20, Japanese 01.
std::uint8_t language_bit(int language);
// "GAME/" + language_prefix(1) + "FILES.PAK" = "GAME/02_FILES.PAK".
std::string language_prefix(int language);

class Pak;
// The name `name` has in `pak` for `language`, looked for as the original does: the
// file for every language, the language's own, then the versions shared by groups
// of languages (FE all but Japanese, 03 Japanese and English, FB all but German).
// Throws FormatError if there is none.
std::string localized(const Pak& pak, const std::string& name, int language);

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
