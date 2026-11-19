#pragma once
// Disc releases the project has been verified against. Only hashes and layout
// facts live here; no game data is ever stored in this repository.

#include <string>
#include <string_view>

namespace vib {

struct KnownDisc {
  std::string_view serial;
  std::string_view region;
  std::string_view data_track_sha1;
  int audio_tracks;
};

// Returns the matching release, or nullptr for an unknown image.
const KnownDisc* identify_by_data_track_sha1(const std::string& sha1_hex);

}  // namespace vib
