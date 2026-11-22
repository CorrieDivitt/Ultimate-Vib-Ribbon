// Checks against the owner's real disc image. Skipped unless VIB_TEST_CUE is set,
// so CI and anyone without the game can still run the suite.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "test.h"
#include "vib/colours.h"
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
#include "vib/perf_overlay.h"
#include "vib/file_picker.h"
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
  // The music's loudness for the sound effects: some quieter and some louder stretches,
  // none silent once the song is going, never over 100.
  {
    std::vector<std::uint8_t> level = loudness_levels(blocks);
    std::vector<std::uint8_t> sorted(level.begin() + 400, level.end() - 400);
    std::sort(sorted.begin(), sorted.end());
    const int low = sorted[sorted.size() / 20], mid = sorted[sorted.size() / 2], high = sorted[sorted.size() * 19 / 20];
    std::printf("    loudness of the first song: 5%% %d, median %d, 95%% %d\n", low, mid, high);
    CHECK(low > 0 && low < mid && mid < high && high <= 100);
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
    // The evolution meter comes last, a flat ring in the road's own plane (a headset moves it nearer).
    CHECK(scene.evo_first() <= lines.size());
    for (std::size_t k = scene.evo_first(); k < lines.size(); ++k) CHECK(std::abs(lines[k].a.x) < 50 && std::abs(lines[k].b.x) < 50);
  }
  CHECK(moved && scene.move_done(0.5));
  // Every line says what it belongs to, for custom colours: the road between pieces,
  // each piece by its road (0-5 here), Vibri by her form, then the evolution ring.
  {
    std::array<int, 17> count{};
    for (const SceneLine& l : lines) count[std::min<std::size_t>(l.part, 16)]++;
    CHECK(count[line_part::kNone] == 0);
    CHECK(count[line_part::kRoad] >= 7);  // (a join to each of the 6 pieces and to the far point ahead)
    for (int k = 0; k < 6; ++k) CHECK(count[std::size_t(line_part::kObstacle + k)] > 0);
    CHECK(count[std::size_t(line_part::kObstacle + 6)] == 0);
    int vibri_parts = 0;
    for (int form = 0; form < 4; ++form) vibri_parts += count[std::size_t(line_part::kVibri + form)] > 0;
    CHECK(vibri_parts == 1);  // (one form at a time)
    CHECK(count[line_part::kEvo] == int(lines.size() - scene.evo_first()));
    // Tinted: a white line takes the colour, a dark one stays dark, the ring keeps its own.
    std::array<std::uint8_t, kColourSlots> choice{};
    choice[kColourObstacle + 2] = 1;  // the loop: red
    CHECK((tinted({255, 255, 255}, std::uint8_t(line_part::kObstacle + 2), choice).g == kLinePalette[1].g));
    CHECK((tinted({0, 0, 2}, std::uint8_t(line_part::kObstacle + 2), choice).r == 0));
    CHECK((tinted({255, 255, 255}, line_part::kRoad, choice).g == 255));  // (the road: still white)
    CHECK((tinted({255, 255, 255}, line_part::kEvo, choice).g == 255));
    choice[kColourBackground] = 1;
    CHECK((background_colour(choice).b == kBackgroundPalette[1].b));
    // On a light background the lines with no colour of their own (the settings' rows:
    // white chosen, grey others) are inverted, so neither matches it; dark ones keep theirs.
    CHECK(!light_background(choice) && (shown_colour({255, 255, 255}, line_part::kNone, choice).r == 255));
    choice[kColourBackground] = 5;  // white
    CHECK(light_background(choice));
    const Rgb chosen = shown_colour({255, 255, 255}, line_part::kNone, choice), other = shown_colour({110, 110, 110}, line_part::kNone, choice);
    const Rgb back = background_colour(choice);
    CHECK(chosen.r != back.r && other.r != back.r && chosen.r != other.r);
    CHECK((shown_colour({255, 255, 255}, std::uint8_t(line_part::kObstacle + 2), choice).g == kLinePalette[1].g));  // (custom: as chosen)
    // Menu items: the chosen colour (rows keep their brightness), never lost in the
    // background: every menu colour on every background stays clearly apart from it.
    {
      std::array<std::uint8_t, kColourSlots> m{};
      CHECK((shown_colour({255, 255, 255}, line_part::kMenu, m).g == 255));            // (white on black: as it was)
      m[kColourMenu] = 1;                                                              // red
      CHECK((shown_colour({255, 255, 255}, line_part::kMenu, m).g == kLinePalette[1].g));
      CHECK((shown_colour({110, 110, 110}, line_part::kMenu, m).r < shown_colour({255, 255, 255}, line_part::kMenu, m).r));  // (the other rows dimmer)
      for (std::uint8_t mc = 0; mc < kLineColours; ++mc)
        for (std::uint8_t bc = 0; bc < kBackgroundColours; ++bc) {
          m[kColourMenu] = mc, m[kColourBackground] = bc;
          const Rgb b = background_colour(m), c = shown_colour({255, 255, 255}, line_part::kMenu, m);
          const int dr = int(c.r) - b.r, dg = int(c.g) - b.g, db = int(c.b) - b.b;
          CHECK(dr * dr + dg * dg + db * db >= 40 * 40);  // (never the background's own colour)
          // Black is black on every background but black (owner).
          if (mc == 8 && bc != 0) CHECK((c.r == 0 && c.g == 0 && c.b == 0));
        }
    }
    for (std::uint8_t bgc = 0; bgc < kBackgroundColours; ++bgc) {  // every background: both rows differ from it
      choice[kColourBackground] = bgc;
      const Rgb b = background_colour(choice), c1 = shown_colour({255, 255, 255}, 0, choice), c2 = shown_colour({110, 110, 110}, 0, choice);
      CHECK((std::abs(int(c1.r) - b.r) + std::abs(int(c1.g) - b.g) + std::abs(int(c1.b) - b.b)) > 120);
      CHECK((std::abs(int(c2.r) - b.r) + std::abs(int(c2.g) - b.g) + std::abs(int(c2.b) - b.b)) > 120);
    }
  }
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
  // The own-CD side's mode select: six items, the second is all tracks, back leaves.
  t.go_to(S::UnknownModeSelect);
  run(80, {});
  CHECK(t.screen() == S::UnknownModeSelect);
  run(10, down);
  CHECK(run(5, confirm) == R::AllTracks);
  t.go_to(S::UnknownModeSelect);
  run(80, {});
  run(10, down), run(10, down);  // (not from the main menu: the cursor stays on all tracks)
  CHECK(run(5, confirm) == R::InOrder);
  t.go_to(S::UnknownModeSelect);
  run(80, {});
  run(10, cancel);  // cancel jumps to back
  CHECK(run(80, confirm) == R::UnknownBack);
  // UNKNOWN HISCORE: five courses round with left and right (speed ones as times);
  // down then confirm leaves.
  for (std::size_t c = 0; c < t.high_scores.size(); ++c) t.high_scores[c] = {300, 200, 100};
  t.go_to(S::UnknownHiscore);
  run(80, {});
  CHECK(t.screen() == S::UnknownHiscore);
  for (int k = 0; k < 6; ++k) CHECK(run(30, right) == R::None);
  run(10, down);
  CHECK(run(80, confirm) == R::HiscoreBack);
  // Own-CD results: one track and all tracks clear as the game disc's courses do and
  // end by themselves; "again?" after a loss names the course; a speed course's
  // cleared time is followed by its table.
  TitleScreens::Input none;
  for (const int course : {3, 4}) {
    t.results_course = course;
    t.results_speed = 0;
    t.results_score = 7000;
    t.results_bonus = 700;
    t.results_form = 2;
    t.results_rank = 1;
    t.go_to(S::Clear);
    R seen = R::None;
    for (int i = 0; i < 4000 && seen == R::None; ++i) seen = t.tick(none);
    CHECK(seen == R::ResultsDone);
  }
  t.results_course = 6;
  t.results_speed = 2;
  t.results_time_up = false;
  t.results_score = 70;
  t.results_rank = 2;
  t.go_to(S::Clear);
  {
    R seen = R::None;
    for (int i = 0; i < 4000 && seen == R::None; ++i) seen = t.tick(none);
    CHECK(seen == R::ResultsDone);
  }
  t.results_speed = 0;
  t.go_to(S::AgainSelect);
  run(80, {});
  CHECK(t.screen() == S::AgainSelect);
  // CD CHANGE: starts on "this CD"; up is "change the CD"; back leaves with its clip.
  TitleScreens::Input up, left;
  up.up = true;
  left.left = true;
  t.go_to(S::CdChange);
  run(80, {});
  CHECK(t.screen() == S::CdChange);
  CHECK(run(5, confirm) == R::CdReady);
  t.go_to(S::CdChange);
  run(80, {});
  run(10, up);
  CHECK(run(5, confirm) == R::ChangeCd);
  // TRACK SELECT: right and left step the number round the CD's tracks; down is back.
  t.num_tracks = 3;
  t.go_to(S::TrackSelect);
  run(80, {});
  CHECK(t.screen() == S::TrackSelect && t.track == 1);
  run(10, right);
  CHECK(t.track == 2);
  run(10, left), run(10, left);
  CHECK(t.track == 3);  // (wrapped round)
  CHECK(run(5, confirm) == R::TrackChosen && t.track == 3);
  t.go_to(S::TrackSelect);
  run(80, {});
  run(10, down);
  CHECK(run(5, confirm) == R::TrackBack);
  t.go_to(S::Main);
  run(60, {});
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

  // A speed course's time: the words for it are read (two parts outside Japanese),
  // its clear screen writes them and ends by itself; time up does too.
  CHECK(!words.get(GameText::List::SpeedTime, 0).empty());
  CHECK(!words.get(GameText::List::SpeedTime, 1).empty());
  CHECK(words.get(GameText::List::SpeedTime, 2).empty());
  CHECK(!words.get(GameText::List::SpeedTime, 2, GameText::kJapanese).empty());
  for (const bool up : {false, true}) {
    t.results_speed = 2;
    t.results_time_up = up;
    t.results_score = 61;
    t.go_to(up ? TitleScreens::Screen::Again : TitleScreens::Screen::Clear);
    bool wrote = false, done = false;
    for (int i = 0; i < 3000 && !done; ++i) {
      done = t.tick({}) == TitleScreens::Result::ResultsDone;
      wrote = wrote || (t.layers().size() >= 2 && !t.layers().back().lines.empty());
    }
    CHECK(wrote);
    CHECK(done);
  }
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
                              I::SeeAhead, I::SeeBehind, I::ScoreNumbers, I::Recentre, I::SoundEffects, I::Vibration, I::SoundTiming, I::QuitSong,
                              I::ResumeStyle, I::Performance};
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
    for (int level = 0; level < 3; ++level) {
      o.perf_overlay = level;
      CHECK(!p.value(I::Performance, o, true).empty() && p.value(I::Performance, o, true).find('?') == std::string::npos);
    }
    o.perf_overlay = 0;
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
  CHECK(p.tick(up, 0.02, o, true) == A::Moved && p.current() == I::Performance);  // wraps in play
  // The performance overlay: confirm or right steps it off, on, detailed, round again; left goes back.
  CHECK(o.perf_overlay == 0 && p.tick(confirm, 0.02, o, true) == A::Changed && o.perf_overlay == 1 && p.value(I::Performance, o, true) == "On");
  CHECK(p.tick(right, 0.02, o, true) == A::Changed && o.perf_overlay == 2 && p.value(I::Performance, o, true) == "Detailed");
  CHECK(p.tick(right, 0.02, o, true) == A::Changed && o.perf_overlay == 0);
  CHECK(p.tick(left, 0.02, o, true) == A::Changed && o.perf_overlay == 2);
  o.perf_overlay = 0;
  p.tick(up, 0.02, o, true);
  CHECK(p.current() == I::ResumeStyle);
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
  // Past a minute in longer steps, then the whole song (the line runs to the horizon).
  for (int i = 0; i < 40; ++i) p.tick(right, 0.02, o, true);
  CHECK(o.vr_ahead_seconds == kWholeSongSeconds && p.value(I::SeeAhead, o, true) == "Whole song");
  CHECK(p.tick(right, 0.02, o, true) == A::None);
  CHECK(p.tick(left, 0.02, o, true) == A::Changed && o.vr_ahead_seconds == 300.0 && p.value(I::SeeAhead, o, true) == "300 s");
  for (int i = 0; i < 7; ++i) p.tick(left, 0.02, o, true);
  CHECK(o.vr_ahead_seconds == 60.0);
  go_to(I::SeeBehind);
  for (int i = 0; i < 60; ++i) p.tick(right, 0.02, o, true);
  CHECK(o.vr_behind_seconds == kWholeSongSeconds);
  for (int i = 0; i < 80; ++i) p.tick(left, 0.02, o, true);
  CHECK(o.vr_behind_seconds == 2.0);  // (no lower)
  o.vr_ahead_seconds = o.vr_behind_seconds = 30.0;
  // Held down, the list scrolls (after 0.35 s, about 14 rows a second) and stops at the end.
  {
    p.home();
    SettingsPanel::Input held_down;
    held_down.held_down = true;
    p.tick(down, 0.02, o, true);
    for (int k = 0; k < 100; ++k) p.tick(held_down, 0.02, o, true);  // 2 s
    CHECK(p.current() == all.back());  // (at the end, not wrapped)
    p.home();
  }
  // Our settings show the original game's value after the name; the original's options and the actions do not.
  CHECK(p.label(I::SeeAhead, true) == "See ahead (8 s)" && p.label(I::SeeBehind, true) == "See behind (2 s)");
  CHECK(p.label(I::LineWidth, true) == "Line width (0.05)" && p.label(I::LineWidth, false) == "Line width (1.000)");
  CHECK(p.label(I::WobbleHold, true) == "Wobble hold (1)" && p.label(I::WobbleSize, true) == "Wobble size (1.0)");
  CHECK(p.label(I::Smooth) == "Smooth motion (Off)" && p.label(I::SoundTiming) == "Sound timing (0 ms)");
  CHECK(p.label(I::Performance) == "Performance" && p.label(I::ResumeStyle) == "Resume");  // (owner: none for these)
  CHECK(p.label(I::ScoreNumbers) == "Score in numbers");  // (owner: none)
  CHECK(p.label(I::EffectsVolume) == "Effects volume (10)" && p.value(I::EffectsVolume, o, true) == "10");
  CHECK(p.label(I::FixedCourses) == "Consistent courses (Off)" && p.value(I::FixedCourses, o, true) == "Off");  // the original varies
  CHECK(p.label(I::ScrollSpeed) == "Scroll speed (1x)" && p.value(I::ScrollSpeed, o, true) == "1x");
  {  // 0.5x to 4x in quarters, stopping at the ends
    Options so;
    so.scroll_speed = 9.75;
    // FC celebration: off by default, no bracket; the word pops in letter by letter, rays
    // burst out, and it fades by the end.
    CHECK(p.label(I::FcCelebration) == "FC celebration" && p.value(I::FcCelebration, Options{}, true) == "Off");
    CHECK(to_font_text("Full Combo!") == "Full Combo!");  // (every character is in the font)
    auto width_of = [](const std::vector<SceneLine>& ls) {
      float lo = 1e9f, hi = -1e9f;
      for (const SceneLine& l : ls)
        if (l.colour.r > 0) lo = std::min({lo, l.a.x, l.b.x}), hi = std::max({hi, l.a.x, l.b.x});
      return std::pair<float, float>{lo, hi};
    };
    CHECK(p.full_combo(0.0, 1).empty());
    const auto early = p.full_combo(0.2, 1), settled = p.full_combo(2.05, 1), burst = p.full_combo(0.8, 1);
    CHECK(!early.empty() && early.size() < settled.size());  // letters still coming
    const auto [fc_lo, fc_hi] = width_of(settled);
    CHECK(fc_hi - fc_lo > 1400.0f && fc_hi - fc_lo < 2200.0f && std::abs(fc_lo + fc_hi) < 200.0f);  // the whole word, centred
    bool rays = false;
    for (const SceneLine& l : burst) rays = rays || std::abs(l.b.x) > 1000.0f;
    CHECK(rays);  // (the first burst is out past the word at 0.8 s)
    bool dim = true;
    for (const SceneLine& l : p.full_combo(SettingsPanel::kFullComboSeconds - 0.01, 1)) dim = dim && l.colour.r < 10;
    CHECK(dim);  // faded out at the end
    CHECK(!p.draw_full_combo(1.0, 2).lines.empty());
    // The full course combo: bigger and longer. Nothing at first; "Combo!" arrives at 1 s,
    // with a shockwave ring; at 2.5 s both lines are in, wider than a song's word, and
    // sparkles twinkle; faded by the end.
    CHECK(p.full_course_combo(0.0, 1).empty());
    const auto before_slam = p.full_course_combo(0.95, 1), after_slam = p.full_course_combo(1.35, 1);
    float deepest_before = -1e9f, deepest_after = -1e9f, ring = 0;
    // (Letters' lines carry vertex numbers; the rays, ring and sparkles none.)
    for (const SceneLine& l : before_slam)
      if (l.vertex_a) deepest_before = std::max(deepest_before, std::max(l.a.y, l.b.y));
    for (const SceneLine& l : after_slam) {
      if (l.vertex_a) deepest_after = std::max(deepest_after, std::max(l.a.y, l.b.y));
      ring = std::max(ring, std::abs(l.a.x));
    }
    CHECK(deepest_before < 100.0f && deepest_after > 400.0f);  // "Combo!" is below "Full Course"
    CHECK(ring > 1500.0f);                                     // the ring and the rays are out
    const auto all_in = p.full_course_combo(3.0, 1);
    const auto [cc_lo, cc_hi] = width_of(all_in);
    CHECK(cc_hi - cc_lo > (fc_hi - fc_lo));                     // bigger than a song's
    CHECK(all_in.size() > settled.size() + 40);                 // ("Combo!" and sparkles too)
    bool course_dim = true;
    for (const SceneLine& l : p.full_course_combo(SettingsPanel::kFullCourseComboSeconds - 0.01, 1)) course_dim = course_dim && l.colour.r < 10;
    CHECK(course_dim);
    CHECK(!p.draw_full_combo(2.0, 2, true).lines.empty());
    // Groups: confirm opens one (its rows, then Back), B or Back returns to the row it
    // was opened from; groups nest; a group's row shows ">".
    {
      Options go;
      p.set_group(I::Modifiers, {I::ScrollSpeed, I::CustomColours});
      p.set_group(I::CustomColours, {});
      p.set_items({I::Continue, I::Visual, I::Modifiers, I::QuitSong});
      SettingsPanel::Input dn, ok, back;
      dn.down = true, ok.confirm = true, back.cancel = true;
      p.tick(dn, 0.0, go, true);
      p.tick(dn, 0.0, go, true);
      CHECK(p.current() == I::Modifiers && p.value(I::Modifiers, go, true) == ">");
      CHECK(p.tick(ok, 0.0, go, true) == A::Moved && p.depth() == 1 && p.current() == I::ScrollSpeed && p.open_group() == I::Modifiers);
      p.tick(dn, 0.0, go, true);
      CHECK(p.tick(ok, 0.0, go, true) == A::Moved && p.depth() == 2 && p.current() == I::Back);  // (an empty group: Back alone)
      CHECK(!p.draw(go, true, 1).lines.empty());                                                  // (its title)
      CHECK(p.tick(ok, 0.0, go, true) == A::Moved && p.depth() == 1 && p.current() == I::CustomColours);
      CHECK(p.tick(back, 0.0, go, true) == A::Moved && p.depth() == 0 && p.current() == I::Modifiers);
      CHECK(p.tick(back, 0.0, go, true) == A::Close);  // (at the top, B closes)
      p.set_items({I::Modifiers});
      CHECK(p.depth() == 0);
    }
    // Calibration: a group; its Sound calibration row starts the click track (an action).
    {
      Options ca;
      p.set_items({I::SoundCalibration});
      SettingsPanel::Input ok3;
      ok3.confirm = true;
      CHECK(p.label(I::Calibration) == "Calibration" && p.value(I::Calibration, ca, true) == ">");
      CHECK(p.label(I::SoundCalibration) == "Sound calibration" && p.tick(ok3, 0.0, ca, true) == A::Calibrate);
      p.set_items({I::PictureCalibration, I::PictureTiming});
      CHECK(p.label(I::PictureCalibration) == "Picture calibration" && p.tick(ok3, 0.0, ca, true) == A::CalibratePicture);
      CHECK(p.label(I::PictureTiming) == "Picture timing (0 ms)");
      ca.audio_offset_ms = 70, ca.picture_offset_ms = -15;  // (the Calibration rows show what they set)
      CHECK(p.value(I::SoundCalibration, ca, true) == "70 ms" && p.value(I::PictureCalibration, ca, true) == "-15 ms");
      ca.audio_offset_ms = 0, ca.picture_offset_ms = 0;
      // Refresh rate: Auto, then each rate the headset offers, round either way.
      {
        Options rr;
        CHECK(p.label(I::RefreshRate) == "Refresh rate" && p.value(I::RefreshRate, rr, true) == "Auto");
        p.set_items({I::RefreshRate});
        SettingsPanel::Input next_rate;
        next_rate.right = true;
        CHECK(p.tick(next_rate, 0.0, rr, true) == A::None);  // (no rates offered: nothing to choose)
        p.set_refresh_rates({72, 90, 120});
        for (const int want : {72, 90, 120, 0}) CHECK(p.tick(next_rate, 1.0, rr, true) == A::Changed && rr.vr_refresh_hz == want);
        CHECK(p.value(I::RefreshRate, rr, true) == "Auto");
        rr.vr_refresh_hz = 90;
        CHECK(p.value(I::RefreshRate, rr, true) == "90 Hz");
        p.set_refresh_rates({});
      }
      // Glow: off (the original) to strong, five levels, stopping at the ends.
      CHECK(p.label(I::Glow) == "Glow (Off)" && p.value(I::Glow, ca, true) == "Off");
      p.set_items({I::Glow});
      SettingsPanel::Input brighter;
      brighter.right = true;
      for (const char* want : {"Faint", "Soft", "Medium", "Strong"}) CHECK(p.tick(brighter, 0.0, ca, true) == A::Changed && p.value(I::Glow, ca, true) == want);
      CHECK(p.tick(brighter, 0.0, ca, true) == A::None && ca.glow == 4);
      ca.glow = 0;
      p.set_items({I::PictureCalibration, I::PictureTiming});
      SettingsPanel::Input dn4, rt4;
      dn4.down = true, rt4.right = true;
      p.tick(dn4, 0.0, ca, true);
      ca.picture_offset_ms = 295;
      CHECK(p.tick(rt4, 0.0, ca, true) == A::Changed && ca.picture_offset_ms == 300.0);
      CHECK(p.tick(rt4, 0.0, ca, true) == A::None && p.value(I::PictureTiming, ca, true) == "300 ms");
    }
    // Custom colors: the original's white (black behind) in brackets; left and right go
    // round the palette; Reset puts every colour back.
    {
      Options co;
      CHECK(p.label(I::ColourRoad) == "Road line (White)" && p.label(I::ColourBackground) == "Background (Black)");
      CHECK(p.label(I::ColourPitWave) == "Pit + Wave (White)" && p.label(I::ColourFrog) == "Vibri frog (White)");
      CHECK(p.label(I::ColourMenu) == "Menu (White)" && p.value(I::ColourMenu, co, true) == "White");
      p.set_items({I::ColourLoop, I::ColourBackground, I::ResetColours});
      SettingsPanel::Input lt, rt, dn2, ok2;
      lt.left = true, rt.right = true, dn2.down = true, ok2.confirm = true;
      CHECK(p.tick(lt, 0.0, co, true) == A::Changed && p.value(I::ColourLoop, co, true) == "Black");  // (round from white)
      CHECK(p.tick(rt, 0.0, co, true) == A::Changed && p.value(I::ColourLoop, co, true) == "White");
      CHECK(p.tick(ok2, 0.0, co, true) == A::Changed && p.value(I::ColourLoop, co, true) == "Red");
      p.tick(dn2, 0.0, co, true);
      for (int k = 0; k < 5; ++k) p.tick(rt, 0.0, co, true);
      CHECK(p.value(I::ColourBackground, co, true) == "White");
      p.tick(rt, 0.0, co, true);
      CHECK(p.value(I::ColourBackground, co, true) == "Black");  // (six, then round)
      p.set_passthrough_available(true);  // a headset that shows the room: Passthrough after White
      for (int k = 0; k < 5; ++k) p.tick(rt, 0.0, co, true);
      p.tick(rt, 0.0, co, true);
      CHECK(p.value(I::ColourBackground, co, true) == "Passthrough" && co.colours[kColourBackground] == kBackgroundPassthrough);
      CHECK(!light_background(co.colours) && background_colour(co.colours).r == 0);  // (drawn as black: lines keep their colours)
      p.tick(rt, 0.0, co, true);
      CHECK(p.value(I::ColourBackground, co, true) == "Black");
      p.set_passthrough_available(false);
      p.tick(rt, 0.0, co, true);
      p.tick(dn2, 0.0, co, true);
      CHECK(p.current() == I::ResetColours);
      co.colours[3] = 2;
      CHECK(p.tick(lt, 0.0, co, true) == A::None && p.tick(rt, 0.0, co, true) == A::None && co.colours[3] == 2);  // (A only)
      CHECK(p.tick(ok2, 0.0, co, true) == A::Changed && co.colours == Options{}.colours);
    }
    // The course around you: off, a ring of 3 m by default, 1 to 10 m in half metres.
    CHECK(p.label(I::WrapCourse, true) == "Course around you (Off)" && p.label(I::WrapSize, true) == "Circle size (3.0 m)");
    {
      Options wo;
      wo.vr_wrap_radius = 9.5;
      p.set_items({I::WrapSize});
      SettingsPanel::Input bigger;
      bigger.right = true;
      CHECK(p.tick(bigger, 0.0, wo, true) == A::Changed && wo.vr_wrap_radius == 10.0);
      CHECK(p.tick(bigger, 0.0, wo, true) == A::None && p.value(I::WrapSize, wo, true) == "10.0 m");
    }
    p.set_items({I::ScrollSpeed});
    SettingsPanel::Input faster;
    faster.right = true;
    CHECK(p.tick(faster, 0.0, so, true) == A::Changed && so.scroll_speed == 10.0 && p.value(I::ScrollSpeed, so, true) == "10x");
    CHECK(p.tick(faster, 0.0, so, true) == A::None && so.scroll_speed == 10.0);
    CHECK(p.scroll_line(so) == "Scroll speed: 10x");
    // Timing feedback: no bracket (the original has none), on and off, and its text.
    CHECK(p.label(I::TimingFeedback) == "Timing feedback" && p.value(I::TimingFeedback, so, true) == "Off");
    p.set_items({I::TimingFeedback});
    SettingsPanel::Input ok;
    ok.confirm = true;
    CHECK(p.tick(ok, 0.0, so, true) == A::Changed && so.timing_feedback);
    CHECK(SettingsPanel::offset_text(-12.4) == "-12 ms" && SettingsPanel::offset_text(7.6) == "+8 ms" && SettingsPanel::offset_text(0.2) == "0 ms");
    CHECK(to_font_text(SettingsPanel::offset_text(23)) == "+23 ms");  // (+ is one of our own strokes)
    const Hud::Layer t = p.draw_timing("+23 ms", 0.5f, 3);
    CHECK(!t.lines.empty() && t.lines.front().colour.r == 127);
    p.set_items({I::ScrollSpeed});
    so.scroll_speed = 0.75;
    SettingsPanel::Input slower;
    slower.left = true;
    CHECK(p.tick(slower, 0.0, so, true) == A::Changed && so.scroll_speed == 0.5 && p.value(I::ScrollSpeed, so, true) == "0.5x");
    CHECK(p.tick(slower, 0.0, so, true) == A::None);
    so.scroll_speed = 1.25;
    CHECK(p.value(I::ScrollSpeed, so, true) == "1.25x");
    // The course selects' line at the bottom middle, in the font.
    const Hud::Layer line = p.draw_scroll_speed(so, 7);
    float lo = 1e9f, hi = -1e9f;
    for (const SceneLine& l : line.lines) lo = std::min({lo, l.a.x, l.b.x}), hi = std::max({hi, l.a.x, l.b.x});
    CHECK(!line.lines.empty() && std::abs(lo + hi) < 150.0f);  // centred
    CHECK(!line.lines.empty() && line.lines.front().a.y > 0);  // below the middle (y down)
    p.set_items(all);
  }
  // The score in numbers: the font's digits, centred, and on the monitor a layer of its own.
  {
    const auto digits = p.text_lines("116279", 1.0f, 3, 4);
    float lo = 1e9f, hi = -1e9f;
    for (const SceneLine& l : digits) lo = std::min({lo, l.a.x, l.b.x}), hi = std::max({hi, l.a.x, l.b.x});
    CHECK(digits.size() > 10 && std::abs(lo + hi) < 60);
    CHECK(!p.draw_score(1234, 5).lines.empty());
  }
  // No counterpart in the original: ours.
  CHECK(p.label(I::Zoom, true) == "Zoom (1.00)" && p.label(I::ViewAngle, true) == "View angle (45)");
  CHECK(p.label(I::SoundEffects) == "Sound effects" && p.label(I::Continue) == "Continue" && p.label(I::Recentre, true) == "Recenter view");
  go_to(I::ViewAngle);
  o.vr_angle = 180;
  p.tick(right, 0.02, o, true);
  CHECK(std::abs(o.vr_angle + 165) < 1e-9);
  go_to(I::LineWidth);
  p.tick(right, 0.02, o, true);
  CHECK(std::abs(o.vr_line_width - 0.055) < 1e-9 && p.value(I::LineWidth, o, true) == "0.055");  // (steps of 0.005)
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

