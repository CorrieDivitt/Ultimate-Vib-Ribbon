// Checks against the owner's real disc image. Skipped unless VIB_TEST_CUE is set,
// so CI and anyone without the game can still run the suite.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
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
#include "vib/title_intro.h"
#include "vib/kiosk_movie.h"
#include "vib/settings_panel.h"
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

TEST(disc_sces02873_title_intro) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  const Pak title_pak(iso.read("TITLE/FILES.PAK"));
  CHECK(TitleIntro::game_frames(0) == 0);
  CHECK(TitleIntro::game_frames(821) == 1359);  // the title, about 27 s in
  TitleIntro t(title_pak);
  auto lines = [&] {
    std::size_t n = 0;
    for (const auto& l : t.layers()) n += l.lines.size();
    return n;
  };
  // The intro starts with its first sound and lines.
  t.tick(false);
  auto sounds = t.take_sounds();
  CHECK(sounds.size() == 1 && sounds[0].number == 0x110);
  CHECK(!t.intro_over());
  // Played through: lines from the first few frames on (it opens on nothing), the
  // second and third sounds at their times, then the title.
  int heard = 0;
  for (int i = 1; i < 1400; ++i) {
    t.tick(false);
    heard += int(t.take_sounds().size());
    if (i >= 10) CHECK(lines() > 0);
  }
  CHECK(heard == 2);
  CHECK(t.intro_over());
  t.leave();
  for (int i = 0; i < 25; ++i) t.tick(false);
  CHECK(t.over());
  // A button during the intro jumps to the title.
  t.start(false);
  for (int i = 0; i < 100; ++i) t.tick(false);
  (void)t.take_sounds();
  t.tick(true);
  sounds = t.take_sounds();
  CHECK(sounds.size() == 1 && sounds[0].number == 0x111);
  t.tick(true);  // no longer skippable
  CHECK(t.intro_over());
  // Coming back from a course: straight to the title, its lines gathering in.
  t.start(true);
  t.tick(false);
  CHECK(t.intro_over());
}

TEST(disc_sces02873_audio_cd_tracks_laid_out_in_the_medley) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  const Pak game(iso.read("GAME/02_FILES.PAK"));
  const CourseScript script = parse_course_script(game.view("SCRIPT/SYSTEM3.FSL"));
  // A made-up CD of five tracks, 3 to 5 minutes.
  const std::vector<std::int32_t> lengths = {180 * 22050, 240 * 22050, 200 * 22050, 300 * 22050, 260 * 22050};
  CourseGenerator a(script), b(script);
  a.prepare_disc(lengths, disc_key(150000, 5));
  b.prepare_disc(lengths, disc_key(150000, 5));
  CHECK(a.track_offsets() == b.track_offsets());  // the same disc, the same layout
  // Each track has its own span of the 74-minute script, none overlapping.
  std::vector<std::pair<std::int32_t, std::int32_t>> spans;
  for (std::size_t t = 0; t < lengths.size(); ++t) spans.push_back({a.track_offsets()[t], a.track_offsets()[t] + lengths[t]});
  std::sort(spans.begin(), spans.end());
  for (std::size_t i = 0; i < spans.size(); ++i) {
    CHECK(spans[i].first >= 0 && spans[i].second <= 74 * 60 * 22050 + 22050);
    if (i) CHECK(spans[i - 1].second <= spans[i].first);
  }
  b.prepare_disc(lengths, disc_key(150001, 5));
  CHECK(a.track_offsets() != b.track_offsets());  // another disc, another layout
  // A track's course: parts come, and a second run gives the same ones.
  const auto blocks = make_blocks(disc.read_audio_track(2));
  auto run = [&](CourseGenerator& g) {
    g.start_track(2, 0);
    for (std::size_t i = 0; i < blocks.size(); ++i) g.process(std::int32_t(i * kBlockSize), blocks[i]);
    std::size_t parts = 0;
    for (const auto& e : g.events()) parts += e.type == CourseEvent::Type::Part;
    return parts;
  };
  CourseGenerator c(script), d(script);
  c.prepare_disc(lengths, disc_key(150000, 5));
  d.prepare_disc(lengths, disc_key(150000, 5));
  const std::size_t n = run(c);
  CHECK(n > 20 && n == run(d));
}

