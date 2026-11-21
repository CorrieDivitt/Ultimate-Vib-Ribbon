#include "vib/title.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "vib/bytes.h"

namespace vib {

namespace {

constexpr double kTau = 6.28318530717958647692;
constexpr double kTick = 1.0 / TitleScreens::kFrameRate;
constexpr int kGteOne = 4096;   // the menus count jitter in the geometry unit's fixed point
constexpr int kMenuJitter = 5;  // the menus' models
constexpr int kTextJitter = 11; // the item under the cursor, on the high scores
constexpr Rgb kHidden{0, 0, 2}; // the original "hides" an object by drawing it nearly black

// The original counts these screens in frames of its 50 Hz display.
constexpr int frames(int sixtieths) { return 50 * sixtieths / 60; }

// Vibri, and the high scores, are seen through a fixed camera looking along +z.
SceneView vibri_view() {
  SceneView v;
  v.eye = {627, 0, -2661};
  v.target = {627, 0, 0};
  v.fov_y = float(534 * kTau / 4096.0);
  return v;
}

}  // namespace

void TitleScreens::Model::reset_look() {
  const std::size_t n = tmd.objects.size();
  look.swap.resize(n);
  for (std::size_t i = 0; i < n; ++i) look.swap[i] = int(i);
  look.jitter.assign(n, 0);
  look.colour.assign(n, std::nullopt);
}

void TitleScreens::Model::hide(int object, bool hidden) {
  look.colour[std::size_t(object)] = hidden ? std::optional<Rgb>(kHidden) : std::nullopt;
}

int TitleScreens::load_model(const Pak& pak, int language, const char* name) {
  // Models with words on them carry their languages before their file name.
  Model m;
  m.tmd = parse_tmd(pak.view(localized(pak, std::string("TITLE/") + name, language)));
  m.reset_look();
  models_.push_back(std::move(m));
  return int(models_.size()) - 1;
}

int TitleScreens::load_movie(const Pak& pak, int model, const char* name, bool loop) {
  Movie mv{model, parse_anm(pak.view(std::string("TITLE/") + name)), {}};
  mv.clip = Clip(int(mv.anim.frames.size()), mv.anim.rate, loop);
  movies_.push_back(std::move(mv));
  return int(movies_.size()) - 1;
}

int TitleScreens::load_camera(const Pak& pak, const char* name, bool loop) {
  Camera c{parse_anc(pak.view(std::string("TITLE/CAM/") + name)), {}};
  c.clip = Clip(int(c.anim.keys.size()), c.anim.rate, loop);
  cameras_.push_back(std::move(c));
  return int(cameras_.size()) - 1;
}

TitleScreens::TitleScreens(const Pak& pak, int language) {
  // The line font: Japanese has its own, the others share one.
  font_ = parse_tmd(pak.view(localized(pak, "FONT/FONT.TMD", language)));
  m_vibri_ = load_model(pak, language, "VIBRI/VIBRI.TMD");
  m_main_ = load_model(pak, language, "MA/MA.TMD");
  m_psj_ = load_model(pak, language, "MODE/MDV.TMD");
  m_option_ = load_model(pak, language, "OPTION/OP.TMD");
  m_hiscore_ = load_model(pak, language, "HISCORE/HI.TMD");
  m_end_ = load_model(pak, language, "END/CL_OV.TMD");
  m_again_ = load_model(pak, language, "AGAIN/AG.TMD");
  m_song_ = load_model(pak, language, "END/CLR_SONG.TMD");

  // Vibri lying, sitting and standing, each still and speaking; all loop.
  const char* vibri[6] = {"VIBRI/MN_V01A.ANM", "VIBRI/MN_V01B.ANM", "VIBRI/MN_V02A.ANM",
                          "VIBRI/MN_V02B.ANM", "VIBRI/MN_V03A.ANM", "VIBRI/MN_V03B.ANM"};
  for (std::size_t i = 0; i < 6; ++i) vibri_movies_[i] = load_movie(pak, m_vibri_, vibri[i], true);
  // Each menu: the clip from item i to item i + 1 (the last back round to the first).
  const char* main_steps[4] = {"MA/MA12.ANM", "MA/MA23.ANM", "MA/MA34.ANM", "MA/MA41.ANM"};
  for (std::size_t i = 0; i < 4; ++i) main_steps_[i] = load_movie(pak, m_main_, main_steps[i]);
  const char* psj_steps[5] = {"MODE/MDV12.ANM", "MODE/MDV23.ANM", "MODE/MDV34.ANM", "MODE/MDV45.ANM", "MODE/MDV51.ANM"};
  for (std::size_t i = 0; i < 5; ++i) psj_steps_[i] = load_movie(pak, m_psj_, psj_steps[i]);
  psj_open_ = load_movie(pak, m_psj_, "MODE/MDV_OP.ANM");
  psj_close_ = load_movie(pak, m_psj_, "MODE/MDV_CL.ANM");
  // The own-CD mode select: one track, all tracks, speed, in order, high scores, back.
  m_unknown_ = load_model(pak, language, "MODE/MDU.TMD");
  const char* unknown_steps[6] = {"MODE/MDU12.ANM", "MODE/MDU23.ANM", "MODE/MDU34.ANM", "MODE/MDU45.ANM", "MODE/MDU56.ANM", "MODE/MDU61.ANM"};
  for (std::size_t i = 0; i < 6; ++i) unknown_steps_[i] = load_movie(pak, m_unknown_, unknown_steps[i]);
  unknown_open_ = load_movie(pak, m_unknown_, "MODE/MDU_OP.ANM");
  unknown_close_ = load_movie(pak, m_unknown_, "MODE/MDU_CL.ANM");
  // CD CHANGE: change the CD, this CD, back.
  m_cd_ = load_model(pak, language, "CD/CD.TMD");
  const char* cd_steps[3] = {"CD/CD-12.ANM", "CD/CD-23.ANM", "CD/CD-31.ANM"};
  for (std::size_t i = 0; i < 3; ++i) cd_steps_[i] = load_movie(pak, m_cd_, cd_steps[i]);
  cd_open_ = load_movie(pak, m_cd_, "CD/OPEN.ANM");
  cd_close_ = load_movie(pak, m_cd_, "CD/CLOSE.ANM");
  // TRACK SELECT: the arrows either side of the number, and back below.
  m_trk_ = load_model(pak, language, "TRK/TRK.TMD");
  trk_left_ = load_movie(pak, m_trk_, "TRK/TRK_L.ANM");
  trk_right_ = load_movie(pak, m_trk_, "TRK/TRK_R.ANM");
  trk_exit_ = load_movie(pak, m_trk_, "TRK/TRK_EXIT.ANM");
  const char* option_steps[3] = {"OPTION/OP01_02.ANM", "OPTION/OP02_03.ANM", "OPTION/OP03_01.ANM"};
  for (std::size_t i = 0; i < 3; ++i) option_steps_[i] = load_movie(pak, m_option_, option_steps[i]);
  option_open_ = load_movie(pak, m_option_, "OPTION/OPEN.ANM");
  option_close_ = load_movie(pak, m_option_, "OPTION/CLOSE.ANM");
  hi_open_ = load_movie(pak, m_hiscore_, "HISCORE/HI1_OPEN.ANM");
  hi_loop_ = load_movie(pak, m_hiscore_, "HISCORE/HI1_LOOP.ANM", true);
  hi_left_ = load_movie(pak, m_hiscore_, "HISCORE/HI2_L.ANM");
  hi_right_ = load_movie(pak, m_hiscore_, "HISCORE/HI2_R.ANM");
  hi_exit_ = load_movie(pak, m_hiscore_, "HISCORE/HI2_EX.ANM");
  const char* over[4] = {"END/G_OV_01.ANM", "END/G_OV_02.ANM", "END/G_OV_03.ANM", "END/G_OV_04.ANM"};
  for (std::size_t i = 0; i < 4; ++i) over_[i] = load_movie(pak, m_end_, over[i]);
  gov_fall_ = load_movie(pak, m_vibri_, "V_OV/GOV_V01.ANM");
  const char* gov[7] = {"V_OV/GOV_V03A.ANM", "V_OV/GOV_V03B.ANM", "V_OV/GOV_V03C.ANM", "V_OV/GOV_V05.ANM",
                        "V_OV/GOV_V06A.ANM", "V_OV/GOV_V06B.ANM", "V_OV/GOV_V07.ANM"};
  for (std::size_t i = 0; i < 7; ++i) gov_[i] = load_movie(pak, m_vibri_, gov[i], i == 1 || i == 5);  // 03b and 06b loop
  again_steps_[0] = load_movie(pak, m_again_, "AGAIN/AG_12.ANM");
  again_steps_[1] = load_movie(pak, m_again_, "AGAIN/AG_21.ANM");
  again_close_ = load_movie(pak, m_again_, "AGAIN/CLOSE.ANM");
  // CLEAR: the sign coming in and staying, Vibri during the bonus, then her waiting,
  // counting, and her four comments when the score does not win a medal.
  clear_title_ = load_movie(pak, m_end_, "END/CLR_01.ANM");
  clear_sign_ = load_movie(pak, m_end_, "END/CLR_00.ANM");
  clear_bonus_vibri_ = load_movie(pak, m_vibri_, "V_CL/CLR_V00.ANM");
  const char* clr[6] = {"V_CL/CLR_V04.ANM", "V_CL/CLR_V05.ANM", "V_CL/CLR_V12.ANM", "V_CL/CLR_V14.ANM", "V_CL/CLR_V16.ANM", "V_CL/CLR_V15.ANM"};
  for (std::size_t i = 0; i < 6; ++i) clr_[i] = load_movie(pak, m_vibri_, clr[i], i == 0);
  // The medal sequence: ten steps, each Vibri's clip with its own camera.
  const char* steps[10] = {"V01", "V02", "V03A", "V03B", "V04", "V05A", "V05B", "V06", "V07", "V08"};
  const char* step_cams[10] = {"SONG_01.ANC", "SONG_02.ANC", "SONG_03A.ANC", "SONG_03B.ANC", "SONG_04.ANC",
                               "SONG_05A.ANC", "SONG_05B.ANC", "SONG_06.ANC", "SONG_07.ANC", "SONG_08.ANC"};
  for (std::size_t i = 0; i < 10; ++i) {
    song_[i] = load_movie(pak, m_vibri_, (std::string("V_SONG/") + steps[i] + ".ANM").c_str());
    song_cams_[i] = load_camera(pak, step_cams[i]);
  }
  song_medal_ = load_movie(pak, m_song_, "END/SONG_01.ANM", true);
  song_ten_ = load_movie(pak, m_song_, "END/SONG_TEN.ANM");

  cam_left_ = load_camera(pak, "C_L.ANC");
  cam_right_ = load_camera(pak, "R_C.ANC");
  cam_zoom_ = load_camera(pak, "ZOOM_IN.ANC");
  cam_menu_ = load_camera(pak, "MA_CAM.ANC", true);
  view_ = camera_view(cam_menu_);
}

// ---------------------------------------------------------------- flow

namespace {
// Which screens have the menu music: all but the results and the course itself.
bool has_music(TitleScreens::Screen s) {
  using S = TitleScreens::Screen;
  return s == S::Main || s == S::PsjModeSelect || s == S::UnknownModeSelect || s == S::CdChange || s == S::TrackSelect ||
         s == S::Option || s == S::PsjHiscore;
}
}  // namespace

void TitleScreens::go_to(Screen s) {
  // The music stops when the next screen has none (or another).
  if (has_music(screen_) && !has_music(s)) sounds_.push_back({Channel::Music, {}, 0, true});
  if (s != Screen::None) {
    fade_frame_ = 0;
    fade_step_ = 1;
    next_ = s;
    previous_ = screen_;
  } else {
    screen_ = s;
  }
}

void TitleScreens::update_fade() {
  // Out over 16 frames, switch, then in over 17: the original's 20/60 s each way.
  const int n = frames(20);
  fade_frame_ += fade_step_;
  arrived_ = fade_frame_ == n && fade_step_ == 1;
  if (fade_frame_ == n) {
    if (screen_ != next_) results_frame_ = 0;
    screen_ = next_;
    --fade_step_;
  } else if (fade_frame_ == 0) {
    fade_step_ = 0;
  }
  if (fade_step_ > 0) fade_ = kOut;
  else if (fade_step_ < 0 || fade_frame_ == n) fade_ = kIn;
  else fade_ = kNone;
}

TitleScreens::Result TitleScreens::tick(const Input& in) {
  ++frame_count_;
  layers_.clear();
  voice_playing_ = in.voices_playing;
  music_playing_ = in.music_playing;
  lines_.seed(next_random(frame_count_ * 2654435761u));
  update_fade();
  Result r = Result::None;
  switch (screen_) {
    case Screen::Main: r = main_menu(in); break;
    case Screen::PsjModeSelect: r = psj_mode_select(in); break;
    case Screen::UnknownModeSelect: r = unknown_mode_select(in); break;
    case Screen::CdChange: r = cd_change(in); break;
    case Screen::TrackSelect: r = track_select(in); break;
    case Screen::Option: r = options(in); break;
    case Screen::PsjHiscore: r = psj_hiscore(in, 0); break;
    case Screen::Results: r = psj_hiscore(in, ++results_frame_); break;
    case Screen::Clear: r = clear(in); break;
    case Screen::Again: r = again(in); break;
    case Screen::AgainSelect: r = again_select(in); break;
    default: break;
  }
  if (fade_ == kOut) r = Result::None;
  return r;
}

// ---------------------------------------------------------------- pieces

TitleScreens::MenuRead TitleScreens::read(Cursor& c, const Input& in, bool wrap, bool active) {
  MenuRead r;
  if (!active) return r;
  // Key repeat: a direction held for 0.66 s comes again every 0.33 s.
  bool up = in.up, down = in.down, left = in.left, right = in.right;
  const bool pressed = up || down || left || right, held = in.held_up || in.held_down || in.held_left || in.held_right;
  const int delay = int(std::lround(0.66 * kFrameRate)), rate = int(std::lround(0.33 * kFrameRate));
  if (repeat_state_ == 0) {
    if (pressed) {
      repeat_timer_ = int(frame_count_) + delay;
      repeat_state_ = 1;
    }
  } else {
    if (pressed) repeat_timer_ = int(frame_count_) + delay;
    if (held) {
      if (int(frame_count_) >= repeat_timer_) {
        up |= in.held_up, down |= in.held_down, left |= in.held_left, right |= in.held_right;
        repeat_timer_ = int(frame_count_) + rate;
      }
    } else {
      repeat_state_ = 0;
    }
  }
  if (in.confirm) {
    r.confirm = true;
  } else if (in.cancel) {
    r.back = c.index != c.back;
    c.index = c.back;
  }
  r.up = up, r.down = down, r.left = left, r.right = right;
  const int from = c.index;
  int dy = 0;
  if (up) dy = -1;
  if (down) dy = 1;
  int y = c.index + dy;
  if (wrap) y = (y + c.count) % c.count;
  else y = std::clamp(y, 0, c.count - 1);
  c.index = y;
  r.changed = c.index != from;
  return r;
}

void TitleScreens::rewind_movies() {
  for (Movie& m : movies_) m.clip.rewind();
}

void TitleScreens::set_camera(int cue) {
  // Cues: the camera to the left, from the right, and zooming in, each forwards
  // or backwards. Every camera goes back to its start first.
  if (cue < 0) return;
  for (Camera& c : cameras_) {
    c.clip.rewind();
    c.clip.set_speed(1);
  }
  const int cams[3] = {cam_left_, cam_right_, cam_zoom_};
  Clip& clip = cameras_[std::size_t(cams[cue / 2])].clip;
  const bool backwards = cue % 2 == 1;
  clip.rewind(backwards);
  clip.set_speed(backwards ? -1 : 1);
}

SceneView TitleScreens::camera_view(int camera) {
  const Camera& c = cameras_[std::size_t(camera)];
  return camera_at(c.anim, c.clip.pose());
}

void TitleScreens::update_camera(int cue) {
  if (cue < 0) return;
  const int cams[3] = {cam_left_, cam_right_, cam_zoom_};
  const int cam = cams[cue / 2];
  view_ = camera_view(cam);
  cameras_[std::size_t(cam)].clip.advance(kTick);
}

void TitleScreens::draw_movie(int movie, double speed, std::vector<SceneLine>& out) {
  Movie& m = movies_[std::size_t(movie)];
  m.clip.set_speed(speed);
  Model& model = models_[std::size_t(m.model)];
  lines_.animated(model.tmd, m.anim, m.clip.pose(), {}, 0, out, model.look);
  m.clip.advance(kTick);
}

void TitleScreens::draw_menu_movie(int movie, std::vector<SceneLine>& out) {
  if (fade_ == kNone) speed_ = 1.0;
  draw_movie(movie, speed_, out);
  if (movies_[std::size_t(movie)].clip.finished()) {
    speed_ = 0;
    entering_ = false;
  }
}

void TitleScreens::say(std::vector<int> sounds, int volume, Vibri speaking) {
  sounds_.push_back({Channel::Voice, std::move(sounds), volume, false});
  voice_playing_ = true;  // from the moment it is asked for, as the original's sequence counts it
  if (speaking != Vibri::Hidden) vibri_ = speaking;
}

void TitleScreens::play_music() { sounds_.push_back({Channel::Music, {32}, 90, false}); }

const std::string& TitleScreens::words(GameText::List list, int index) const {
  static const std::string none;
  return text_ ? text_->get(list, index, text_language_) : none;
}

void TitleScreens::draw_text(const std::string& text, int jitter) {
  if (!text_ || text.empty()) return;
  // The original's DrawText: the camera at the origin looking along +z (LookAlongZ
  // 0, 0, 0 to 295.625, fov 3.75), the text from (-1800, 1050) at depth 2690, each
  // character 90 wide and each line 230 down, at scale 1.
  constexpr int kX = -1800, kY = 1050, kDepth = 2690, kWidth = 90, kLineHeight = 230;
  Hud::Layer layer;
  layer.view.eye = {0, 0, 0};
  layer.view.target = {0, 0, 4730};
  layer.view.fov_y = float(682 * kTau / 4096.0);
  lines_.set_view(layer.view);
  // Each character jitters by the screen's jitter plus 4 of the font's own.
  const int range = jitter / kGteOne + 4;
  int column = 0, row = 0;
  bool second = false;
  for (const char ch : text) {
    const auto c = static_cast<unsigned char>(ch);
    const bool this_second = second;
    second = false;
    if (c == '~') {
      second = true;
    } else if (c == ' ') {
      ++column;
    } else if (c == '\n') {
      ++row;
      column = 0;
    } else {
      const int object = text_->object_for(c, this_second);
      if (object >= 0 && object < int(font_.objects.size())) {
        LineBuilder::Xform xf;
        xf.t = {float(kX + kWidth * column), float(kY + kLineHeight * row), float(kDepth)};
        lines_.object(font_.objects[std::size_t(object)], xf, range, 0, nullptr, layer.lines);
      }
      ++column;
    }
  }
  layers_.push_back(std::move(layer));
}

void TitleScreens::set_text(const GameText* text, int language) {
  text_ = text;
  text_language_ = language;
  if (!text_) return;
  // The own-CD mode select's item labels are objects 1 to 6 of its model, each a flat
  // ten-sided frame (corners at x +-288, +-233, +-89, y 0, +-121, +-196) with its word
  // inside. Object 1 (the original's "one track") keeps its frame; its word is
  // replaced by ours, built from the font's letters as two lines.
  static const char* const kPick[5][2] = {{"Titel", "w~Ghlen"}, {"Pick", "track(s)"}, {"Elegir", "pistas"},
                                          {"Choisir", "pistes"}, {"Scegli", "brani"}};
  const int l = language >= 0 && language < 5 ? language : 1;
  TmdObject label;
  std::size_t longest = 0;
  for (const char* row : kPick[l]) {
    std::size_t n = 0;
    for (const char* c = row; *c; ++c) n += *c != '~';
    longest = std::max(longest, n);
  }
  const float s = std::min(0.5f, 400.0f / (float(longest) * 90.0f));  // inside the frame, the size of the other words
  const float pitch = 230.0f * s;
  for (int r = 0; r < 2; ++r) {
    const char* row = kPick[l][r];
    std::size_t n = 0;
    for (const char* c = row; *c; ++c) n += *c != '~';
    float x = -0.5f * float(n - 1) * 90.0f * s;
    const float y = (float(r) - 0.5f) * pitch;
    bool second = false;
    for (const char* c = row; *c; ++c) {
      if (*c == '~') {
        second = true;
        continue;
      }
      const int object = text_->object_for(static_cast<unsigned char>(*c), second);
      second = false;
      if (object >= 0 && object < int(font_.objects.size())) {
        const TmdObject& glyph = font_.objects[std::size_t(object)];
        const auto base = std::uint16_t(label.vertices.size());
        for (const Vec3s& v : glyph.vertices)
          label.vertices.push_back({std::int16_t(std::lround(v.x * s + x)), std::int16_t(std::lround(v.y * s + y)), std::int16_t(0)});
        for (TmdLine line : glyph.lines) {
          line.a = std::uint16_t(line.a + base), line.b = std::uint16_t(line.b + base);
          label.lines.push_back(line);
        }
      }
      x += 90.0f * s;
    }
  }
  Model& m = models_[std::size_t(m_unknown_)];
  if (m.tmd.objects.size() <= 1 || label.lines.empty()) return;
  // The frame: the original object's lines whose two ends are both frame corners.
  const TmdObject& original = m.tmd.objects[1];
  auto corner = [](const Vec3s& v) {
    const int ax = std::abs(v.x), ay = std::abs(v.y);
    return (ax == 288 && ay == 0) || (ax == 233 && ay == 121) || (ax == 89 && ay == 196);
  };
  for (const TmdLine& line : original.lines) {
    if (line.a >= original.vertices.size() || line.b >= original.vertices.size()) continue;
    const Vec3s& a = original.vertices[line.a];
    const Vec3s& b = original.vertices[line.b];
    if (!corner(a) || !corner(b)) continue;
    const auto base = std::uint16_t(label.vertices.size());
    label.vertices.push_back(a);
    label.vertices.push_back(b);
    TmdLine copy = line;
    copy.a = base, copy.b = std::uint16_t(base + 1);
    label.lines.push_back(copy);
  }
  m.tmd.objects[1] = label;
}

void TitleScreens::draw_text_at(const std::string& text, float x, float y, float scale, int range) {
  if (!text_ || text.empty()) return;
  constexpr float kDepth = 2690, kWidth = 90;  // as draw_text: the original's text space
  Hud::Layer layer;
  layer.view.eye = {0, 0, 0};
  layer.view.target = {0, 0, 4730};
  layer.view.fov_y = float(682 * kTau / 4096.0);
  lines_.set_view(layer.view);
  LineBuilder::Xform xf;
  xf.m.m = {{{scale, 0, 0}, {0, scale, 0}, {0, 0, scale}}};
  int column = 0;
  for (const char ch : text) {
    const int object = text_->object_for(static_cast<unsigned char>(ch), false);
    if (ch != ' ' && object >= 0 && object < int(font_.objects.size())) {
      xf.t = {x + kWidth * scale * float(column), y, kDepth};
      lines_.object(font_.objects[std::size_t(object)], xf, range, 0, nullptr, layer.lines);
    }
    ++column;
  }
  layers_.push_back(std::move(layer));
}

void TitleScreens::draw_vibri(int jitter, std::vector<SceneLine>& out) {
  if (vibri_ == Vibri::Hidden) return;
  Model& m = models_[std::size_t(m_vibri_)];
  m.jitter_all((jitter + 4 * kGteOne) / kGteOne);
  const bool results = screen_ == Screen::PsjHiscore || screen_ == Screen::Results || screen_ == Screen::Again || screen_ == Screen::Clear;
  m.hide(19, results);
  m.hide(20, results);
  const int movie_of[6] = {0, 1, 2, 3, 4, 5};
  if (vibri_ == Vibri::LyingSpeaking || vibri_ == Vibri::SittingSpeaking || vibri_ == Vibri::StandingSpeaking) {
    movies_[std::size_t(vibri_movies_[std::size_t(vibri_movie_)])].clip.set_speed(1);
    if (!voice_playing_) {
      vibri_ = Vibri::Paused;
      vibri_frame_ = 0;
    }
  }
  if (vibri_ == Vibri::Paused) {
    movies_[std::size_t(vibri_movies_[std::size_t(vibri_movie_)])].clip.set_speed(0);
    if (++vibri_frame_ == frames(90)) {  // from speaking back to the pose it came from
      if (vibri_movie_ == 1) vibri_ = Vibri::Lying;
      if (vibri_movie_ == 3) vibri_ = Vibri::Sitting;
      if (vibri_movie_ == 5) vibri_ = Vibri::Standing;
      vibri_frame_ = 0;
    }
  }
  const int movie = vibri_ == Vibri::Paused ? vibri_movie_ : movie_of[int(vibri_)];
  Movie& mv = movies_[std::size_t(vibri_movies_[std::size_t(movie)])];
  lines_.animated(m.tmd, mv.anim, mv.clip.pose(), {}, 0, out, m.look);
  mv.clip.advance(kTick);
  vibri_movie_ = movie;
}

void TitleScreens::show_number(Model& m, const std::array<int, 6>& slots, int first_digit_object, int value) {
  // Slot 0 is the units. Leading zeros show the slot's own (blank) object.
  int divisor = 1;
  int digits[6];
  for (int i = 0; i < 6; ++i) {
    digits[i] = value / divisor % 10;
    divisor *= 10;
  }
  bool shown = false;
  for (int i = 5; i >= 0; --i) {
    if (digits[i] != 0 || shown) {
      shown = true;
      m.swap(slots[std::size_t(i)], first_digit_object + digits[i]);
    } else {
      m.swap(slots[std::size_t(i)], slots[std::size_t(i)]);
    }
  }
  if (!shown) m.swap(slots[0], first_digit_object);
}

// ---------------------------------------------------------------- screens

TitleScreens::Result TitleScreens::main_menu(const Input& in) {
  static const Result results[3] = {Result::PsjModes, Result::UnknownModes, Result::Options};
  // Vibri says each item's name: two or three syllables of the gameplay bank.
  static const std::vector<int> voices[4] = {{240, 241, 126}, {125, 243, 126}, {119}, {96}};
  const bool still = fade_frame_ == 0;
  Result result = Result::None;
  if (arrived_) {
    sounds_.push_back({Channel::Voice, {}, 0, true});
    if (main_cursor_.index == 3) main_cursor_.index = 0;
    play_music();
    vibri_ = Vibri::Lying;
    menu_movie_ = main_steps_[std::size_t(main_cursor_.index)];
    speed_ = 0;
    rewind_movies();
    set_camera(1);
    leaving_ = false;
    wait_frame_ = 0;
  }
  const MenuRead r = read(main_cursor_, in, true, still);
  int jitter = kMenuJitter * kGteOne;
  if (previous_ == Screen::None) jitter += fade_jitter();  // the first screen scatters in
  const int i = main_cursor_.index;
  if (r.changed) {
    if (r.up) {
      menu_movie_ = main_steps_[std::size_t(i)];
      movies_[std::size_t(menu_movie_)].clip.rewind(true);
      speed_ = -1;
    } else if (r.down) {
      menu_movie_ = main_steps_[std::size_t((i + 3) % 4)];
      movies_[std::size_t(menu_movie_)].clip.rewind();
      speed_ = 1;
    }
    if (r.up || r.down) say(voices[i], 64, Vibri::LyingSpeaking);
  }
  if (r.back) {
    menu_movie_ = main_steps_[3];
    movies_[std::size_t(menu_movie_)].clip.rewind();
    say(voices[3], 64, Vibri::LyingSpeaking);
    speed_ = 0;
  }
  if (leaving_) {
    jitter = wait_frame_ * 300 * kGteOne;  // the menu shakes apart
    if (++wait_frame_ == frames(20)) {
      wait_frame_ = 0;
      leaving_ = false;
      return Result::Leave;
    }
  }
  if (r.confirm && !leaving_) {
    say({1}, 90, Vibri::Hidden);  // the confirm sound
    if (i < 3) {
      result = results[i];
      set_camera(0);
    } else {
      leaving_ = true;
    }
  }
  models_[std::size_t(m_main_)].jitter_all(jitter / kGteOne);
  Hud::Layer vibri, menu;
  vibri.view = vibri_view();
  lines_.set_view(vibri.view);
  draw_vibri(jitter, vibri.lines);
  view_ = camera_view(cam_menu_);
  cameras_[std::size_t(cam_menu_)].clip.advance(kTick);
  if (fade_ == kOut && i != 3) update_camera(0);
  if (fade_ == kIn && previous_ != Screen::None) update_camera(1);
  menu.view = view_;
  lines_.set_view(menu.view);
  draw_movie(menu_movie_, speed_, menu.lines);
  layers_.push_back(std::move(vibri));
  layers_.push_back(std::move(menu));
  return result;
}

TitleScreens::Result TitleScreens::psj_mode_select(const Input& in) {
  static const Result results[5] = {Result::Bronze, Result::Silver, Result::Gold, Result::PsjHiscores, Result::PsjBack};
  static const int voices[5] = {114, 113, 112, 128, 96};  // bronze, silver, gold, high scores, back
  const bool still = fade_frame_ == 0;
  if (arrived_) {
    vibri_ = Vibri::Sitting;
    leaving_ = false;
    rewind_movies();
    if (previous_ == Screen::Main) {
      psj_cursor_.index = 0;
      camera_cue_ = 2;
      entering_ = true;
    } else {
      camera_cue_ = 1;
      entering_ = false;
    }
    menu_movie_ = psj_steps_[std::size_t(psj_cursor_.index)];
    speed_ = 0;
    set_camera(camera_cue_);
    play_music();
  }
  const MenuRead r = read(psj_cursor_, in, true, still && !entering_ && !leaving_);
  int jitter = kMenuJitter * kGteOne;
  if (next_ == Screen::Done) jitter += fade_jitter();  // leaving for the course: the lines fly apart
  models_[std::size_t(m_psj_)].jitter_all(jitter / kGteOne);
  const int i = psj_cursor_.index;
  if (r.changed) {
    if (r.up || r.down) say({voices[i]}, 90, Vibri::SittingSpeaking);
    if (r.up) {
      menu_movie_ = psj_steps_[std::size_t(i)];
      movies_[std::size_t(menu_movie_)].clip.rewind(true);
      speed_ = -1;
    } else if (r.down) {
      menu_movie_ = psj_steps_[std::size_t((i + 4) % 5)];
      movies_[std::size_t(menu_movie_)].clip.rewind();
      speed_ = 1;
    }
  }
  if (r.back) {
    menu_movie_ = psj_steps_[4];
    movies_[std::size_t(menu_movie_)].clip.rewind();
    say({voices[i]}, 90, Vibri::SittingSpeaking);
    speed_ = 0;
  }
  Hud::Layer vibri, menu;
  vibri.view = vibri_view();
  lines_.set_view(vibri.view);
  draw_vibri(jitter, vibri.lines);
  view_ = camera_view(cam_menu_);
  cameras_[std::size_t(cam_menu_)].clip.advance(kTick);
  if (next_ != Screen::Done && !still) update_camera(camera_cue_);
  menu.view = view_;
  lines_.set_view(menu.view);
  if (entering_) draw_menu_movie(psj_open_, menu.lines);
  else draw_movie(menu_movie_, speed_, menu.lines);
  layers_.push_back(std::move(vibri));
  layers_.push_back(std::move(menu));
  if (leaving_ && movies_[std::size_t(psj_close_)].clip.finished()) {
    leaving_ = false;
    camera_cue_ = 3;
    set_camera(3);
    return results[i];
  }
  if (r.confirm) {
    say({1}, 90, Vibri::Hidden);
    if (i != 4) {
      camera_cue_ = 0;
      set_camera(0);
      return results[i];
    }
    menu_movie_ = psj_close_;
    leaving_ = true;
    speed_ = 1;
  }
  return Result::None;
}

// The own-CD side's mode select (UNKNOWN MODE SELECT): six items with Vibri sitting,
// built as the game-disc course select is. Each item says its words when the cursor
// reaches it (one track and all tracks have a second word); back leaves with the
// closing clip. Behaviour read from the original's TitleDraw; our own code.
TitleScreens::Result TitleScreens::unknown_mode_select(const Input& in) {
  static const Result results[6] = {Result::OneTrack, Result::AllTracks, Result::SpeedModes,
                                    Result::InOrder,  Result::UnknownHiscores, Result::UnknownBack};
  static const std::vector<int> voices[6] = {{115, 120}, {117, 120}, {116}, {118}, {128}, {96}};
  constexpr int kBack = 5;
  const bool still = fade_frame_ == 0;
  if (arrived_) {
    vibri_ = Vibri::Sitting;
    leaving_ = false;
    rewind_movies();
    if (previous_ == Screen::Main) {
      unknown_cursor_.index = 0;
      camera_cue_ = 2;
      entering_ = true;
    } else {
      camera_cue_ = 1;
      entering_ = false;
    }
    menu_movie_ = unknown_steps_[std::size_t(unknown_cursor_.index)];
    speed_ = 0;
    set_camera(camera_cue_);
    play_music();
  }
  const MenuRead r = read(unknown_cursor_, in, true, still && !entering_ && !leaving_);
  int jitter = kMenuJitter * kGteOne;
  if (next_ == Screen::Done) jitter += fade_jitter();
  models_[std::size_t(m_unknown_)].jitter_all(jitter / kGteOne);
  const int i = unknown_cursor_.index;
  if (r.changed) {
    if (r.up || r.down) say(voices[i], 64, Vibri::SittingSpeaking);
    if (r.up) {
      menu_movie_ = unknown_steps_[std::size_t(i)];
      movies_[std::size_t(menu_movie_)].clip.rewind(true);
      speed_ = -1;
    } else if (r.down) {
      menu_movie_ = unknown_steps_[std::size_t((i + 5) % 6)];
      movies_[std::size_t(menu_movie_)].clip.rewind();
      speed_ = 1;
    }
  }
  if (r.back) {
    menu_movie_ = unknown_steps_[kBack];
    movies_[std::size_t(menu_movie_)].clip.rewind();
    say(voices[i], 64, Vibri::SittingSpeaking);
    speed_ = 0;
  }
  Hud::Layer vibri, menu;
  vibri.view = vibri_view();
  lines_.set_view(vibri.view);
  draw_vibri(jitter, vibri.lines);
  view_ = camera_view(cam_menu_);
  cameras_[std::size_t(cam_menu_)].clip.advance(kTick);
  if (next_ != Screen::Done && !still) update_camera(camera_cue_);
  menu.view = view_;
  lines_.set_view(menu.view);
  if (entering_) draw_menu_movie(unknown_open_, menu.lines);
  else draw_movie(menu_movie_, speed_, menu.lines);
  layers_.push_back(std::move(vibri));
  layers_.push_back(std::move(menu));
  if (leaving_ && movies_[std::size_t(unknown_close_)].clip.finished()) {
    leaving_ = false;
    camera_cue_ = 3;
    set_camera(3);
    return results[i];
  }
  if (r.confirm) {
    say({1}, 90, Vibri::Hidden);
    if (i != kBack) {
      camera_cue_ = 0;
      set_camera(0);
      return results[i];
    }
    menu_movie_ = unknown_close_;
    leaving_ = true;
    speed_ = 1;
  }
  return Result::None;
}

// CD CHANGE (own-CD side): change the CD, this CD (go on), back; Vibri sitting.
// Arriving from the track select keeps the cursor (moved off back); from anywhere
// else it starts on "this CD". Our app opens its file picker for "change the CD".
TitleScreens::Result TitleScreens::cd_change(const Input& in) {
  static const Result results[3] = {Result::ChangeCd, Result::CdReady, Result::CdChangeBack};
  static const std::vector<int> voices[3] = {{246, 247}, {246, 248}, {96}};
  static const int cues[3] = {0, 0, 3};
  constexpr int kBack = 2;
  const bool still = fade_frame_ == 0;
  if (arrived_) {
    vibri_ = Vibri::Sitting;
    leaving_ = false;
    speed_ = 0;
    rewind_movies();
    if (previous_ == Screen::TrackSelect) {
      camera_cue_ = 1;
      if (cd_cursor_.index == kBack) cd_cursor_.index = 1;
      entering_ = false;
    } else {
      cd_cursor_.index = 1;
      camera_cue_ = 2;
      entering_ = true;
    }
    menu_movie_ = cd_steps_[std::size_t(cd_cursor_.index)];
    set_camera(camera_cue_);
    play_music();
  }
  const MenuRead r = read(cd_cursor_, in, true, still && !entering_ && !leaving_);
  int jitter = kMenuJitter * kGteOne;
  if (next_ == Screen::Done) jitter += fade_jitter();
  models_[std::size_t(m_cd_)].jitter_all(jitter / kGteOne);
  const int i = cd_cursor_.index;
  if (r.changed && (r.up || r.down)) {
    say(voices[i], 64, Vibri::SittingSpeaking);
    if (r.up) {
      menu_movie_ = cd_steps_[std::size_t(i)];
      movies_[std::size_t(menu_movie_)].clip.rewind(true);
      speed_ = -1;
    } else {
      menu_movie_ = cd_steps_[std::size_t((i + 2) % 3)];
      movies_[std::size_t(menu_movie_)].clip.rewind();
      speed_ = 1;
    }
  }
  if (r.back) {
    menu_movie_ = cd_steps_[std::size_t(i)];
    movies_[std::size_t(menu_movie_)].clip.rewind();
    say(voices[i], 64, Vibri::SittingSpeaking);
    speed_ = 0;
  }
  Hud::Layer vibri, menu;
  vibri.view = vibri_view();
  lines_.set_view(vibri.view);
  draw_vibri(jitter, vibri.lines);
  view_ = camera_view(cam_menu_);
  cameras_[std::size_t(cam_menu_)].clip.advance(kTick);
  if (fade_ == kOut && !leaving_) update_camera(cues[i]);
  if (fade_ == kIn) update_camera(camera_cue_);
  menu.view = view_;
  lines_.set_view(menu.view);
  if (entering_) draw_menu_movie(cd_open_, menu.lines);
  else draw_movie(menu_movie_, speed_, menu.lines);
  layers_.push_back(std::move(vibri));
  layers_.push_back(std::move(menu));
  if (leaving_ && movies_[std::size_t(cd_close_)].clip.finished()) {
    leaving_ = false;
    set_camera(cues[i]);
    return results[i];
  }
  if (r.confirm) {
    say({1}, 90, Vibri::Hidden);
    if (i < kBack) {
      set_camera(cues[i]);
      return results[i];
    }
    leaving_ = true;
    menu_movie_ = cd_close_;
    movies_[std::size_t(cd_close_)].clip.rewind();
    speed_ = 1;
  }
  return Result::None;
}

// TRACK SELECT (own-CD side, for one track and the speed courses): the track's number
// between two arrows, left and right step it (wrapping), Vibri standing says it; the
// number is drawn large and shaking while it has the cursor; back below. Behaviour read
// from the original's TitleDraw; our own code.
TitleScreens::Result TitleScreens::track_select(const Input& in) {
  const bool still = fade_frame_ == 0;
  auto say_track = [&]() { say(say_number(track, 1).clips, 64, Vibri::StandingSpeaking); };
  if (arrived_) {
    vibri_ = Vibri::Standing;
    play_music();
    track = 1;
    trk_cursor_.index = 0;
    menu_movie_ = trk_left_;
    rewind_movies();
    camera_cue_ = 4;
    set_camera(4);
    speed_ = 0;
    leaving_ = false;
    trk_wait_ = 0;
    say_track();
  }
  const MenuRead r = read(trk_cursor_, in, false, still);
  int jitter = kMenuJitter * kGteOne;
  int text_jitter = kTextJitter * kGteOne;
  if (fade_ == kOut && trk_cursor_.index == 0) jitter += fade_jitter(), text_jitter += fade_jitter();
  Model& model = models_[std::size_t(m_trk_)];
  model.jitter_all(jitter / kGteOne);
  const int tracks = std::max(1, num_tracks);
  if (trk_cursor_.index == 0 && (r.right || r.left)) {
    if (r.right) track = track % tracks + 1;
    else track = track == 1 ? tracks : track - 1;
    say_track();
    model.look.jitter[r.right ? 1 : 0] = text_jitter / kGteOne;  // the arrow pressed shakes
    menu_movie_ = r.right ? trk_right_ : trk_left_;
    movies_[std::size_t(menu_movie_)].clip.rewind();
    speed_ = 1;
  }
  if (r.back) {
    menu_movie_ = trk_exit_;
    movies_[std::size_t(trk_exit_)].clip.rewind();
    speed_ = 1;
    say({96}, 90, Vibri::StandingSpeaking);
  }
  if (r.changed) {
    menu_movie_ = trk_exit_;
    if (r.up) {
      movies_[std::size_t(trk_exit_)].clip.rewind(true);
      speed_ = -1;
      say_track();
    } else if (r.down) {
      movies_[std::size_t(trk_exit_)].clip.rewind();
      speed_ = 1;
      say({96}, 90, Vibri::StandingSpeaking);
    }
  }
  if (trk_cursor_.index != 0) model.look.jitter[2] = text_jitter / kGteOne;  // back shakes while chosen
  Hud::Layer vibri, menu;
  vibri.view = vibri_view();
  lines_.set_view(vibri.view);
  draw_vibri(jitter, vibri.lines);
  view_ = camera_view(cam_menu_);
  cameras_[std::size_t(cam_menu_)].clip.advance(kTick);
  update_camera(camera_cue_);
  menu.view = view_;
  lines_.set_view(menu.view);
  draw_movie(menu_movie_, speed_, menu.lines);
  layers_.push_back(std::move(vibri));
  layers_.push_back(std::move(menu));
  // The number: bigger and shaking while it is the item chosen.
  const bool on_number = trk_cursor_.index == 0;
  int number_range = on_number ? 5 : 0;
  if (fade_ == kOut && on_number) number_range += fade_jitter() / kGteOne;
  draw_text_at(std::to_string(track), track >= 10 ? 70.0f : 170.0f, 0.0f, on_number ? 2.8f : 1.8f, number_range);
  if (leaving_ && ++trk_wait_ == frames(10)) {
    camera_cue_ = 5;
    set_camera(5);
    leaving_ = false;
  }
  if (r.confirm) {
    say({1}, 90, Vibri::Hidden);
    if (trk_cursor_.index != 0) {
      leaving_ = true;
      return Result::TrackBack;
    }
    return Result::TrackChosen;
  }
  return Result::None;
}

TitleScreens::Result TitleScreens::options(const Input& in) {
  static const int voices[3] = {129, 130, 96};
  const bool still = fade_frame_ == 0;
  Model& model = models_[std::size_t(m_option_)];
  auto show_switches = [&]() {
    // ON is the model's own object; OFF is drawn in its place.
    model.swap(2, sound_effects ? 2 : 1);
    model.swap(4, vibration ? 4 : 3);
  };
  if (arrived_) {
    vibri_ = Vibri::Sitting;
    leaving_ = false;
    show_switches();
    play_music();
    entering_ = true;
    menu_movie_ = option_steps_[0];
    option_cursor_.index = 0;
    rewind_movies();
    camera_cue_ = 2;
    set_camera(2);
    speed_ = 0;
  }
  const MenuRead r = read(option_cursor_, in, true, still && !entering_ && !leaving_);
  const int jitter = kMenuJitter * kGteOne;
  model.jitter_all(jitter / kGteOne);
  const int i = option_cursor_.index;
  if (r.changed) {
    if (r.up || r.down) say({voices[i]}, 90, Vibri::SittingSpeaking);
    if (r.up) {
      menu_movie_ = option_steps_[std::size_t(i)];
      movies_[std::size_t(menu_movie_)].clip.rewind(true);
      speed_ = -1;
    } else if (r.down) {
      menu_movie_ = option_steps_[std::size_t((i + 2) % 3)];
      movies_[std::size_t(menu_movie_)].clip.rewind();
      speed_ = 1;
    }
  }
  if (r.back) {
    menu_movie_ = option_steps_[std::size_t(i)];
    movies_[std::size_t(menu_movie_)].clip.rewind();
    say({voices[i]}, 90, Vibri::SittingSpeaking);
    speed_ = 0;
  }
  Hud::Layer vibri, menu;
  vibri.view = vibri_view();
  lines_.set_view(vibri.view);
  draw_vibri(jitter, vibri.lines);
  if (!still) {
    update_camera(camera_cue_);
  } else {
    view_ = camera_view(cam_menu_);
    cameras_[std::size_t(cam_menu_)].clip.advance(kTick);
  }
  menu.view = view_;
  lines_.set_view(menu.view);
  if (entering_) draw_menu_movie(option_open_, menu.lines);
  else draw_movie(menu_movie_, speed_, menu.lines);
  layers_.push_back(std::move(vibri));
  layers_.push_back(std::move(menu));
  if (leaving_ && movies_[std::size_t(option_close_)].clip.finished()) {
    leaving_ = false;
    camera_cue_ = 3;
    set_camera(3);
    return Result::OptionBack;
  }
  if (r.confirm) {
    if (i == 0 || i == 1) {
      if (i == 0) sound_effects = !sound_effects;
      else vibration = !vibration;
      const bool on = i == 0 ? sound_effects : vibration;
      say({on ? 131 : 132}, 90, Vibri::SittingSpeaking);
      show_switches();
    } else {
      say({1}, 90, Vibri::Hidden);
      menu_movie_ = option_close_;
      leaving_ = true;
      speed_ = 1;
    }
  }
  return Result::None;
}

TitleScreens::Result TitleScreens::psj_hiscore(const Input& in, int frame) {
  // Objects of the high-score model: the menu (arrows and back), the three places,
  // each course's name and the digits of the three scores.
  static const int menu_objects[4] = {0, 1, 2, 7};
  static const int place_objects[3] = {27, 48, 49};
  static const int course_names[3] = {6, 47, 44};
  static const std::array<int, 6> rows[3] = {{8, 9, 10, 11, 12, 13}, {14, 15, 16, 17, 18, 19}, {20, 21, 22, 23, 24, 25}};
  static const int voices[4] = {114, 113, 112, 96};
  Model& model = models_[std::size_t(m_hiscore_)];
  const bool still = fade_frame_ == 0;
  const bool results = frame != 0;

  // After a course it is set up on its second frame, as the original's results do.
  if (arrived_ || frame == 2) {
    back_cursor_.index = 0;
    menu_movie_ = hi_left_;
    speed_ = 0;
    if (!results) play_music();
    rewind_movies();
    entering_ = true;
    leaving_ = false;
    vibri_ = Vibri::Lying;
    const int course = results ? results_course : hiscore_course_;
    model.swap(6, course_names[course]);
    model.swap(5, 5);
    for (int o : menu_objects) model.hide(o, results);  // after a course: no menu
    for (int o : place_objects) model.hide(o, false);
  }
  const int course = results ? results_course : hiscore_course_;
  for (std::size_t row = 0; row < 3; ++row)
    show_number(model, rows[row], 28, psj_high_scores[std::size_t(course)][row]);
  const MenuRead r = read(back_cursor_, in, false, still && !results);

  Hud::Layer layer;
  layer.view = vibri_view();  // this screen is drawn through Vibri's camera
  lines_.set_view(layer.view);
  const int jitter = kMenuJitter * kGteOne;
  if (results) {
    model.jitter_all(kMenuJitter);
    for (int o : place_objects) model.hide(o, false);
    // The new score's place blinks.
    if (results_rank >= 1 && results_rank <= 3)
      model.hide(place_objects[results_rank - 1], frame % 50 < frames(30));
    draw_vibri(jitter, layer.lines);
    Movie& ex = movies_[std::size_t(hi_exit_)];
    if (fade_ == kNone && frames(20) < frame) {
      ex.clip.set_speed(0);
      ex.clip.seek(0);
      draw_movie(hi_exit_, 0, layer.lines);
      draw_movie(hi_loop_, 1, layer.lines);
    } else {
      draw_movie(hi_open_, fade_ == kOut ? -1 : 1, layer.lines);
    }
    layers_.push_back(std::move(layer));
    if (frame == frames(180)) {
      movies_[std::size_t(hi_open_)].clip.rewind(true);
      return Result::ResultsDone;
    }
    return Result::None;
  }

  if (r.back) say({voices[3]}, 90, Vibri::LyingSpeaking);
  if (back_cursor_.index == 0 && (r.left || r.right)) {
    if (r.left) {
      menu_movie_ = hi_left_;
      hiscore_course_ = (hiscore_course_ + 2) % 3;
    } else {
      menu_movie_ = hi_right_;
      hiscore_course_ = (hiscore_course_ + 1) % 3;
    }
    say({voices[hiscore_course_]}, 90, Vibri::LyingSpeaking);
    movies_[std::size_t(menu_movie_)].clip.rewind();
    speed_ = 1;
    model.swap(6, course_names[hiscore_course_]);
  }
  if (r.changed && (r.up || r.down)) say({back_cursor_.index != 0 ? voices[3] : voices[hiscore_course_]}, 90, Vibri::LyingSpeaking);
  model.jitter_all(kMenuJitter);
  // The item under the cursor shakes harder: the course's name, or "back".
  const int under = back_cursor_.index == 0 ? course_names[hiscore_course_] : 7;
  model.look.jitter[std::size_t(under)] = kTextJitter;
  draw_vibri(jitter, layer.lines);
  Movie& ex = movies_[std::size_t(hi_exit_)];
  ex.clip.set_speed(0);
  ex.clip.seek(back_cursor_.index);
  if (fade_ == kNone) {
    if (back_cursor_.index != 0) draw_movie(hi_exit_, 0, layer.lines);
    else draw_movie(menu_movie_, speed_, layer.lines);
    draw_movie(hi_loop_, 1, layer.lines);
  } else {
    draw_movie(hi_open_, fade_ == kIn ? 1 : -1, layer.lines);
  }
  layers_.push_back(std::move(layer));
  if (r.confirm && back_cursor_.index != 0) {
    say({1}, 90, Vibri::Hidden);
    movies_[std::size_t(hi_open_)].clip.rewind(true);
    return Result::HiscoreBack;
  }
  return Result::None;
}

TitleScreens::Result TitleScreens::again(const Input& in) {
  // Vibri's four reactions to losing: what she says (sounds of the gameplay bank),
  // and how likely each is (4, 3, 2 and 1 in 10).
  static const std::vector<int> reaction_words[4] = {
      {12, 176, 177, 179, 180, 181, 180, 181},
      {12, 176, 177, 184},
      {12, 176, 177, 185, 186, 187, 188, 189, 190, 190, 190, 190, 191},
      {12, 176, 177, 183},
  };
  Model& end = models_[std::size_t(m_end_)];
  Model& vibri = models_[std::size_t(m_vibri_)];
  if (arrived_) {
    // The score as digits (slots 31-36, digit d is object 52 + d) and as the row of
    // marks the game showed (slots 27 down to 21, shape n is object 37 + n).
    for (int o = 0; o < int(end.tmd.objects.size()); ++o) end.swap(o, o);
    static const std::array<int, 6> digits = {31, 32, 33, 34, 35, 36};
    show_number(end, digits, 52, results_score);
    const auto marks = Hud::marks(results_score);
    for (int i = 0; i < 7; ++i) end.swap(27 - i, 37 + marks[std::size_t(i)]);
    random_ = next_random(random_);
    const int n = static_cast<int>((std::uint64_t(random_) * 10) >> 32);
    comment_ = n < 4 ? 0 : n < 7 ? 1 : n < 9 ? 2 : 3;
    hiscore_frame_ = 0;
    leave_frame_ = 0;
    result_frame_ = 0;
    rewind_movies();
    sounds_.push_back({Channel::Music, {}, 0, true});
    say(reaction_words[comment_], 64, Vibri::Hidden);
  }
  const int leave_jitter = leave_frame_ * 300 * kGteOne;
  int jitter = kMenuJitter * kGteOne + leave_jitter;
  if (fade_ == kOut) jitter += fade_jitter();
  end.jitter_all(jitter / kGteOne);
  vibri.jitter_all(jitter / kGteOne);
  vibri.hide(19, true);
  vibri.hide(20, true);
  // Any button: on to the high scores if the score made them, else away.
  if ((in.confirm || in.cancel) && fade_ == kNone && !hiscore_frame_ && !leave_frame_) {
    say({1}, 90, Vibri::Hidden);
    if (results_rank == 0) return Result::ResultsDone;
    leave_frame_ = 1;
  }
  if (leave_frame_ && ++leave_frame_ == frames(20)) {
    hiscore_frame_ = 1;
    leave_frame_ = 0;
  }
  if (hiscore_frame_) {
    sounds_.push_back({Channel::Music, {}, 0, true});
    return psj_hiscore(in, hiscore_frame_++);
  }

  Hud::Layer layer;
  layer.view.eye = {0, 0, -3000};
  layer.view.target = {0, 0, 0};
  layer.view.fov_y = float(454 * kTau / 4096.0);
  lines_.set_view(layer.view);
  const int f = result_frame_;
  // The GAME OVER sign: it falls in, lands, settles; the score comes up later.
  draw_movie(over_[f < frames(100) ? 0 : f < frames(112) ? 1 : 2], 1, layer.lines);
  if (f >= frames(140)) draw_movie(over_[3], 1, layer.lines);
  // Vibri falls, then reacts.
  if (f < frames(144)) {
    draw_movie(gov_fall_, 1, layer.lines);
  } else if (comment_ == 0) {
    draw_movie(gov_[f < frames(212) ? 0 : 1], 1, layer.lines);
    if (f >= frames(188)) draw_movie(gov_[2], 1, layer.lines);
  } else if (comment_ == 1) {
    draw_movie(gov_[f < frames(200) ? 4 : 5], 1, layer.lines);
  } else {
    draw_movie(gov_[comment_ == 2 ? 6 : 3], 1, layer.lines);
  }
  layers_.push_back(std::move(layer));
  // Her words, from frame 200; the third reaction says a second thing from 360.
  if (f > frames(200)) {
    const int which = comment_ == 2 && f >= frames(360) ? 4 : comment_;
    draw_text(words(GameText::List::FailComments, which), leave_jitter + (fade_ == kOut ? fade_jitter() : 0));
  }
  ++result_frame_;
  if (result_frame_ == frames(480)) {
    if (results_rank != 0) {
      leave_frame_ = 1;
      return Result::None;
    }
    return Result::ResultsDone;
  }
  return Result::None;
}

TitleScreens::Result TitleScreens::again_select(const Input& in) {
  static const int voices[2] = {99, 100};  // "again?" and "no"
  const bool still = fade_frame_ == 0;
  Model& model = models_[std::size_t(m_again_)];
  if (arrived_) {
    // The sign names the course played: object 3 as itself, or 6 or 7 in its place.
    static const int course_sign[3] = {3, 6, 7};
    model.swap(3, course_sign[std::clamp(results_course, 0, 2)]);
    model.swap(15, 15);
    menu_movie_ = again_steps_[0];
    again_cursor_.index = 0;
    speed_ = 0;
    rewind_movies();
    vibri_ = Vibri::Sitting;
    leaving_ = false;
    camera_cue_ = 1;
    set_camera(1);
  }
  const MenuRead r = read(again_cursor_, in, true, still);
  int jitter = kMenuJitter * kGteOne;
  const int i = again_cursor_.index;
  if (r.up) {
    menu_movie_ = again_steps_[std::size_t(i)];
    movies_[std::size_t(menu_movie_)].clip.rewind(true);
    speed_ = -1;
  } else if (r.down) {
    menu_movie_ = again_steps_[std::size_t((i + 1) % 2)];
    movies_[std::size_t(menu_movie_)].clip.rewind();
    speed_ = 1;
  }
  if (r.up || r.down) say({voices[i]}, 90, Vibri::SittingSpeaking);
  if (r.back) {
    menu_movie_ = again_steps_[1];
    movies_[std::size_t(menu_movie_)].clip.rewind();
    say({voices[i]}, 90, Vibri::SittingSpeaking);
    speed_ = 0;
  }
  if (next_ == Screen::Done) jitter += fade_jitter();
  model.jitter_all(jitter / kGteOne);
  Hud::Layer vibri, menu;
  vibri.view = vibri_view();
  lines_.set_view(vibri.view);
  draw_vibri(jitter, vibri.lines);
  if (!still && next_ != Screen::Done) {
    update_camera(camera_cue_);
  } else {
    view_ = camera_view(cam_menu_);
    cameras_[std::size_t(cam_menu_)].clip.advance(kTick);
  }
  menu.view = view_;
  lines_.set_view(menu.view);
  draw_movie(menu_movie_, speed_, menu.lines);
  layers_.push_back(std::move(vibri));
  layers_.push_back(std::move(menu));
  if (leaving_ && movies_[std::size_t(again_close_)].clip.finished()) {
    leaving_ = false;
    camera_cue_ = 3;
    set_camera(3);
    return Result::AgainBack;
  }
  if (r.confirm) {
    say({1}, 90, Vibri::Hidden);
    if (i != 1) return Result::PlayAgain;
    menu_movie_ = again_close_;
    leaving_ = true;
    speed_ = 1;
  }
  return Result::None;
}

TitleScreens::SpokenNumber TitleScreens::say_number(int number, int form) {
  // Japanese, as the original says it: man (ten thousand), sen, hyaku, juu, with the
  // sound changes before them (san-byaku, sen vs. san-zen, ...). Each piece is a
  // sound of the gameplay bank lasting one or two beats.
  struct Piece {
    int sound, beats;
  };
  auto syllable = [](int i) {  // the digits' syllables, sounds 64-79
    static const int beats[16] = {1, 1, 1, 2, 2, 2, 1, 1, 1, 2, 1, 1, 2, 1, 1, 1};
    return Piece{64 + i, beats[i]};
  };
  enum Form { kPlain = 0, kSpeed = 1, kBeforeMan = 2, kBeforeHundred = 3 };
  // A digit's syllables in each form; -1 for none.
  auto digit_syllables = [](int d, int f) -> std::array<int, 2> {
    switch (d) {
      case 1: return f == kPlain ? std::array<int, 2>{0, 13} : f == kBeforeHundred ? std::array<int, 2>{-1, -1} : std::array<int, 2>{0, 1};
      case 2: return {2, -1};
      case 3: return {f == kBeforeMan ? 4 : 3, -1};
      case 4: return {5, -1};
      case 5: return {6, -1};
      case 6: return {7, f == kBeforeHundred ? 14 : 8};
      case 7: return {9, -1};
      case 8: return {10, (f == kPlain || f == kBeforeHundred) ? 15 : 11};
      case 9: return {12, -1};
      default: return {-1, -1};
    }
  };
  std::vector<int> clips;
  int beats = 0;
  auto digit = [&](int d, int f) {
    if (d == 0) return;
    for (int i : digit_syllables(d, f))
      if (i >= 0) {
        const Piece c = syllable(i);
        clips.push_back(c.sound);
        beats += c.beats;
      }
  };
  auto unit = [&](int sound) {
    clips.push_back(sound);
    beats += 2;
  };
  static const int tens_sound[10] = {0, 81, 80, 80, 80, 81, 81, 80, 81, 80};
  static const int hundreds_sound[10] = {0, 82, 82, 83, 84, 85, 85, 84, 85, 84};
  auto tens = [&](int n, int f) {  // 0-99
    const int t = n / 10, u = n % 10;
    if (t > 0) {
      if (t >= 2) digit(t, f != kBeforeMan ? f : kPlain);
      if (u == 0 && f == kPlain) {  // "juu" said last, then a rest
        clips.push_back(89);
        clips.push_back(91);
        beats += 2;
      } else {
        unit(tens_sound[t]);
      }
    }
    digit(u, f);
  };
  auto thousands = [&](int n, int f) {  // 0-9999
    const int th = n / 1000, h = n / 100 % 10;
    if (th >= 2) digit(th, f);
    if (th > 0) unit(th == 3 ? 87 : 86);
    if (h >= 2) digit(h, kBeforeHundred);
    if (h > 0) unit(hundreds_sound[h]);
    tens(n % 100, f);
  };
  SpokenNumber out;
  if (number <= 0) return out;
  number = std::min(number, 999999);
  const int man = number / 10000;
  tens(man, kBeforeMan);
  if (man > 0) unit(88);
  thousands(number % 10000, form);
  out.clips = clips;
  const int length = beats >= 15 ? 22 : beats >= 7 ? 14 : 6;
  out.tempo = beats >= 15 ? 2 : beats >= 7 ? 1 : 0;
  // The beat grid: rests (sound 91) first, then the clips, as the original lays it out.
  const int rests = length - beats;
  for (int i = 0; i < length; ++i) {
    if (i < rests) out.timeline.push_back(91);
    else if (int(clips.size()) - (i - rests) > 0) out.timeline.push_back(clips[std::size_t(i - rests)]);
  }
  return out;
}

TitleScreens::Result TitleScreens::clear(const Input& in) {
  static const std::vector<int> medal_words[3] = {
      {48, 48}, {48, 48, 51, 52}, {48, 48, 49, 50, 48, 48, 51, 52}};
  static const std::vector<int> course_name_words[3] = {{36, 53, 54}, {35, 53, 54}, {34, 53, 54}};
  static const std::vector<int> score_beat[3] = {{62, 60, 61}, {62, 63, 62, 60, 61}, {62, 63, 62, 63, 62, 60, 61}};
  static const std::vector<int> comment_words[4] = {
      {196}, {197, 198, 199}, {200, 201, 202, 203, 204}, {208, 209, 210, 211, 212, 213, 214, 215, 216, 217}};
  // Each medal's steps, bronze to gold; which of them show the medal (0) or the score (1).
  static const std::vector<int> medal_steps[3] = {{0, 4, 5, 6, 7, 9}, {0, 1, 4, 5, 6, 7, 8, 9}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}};
  static const int step_sign[10] = {0, 0, 0, 0, 1, 1, 1, -1, 0, 0};
  static const int medal_scores[3][3] = {{2000, 4000, 6000}, {2000, 4000, 6000}, {1500, 3500, 5500}};
  static const int comment_for_form[4] = {3, 1, 0, 2};  // the sign's comment by Vibri's final form
  static const int comment_signs[4] = {10, 7, 8, 9};    // its objects
  Model& end = models_[std::size_t(m_end_)];
  Model& vibri = models_[std::size_t(m_vibri_)];
  Model& song = models_[std::size_t(m_song_)];
  const int course = std::clamp(results_course, 0, 2);
  const int total = results_score + results_bonus;

