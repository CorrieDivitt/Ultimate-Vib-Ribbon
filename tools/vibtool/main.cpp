// vibtool: developer command line for working with your own Vib-Ribbon disc image.
//
//   vibtool info    <disc.cue>              identify the disc, list tracks and files
//   vibtool extract <disc.cue> <out_dir>    copy every file off the data track and unpack PAKs
//   vibtool audio   <disc.cue> <out_dir>    write each audio track as a WAV file
//   vibtool formats <disc.cue>              parse every model, animation, camera and course script
//   vibtool sounds  <disc.cue> <out_dir>    decode the three sound banks to WAV files (44.1 kHz assumed)
//   vibtool course  <disc.cue> <round 1-6> [<cue interval in blocks>]
//                                           generate a song's obstacle course with our own code, as JSON lines
//   vibtool play    <disc.cue> <round 1-6>  play a song perfectly with our own rules and report the score
//   vibtool obj     <disc.cue> <pak> <model.tmd> <out.obj> [<anim.anm> <frame>]
//                                           export a model's lines as a Wavefront OBJ (Y up)
//
// Output stays on your machine. Never commit it.

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

#include "vib/disc_image.h"
#include "vib/iso9660.h"
#include "vib/course_gen.h"
#include "vib/course_script.h"
#include "vib/gameplay.h"
#include "vib/known_discs.h"
#include "vib/motion.h"
#include "vib/pak.h"
#include "vib/pose.h"
#include "vib/sound_bank.h"
#include "vib/tmd.h"

namespace fs = std::filesystem;
using namespace vib;