TEST(disc_sces02873_title_language_and_mode_menus) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  const Pak title_pak(iso.read("TITLE/FILES.PAK"));
  for (int lang = 0; lang < 6; ++lang) TitleIntro check(title_pak, lang);  // each language's menu loads
  TitleIntro t(title_pak, 1);
  using C = TitleIntro::Choice;
  using M = TitleIntro::Menu;
  TitleIntro::Input none, right, down, confirm, cancel;
  right.right = true;
  down.down = true;
  confirm.confirm = true;
  cancel.cancel = true;
  t.start(true);
  for (int i = 0; i < 5; ++i) t.tick(none);
  t.show(M::Language);
  for (int i = 0; i < 3; ++i) t.tick(none);
  t.tick(right);  // English to Spanish, with the cursor sound
  auto sounds = t.take_sounds();
  CHECK(!sounds.empty() && sounds.back().number == 0x06 && sounds.back().voice == 5);
  t.tick(down);   // Spanish to Italian (below it)
  t.tick(confirm);
  CHECK(t.choice() == C::Italian);
  t.set_language_cursor(1);
  t.tick(confirm);
  CHECK(t.choice() == C::English);
  // To the mode menu: the language menu zooms away and the mode menu in.
  t.show(M::Mode);
  int frames = 0;
  while (t.choice() == C::None && frames < 200) {
    t.tick(frames % 2 ? none : confirm);
    ++frames;
  }
  CHECK(t.choice() == C::Play);
  t.show(M::Mode);
  for (int i = 0; i < 100; ++i) t.tick(none);
  t.tick(cancel);  // the cursor to "back"
  t.tick(confirm);
  CHECK(t.choice() == C::Back);
  // Play: the menu flies apart while the confirm sound lasts, then it has started.
  t.show(M::Start);
  TitleIntro::Input playing;
  playing.confirm_sound_playing = true;
  for (int i = 0; i < 10; ++i) t.tick(playing);
  CHECK(t.choice() == C::None);
  t.tick(none);
  CHECK(t.choice() == C::Started);
}

TEST(disc_sces02873_every_language_loads) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  const GameText words = GameText::from_exe(iso.read("MAIN_G.EXE"));
  for (int language = 0; language < 5; ++language) {
    const Pak pak(iso.read("GAME/" + language_prefix(language) + "FILES.PAK"));
    TitleScreens t(pak, language);
    t.set_text(&words, language);
    Hud hud(pak, language);
    Scene scene(pak);
    t.go_to(TitleScreens::Screen::Main);
    for (int i = 0; i < 40; ++i) t.tick({});
    std::size_t lines = 0;
    for (const auto& l : t.layers()) lines += l.lines.size();
    CHECK(lines > 0);
    CHECK(!words.get(GameText::List::Comments, 0, language).empty());
  }
}

TEST(disc_sces02873_language_files_named_as_the_original) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  // German 04, English 02, Spanish 08, French 10, Italian 20, Japanese 01.
  CHECK(language_prefix(0) == "04_" && language_prefix(1) == "02_" && language_prefix(2) == "08_");
  CHECK(language_prefix(3) == "10_" && language_prefix(4) == "20_" && language_prefix(5) == "01_");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  for (int language = 0; language < 6; ++language) {
    const Pak pak(iso.read("GAME/" + language_prefix(language) + "FILES.PAK"));
    // The font shared by every language but Japanese, which has its own.
    CHECK(localized(pak, "FONT/FONT.TMD", language) == (language == 5 ? "FONT/01_FONT.TMD" : "FONT/FE_FONT.TMD"));
    CHECK(localized(pak, "FONT/MARK.TMD", language) == "FONT/MARK.TMD");  // one for all
  }
  const Pak title(iso.read("TITLE/FILES.PAK"));
  CHECK(localized(title, "MENU/MODE/MODE.TMD", 0) == "MENU/MODE/04_MODE.TMD");
  CHECK(localized(title, "MENU/MODE/MODE.TMD", 4) == "MENU/MODE/20_MODE.TMD");
}

