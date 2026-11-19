#pragma once
// Read access to a raw CD image described by a CUE sheet: user data from the
// data track, and 44.1 kHz 16-bit stereo PCM from audio tracks.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "vib/bytes.h"
#include "vib/cue.h"

namespace vib {

struct TrackInfo {
  int number = 0;
  TrackMode mode = TrackMode::Audio;
  std::filesystem::path file;
  std::uint64_t file_start_frame = 0;  // INDEX 01 position in its file
  std::uint32_t pregap_frames = 0;     // INDEX 01 - INDEX 00, 0 if absent
  std::uint32_t length_frames = 0;     // from INDEX 01 to the end of the track
  double seconds() const { return double(length_frames) / kFramesPerSecond; }
};

class DiscImage {
 public:
  static DiscImage open(const std::filesystem::path& cue_path);

  const std::vector<TrackInfo>& tracks() const { return tracks_; }
  const TrackInfo& track(int number) const;
  const TrackInfo& data_track() const;  // first non-audio track

  // One sector of the data track, `lba` counted from the track's INDEX 01.
  // Returns user data: 2048 bytes for Mode 1 and Mode 2 Form 1, 2324 for Mode 2 Form 2.
  Bytes read_data_sector(std::uint32_t lba);

  // `count` user-data bytes starting at `lba`, Form 1 sectors only.
  Bytes read_data(std::uint32_t lba, std::size_t count);

  // Whole audio track as interleaved little-endian int16 stereo, 44,100 Hz.
  std::vector<std::int16_t> read_audio_track(int number);

  // SHA-1 of the whole data track file region (what Redump lists for track 1).
  std::string data_track_sha1();

 private:
  std::ifstream& stream_for(const std::filesystem::path& file);
  void read_raw(const TrackInfo& t, std::uint64_t frame_in_track, std::uint8_t* out, std::size_t frames);

  std::vector<TrackInfo> tracks_;
  std::filesystem::path open_path_;
  std::unique_ptr<std::ifstream> stream_;
};

}  // namespace vib
