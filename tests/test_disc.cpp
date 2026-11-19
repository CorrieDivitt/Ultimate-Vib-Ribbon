// Checks against the owner's real disc image. Skipped unless VIB_TEST_CUE is set,
// so CI and anyone without the game can still run the suite.

#include <cmath>
#include <cstdlib>
#include <string>

#include "test.h"
#include "vib/disc_image.h"
#include "vib/iso9660.h"
#include "vib/known_discs.h"
#include "vib/pak.h"
#include "vib/course_gen.h"
#include "vib/game_text.h"
#include "vib/scene.h"
#include "vib/title.h"
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

TEST(disc_sces02873_scene_draws) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  const Pak game(iso.read("GAME/02_FILES.PAK"));
  Scene scene(game);
  scene.reset(true, false);
  scene.camera().event(23);
  std::vector<SceneLine> lines;
  SceneView view;
  bool moved = false;
  for (int i = 0; i < 300; ++i) {
    Scene::Frame f;
    f.seconds = 0.02;
    f.jitter_seed = std::uint32_t(i);
    f.speed = Fixed::whole(2);
    for (int k = 0; k < 6; ++k) f.road.push_back({k, Fixed::from(1.25 + 1.5 * k - 0.03 * (i % 50)), false, k == 3});
    if (i == 50) scene.set_move(2, false);
    if (i == 51) moved = !scene.move_done(0.5);
    scene.draw(f, lines, view);
    CHECK(lines.size() > 100);
    CHECK(view.fov_y > 0.5f && view.fov_y < 1.6f);
  }
  CHECK(moved && scene.move_done(0.5));
  // Vibri stands at road position 1.25: z = (1.25 - 5) x 600.
  CHECK(std::abs(view.target.z - -2250.0f) < 3000.0f);
}

TEST(disc_sces02873_hud_draws) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  const Pak game(iso.read("GAME/02_FILES.PAK"));
  Hud hud(game);
  hud.start_round(0, true);
  std::vector<Hud::Layer> layers;
  Hud::Frame f;
  f.seconds = 0.02;
  f.score = 1234;
  f.time_left = 0.5;
  hud.draw(f, layers);
  CHECK(layers.size() == 3);
  for (const auto& l : layers) CHECK(!l.lines.empty() && l.view.fov_y > 0.1f);
}