TEST(disc_sces02873_line_ends_know_their_vertex_for_fresh_jitter) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  const Pak game(iso.read("GAME/02_FILES.PAK"));
  TitleScreens t(game);
  t.go_to(TitleScreens::Screen::Main);
  for (int i = 0; i < 60; ++i) t.tick({});
  std::size_t lines = 0, shaking = 0;
  for (const auto& layer : t.layers()) {
    std::map<std::uint32_t, Vec3f> place;  // a vertex is at one place within a frame
    for (const SceneLine& l : layer.lines) {
      ++lines;
      CHECK(l.vertex_b != 0);
      shaking += l.shake_b > 0;
      for (const auto& [v, p] : {std::pair{l.vertex_a, l.a}, std::pair{l.vertex_b, l.b}}) {
        if (v == 0) continue;
        const auto it = place.find(v);
        if (it == place.end()) place[v] = p;
        else CHECK(it->second.x == p.x && it->second.y == p.y && it->second.z == p.z);
      }
    }
  }
  CHECK(lines > 100 && shaking > 0);
}

TEST(disc_sces02873_demo_sign_shows_in_the_demo) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  for (int language = 0; language < 5; ++language) {
    const Pak pak(iso.read("GAME/" + language_prefix(language) + "FILES.PAK"));
    Hud hud(pak, language);
    std::vector<Hud::Layer> plain, demo;
    Hud::Frame f;
    f.seconds = 0.02;
    hud.draw(f, plain);
    f.demo = true;
    hud.draw(f, demo);
    CHECK(demo.size() == plain.size() + 1 && !demo.back().lines.empty());
  }
}

TEST(disc_sces02873_kiosk_movie_plays_and_ends) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  for (int language = 0; language < 5; ++language) {
    const Pak pak(iso.read("KIOSK/" + language_prefix(language) + "FILES.PAK"));
    KioskMovie k(pak, language);
    KioskMovie::Input in;
    k.tick(in);
    auto sounds = k.take_sounds();
    CHECK(sounds.size() == 1 && sounds[0].number == 0x100 && sounds[0].voice == 0);  // the music
    in.music_playing = true;
    std::size_t most = 0;
    int ticks = 1;
    for (; ticks < 6000 && !k.finished(); ++ticks) {
      if (ticks == 5000) in.music_playing = false;  // (the music has ended by then)
      k.tick(in);
      std::size_t n = 0;
      for (const auto& l : k.layers()) n += l.lines.size();
      most = std::max(most, n);
    }
    CHECK(most > 100);
    CHECK(k.finished() && ticks > TitleIntro::game_frames(3025));  // it played to its end (about 100 s)
    // A button ends it once the confirm sound has sounded.
    k.start();
    KioskMovie::Input button;
    button.confirm = true;
    k.tick(button);
    sounds = k.take_sounds();
    CHECK(!sounds.empty() && sounds.back().number == 0x00 && sounds.back().voice == 1);
    KioskMovie::Input sounding;
    sounding.confirm_sound_playing = true;
    k.tick(sounding);
    CHECK(!k.finished());
    k.tick({});
    CHECK(k.finished());
  }
}