  if (arrived_) {
    spoken_ = say_number(total, 0);
    for (Camera& c : cameras_) c.clip.rewind();
    end.reset_look();
    song.reset_look();
    // The score, the bonus and the total as digits; the score as marks too.
    show_number(end, {31, 32, 33, 34, 35, 36}, 52, results_score);
    const auto marks = Hud::marks(results_score);
    for (int i = 0; i < 7; ++i) end.swap(27 - i, 37 + marks[std::size_t(i)]);
    show_number(end, {0, 1, 2, 3, 4, 5}, 52, results_bonus);
    show_number(end, {11, 12, 13, 14, 15, 16}, 52, total);
    show_number(song, {14, 15, 16, 17, 18, 19}, 2, total);
    random_ = next_random(random_);
    const int n = static_cast<int>((std::uint64_t(random_) * 10) >> 32);
    comment_ = n < 4 ? 0 : n < 7 ? 1 : n < 9 ? 2 : 3;
    // The sign shows the comment for her final form; what she does and says is the one just picked.
    static const int sign_shows[4] = {1, 0, 2, 3};
    end.swap(10, comment_signs[sign_shows[comment_for_form[std::clamp(results_form, 0, 3)]]]);
    result_frame_ = wait_frame_ = repeat_ = hiscore_frame_ = leave_frame_ = 0;
    rewind_movies();
    sounds_.push_back({Channel::Music, {}, 0, true});
    say({13, 14, 225, 226, 227, 192}, 64, Vibri::Hidden);
    pass_score_ = medal_scores[course][0];
    if (total >= medal_scores[course][2]) medal_ = 2;
    else if (total >= medal_scores[course][1]) medal_ = 1;
    else if (total >= pass_score_) medal_ = 0;
    entering_ = leaving_ = medal_shown_ = false;
    step_ = 0;
  }
  const int leave_jitter = leave_frame_ * 300 * kGteOne;
  int jitter = kMenuJitter * kGteOne + leave_jitter;
  if (fade_ == kOut) {
    jitter += fade_jitter();
    sounds_.push_back({Channel::Music, {}, 0, true});
  }
  for (Model* m : {&end, &vibri, &song}) m->jitter_all(jitter / kGteOne);
  vibri.hide(19, true);
  vibri.hide(20, true);
  if ((in.confirm || in.cancel) && fade_ == kNone && !hiscore_frame_ && !leave_frame_) {
    sounds_.push_back({Channel::Music, {}, 0, true});
    say({1}, 90, Vibri::Hidden);
    if (results_rank == 0) return Result::ResultsDone;
    leave_frame_ = 1;
  }
  if (leave_frame_ && ++leave_frame_ == frames(20)) {
    hiscore_frame_ = 1;
    leave_frame_ = 0;
  }
  if (hiscore_frame_) {
    sounds_.push_back({Channel::Music, {}, 0, true});
    return psj_hiscore(in, hiscore_frame_++);
  }