TEST(disc_sces02873_performance_overlay_draws_its_rows_and_graph) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  const GameText words = GameText::from_exe(iso.read("MAIN_G.EXE"));
  FrameStats st;
  std::uint64_t t = 0;
  for (int i = 0; i < 90; ++i) {
    FrameSample f;
    f.ns = t += 12'500'000, f.interval_ms = i == 40 ? 30.0 : 12.5, f.period_ms = 12.5;
    st.add(f);
  }
  st.add_press(-8);
  const auto rows = perf_rows(st.summary(t), 2, 45);
  const auto graph = st.intervals(t);
  for (int language = 0; language < 5; ++language) {
    const Pak game(iso.read("GAME/" + language_prefix(language) + "FILES.PAK"));
    PerfOverlay overlay(game, language, &words);
    for (const bool vr : {false, true}) {
      const Hud::Layer text_only = overlay.draw(rows, {}, 12.5, vr, 16.0f / 9.0f);
      const Hud::Layer with_graph = overlay.draw(rows, graph, 12.5, vr, 16.0f / 9.0f);
      CHECK(text_only.lines.size() > 200);  // every row written
      CHECK(with_graph.lines.size() == text_only.lines.size() + 2 + graph.size());
      // Nothing wobbles, and it all stays in its corner: on the monitor left of the
      // centre; in a headset right of it and above the eyes' level (y down).
      float left = 1e9f, right = -1e9f, bottom = -1e9f;
      for (const SceneLine& l : with_graph.lines) {
        CHECK(l.shake_a == 0 && l.shake_b == 0);
        left = std::min({left, l.a.x, l.b.x});
        right = std::max({right, l.a.x, l.b.x});
        bottom = std::max({bottom, l.a.y, l.b.y});
      }
      if (vr) CHECK(left > 50 && bottom < -100);
      else CHECK(right < 0);
    }
  }
}