TEST(disc_sces02873_settings_panel) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  const GameText words = GameText::from_exe(iso.read("MAIN_G.EXE"));
  using I = SettingsPanel::Item;
  using A = SettingsPanel::Action;
  const std::vector<I> all = {I::Continue, I::WobbleSize, I::WobbleHold, I::Smooth, I::LineWidth, I::Zoom, I::ViewAngle,
                              I::SeeAhead, I::SeeBehind, I::Recentre, I::SoundEffects, I::Vibration, I::SoundTiming, I::QuitSong,
                              I::ResumeStyle};
  for (int language = 0; language < 5; ++language) {
    const Pak game(iso.read("GAME/" + language_prefix(language) + "FILES.PAK"));
    SettingsPanel p(game, language, &words);
    p.set_items(all);
    Options o;
    for (const I item : all) {
      const std::string l = p.label(item);
      CHECK(!l.empty() && l.find('?') == std::string::npos);  // every letter is in the font
    }
    for (const bool countdown : {false, true}) {
      o.resume_countdown = countdown;
      CHECK(p.value(I::ResumeStyle, o, true).find('?') == std::string::npos);
    }
    for (const bool vr : {false, true}) {
      const Hud::Layer layer = p.draw(o, vr, 1234);
      CHECK(layer.lines.size() > 100);
    }
  }
  const Pak game(iso.read("GAME/02_FILES.PAK"));
  SettingsPanel p(game, 1, &words);
  p.set_items(all);
  Options o;
  SettingsPanel::Input up, down, left, right, confirm, cancel, none;
  up.up = down.down = left.left = right.right = confirm.confirm = cancel.cancel = true;
  auto go_to = [&](I item) {
    p.home();
    while (p.current() != item) p.tick(down, 0.02, o, true);
  };
  CHECK(p.tick(confirm, 0.02, o, true) == A::Close);  // Continue
  CHECK(p.tick(up, 0.02, o, true) == A::Moved && p.current() == I::ResumeStyle);  // wraps in play
  CHECK(!o.resume_countdown && p.tick(confirm, 0.02, o, true) == A::Changed && o.resume_countdown);
  CHECK(p.value(I::ResumeStyle, o, true) == "Countdown");
  p.tick(up, 0.02, o, true);
  CHECK(p.tick(confirm, 0.02, o, true) == A::QuitSong);
  CHECK(!p.draw_big("3", 1).lines.empty() && !p.draw_hint(1).lines.empty());
  go_to(I::WobbleSize);
  CHECK(p.tick(right, 0.02, o, true) == A::Changed && std::abs(o.vr_jitter_scale - 1.1) < 1e-9);
  CHECK(o.jitter_scale == 1.0);  // the monitor's own is left alone
  for (int i = 0; i < 30; ++i) p.tick(right, 0.02, o, true);
  CHECK(o.vr_jitter_scale == 2.0);
  for (int i = 0; i < 30; ++i) p.tick(left, 0.02, o, true);
  CHECK(o.vr_jitter_scale == 0.0);
  CHECK(p.tick(left, 0.02, o, true) == A::None);
  // Held: after 0.4 s it repeats.
  go_to(I::SeeAhead);
  p.tick(right, 0.02, o, true);
  SettingsPanel::Input held;
  held.held_right = true;
  for (int i = 0; i < 50; ++i) p.tick(held, 0.02, o, true);  // 1 s
  CHECK(o.vr_ahead_seconds > 34 && o.vr_ahead_seconds < 60);
  go_to(I::ViewAngle);
  o.vr_angle = 180;
  p.tick(right, 0.02, o, true);
  CHECK(std::abs(o.vr_angle + 165) < 1e-9);
  go_to(I::LineWidth);
  p.tick(right, 0.02, o, true);
  CHECK(std::abs(o.vr_line_width - 0.15 * 1.25) < 1e-9);
  go_to(I::SoundEffects);
  CHECK(p.tick(confirm, 0.02, o, true) == A::Changed && !o.sound_effects);
  go_to(I::Recentre);
  CHECK(p.tick(confirm, 0.02, o, true) == A::Recentre);
  CHECK(p.tick(cancel, 0.02, o, true) == A::Close);
  // Below the Options screen: up from the first leaves it.
  p.set_wraps(false);
  p.home();
  CHECK(p.tick(up, 0.02, o, true) == A::LeaveTop);
  // A long list scrolls: only `rows` rows are drawn.
  p.set_place(0.5f, 3, 0.65f);
  const std::size_t three = p.draw(o, true, 1).lines.size();
  p.set_place(0.5f, 14);
  CHECK(three > 0 && three < p.draw(o, true, 1).lines.size());
}