  Hud::Layer layer;
  layer.view.eye = {0, 0, -3000};
  layer.view.target = {0, 0, 0};
  layer.view.fov_y = float(454 * kTau / 4096.0);
  const int f = result_frame_;
  Result result = Result::None;
  std::string text;  // what is written this frame
  const int text_jitter = leave_jitter + (fade_ == kOut ? fade_jitter() : 0);
  auto wait = [&](int sixtieths) {
    if (++wait_frame_ == frames(sixtieths)) {
      wait_frame_ = 0;
      return true;
    }
    return false;
  };
  // The music (here the score said, under the counting) comes in at 7.2 seconds.
  if (f == frames(360)) sounds_.push_back({Channel::Music, spoken_.clips, 64, false});
  std::vector<SceneLine>& out = layer.lines;
  lines_.set_view(layer.view);
  if (f < frames(112)) draw_movie(clear_title_, 1, out);
  else if (!medal_shown_) draw_movie(clear_sign_, 1, out);
  if (f < frames(360)) {
    draw_movie(clear_bonus_vibri_, 1, out);  // the bonus, and its words
    if (f >= frames(320)) text = words(GameText::List::Bonus, 1);
    else if (f >= frames(190)) text = words(GameText::List::Bonus, 0);
  } else if (!leaving_ && !medal_shown_) {
    // The score counted, then whether it wins a medal.
    if (!entering_ && frames(362) < f && !in.voices_playing && !music_playing_) {
      entering_ = true;
      sounds_.push_back({Channel::Music, {193, 207, 194}, 64, false});
    }
    if (entering_) {
      draw_movie(clr_[1], 1, out);
      if (movies_[std::size_t(clr_[1])].clip.finished() && wait(40)) {
        if (total < pass_score_) {
          leaving_ = true;
          say(comment_words[comment_], 64, Vibri::Hidden);
        } else {
          medal_shown_ = true;
          say(medal_words[medal_], 64, Vibri::Hidden);
        }
        entering_ = false;
      }
    } else {
      draw_movie(clr_[0], 1, out);
    }
    text = fill_in(words(GameText::List::Bonus, 2), total);
  } else if (leaving_) {
    // No medal: Vibri's comment.
    const int movie = clr_[std::size_t(2 + comment_)];
    draw_movie(movie, 1, out);
    text = words(GameText::List::Comments, comment_);
    if (movies_[std::size_t(movie)].clip.finished() && wait(30)) {
      if (results_rank != 0) leave_frame_ = 1;
      else result = Result::ResultsDone;
    }
  } else {
    // The medal, the course's score said aloud, and the high score if there is one.
    if (frames(30) < wait_frame_) {
      wait_frame_ = 0;
      if (results_rank == 0) return Result::ResultsDone;
      leave_frame_ = 1;
    }
    const std::vector<int>& steps = medal_steps[medal_];
    const int step = steps[std::size_t(step_)];
    const int movie = song_[std::size_t(step)];
    layer.view = camera_view(song_cams_[std::size_t(step)]);
    cameras_[std::size_t(song_cams_[std::size_t(step)])].clip.advance(kTick);
    lines_.set_view(layer.view);
    draw_movie(movie, 1, out);
    if (step_sign[step] == 0) draw_movie(song_medal_, 1, out);
    if (step_sign[step] == 1) draw_movie(song_ten_, 1, out);
    // Each step's words: step 4 names the course, steps 5 and 6 give the score.
    const std::string& step_words = words(GameText::List::MedalSteps, step);
    if (step == 4) text = fill_in(step_words, 0, words(GameText::List::CourseNames, course));
    else if (step == 5 || step == 6) text = fill_in(step_words, total);
    else text = step_words;
    if (movies_[std::size_t(movie)].clip.finished()) {
      if (step == 4) {  // "the course score is": now the number, over its beat
        say(spoken_.timeline, 64, Vibri::Hidden);
        sounds_.push_back({Channel::Music, score_beat[spoken_.tempo], 64, false});
      }
      if (step == 5 && repeat_ < spoken_.tempo - 1) {
        movies_[std::size_t(movie)].clip.rewind();  // counted again while it is said
        ++repeat_;
      } else if (step == 4) {
        if (spoken_.tempo != 0) {
          ++step_;
          movies_[std::size_t(movie)].clip.rewind();
        } else {
          step_ += 2;
        }
      } else if (step == 6) {
        step_ += results_rank != 0 ? 1 : 2;  // the high-score step only when there is one
      } else if (step_ + 1 < int(steps.size())) {
        ++step_;
        movies_[std::size_t(song_[std::size_t(steps[std::size_t(step_)])])].clip.rewind();
      } else {
        ++wait_frame_;
      }
      step_ = std::min(step_, int(steps.size()) - 1);
      const int now = steps[std::size_t(step_)];
      if (now == 4) say(course_name_words[course], 64, Vibri::Hidden);
      else if (now == 9 && !wait_frame_) say({59}, 90, Vibri::Hidden);
      else if (now == 8) say({57, 58}, 64, Vibri::Hidden);
      else if (now == 7) say({56}, 90, Vibri::Hidden);
    }
  }
  layers_.push_back(std::move(layer));
  draw_text(text, text_jitter);
  if (result != Result::None) return result;
  ++result_frame_;
  return Result::None;
}

}  // namespace vib
