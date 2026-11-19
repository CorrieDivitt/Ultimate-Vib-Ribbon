#include "vib/disc_image.h"

#include <algorithm>
#include <cstring>

#include "vib/sha1.h"

namespace vib {

DiscImage DiscImage::open(const std::filesystem::path& cue_path) {
  const CueSheet sheet = load_cue(cue_path);
  DiscImage disc;
  for (std::size_t i = 0; i < sheet.tracks.size(); ++i) {
    const CueTrack& c = sheet.tracks[i];
    require(std::filesystem::exists(c.file), "missing track file " + c.file.string());
    const auto file_frames = std::filesystem::file_size(c.file) / kRawSectorSize;
    TrackInfo t;
    t.number = c.number;
    t.mode = c.mode;
    t.file = c.file;
    t.file_start_frame = c.index01;
    t.pregap_frames = c.index00 ? c.index01 - *c.index00 : 0;
    // The track ends where the next track in the same file begins, or at end of file.
    std::uint64_t end = file_frames;
    if (i + 1 < sheet.tracks.size() && sheet.tracks[i + 1].file == c.file) {
      const CueTrack& n = sheet.tracks[i + 1];
      end = n.index00 ? *n.index00 : n.index01;
    }
    require(end >= c.index01, "track " + std::to_string(c.number) + " ends before it starts");
    t.length_frames = static_cast<std::uint32_t>(end - c.index01);
    disc.tracks_.push_back(t);
  }
  return disc;
}

const TrackInfo& DiscImage::track(int number) const {
  for (const auto& t : tracks_)
    if (t.number == number) return t;
  throw FormatError("no track " + std::to_string(number));
}

const TrackInfo& DiscImage::data_track() const {
  for (const auto& t : tracks_)
    if (t.mode != TrackMode::Audio) return t;
  throw FormatError("disc has no data track");
}

std::ifstream& DiscImage::stream_for(const std::filesystem::path& file) {
  if (!stream_ || open_path_ != file) {
    stream_ = std::make_unique<std::ifstream>(file, std::ios::binary);
    if (!*stream_) throw std::runtime_error("cannot open " + file.string());
    open_path_ = file;
  }
  return *stream_;
}

void DiscImage::read_raw(const TrackInfo& t, std::uint64_t frame_in_track, std::uint8_t* out, std::size_t frames) {
  require(frame_in_track + frames <= t.length_frames, "read past end of track " + std::to_string(t.number));
  auto& in = stream_for(t.file);
  in.clear();
  in.seekg(static_cast<std::streamoff>((t.file_start_frame + frame_in_track) * kRawSectorSize));
  in.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(frames * kRawSectorSize));
  if (!in) throw std::runtime_error("short read from " + t.file.string());
}

Bytes DiscImage::read_data_sector(std::uint32_t lba) {
  const TrackInfo& t = data_track();
  std::uint8_t raw[kRawSectorSize];
  read_raw(t, lba, raw, 1);
  static const std::uint8_t sync[12] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};
  require(std::memcmp(raw, sync, 12) == 0, "sector " + std::to_string(lba) + " has no sync pattern");
  const std::uint8_t mode = raw[15];
  if (mode == 1) return Bytes(raw + 16, raw + 16 + 2048);
  require(mode == 2, "sector " + std::to_string(lba) + " has unknown mode " + std::to_string(mode));
  const bool form2 = (raw[18] & 0x20) != 0;  // subheader submode byte
  if (form2) return Bytes(raw + 24, raw + 24 + 2324);
  return Bytes(raw + 24, raw + 24 + 2048);
}

Bytes DiscImage::read_data(std::uint32_t lba, std::size_t count) {
  Bytes out;
  out.reserve(count);
  while (out.size() < count) {
    Bytes s = read_data_sector(lba++);
    require(s.size() == 2048, "read_data hit a Form 2 sector");
    const std::size_t take = std::min<std::size_t>(2048, count - out.size());
    out.insert(out.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(take));
  }
  return out;
}

std::vector<std::int16_t> DiscImage::read_audio_track(int number) {
  const TrackInfo& t = track(number);
  require(t.mode == TrackMode::Audio, "track " + std::to_string(number) + " is not audio");
  Bytes raw(std::size_t(t.length_frames) * kRawSectorSize);
  if (!raw.empty()) read_raw(t, 0, raw.data(), t.length_frames);
  std::vector<std::int16_t> pcm(raw.size() / 2);
  for (std::size_t i = 0; i < pcm.size(); ++i)
    pcm[i] = static_cast<std::int16_t>(raw[2 * i] | (raw[2 * i + 1] << 8));
  return pcm;
}

std::string DiscImage::data_track_sha1() {
  const TrackInfo& t = data_track();
  Sha1 h;
  std::vector<std::uint8_t> chunk(256 * kRawSectorSize);
  for (std::uint32_t f = 0; f < t.length_frames; f += 256) {
    const std::size_t n = std::min<std::uint32_t>(256, t.length_frames - f);
    read_raw(t, f, chunk.data(), n);
    h.update(ByteView(chunk.data(), n * kRawSectorSize));
  }
  return Sha1::to_hex(h.finish());
}

}  // namespace vib
