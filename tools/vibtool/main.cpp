// vibtool: developer command line for working with your own Vib-Ribbon disc image.
//
//   vibtool info    <disc.cue>              identify the disc, list tracks and files
//   vibtool extract <disc.cue> <out_dir>    copy every file off the data track and unpack PAKs
//   vibtool audio   <disc.cue> <out_dir>    write each audio track as a WAV file
//
// Output stays on your machine. Never commit it.

#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

#include "vib/disc_image.h"
#include "vib/iso9660.h"
#include "vib/known_discs.h"
#include "vib/pak.h"

namespace fs = std::filesystem;
using namespace vib;

namespace {

int usage() {
  std::fputs(
      "usage:\n"
      "  vibtool info    <disc.cue>\n"
      "  vibtool extract <disc.cue> <out_dir>\n"
      "  vibtool audio   <disc.cue> <out_dir>\n",
      stderr);
  return 2;
}

const char* mode_name(TrackMode m) {
  switch (m) {
    case TrackMode::Audio: return "AUDIO";
    case TrackMode::Mode1Raw: return "MODE1/2352";
    case TrackMode::Mode2Raw: return "MODE2/2352";
  }
  return "?";
}

bool is_pak(const std::string& path) {
  return path.size() > 4 && path.compare(path.size() - 4, 4, ".PAK") == 0;
}

int cmd_info(const fs::path& cue) {
  DiscImage disc = DiscImage::open(cue);
  const std::string sha = disc.data_track_sha1();
  const KnownDisc* known = identify_by_data_track_sha1(sha);
  std::printf("Data track SHA-1: %s\n", sha.c_str());
  if (known)
    std::printf("Identified: Vib-Ribbon %s (%.*s), supported\n\n", std::string(known->region).c_str(),
                int(known->serial.size()), known->serial.data());
  else
    std::printf("Not a release this project has been verified against. Reading anyway.\n\n");

  std::printf("Tracks:\n");
  for (const auto& t : disc.tracks()) {
    const double s = t.seconds();
    std::printf("  %2d  %-11s %6u sectors  %2d:%05.2f  pregap %u\n", t.number, mode_name(t.mode), t.length_frames,
                int(s / 60), s - 60 * int(s / 60), t.pregap_frames);
  }

  Iso9660 iso(disc);
  std::printf("\nFiles on the data track (system id \"%s\"):\n", iso.system_id().c_str());
  for (const auto& e : iso.entries()) {
    if (e.directory) continue;
    std::printf("  %9u  %s", e.size, e.path.c_str());
    if (is_pak(e.path)) std::printf("  (%zu files)", Pak(iso.read(e)).entries().size());
    std::printf("\n");
  }
  return 0;
}

int cmd_extract(const fs::path& cue, const fs::path& out) {
  DiscImage disc = DiscImage::open(cue);
  Iso9660 iso(disc);
  std::size_t files = 0, unpacked = 0;
  for (const auto& e : iso.entries()) {
    if (e.directory) continue;
    const Bytes data = iso.read(e);
    write_file(out / "disc" / path_from_utf8(e.path), data);
    ++files;
    if (is_pak(e.path)) {
      std::string folder = e.path;
      for (char& c : folder) if (c == '/' || c == '.') c = '_';
      const Pak pak{Bytes(data)};
      for (const auto& p : pak.entries()) {
        write_file(out / "pak" / path_from_utf8(folder) / path_from_utf8(p.name), pak.view(p));
        ++unpacked;
      }
    }
  }
  std::printf("Wrote %zu files to %s and %zu unpacked PAK entries to %s\n", files, (out / "disc").string().c_str(),
              unpacked, (out / "pak").string().c_str());
  return 0;
}

void put(Bytes& b, const char* tag) { b.insert(b.end(), tag, tag + 4); }
void put32(Bytes& b, std::uint32_t v) {
  for (int k = 0; k < 4; ++k) b.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
}
void put16(Bytes& b, std::uint16_t v) {
  b.push_back(static_cast<std::uint8_t>(v));
  b.push_back(static_cast<std::uint8_t>(v >> 8));
}

int cmd_audio(const fs::path& cue, const fs::path& out) {
  DiscImage disc = DiscImage::open(cue);
  for (const auto& t : disc.tracks()) {
    if (t.mode != TrackMode::Audio) continue;
    const auto pcm = disc.read_audio_track(t.number);
    const auto data_bytes = static_cast<std::uint32_t>(pcm.size() * 2);
    Bytes wav;
    wav.reserve(44 + data_bytes);
    put(wav, "RIFF"); put32(wav, 36 + data_bytes); put(wav, "WAVE"); put(wav, "fmt ");
    put32(wav, 16); put16(wav, 1); put16(wav, 2); put32(wav, 44100); put32(wav, 44100 * 4); put16(wav, 4); put16(wav, 16);
    put(wav, "data"); put32(wav, data_bytes);
    for (std::int16_t s : pcm) put16(wav, static_cast<std::uint16_t>(s));
    char name[32];
    std::snprintf(name, sizeof name, "track%02d.wav", t.number);
    write_file(out / name, wav);
    std::printf("  %s  %.2f s\n", (out / name).string().c_str(), t.seconds());
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 3) return usage();
    const std::string cmd = argv[1];
    const fs::path cue = path_from_utf8(argv[2]);
    if (cmd == "info" && argc == 3) return cmd_info(cue);
    if (cmd == "extract" && argc == 4) return cmd_extract(cue, path_from_utf8(argv[3]));
    if (cmd == "audio" && argc == 4) return cmd_audio(cue, path_from_utf8(argv[3]));
    return usage();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "vibtool: %s\n", e.what());
    return 1;
  }
}
