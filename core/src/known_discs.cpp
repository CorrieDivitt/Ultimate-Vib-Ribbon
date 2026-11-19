#include "vib/known_discs.h"

namespace vib {
namespace {
// Vib-Ribbon (Europe) (En,Fr,De,Es,It), SCES-02873. Data track SHA-1 verified on
// the project owner's dump and matches the hash used by vib-ribbon-recomp.
constexpr KnownDisc kDiscs[] = {
    {"SCES-02873", "Europe", "a2ba376167bda9ed03ab89a11ade12410d75ede7", 7},
};
}  // namespace

const KnownDisc* identify_by_data_track_sha1(const std::string& sha1_hex) {
  for (const auto& d : kDiscs)
    if (d.data_track_sha1 == sha1_hex) return &d;
  return nullptr;
}

}  // namespace vib