TEST(disc_sces02873_whole_song_drawn_ahead_fits_and_runs_away) {
  // The headset's "whole song" line: every part of song 4 (the longest course) built
  // ahead and drawn at once from the song's start. It must fit the renderers' line
  // buffers (131,072) and run on away from Vibri without wrapping back.
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  const Pak game(iso.read("GAME/02_FILES.PAK"));
  const CourseScript script = parse_course_script(game.view("SCRIPT/SYSTEM3.FSL"));
  const std::vector<Block> blocks = make_blocks(disc.read_audio_track(5));
  CourseGenerator gen(script);
  gen.start_round(4, 0);
  for (std::size_t i = 0; i < blocks.size(); ++i) gen.process(static_cast<std::int32_t>(i * kBlockSize), blocks[i]);
  Gameplay play;
  RoundSetup setup;
  setup.course = Course::Silver;
  setup.course_seconds = static_cast<int>(blocks.size() * kBlockSize / kSampleRate);
  setup.track_index = 1;
  play.start(setup);
  Scene::Frame f;
  f.seconds = 0.02;
  f.speed = Fixed::whole(1);
  RoadPiece piece;
  for (const CourseEvent& e : gen.events())
    if (play.preview(e, 0, piece)) f.road.push_back(piece);
  CHECK(f.road.size() >= 150);
  for (std::size_t i = 1; i < f.road.size(); ++i) CHECK(f.road[i - 1].position < f.road[i].position);
  Scene scene(game);
  scene.reset(true, false);
  std::vector<SceneLine> lines;
  SceneView view;
  scene.draw(f, lines, view);
  CHECK(lines.size() > 154 * 4 && lines.size() < 131072);  // (about 1,000: a road piece is a few lines)
  float far_z = -1e30f;
  for (const SceneLine& l : lines) far_z = std::max({far_z, l.a.z, l.b.z});
  // The last part is over two minutes ahead: far beyond the 8 s play builds (about 7 m).
  CHECK(far_z - scene.vibri_z() > 600.0f * 100);
}

