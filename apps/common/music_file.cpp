#include "music_file.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "dr_flac.h"  // (implementations compiled in decoders.cpp)
#include "dr_mp3.h"

namespace vibr {
namespace {

std::vector<unsigned char> read_file(const std::string& utf8_path) {
  // Through std::filesystem so names with any characters open on Windows too.
  const std::filesystem::path path(std::u8string(utf8_path.begin(), utf8_path.end()));
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open " + utf8_path);
  return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool ends_with(std::string s, const char* tail) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  const std::size_t n = std::strlen(tail);
  return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

// To 44.1 kHz stereo: mono doubled, extra channels dropped, other rates by linear interpolation.
std::vector<std::int16_t> to_cd(const std::int16_t* in, std::uint64_t frames, unsigned channels, unsigned rate) {
  std::vector<std::int16_t> stereo(std::size_t(frames) * 2);
  for (std::uint64_t f = 0; f < frames; ++f) {
    const std::int16_t l = in[f * channels], r = channels > 1 ? in[f * channels + 1] : l;
    stereo[std::size_t(f) * 2] = l;
    stereo[std::size_t(f) * 2 + 1] = r;
  }
  if (rate == 44100 || rate == 0 || frames == 0) return stereo;
  const auto out_frames = static_cast<std::uint64_t>(double(frames) * 44100.0 / rate);
  std::vector<std::int16_t> out(std::size_t(out_frames) * 2);
  for (std::uint64_t o = 0; o < out_frames; ++o) {
    const double at = double(o) * rate / 44100.0;
    const auto i = std::min<std::uint64_t>(static_cast<std::uint64_t>(at), frames - 1);
    const std::uint64_t j = std::min<std::uint64_t>(i + 1, frames - 1);
    const double t = at - double(i);
    for (int c = 0; c < 2; ++c)
      out[std::size_t(o) * 2 + c] = static_cast<std::int16_t>(std::lround((1 - t) * stereo[std::size_t(i) * 2 + c] + t * stereo[std::size_t(j) * 2 + c]));
  }
  return out;
}

// A file name as the C libraries want it: wide on Windows, so any characters open.
#ifdef _WIN32
std::wstring native_name(const std::string& utf8_path) {
  return std::filesystem::path(std::u8string(utf8_path.begin(), utf8_path.end())).wstring();
}
#endif

// The frame count to_cd() gives for `frames` at `rate`.
std::uint64_t cd_frames(std::uint64_t frames, unsigned rate) {
  if (rate == 44100 || rate == 0 || frames == 0) return frames;
  return static_cast<std::uint64_t>(double(frames) * 44100.0 / rate);
}

constexpr std::uint64_t kShortest = 44100 * 10;  // a course needs 10 s at least

}  // namespace

std::uint64_t probe_music_frames(const std::string& utf8_path) {
  std::uint64_t frames = 0;
  unsigned rate = 0;
  if (ends_with(utf8_path, ".mp3")) {
    drmp3 mp3;
#ifdef _WIN32
    const bool ok = drmp3_init_file_w(&mp3, native_name(utf8_path).c_str(), nullptr);
#else
    const bool ok = drmp3_init_file(&mp3, utf8_path.c_str(), nullptr);
#endif
    if (!ok) throw std::runtime_error("not a readable MP3: " + utf8_path);
    rate = mp3.sampleRate;
    frames = drmp3_get_pcm_frame_count(&mp3);
    drmp3_uninit(&mp3);
  } else if (ends_with(utf8_path, ".flac")) {
#ifdef _WIN32
    drflac* flac = drflac_open_file_w(native_name(utf8_path).c_str(), nullptr);
#else
    drflac* flac = drflac_open_file(utf8_path.c_str(), nullptr);
#endif
    if (!flac) throw std::runtime_error("not a readable FLAC: " + utf8_path);
    rate = flac->sampleRate;
    frames = flac->totalPCMFrameCount;
    drflac_close(flac);
    if (frames == 0) return std::uint64_t(load_music(utf8_path).pcm.size() / 2);  // (a stream that does not say)
  } else {
    throw std::runtime_error("only MP3 and FLAC files are supported for now: " + utf8_path);
  }
  frames = cd_frames(frames, rate);
  if (frames < kShortest) throw std::runtime_error("too short to make a course from (under 10 seconds): " + utf8_path);
  return frames;
}

Music load_music(const std::string& utf8_path) {
  const std::vector<unsigned char> data = read_file(utf8_path);
  Music m;
  if (ends_with(utf8_path, ".mp3")) {
    drmp3_config config{};
    drmp3_uint64 frames = 0;
    drmp3_int16* pcm = drmp3_open_memory_and_read_pcm_frames_s16(data.data(), data.size(), &config, &frames, nullptr);
    if (!pcm) throw std::runtime_error("not a readable MP3: " + utf8_path);
    m.source_rate = config.sampleRate;
    m.source_channels = config.channels;
    m.pcm = to_cd(pcm, frames, std::max(1u, config.channels), config.sampleRate);
    drmp3_free(pcm, nullptr);
  } else if (ends_with(utf8_path, ".flac")) {
    unsigned channels = 0, rate = 0;
    drflac_uint64 frames = 0;
    drflac_int16* pcm = drflac_open_memory_and_read_pcm_frames_s16(data.data(), data.size(), &channels, &rate, &frames, nullptr);
    if (!pcm) throw std::runtime_error("not a readable FLAC: " + utf8_path);
    m.source_rate = rate;
    m.source_channels = channels;
    m.pcm = to_cd(pcm, frames, std::max(1u, channels), rate);
    drflac_free(pcm, nullptr);
  } else {
    throw std::runtime_error("only MP3 and FLAC files are supported for now: " + utf8_path);
  }
  if (m.pcm.size() < kShortest * 2) throw std::runtime_error("too short to make a course from (under 10 seconds): " + utf8_path);
  return m;
}

}  // namespace vibr
