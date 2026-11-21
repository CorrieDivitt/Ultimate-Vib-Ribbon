#pragma once
// The player's own music: MP3 and FLAC files, decoded to what the game analyses
// and plays, 44,100 Hz 16-bit stereo, as an audio CD would give it.

#include <cstdint>
#include <string>
#include <vector>

namespace vibr {

struct Music {
  std::vector<std::int16_t> pcm;  // interleaved left, right at 44.1 kHz
  std::uint32_t source_rate = 0;  // the file's own rate, for the console
  std::uint32_t source_channels = 0;
};

// Throws std::runtime_error with a plain message when the file cannot be read or
// is not MP3 or FLAC.
Music load_music(const std::string& utf8_path);

// The file's length as load_music() would give it (44.1 kHz frames), read without
// decoding the sound (FLAC: its header; MP3: its frames counted), so a whole folder's
// table of contents can be had quickly. Throws as load_music does, including for a
// file too short to make a course from.
std::uint64_t probe_music_frames(const std::string& utf8_path);

}  // namespace vibr