TEST(disc_sces02873_settings_labels_with_defaults_fit_beside_their_values) {
  // Each row: the label with its default on the left, the value on the right. With
  // the longest values (the whole song, the count-in), in every language, the two
  // must not meet: the pause's columns are 3200 apart, the menus' settings screen's 2900.
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  const GameText words = GameText::from_exe(iso.read("MAIN_G.EXE"));
  using I = SettingsPanel::Item;
  const std::vector<I> all = {I::WobbleSize, I::WobbleHold, I::Smooth, I::LineWidth, I::Zoom, I::ViewAngle, I::SeeAhead,
                              I::SeeBehind, I::Recentre, I::SoundEffects, I::Vibration, I::SoundTiming, I::QuitSong,
                              I::ResumeStyle, I::Performance, I::ScoreNumbers, I::FixedCourses, I::EffectsVolume, I::ScrollSpeed, I::TimingFeedback, I::FcCelebration, I::WrapCourse, I::WrapSize, I::Visual, I::Modifiers, I::CustomColours, I::Back,
                              I::ColourRoad, I::ColourBlock, I::ColourPit, I::ColourLoop, I::ColourWave, I::ColourBlockPit, I::ColourBlockLoop,
                              I::ColourBlockWave, I::ColourPitLoop, I::ColourPitWave, I::ColourLoopWave, I::ColourAngel, I::ColourRabbit,
                              I::ColourFrog, I::ColourWorm, I::ColourBackground, I::ColourMenu, I::ResetColours, I::Calibration, I::SoundCalibration, I::PictureTiming, I::PictureCalibration, I::Glow, I::RefreshRate, I::Continue};
  Options longest;
  longest.scroll_speed = 9.75;
  longest.vr_ahead_seconds = longest.vr_behind_seconds = kWholeSongSeconds;
  longest.resume_countdown = true;
  longest.perf_overlay = 2;
  longest.audio_offset_ms = -500;
  longest.vr_angle = -165;
  for (int language = 0; language < 5; ++language) {
    const Pak game(iso.read("GAME/" + language_prefix(language) + "FILES.PAK"));
    SettingsPanel p(game, language, &words);
    for (const bool vr : {false, true})
      for (const I item : all)
        for (const bool at_longest : {true, false}) {
          const Options use = at_longest ? longest : Options{};
          const float needed = p.text_width(p.label(item, vr)) + 72.0f + p.text_width(p.value(item, use, vr));
          if (needed > 2900.0f) std::printf("    too wide: language %d, %s | %s (%.0f)\n", language, p.label(item, vr).c_str(), p.value(item, use, vr).c_str(), double(needed));
          CHECK(needed <= 2900.0f);
          CHECK(p.label(item, vr).find('?') == std::string::npos);
        }
  }
}