TEST(disc_sces02873_menus_flow) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  const Pak game(iso.read("GAME/02_FILES.PAK"));
  TitleScreens t(game);
  using R = TitleScreens::Result;
  using S = TitleScreens::Screen;
  // Runs `n` frames pressing `press` on the first, returns the first result seen.
  auto run = [&](int n, TitleScreens::Input press) {
    R seen = R::None;
    for (int i = 0; i < n; ++i) {
      const R r = t.tick(i == 0 ? press : TitleScreens::Input{});
      if (seen == R::None) seen = r;
      // (A screen returns its result before drawing that frame, as the original does.)
      if (seen == R::None && t.screen() != S::None && t.screen() != S::Done) {
        CHECK(!t.layers().empty());
        std::size_t lines = 0;
        for (const auto& l : t.layers()) lines += l.lines.size();
        CHECK(lines > 0);
      }
    }
    return seen;
  };
  TitleScreens::Input confirm, down, cancel, right;
  confirm.confirm = true;
  down.down = true;
  cancel.cancel = true;
  right.right = true;
  t.go_to(S::Main);
  CHECK(run(60, {}) == R::None);
  CHECK(t.screen() == S::Main);
  CHECK(run(5, confirm) == R::PsjModes);
  t.go_to(S::PsjModeSelect);
  run(80, {});
  CHECK(t.screen() == S::PsjModeSelect);
  run(10, down);  // to silver
  CHECK(run(5, confirm) == R::Silver);
  t.go_to(S::Done);
  run(40, {});
  CHECK(t.done());
  // Back from a course to the high scores and the options.
  t.go_to(S::PsjHiscore);
  run(60, {});
  run(10, right);
  CHECK(run(10, down) == R::None);
  CHECK(run(5, confirm) == R::HiscoreBack);
  t.go_to(S::Option);
  run(80, {});
  CHECK(t.sound_effects);
  run(5, confirm);
  CHECK(!t.sound_effects);
  t.results_course = 1;
  t.results_rank = 2;
  t.go_to(S::Results);
  CHECK(run(200, {}) == R::ResultsDone);
  // Game over: no high score, so after Vibri's reaction it is over by itself.
  t.results_rank = 0;
  t.results_score = 1234;
  t.go_to(S::Again);
  CHECK(run(450, {}) == R::ResultsDone);
  // With a high score a button goes on to the table, which ends by itself.
  t.results_rank = 3;
  t.go_to(S::Again);
  run(40, {});
  CHECK(run(200, confirm) == R::ResultsDone);
  // Cleared: without a medal Vibri comments; with one, the medal steps play out.
  t.results_rank = 0;
  t.results_score = 900;
  t.results_bonus = 100;
  t.go_to(S::Clear);
  CHECK(run(1200, {}) == R::ResultsDone);
  t.results_course = 2;
  t.results_score = 5000;
  t.results_bonus = 1000;
  t.results_form = 0;
  t.go_to(S::Clear);
  CHECK(run(3000, {}) == R::ResultsDone);
  t.go_to(S::AgainSelect);
  run(40, {});
  CHECK(run(5, confirm) == R::PlayAgain);
  t.go_to(S::AgainSelect);
  run(40, {});
  run(10, down);
  run(5, confirm);
  CHECK(run(40, {}) == R::AgainBack);
}

TEST(disc_sces02873_screen_words_read_from_the_program) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  // Only the shape is checked here: the words themselves stay on the player's disc.
  const GameText words = GameText::from_exe(iso.read("MAIN_G.EXE"));
  using L = GameText::List;
  const std::pair<L, int> lists[] = {{L::CourseNames, 5}, {L::Bonus, 3}, {L::Comments, 4},
                                     {L::MedalSteps, 10}, {L::FailComments, 5}, {L::EndWords, 2}};
  for (const auto& [list, count] : lists)
    for (int i = 0; i < count; ++i) CHECK(!words.get(list, i).empty());
  CHECK(words.get(L::Bonus, 2).find("%d") != std::string::npos);
  CHECK(words.get(L::MedalSteps, 4).find("%s") != std::string::npos);
  CHECK(words.get(L::MedalSteps, 5).find("%d") != std::string::npos);
  CHECK(words.get(L::MedalSteps, 6).find("%d") != std::string::npos);
  for (char c = '0'; c <= '9'; ++c) CHECK(words.object_for(static_cast<unsigned char>(c), false) == c - '0');
  // Anything that is not the program is refused.
  const Bytes not_it(4096, 0);
  bool refused = false;
  try {
    (void)GameText::from_exe(not_it);
  } catch (const FormatError&) {
    refused = true;
  }
  CHECK(refused);

  // The clear screen writes its words as a layer of their own.
  const Pak game(iso.read("GAME/02_FILES.PAK"));
  TitleScreens t(game);
  t.set_text(&words);
  t.results_course = 0;
  t.results_score = 900;
  t.results_bonus = 100;
  t.results_rank = 0;
  t.go_to(TitleScreens::Screen::Clear);
  bool written = false;
  for (int i = 0; i < 400 && !written; ++i) {
    t.tick({});
    written = t.layers().size() >= 2 && !t.layers().back().lines.empty();
  }
  CHECK(written);
}

TEST(fill_in_takes_a_number_and_a_name) {
  CHECK(fill_in("a %d b", 42) == "a 42 b");
  CHECK(fill_in("%s: %d", 7, "x") == "x: 7");
  CHECK(fill_in("100%% %d %d", 1) == "100% 1 %d");
}
