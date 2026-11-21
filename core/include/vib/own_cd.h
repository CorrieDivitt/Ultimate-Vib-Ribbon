#pragma once
// Ours: the player's own music taken as an audio CD. The original makes courses
// from an audio CD's tracks, placed in its medley script by the disc's key (from its
// table of contents). Here a "CD" is the set of music files the player picked from
// one folder (owner's decision): the files are its tracks in name order, each laid
// out on the virtual disc as a real one would be, so the same files always give the
// same disc key and the same courses.

#include <cstdint>
#include <string>
#include <vector>

namespace vib {

struct OwnCd {
  struct Track {
    std::string name;          // the file's name (UTF-8), for the screens
    std::uint32_t frames = 0;  // its length in 44.1 kHz stereo frames
    std::uint32_t start = 0;   // where it starts on the virtual disc, in sectors (75 a second)
    std::uint32_t sectors = 0; // its length, rounded up to whole sectors (588 frames each)
  };
  std::vector<Track> tracks;
  std::uint32_t lead_out = 0;  // the sector after the last track
  std::uint32_t key = 0;       // disc_key(lead_out, number of tracks)

  // Each track's whole seconds at 22,050 Hz, as the original counts a CD track
  // (CourseGenerator::prepare_disc takes these).
  std::vector<std::int32_t> track_samples() const;
};

// The virtual disc for these tracks, given in the order the player will see them
// (name order). The first track starts at sector 150, as on an audio CD; each one
// after the one before. With one track this is the same disc `vibplay --music` has
// always made, so a file keeps its course.
OwnCd make_own_cd(const std::vector<std::pair<std::string, std::uint32_t>>& name_and_frames);

// Name order for the files of a folder: case-insensitive on ASCII letters, numbers
// compared by value ("2 x" before "10 x"), so tracks numbered without leading zeros
// come in disc order.
bool track_name_less(const std::string& a, const std::string& b);

}  // namespace vib