TEST(disc_sces02873_file_picker_ticks_files_and_draws_every_row) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  const GameText words = GameText::from_exe(iso.read("MAIN_G.EXE"));
  using A = FilePicker::Action;
  SettingsPanel::Input up, down, confirm, cancel, none;
  up.up = down.down = confirm.confirm = cancel.cancel = true;
  for (int language = 0; language < 5; ++language) {
    const Pak game(iso.read("GAME/" + language_prefix(language) + "FILES.PAK"));
    SettingsPanel panel(game, language, &words);
    FilePicker p(panel, language);
    // Folder mode: only folders are listed, after "use this folder" and "up".
    p.show(FilePicker::Mode::Folder, "C:/Users/me/Music", {{"Albums", true}, {"Ünïcode Földer", true}, {"stray.mp3", false}}, true);
    CHECK(p.rows().size() == 4);
    for (const std::string& r : p.rows()) CHECK(!r.empty());
    CHECK(p.tick(confirm, 0.02) == A::UseFolder);
    CHECK(p.start() == A::UseFolder);  // Start: Use this folder, from anywhere
    CHECK(p.tick(down, 0.02) == A::Moved && p.tick(confirm, 0.02) == A::Up);
    CHECK(p.tick(down, 0.02) == A::Moved && p.tick(confirm, 0.02) == A::Enter && p.entry() && p.entry()->name == "Albums");
    CHECK(!p.draw(1.0f, 7).lines.empty() && !p.draw(0.25f, 7).lines.empty());
    // Long names: up to 102 characters shown (owner: three times the first 34), a longer
    // one shortened from the middle; every row still fits inside a 4:3 picture.
    {
      const std::string fits(96, 'a'), longer = std::string(80, 'b') + std::string(80, 'c') + ".mp3";
      p.show(FilePicker::Mode::Files, "C:/Music", {{fits + ".mp3", false}, {longer, false}}, true);
      bool kept = false, shortened = false;
      for (const std::string& r : p.rows()) {
        kept = kept || r.find(fits) != std::string::npos;
        if (r.find("...") != std::string::npos) shortened = r.size() <= 102 + 4 && r.find("bbb") != std::string::npos && r.find(".mp3") != std::string::npos;
      }
      CHECK(kept && shortened);
      float widest = 0;
      for (const SceneLine& l : p.draw(1.0f, 3).lines) widest = std::max({widest, std::abs(l.a.x), std::abs(l.b.x)});
      CHECK(widest < 3080.0f);  // (4:3 at the text camera's depth: about 3,080 either side)
    }
    // Files mode: tick two files; Done is refused until something is ticked.
    p.show(FilePicker::Mode::Files, "C:/Users/me/Music/Albums", {{"Disc 2", true}, {"01 One.mp3", false}, {"02 Two.flac", false}, {"03 Three.mp3", false}}, true);
    CHECK(p.tick(confirm, 0.02) == A::None);  // Done, nothing ticked
    CHECK(p.start() == A::None);  // Start does nothing until something is ticked
    p.tick(down, 0.02), p.tick(down, 0.02), p.tick(down, 0.02), p.tick(down, 0.02);  // up, clear, the folder, the first file
    CHECK(p.tick(confirm, 0.02) == A::Toggled);
    p.tick(down, 0.02), p.tick(down, 0.02);
    CHECK(p.tick(confirm, 0.02) == A::Toggled);
    CHECK((p.ticked() == std::vector<std::string>{"01 One.mp3", "03 Three.mp3"}));
    CHECK(p.rows()[0].find("(2)") != std::string::npos);
    CHECK(p.start() == A::Done);  // Start: Done, from anywhere in the list
    p.tick(down, 0.02);  // from the last file it wraps round to Done
    CHECK(p.tick(confirm, 0.02) == A::Done);
    // Clear selection (owner): the row below Up a folder takes every tick off; with
    // none ticked it does nothing.
    p.tick(down, 0.02), p.tick(down, 0.02);
    CHECK(p.rows()[2].find("(") == std::string::npos && !p.entry());
    CHECK(p.tick(confirm, 0.02) == A::Cleared && p.ticked().empty() && p.rows()[0].find("(") == std::string::npos);
    CHECK(p.tick(confirm, 0.02) == A::None);
    CHECK(p.start() == A::None);  // (nothing ticked: Start does not play)
    CHECK(p.tick(cancel, 0.02) == A::Cancel);
    // A long list: held down, it scrolls (a press, then 0.35 s, then fast) and stops at the end.
    {
      std::vector<FilePicker::Entry> many;
      for (int k = 1; k <= 200; ++k) many.push_back({"Song " + std::to_string(k) + ".mp3", false});
      p.show(FilePicker::Mode::Files, "C:/Big", many, true);
      SettingsPanel::Input held;
      held.held_down = true;
      p.tick(down, 0.02);
      for (int k = 0; k < 100; ++k) p.tick(held, 0.02);  // 2 s held
      const std::string at = p.entry() ? p.entry()->name : std::string();
      CHECK(at.rfind("Song ", 0) == 0 && std::stoi(at.substr(5)) > 60);
      for (int k = 0; k < 300; ++k) p.tick(held, 0.02);
      CHECK(p.entry() && p.entry()->name == "Song 200.mp3");  // (stopped, not wrapped)
      SettingsPanel::Input held_up;
      held_up.held_up = true;
      p.tick(up, 0.02);
      for (int k = 0; k < 400; ++k) p.tick(held_up, 0.02);
      CHECK(!p.entry());  // back at the top (Done, Up a folder, Clear selection, then the first song)
      p.tick(down, 0.02), p.tick(down, 0.02), p.tick(down, 0.02);
      CHECK(p.entry() && p.entry()->name == "Song 1.mp3");
    }
    // "No music here" for 3 s when a folder with nothing playable is chosen.
    {
      const std::size_t before = p.draw(1.0f, 4).lines.size();
      p.notice_no_music();
      CHECK(p.draw(1.0f, 4).lines.size() > before);
      for (int k = 0; k < 200; ++k) p.tick(none, 0.02);  // 4 s
      CHECK(p.draw(1.0f, 4).lines.size() == before);
    }
    // Another folder clears the ticks.
    p.show(FilePicker::Mode::Files, "C:/Users/me/Music/Albums/Disc 2", {{"x.mp3", false}}, true);
    CHECK(p.ticked().empty());
    CHECK(!p.draw(0.25f, 9).lines.empty());
  }
}

