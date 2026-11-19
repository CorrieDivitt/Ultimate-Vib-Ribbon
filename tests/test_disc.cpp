// Checks against the owner's real disc image. Skipped unless VIB_TEST_CUE is set,
// so CI and anyone without the game can still run the suite.

#include <cstdlib>
#include <string>

#include "test.h"
#include "vib/disc_image.h"
#include "vib/iso9660.h"
#include "vib/known_discs.h"
#include "vib/pak.h"
#include "vib/course_gen.h"
#include "vib/course_script.h"
#include "vib/motion.h"
#include "vib/sound_bank.h"
#include "vib/tmd.h"

using namespace vib;

static std::string test_cue() {
  const char* p = std::getenv("VIB_TEST_CUE");
  return p ? std::string(p) : std::string();
}

TEST(disc_sces02873_layout) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  const KnownDisc* known = identify_by_data_track_sha1(disc.data_track_sha1());
  CHECK(known != nullptr);
  CHECK(disc.tracks().size() == 8);
  CHECK(disc.data_track().length_frames == 6667);
  CHECK(disc.track(2).pregap_frames == 833);

  Iso9660 iso(disc);
  CHECK(iso.system_id() == "PLAYSTATION");
  const IsoEntry* game = iso.find("main_g.exe");
  CHECK(game && game->size == 385024);
  const Bytes exe = iso.read("MAIN_G.EXE");
  CHECK(std::string(exe.begin(), exe.begin() + 8) == "PS-X EXE");

  const Pak pak(iso.read("GAME/02_FILES.PAK"));
  CHECK(pak.entries().size() == 242);
  CHECK(pak.view("SCRIPT/SYSTEM3.FSL").size() == 26920);
  CHECK(read_u32(pak.view("CHARA/PEELOO/MODEL.TMD"), 0) == 0x41);  // TMD id
}

TEST(disc_sces02873_every_format_parses) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  auto ends = [](const std::string& s, const char* x) { return s.size() > 4 && s.compare(s.size() - 4, 4, x) == 0; };
  std::size_t models = 0, anims = 0, cams = 0, unsupported = 0;
  for (const auto& e : iso.entries()) {
    if (e.directory || !ends(e.path, ".PAK")) continue;
    const Pak pak(iso.read(e));
    for (const auto& p : pak.entries()) {
      if (ends(p.name, ".TMD")) { unsupported += parse_tmd(pak.view(p)).unsupported_primitives; ++models; }
      if (ends(p.name, ".ANM")) { (void)parse_anm(pak.view(p)); ++anims; }
      if (ends(p.name, ".ANC")) { (void)parse_anc(pak.view(p)); ++cams; }
    }
  }
  CHECK(unsupported == 0);
  CHECK(anims == 1162);  // counted across all 13 PAKs in session 2
  CHECK(models > 0 && cams > 0);

  const Pak game(iso.read("GAME/02_FILES.PAK"));
  const TmdModel peeloo = parse_tmd(game.view("CHARA/PEELOO/MODEL.TMD"));
  CHECK(peeloo.objects.size() == 16);

  const CourseScript s = parse_course_script(game.view("SCRIPT/SYSTEM3.FSL"));
  CHECK(s.tracks.size() == 7 && s.fixed.size() == 11 && s.distributions.size() == 63);
  CHECK(s.round_tracks.size() == 6 && s.song_tracks.empty() && s.medley_tracks.size() == 1 && s.medley_tracks[0] == 6);
  CHECK(s.tracks[6].control.size() == 774);
  CHECK(s.tracks[6].control.back().start == 97902000);  // 74:00 at 22,050 samples per second
}

TEST(disc_sces02873_sound_banks) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  struct Expect { const char* name; std::size_t programs, samples; };
  for (const Expect& e : {Expect{"GAME/AUDIO/PSJ_SE", 16, 129}, Expect{"TITLE/AUDIO/TITLE", 18, 29}, Expect{"KIOSK/AUDIO/KIOSK", 1, 4}}) {
    const SoundBank bank = parse_vh(iso.read(std::string(e.name) + ".VH"));
    CHECK(bank.programs.size() == e.programs);
    CHECK(bank.sample_sizes.size() == e.samples);
    const Bytes vb = iso.read(std::string(e.name) + ".VB");
    const auto samples = split_vb(bank, vb);
    std::size_t decoded = 0;
    for (const auto& s : samples) decoded += decode_spu_adpcm(s).pcm.size() > 0;
    CHECK(decoded == e.samples);
  }
}

TEST(disc_sces02873_first_song_course) {
  // Checked against a recording of the original in DuckStation (2026-11-19): all 75
  // obstacles of the first song match in time and type, with a constant 168-sample shift.
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  const Pak game(iso.read("GAME/02_FILES.PAK"));
  const CourseScript script = parse_course_script(game.view("SCRIPT/SYSTEM3.FSL"));
  const std::vector<Block> blocks = make_blocks(disc.read_audio_track(2));
  CourseGenerator gen(script);
  gen.start_round(1, 0);
  for (std::size_t b = 0; b < blocks.size(); ++b) gen.process(static_cast<std::int32_t>(b * kBlockSize), blocks[b]);
  std::vector<CourseEvent> parts;
  for (const auto& e : gen.events())
    if (e.type == CourseEvent::Type::Part) parts.push_back(e);
  CHECK(parts.size() == 75);
  if (parts.size() == 75) {
    CHECK(parts[0].time == 11522 && parts[0].value == 2 && parts[0].length == 13229);
    CHECK(parts[4].time == 218934 && parts[4].value == 3);
    CHECK(parts[74].time == 3409832 && parts[74].value == 2);
  }
}
