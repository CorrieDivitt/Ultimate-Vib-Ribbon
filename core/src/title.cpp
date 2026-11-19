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

int TitleScreens::load_model(const Pak& pak, const char* language, const char* name) {
  // Models with words on them carry the language before their file name.
  std::string path = std::string("TITLE/") + name;
  const std::size_t slash = path.rfind('/');
  const std::string localised = path.substr(0, slash + 1) + language + path.substr(slash + 1);
  Model m;
  try {
    m.tmd = parse_tmd(pak.view(localised));
  } catch (const std::exception&) {
    m.tmd = parse_tmd(pak.view(path));
  }
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

TitleScreens::TitleScreens(const Pak& pak, const char* language) {
  m_vibri_ = load_model(pak, language, "VIBRI/VIBRI.TMD");
  m_main_ = load_model(pak, language, "MA/MA.TMD");
  m_psj_ = load_model(pak, language, "MODE/MDV.TMD");
  m_option_ = load_model(pak, language, "OPTION/OP.TMD");
  m_hiscore_ = load_model(pak, language, "HISCORE/HI.TMD");

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
  const char* option_steps[3] = {"OPTION/OP01_02.ANM", "OPTION/OP02_03.ANM", "OPTION/OP03_01.ANM"};
  for (std::size_t i = 0; i < 3; ++i) option_steps_[i] = load_movie(pak, m_option_, option_steps[i]);
  option_open_ = load_movie(pak, m_option_, "OPTION/OPEN.ANM");
  option_close_ = load_movie(pak, m_option_, "OPTION/CLOSE.ANM");
  hi_open_ = load_movie(pak, m_hiscore_, "HISCORE/HI1_OPEN.ANM");
  hi_loop_ = load_movie(pak, m_hiscore_, "HISCORE/HI1_LOOP.ANM", true);
  hi_left_ = load_movie(pak, m_hiscore_, "HISCORE/HI2_L.ANM");
  hi_right_ = load_movie(pak, m_hiscore_, "HISCORE/HI2_R.ANM");
  hi_exit_ = load_movie(pak, m_hiscore_, "HISCORE/HI2_EX.ANM");

  cam_left_ = load_camera(pak, "C_L.ANC");
  cam_right_ = load_camera(pak, "R_C.ANC");
  cam_zoom_ = load_camera(pak, "ZOOM_IN.ANC");
  cam_menu_ = load_camera(pak, "MA_CAM.ANC", true);
  view_ = camera_view(cam_menu_);
}

// ---------------------------------------------------------------- flow

void TitleScreens::go_to(Screen s) {
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
  sounds_.clear();
  lines_.seed(next_random(frame_count_ * 2654435761u));
  update_fade();
  Result r = Result::None;
  switch (screen_) {
    case Screen::Main: r = main_menu(in); break;
    case Screen::PsjModeSelect: r = psj_mode_select(in); break;
    case Screen::Option: r = options(in); break;
    case Screen::PsjHiscore: r = psj_hiscore(in, 0); break;
    case Screen::Results: r = psj_hiscore(in, ++results_frame_); break;
    default: break;
  }
  if (voice_frames_ > 0) --voice_frames_;
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
  SceneView v;
  const Camera& c = cameras_[std::size_t(camera)];
  if (c.anim.keys.empty()) return v;
  const CameraKey& k = c.anim.keys[std::size_t(c.clip.frame())];
  v.eye = {float(k.eye.x), float(k.eye.y), float(k.eye.z)};
  v.target = {float(k.target.x), float(k.target.y), float(k.target.z)};
  v.fov_y = float(k.fov * kTau / 4096.0);
  return v;
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
  lines_.animated(model.tmd, m.anim, m.clip.frame(), {}, 0, out, model.look);
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

void TitleScreens::say(int sequence, int volume, Vibri speaking) {
  sounds_.push_back({sequence, volume, false});
  // The original moves Vibri's mouth until the voice ends. Voices do not play yet,
  // so each counts as about a second.
  voice_frames_ = 50;
  if (speaking != Vibri::Hidden) vibri_ = speaking;
}

void TitleScreens::draw_vibri(int jitter, std::vector<SceneLine>& out) {
  if (vibri_ == Vibri::Hidden) return;
  Model& m = models_[std::size_t(m_vibri_)];
  m.jitter_all((jitter + 4 * kGteOne) / kGteOne);
  const bool results = screen_ == Screen::PsjHiscore || screen_ == Screen::Results;
  m.hide(19, results);
  m.hide(20, results);
  const int movie_of[6] = {0, 1, 2, 3, 4, 5};
  if (vibri_ == Vibri::LyingSpeaking || vibri_ == Vibri::SittingSpeaking || vibri_ == Vibri::StandingSpeaking) {
    movies_[std::size_t(vibri_movies_[std::size_t(vibri_movie_)])].clip.set_speed(1);
    if (voice_frames_ == 0) {
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
  lines_.animated(m.tmd, mv.anim, mv.clip.frame(), {}, 0, out, m.look);
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
  static const int voices[4] = {240, 125, 119, 96};  // first sound of each item's spoken name
  const bool still = fade_frame_ == 0;
  Result result = Result::None;
  if (arrived_) {
    if (main_cursor_.index == 3) main_cursor_.index = 0;
    sounds_.push_back({32, 90, true});
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
    sounds_.push_back({1, 90, false});
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
  static const int voices[5] = {114, 113, 112, 128, 96};
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
    sounds_.push_back({32, 90, true});
  }
  const MenuRead r = read(psj_cursor_, in, true, still && !entering_ && !leaving_);
  int jitter = kMenuJitter * kGteOne;
  if (next_ == Screen::Done) jitter += fade_jitter();  // leaving for the course: the lines fly apart
  models_[std::size_t(m_psj_)].jitter_all(jitter / kGteOne);
  const int i = psj_cursor_.index;
  if (r.changed) {
    if (r.up || r.down) say(voices[i], 90, Vibri::SittingSpeaking);
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
    say(voices[i], 90, Vibri::SittingSpeaking);
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
    sounds_.push_back({1, 90, false});
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
    sounds_.push_back({32, 90, true});
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
    if (r.up || r.down) say(voices[i], 90, Vibri::SittingSpeaking);
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
    say(voices[i], 90, Vibri::SittingSpeaking);
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
      say(on ? 131 : 132, 90, Vibri::SittingSpeaking);
      show_switches();
    } else {
      sounds_.push_back({1, 90, false});
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

  if (arrived_) {
    back_cursor_.index = 0;
    menu_movie_ = hi_left_;
    speed_ = 0;
    if (!results) sounds_.push_back({32, 90, true});
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

  if (r.back) say(voices[3], 90, Vibri::LyingSpeaking);
  if (back_cursor_.index == 0 && (r.left || r.right)) {
    if (r.left) {
      menu_movie_ = hi_left_;
      hiscore_course_ = (hiscore_course_ + 2) % 3;
    } else {
      menu_movie_ = hi_right_;
      hiscore_course_ = (hiscore_course_ + 1) % 3;
    }
    say(voices[hiscore_course_], 90, Vibri::LyingSpeaking);
    movies_[std::size_t(menu_movie_)].clip.rewind();
    speed_ = 1;
    model.swap(6, course_names[hiscore_course_]);
  }
  if (r.changed && (r.up || r.down)) say(back_cursor_.index != 0 ? voices[3] : voices[hiscore_course_], 90, Vibri::LyingSpeaking);
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
    sounds_.push_back({1, 90, false});
    movies_[std::size_t(hi_open_)].clip.rewind(true);
    return Result::HiscoreBack;
  }
  return Result::None;
}

}  // namespace vib