TEST(disc_sces02873_speed_label_shows_in_every_language) {
  // The speed courses' level label: its model is each language's own, its three clips
  // are shared; in every language each level draws its label with the dial.
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  for (int language = 0; language < 5; ++language) {
    const Pak game(iso.read("GAME/" + language_prefix(language) + "FILES.PAK"));
    CHECK(localized(game, "METERS/SPD_LBL.TMD", language) == "METERS/" + language_prefix(language) + "SPD_LBL.TMD");
    for (int level = 1; level <= 3; ++level) {
      Hud hud(game, language);
      hud.start_speed(level);
      Hud::Frame f;
      f.seconds = 0.5;
      f.speed_course = level;
      f.target_speed = 2.0;
      std::vector<Hud::Layer> layers;
      hud.draw(f, layers);
      std::size_t lines = 0;
      for (const Hud::Layer& l : layers) lines += l.lines.size();
      CHECK(lines > 50);
    }
  }
}

TEST(disc_sces02873_vibri_outside_play_takes_her_colour) {
  // In the menus, at the title and in the kiosk film Vibri's lines carry the rabbit
  // form's part (custom colours colour her there too); nothing else on those screens does.
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  Iso9660 iso(disc);
  const std::uint8_t rabbit = std::uint8_t(line_part::kVibri + 1);
  auto count = [&](const std::vector<Hud::Layer>& layers, std::size_t& tagged, std::size_t& other) {
    for (const auto& l : layers)
      for (const SceneLine& s : l.lines) (s.part == rabbit ? tagged : other) += s.part == rabbit || s.part == 0 ? 1 : 0;
  };
  {
    const Pak game(iso.read("GAME/02_FILES.PAK"));
    TitleScreens t(game);
    t.go_to(TitleScreens::Screen::Main);
    std::size_t tagged = 0, other = 0;
    for (int i = 0; i < 120; ++i) {
      t.tick({});
      count(t.layers(), tagged, other);
    }
    CHECK(tagged > 0 && other > 0);  // (Vibri, and the menu itself untouched)
  }
  {
    const Pak title(iso.read("TITLE/FILES.PAK"));
    TitleIntro t(title);
    std::size_t tagged = 0, other = 0;
    for (int i = 0; i < 1500; ++i) {
      t.tick(false);
      if (i > 1400) count(t.layers(), tagged, other);  // (at the title)
    }
    CHECK(tagged > 0 && other > 0);
  }
  {
    const Pak pak(iso.read("KIOSK/02_FILES.PAK"));
    KioskMovie k(pak, 1);
    KioskMovie::Input in;
    in.music_playing = true;
    std::size_t tagged = 0, other = 0;
    for (int i = 0; i < 600; ++i) {
      k.tick(in);
      count(k.layers(), tagged, other);
    }
    CHECK(tagged > 0 && other > 0);
  }
}
