#include "vib/own_cd.h"

#include <cctype>

#include "vib/course_gen.h"

namespace vib {

std::vector<std::int32_t> OwnCd::track_samples() const {
  std::vector<std::int32_t> out;
  for (const Track& t : tracks) out.push_back(std::int32_t(t.sectors / 75) * kSampleRate);
  return out;
}

OwnCd make_own_cd(const std::vector<std::pair<std::string, std::uint32_t>>& name_and_frames) {
  OwnCd cd;
  std::uint32_t at = 150;  // two seconds of lead-in, as on an audio CD
  for (const auto& [name, frames] : name_and_frames) {
    OwnCd::Track t;
    t.name = name;
    t.frames = frames;
    t.start = at;
    t.sectors = (frames + 587) / 588;  // 588 frames a sector, 75 sectors a second
    at += t.sectors;
    cd.tracks.push_back(t);
  }
  cd.lead_out = at;
  cd.key = disc_key(cd.lead_out, std::uint32_t(cd.tracks.size()));
  return cd;
}

bool track_name_less(const std::string& a, const std::string& b) {
  std::size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    const auto ca = static_cast<unsigned char>(a[i]), cb = static_cast<unsigned char>(b[j]);
    if (std::isdigit(ca) && std::isdigit(cb)) {
      // Whole numbers by value: skip leading zeros, then the longer is larger.
      std::size_t ei = i, ej = j;
      while (ei < a.size() && std::isdigit(static_cast<unsigned char>(a[ei]))) ++ei;
      while (ej < b.size() && std::isdigit(static_cast<unsigned char>(b[ej]))) ++ej;
      std::size_t si = i, sj = j;
      while (si + 1 < ei && a[si] == '0') ++si;
      while (sj + 1 < ej && b[sj] == '0') ++sj;
      if (ei - si != ej - sj) return ei - si < ej - sj;
      for (std::size_t k = 0; k < ei - si; ++k)
        if (a[si + k] != b[sj + k]) return a[si + k] < b[sj + k];
      i = ei, j = ej;
      continue;
    }
    const int la = std::tolower(ca), lb = std::tolower(cb);
    if (la != lb) return la < lb;
    ++i, ++j;
  }
  if (a.size() - i != b.size() - j) return a.size() - i < b.size() - j;
  return a < b;  // (ties broken the plain way, so the order is total)
}

}  // namespace vib
