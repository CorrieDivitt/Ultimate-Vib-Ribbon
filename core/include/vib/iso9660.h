#pragma once
// Minimal ISO 9660 reader for PlayStation data tracks.

#include <cstdint>
#include <string>
#include <vector>

#include "vib/disc_image.h"

namespace vib {

struct IsoEntry {
  std::string path;  // upper case, '/' separated, no ";1" suffix, e.g. "GAME/02_FILES.PAK"
  std::uint32_t lba = 0;
  std::uint32_t size = 0;
  bool directory = false;
};

class Iso9660 {
 public:
  explicit Iso9660(DiscImage& disc);

  const std::string& system_id() const { return system_id_; }
  const std::vector<IsoEntry>& entries() const { return entries_; }  // files and directories, depth-first
  const IsoEntry* find(std::string path) const;                    // case-insensitive
  Bytes read(const IsoEntry& file);
  Bytes read(const std::string& path);

 private:
  void walk(std::uint32_t lba, std::uint32_t size, const std::string& prefix, int depth);
  DiscImage& disc_;
  std::string system_id_;
  std::vector<IsoEntry> entries_;
};

}  // namespace vib