namespace {

int usage() {
  std::fputs(
      "usage:\n"
      "  vibtool info    <disc.cue>\n"
      "  vibtool extract <disc.cue> <out_dir>\n"
      "  vibtool audio   <disc.cue> <out_dir>\n"
      "  vibtool formats <disc.cue>\n"
      "  vibtool sounds  <disc.cue> <out_dir>\n"
      "  vibtool course  <disc.cue> <round 1-6> [<cue interval in blocks>]\n"
      "  vibtool play    <disc.cue> <round 1-6>\n"
      "  vibtool obj     <disc.cue> <pak> <model.tmd> <out.obj> [<anim.anm> <frame>]\n"
      "                  e.g. vibtool obj disc.cue GAME/02_FILES.PAK CHARA/PEELOO/MODEL.TMD peeloo.obj\n",
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

Bytes wav_mono(const std::vector<std::int16_t>& pcm, std::uint32_t rate);

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


bool ends_with(const std::string& s, const char* suffix) {
  const std::size_t n = std::strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

int cmd_formats(const fs::path& cue) {
  DiscImage disc = DiscImage::open(cue);
  Iso9660 iso(disc);
  std::size_t models = 0, lines = 0, fills = 0, unsupported = 0, anims = 0, frames = 0, cams = 0, cam_keys = 0, scripts = 0, failures = 0;
  for (const auto& e : iso.entries()) {
    if (e.directory || !is_pak(e.path)) continue;
    const Pak pak(iso.read(e));
    for (const auto& p : pak.entries()) {
      std::string n = p.name;
      for (char& c : n) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      try {
        if (ends_with(n, ".TMD")) {
          const TmdModel m = parse_tmd(pak.view(p));
          ++models;
          for (const auto& o : m.objects) { lines += o.lines.size(); fills += o.marker != FaceMarker::None; }
          unsupported += m.unsupported_primitives;
          if (m.unsupported_primitives) std::printf("  %s/%s: %zu unsupported primitives\n", e.path.c_str(), p.name.c_str(), m.unsupported_primitives);
        } else if (ends_with(n, ".ANM")) {
          frames += parse_anm(pak.view(p)).frames.size();
          ++anims;
        } else if (ends_with(n, ".ANC")) {
          cam_keys += parse_anc(pak.view(p)).keys.size();
          ++cams;
        } else if (ends_with(n, ".FSL")) {
          const CourseScript s = parse_course_script(pak.view(p));
          ++scripts;
          if (scripts == 1) {
            std::printf("Course script %s/%s: %zu tracks, %zu fixed patterns, %zu distributions\n", e.path.c_str(), p.name.c_str(),
                        s.tracks.size(), s.fixed.size(), s.distributions.size());
            for (std::size_t t = 0; t < s.tracks.size(); ++t)
              std::printf("  track %zu: %zu control, %zu pattern, %zu event segments\n", t, s.tracks[t].control.size(),
                          s.tracks[t].patterns.size(), s.tracks[t].events.size());
          }
        }
      } catch (const std::exception& ex) {
        ++failures;
        std::printf("  FAILED %s/%s: %s\n", e.path.c_str(), p.name.c_str(), ex.what());
      }
    }
  }
  std::printf("Models: %zu (%zu lines, %zu objects with a facing marker, %zu unsupported primitives)\n", models, lines, fills, unsupported);
  std::printf("Animations: %zu (%zu frames)   Cameras: %zu (%zu keys)   Course scripts: %zu\n", anims, frames, cams, cam_keys, scripts);
  std::printf("%s\n", failures || unsupported ? "PROBLEMS FOUND" : "All files parsed.");
  return failures || unsupported ? 1 : 0;
}

int cmd_obj(const fs::path& cue, const std::string& pak_path, const std::string& model_name, const fs::path& out,
            const char* anim_name, int frame) {
  DiscImage disc = DiscImage::open(cue);
  Iso9660 iso(disc);
  const Pak pak(iso.read(pak_path));
  const TmdModel model = parse_tmd(pak.view(model_name));
  std::vector<PosedObject> posed;
  if (anim_name) {
    const ModelAnimation anim = parse_anm(pak.view(anim_name));
    if (frame < 0 || std::size_t(frame) >= anim.frames.size())
      throw std::runtime_error("frame out of range; animation has " + std::to_string(anim.frames.size()) + " frames");
    posed = pose(model, anim.frames[std::size_t(frame)]);
  } else {
    posed = rest_pose(model);
  }
  std::string obj = "# Exported by vibtool from your own disc. Do not share or commit.\n";
  std::size_t base = 1, line_count = 0;
  for (const auto& p : posed) {
    obj += "o object_" + std::to_string(p.object) + "\n";
    for (const auto& v : p.vertices)  // flip Y so the model stands upright in Y-up tools such as Blender
      obj += "v " + std::to_string(v.x) + " " + std::to_string(-v.y) + " " + std::to_string(v.z) + "\n";
    for (const auto& l : model.objects[std::size_t(p.object)].lines) {
      obj += "l " + std::to_string(base + l.a) + " " + std::to_string(base + l.b) + "\n";
      ++line_count;
    }
    base += p.vertices.size();
  }
  write_file(out, ByteView(reinterpret_cast<const std::uint8_t*>(obj.data()), obj.size()));
  std::printf("Wrote %s: %zu objects, %zu lines\n", out.string().c_str(), posed.size(), line_count);
  return 0;
}

Bytes wav_mono(const std::vector<std::int16_t>& pcm, std::uint32_t rate) {
  const auto data_bytes = static_cast<std::uint32_t>(pcm.size() * 2);
  Bytes wav;
  put(wav, "RIFF"); put32(wav, 36 + data_bytes); put(wav, "WAVE"); put(wav, "fmt ");
  put32(wav, 16); put16(wav, 1); put16(wav, 1); put32(wav, rate); put32(wav, rate * 2); put16(wav, 2); put16(wav, 16);
  put(wav, "data"); put32(wav, data_bytes);
  for (std::int16_t s : pcm) put16(wav, static_cast<std::uint16_t>(s));
  return wav;
}

int cmd_sounds(const fs::path& cue, const fs::path& out) {
  DiscImage disc = DiscImage::open(cue);
  Iso9660 iso(disc);
  for (const char* name : {"GAME/AUDIO/PSJ_SE", "TITLE/AUDIO/TITLE", "KIOSK/AUDIO/KIOSK"}) {
    const SoundBank bank = parse_vh(iso.read(std::string(name) + ".VH"));
    const Bytes vb = iso.read(std::string(name) + ".VB");
    const auto samples = split_vb(bank, vb);
    std::string folder = name;
    folder = folder.substr(folder.rfind('/') + 1);
    for (std::size_t i = 0; i < samples.size(); ++i) {
      const DecodedSample d = decode_spu_adpcm(samples[i]);
      char file[32];
      std::snprintf(file, sizeof file, "%03zu%s.wav", i + 1, d.loop_start >= 0 ? "_loop" : "");
      write_file(out / path_from_utf8(folder) / file, wav_mono(d.pcm, 44100));
    }
    std::printf("  %s: %zu programs, %zu samples\n", name, bank.programs.size(), samples.size());
  }
  std::printf("Pitch assumption: each sample is written at 44,100 Hz, its rate when keyed at its centre note.\n");
  return 0;
}

int cmd_course(const fs::path& cue, int round, int interval) {
  DiscImage disc = DiscImage::open(cue);
  Iso9660 iso(disc);
  const Pak pak(iso.read("GAME/02_FILES.PAK"));
  const CourseScript script = parse_course_script(pak.view("SCRIPT/SYSTEM3.FSL"));
  // Round n plays the disc's n-th audio track, which is CD track n + 1.
  const std::vector<std::int16_t> pcm = disc.read_audio_track(round + 1);
  const std::vector<Block> blocks = make_blocks(pcm);
  CourseGenerator gen(script, interval);
  gen.start_round(round, 0);
  for (std::size_t b = 0; b < blocks.size(); ++b) gen.process(static_cast<std::int32_t>(b * kBlockSize), blocks[b]);
  static const char* types[] = {"part", "camera", "effect"};
  for (const CourseEvent& e : gen.events())
    std::printf("{\"type\": \"%s\", \"time\": %d, \"value\": %d, \"length\": %d, \"scale\": %d}\n", types[int(e.type)], e.time, e.value,
                e.length, e.scale);
  return 0;
}

// A perfect player: every part pressed the moment it reaches Vibri. The course is
// generated eight seconds ahead of the music heard, as in the game.
int cmd_play(const fs::path& cue, int round) {
  DiscImage disc = DiscImage::open(cue);
  Iso9660 iso(disc);
  const Pak pak(iso.read("GAME/02_FILES.PAK"));
  const CourseScript script = parse_course_script(pak.view("SCRIPT/SYSTEM3.FSL"));
  const std::vector<Block> blocks = make_blocks(disc.read_audio_track(round + 1));
  const auto total = static_cast<std::int32_t>(blocks.size() * kBlockSize);
  CourseGenerator gen(script);
  gen.start_round(round, 0);
  Gameplay game;
  RoundSetup setup;
  setup.course = round <= 2 ? Course::Bronze : round <= 4 ? Course::Silver : Course::Gold;
  setup.course_seconds = total / kSampleRate;
  setup.track_index = (round - 1) % 2;
  game.start(setup);
  game.end_at(total);
  int hits = 0, misses = 0, parts = 0;
  std::size_t fed = 0;
  Outcome outcome = Outcome::Playing;
  const std::int32_t delay = kLookAheadBlocks * kBlockSize;
  for (std::size_t b = 0; outcome == Outcome::Playing && b < blocks.size() + 4000; ++b) {
    const auto analysed = static_cast<std::int32_t>(b * kBlockSize);
    if (b < blocks.size()) gen.process(analysed, blocks[b]);
    for (; fed < gen.events().size(); ++fed) {
      game.add_event(gen.events()[fed]);
      parts += gen.events()[fed].type == CourseEvent::Type::Part;
    }
    const std::int32_t heard = analysed - delay;
    if (heard < 0) continue;
    std::uint8_t press = 0;
    const Part* first = nullptr;
    for (const Part& p : game.parts())
      if (!p.done && (!first || p.time < first->time)) first = &p;
    if (first && first->time <= heard) press = road_presses(first->road);
    outcome = game.update(heard, press);
    for (const GameEvent& e : game.events()) {
      hits += e.type == GameEvent::Type::Hit;
      misses += e.type == GameEvent::Type::Miss;
    }
  }
  static const char* forms[] = {"angel (best)", "rabbit", "frog", "worm"};
  std::printf("round %d: %d parts, %d hit, %d missed, %s\n", round, parts, hits, misses,
              outcome == Outcome::Clear ? "cleared" : outcome == Outcome::GameOver ? "game over" : "did not finish");
  std::printf("score %d + end bonus %d = %d, final form: %s\n", game.score(), game.bonus(), game.score() + game.bonus(), forms[game.form()]);
  return outcome == Outcome::Clear ? 0 : 1;
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
    if (cmd == "formats" && argc == 3) return cmd_formats(cue);
    if (cmd == "play" && argc == 4) return cmd_play(cue, std::atoi(argv[3]));
    if (cmd == "course" && (argc == 4 || argc == 5)) return cmd_course(cue, std::atoi(argv[3]), argc == 5 ? std::atoi(argv[4]) : 1);
    if (cmd == "sounds" && argc == 4) return cmd_sounds(cue, path_from_utf8(argv[3]));
    if (cmd == "obj" && (argc == 6 || argc == 8))
      return cmd_obj(cue, argv[3], argv[4], path_from_utf8(argv[5]), argc == 8 ? argv[6] : nullptr, argc == 8 ? std::atoi(argv[7]) : 0);
    return usage();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "vibtool: %s\n", e.what());
    return 1;
  }
}
