#pragma once
// CUE sheet parsing. Supports one BIN per track (Redump style) and one BIN for
// the whole disc. Positions are in CD frames (sectors): 75 per second.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vib {

constexpr std::uint32_t kRawSectorSize = 2352;
constexpr std::uint32_t kFramesPerSecond = 75;

enum class TrackMode { Audio, Mode1Raw, Mode2Raw };

struct CueTrack {
  int number = 0;
  TrackMode mode = TrackMode::Audio;
  std::filesystem::path file;            // absolute or relative to the CUE's folder
  std::optional<std::uint32_t> index00;  // pregap start, frames into `file`
  std::uint32_t index01 = 0;             // track start, frames into `file`
};

struct CueSheet {
  std::vector<CueTrack> tracks;
};

// Parses CUE text. Relative FILE names are resolved against `base_dir`.
CueSheet parse_cue(std::string_view text, const std::filesystem::path& base_dir);
CueSheet load_cue(const std::filesystem::path& cue_path);

// "MM:SS:FF" to frames.
std::uint32_t msf_to_frames(std::string_view msf);

}  // namespace vib
