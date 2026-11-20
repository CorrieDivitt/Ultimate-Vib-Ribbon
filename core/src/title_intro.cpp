#include "vib/title_intro.h"
#include <algorithm>
#include <iterator>
#include <tuple>

#include <string>

#include "vib/course_gen.h"  // next_random
#include "vib/title.h"       // TitleScreens::kFrameRate

namespace vib {
namespace {

constexpr double kTick = 1.0 / TitleScreens::kFrameRate;
constexpr int kTitleTime = 821;  // the title shows at the end of the intro (30ths of a second)
constexpr int kLineJitter = 6;   // the intro's lines all along
constexpr int kLeaveFrames = 20; // the scatter on leaving, until the menus come
constexpr int kTimeLimit = 3000;

TmdModel model(const Pak& pak, const std::string& name) { return parse_tmd(pak.view("MOVIE/TITLE/" + name)); }
ModelAnimation movie(const Pak& pak, const std::string& name) { return parse_anm(pak.view("MOVIE/TITLE/" + name)); }

}  // namespace

int TitleIntro::game_frames(int thirtieths) {
  // As the original: its frame rate (49.759 in 16.16) over 30, times the time,
  // less the two frames the display is drawn ahead.
  constexpr std::int64_t rate = std::int64_t(49.759f * 65536.0) / 30;
  const auto frames = static_cast<int>((rate * thirtieths) >> 16) - 2;
  return frames < 0 ? 0 : frames;
}

TitleIntro::Movie TitleIntro::load_menu_movie(const Pak& pak, const std::string& name) {
  Movie m{parse_anm(pak.view(name)), {}};
  m.clip = Clip(int(m.anim.frames.size()), m.anim.rate, false);
  return m;
}

TitleIntro::TitleIntro(const Pak& pak, const char* language) {
  // The menus: the language grid (one model for all languages) and "play / demo /
  // back" in the language in use; each with a clip per item and one zooming it.
  menu_models_[0] = parse_tmd(pak.view("MENU/LANG/LANG.TMD"));
  menu_models_[1] = parse_tmd(pak.view(std::string("MENU/MODE/") + language + "MODE.TMD"));
  select_[0] = load_menu_movie(pak, "MENU/LANG/SELECT.ANM");
  select_[1] = load_menu_movie(pak, "MENU/MODE/SELECT.ANM");
  scale_[0] = load_menu_movie(pak, "MENU/LANG/SCALE.ANM");
  scale_[1] = load_menu_movie(pak, "MENU/MODE/SCALE.ANM");

  // The intro's lines: three models, seven clips one after the other, the last looping.
  struct Cue {
    const char* file;
    int time, model, label;
  };
  static const Cue line_cues[] = {{"T011.ANM", 0, 0, 0},   {"T012.ANM", 115, 0, 0}, {"T013.ANM", 230, 0, 0},
                                  {"T014.ANM", 335, 0, 0}, {"T021.ANM", 446, 1, 0}, {"T022.ANM", 497, 1, 0},
                                  {"T03A.ANM", kTitleTime, 2, 1}};
  for (const char* m : {"T01.TMD", "T02.TMD", "T03.TMD"}) lines_element_.models.push_back(model(pak, m));
  auto add = [&](Element& e, const Cue& c, bool loop) {
    Movie mv{movie(pak, c.file), {}};
    mv.clip = Clip(int(mv.anim.frames.size()), mv.anim.rate, loop);
    e.cues.push_back({int(e.movies.size()), c.model, game_frames(c.time), c.label});
    e.movies.push_back(std::move(mv));
  };
  for (std::size_t i = 0; i < std::size(line_cues); ++i) add(lines_element_, line_cues[i], i + 1 == std::size(line_cues));
  // Vibri: arriving, then at the title.
  vibri_.models.push_back(model(pak, "VIBRI.TMD"));
  add(vibri_, {"T022V.ANM", 497, 0, 0}, false);
  add(vibri_, {"V_TITLE.ANM", kTitleTime, 0, 1}, true);
  // The camera: two shots.
  for (const auto& [file, time, label] : {std::tuple{"T01.ANC", 0, 0}, std::tuple{"T02.ANC", 446, 1}}) {
    CameraCue c{parse_anc(pak.view(std::string("MOVIE/TITLE/") + file)), {}, game_frames(time), label};
    c.clip = Clip(int(c.anim.keys.size()), c.anim.rate, false);
    cameras_.push_back(std::move(c));
  }
  logo_model_ = model(pak, "LOGO.TMD");
  logo_anim_ = movie(pak, "LOGO.ANM");
  // Its sounds, in the title's bank: program 17, tones 0, 1 and 2.
  sound_cues_ = {{0x110, game_frames(0), 0}, {0x111, game_frames(446), 1}, {0x112, game_frames(497), 0}};
  start(false);
}

void TitleIntro::start(bool at_title) {
  for (Element* e : {&lines_element_, &vibri_}) {
    e->frame = e->cue = 0;
    e->scatter = 20;
    e->scatter_step = -1;
    for (Movie& m : e->movies) m.clip.rewind();
  }
  for (CameraCue& c : cameras_) c.clip.rewind();
  camera_frame_ = camera_cue_ = 0;
  logo_frame_ = 0;
  logo_scatter_ = 20;
  logo_scatter_step_ = -1;
  sound_frame_ = sound_cue_ = 0;
  sounds_.clear();
  time_ = 0;
  skippable_ = true;
  leaving_ = false;
  leave_frames_ = 0;
  if (at_title) seek_title();
}

void TitleIntro::seek_title() {
  // Every element to its cue labelled 1: the second camera shot, the title's lines
  // and Vibri, the logo, and the second sound, played at once.
  for (Element* e : {&lines_element_, &vibri_}) {
    e->frame = e->cue = 0;
    for (Movie& m : e->movies) m.clip.rewind();
    for (std::size_t i = 0; i < e->cues.size(); ++i)
      if (e->cues[i].label == 1) {
        e->frame = e->cues[i].frame;
        e->cue = int(i);
        break;
      }
  }
  for (CameraCue& c : cameras_) c.clip.rewind();
  camera_frame_ = camera_cue_ = 0;
  for (std::size_t i = 0; i < cameras_.size(); ++i)
    if (cameras_[i].label == 1) {
      camera_frame_ = cameras_[i].frame;
      camera_cue_ = int(i);
      break;
    }
  logo_frame_ = game_frames(kTitleTime) - 1;
  sound_frame_ = sound_cue_ = 0;
  for (std::size_t i = 0; i < sound_cues_.size(); ++i)
    if (sound_cues_[i].label == 1) {
      sound_frame_ = sound_cues_[i].frame;
      sound_cue_ = int(i);  // played by this frame's cue (the original asks twice, on one voice)
      break;
    }
  time_ = game_frames(kTitleTime);
}

int TitleIntro::scatter_jitter(Element& e) {
  // The lines scatter more and more while the title is left; coming back, they gather.
  int jitter = kLineJitter;
  if (leaving_ || e.scatter != 0) {
    e.scatter += e.scatter_step;
    const int s = e.scatter * 60 / 50;
    jitter += s * s * 8;
    if (!leaving_ && e.scatter == 0) e.scatter_step = 1;
  }
  return jitter;
}

void TitleIntro::update_element(Element& e, std::vector<SceneLine>& out) {
  if (e.frame == 0) {
    e.scatter = 0;
    e.scatter_step = 1;
  }
  const int jitter = scatter_jitter(e);
  const bool draw = e.frame >= e.cues[std::size_t(e.cue)].frame;
  if (e.cue + 1 < int(e.cues.size()) && e.cues[std::size_t(e.cue) + 1].frame == e.frame) ++e.cue;
  if (draw) {
    const ModelCue& c = e.cues[std::size_t(e.cue)];
    Movie& m = e.movies[std::size_t(c.movie)];
    lines_.animated(e.models[std::size_t(c.model)], m.anim, m.clip.pose(), {}, jitter, out);
    m.clip.advance(kTick);
  }
  ++e.frame;
}

void TitleIntro::tick(bool confirm) {
  Input in;
  in.confirm = confirm;
  tick(in);
}

void TitleIntro::tick(const Input& in) {
  layers_.clear();
  if (time_ >= game_frames(kTitleTime)) skippable_ = false;
  if (skippable_ && (in.confirm || in.cancel)) {
    seek_title();
    skipped_ = true;  // (that press is not also the menus')
  }
  if (leaving_) ++leave_frames_;

  lines_.seed(seed_);
  seed_ = next_random(seed_);
  Hud::Layer layer;
  // The camera: each shot from its cue on, the last one holding its end.
  const bool show = camera_frame_ >= cameras_[std::size_t(camera_cue_)].frame;
  if (camera_cue_ + 1 < int(cameras_.size()) && cameras_[std::size_t(camera_cue_) + 1].frame == camera_frame_) ++camera_cue_;
  CameraCue& cam = cameras_[std::size_t(camera_cue_)];
  if (show) {
    layer.view = camera_at(cam.anim, cam.clip.pose());
    cam.clip.advance(kTick);
  }
  ++camera_frame_;
  lines_.set_view(layer.view);

  update_element(lines_element_, layer.lines);
  update_element(vibri_, layer.lines);
  update_menus(in, layer.lines);

  // The logo: still, from the title on, hidden while the lines scatter.
  if (logo_frame_ == 0) {
    logo_scatter_ = 0;
    logo_scatter_step_ = 1;
  }
  if (++logo_frame_ > game_frames(kTitleTime)) {
    if (leaving_ || logo_scatter_ != 0) {
      logo_scatter_ += logo_scatter_step_;
      if (!leaving_ && logo_scatter_ == 0) logo_scatter_step_ = 1;
    }
    if (logo_scatter_ == 0) lines_.animated(logo_model_, logo_anim_, 0, {}, 0, layer.lines);
  }

  // The sounds, each at its frame.
  if (sound_cue_ < int(sound_cues_.size()) && sound_cues_[std::size_t(sound_cue_)].frame == sound_frame_)
    sounds_.push_back({sound_cues_[std::size_t(sound_cue_++)].sound, 0});
  ++sound_frame_;

  if (time_ < kTimeLimit) ++time_;
  layers_.push_back(std::move(layer));
}

void TitleIntro::leave() { leaving_ = true; }

bool TitleIntro::intro_over() const { return time_ > game_frames(kTitleTime); }

bool TitleIntro::over() const { return leaving_ && leave_frames_ >= kLeaveFrames; }

std::vector<TitleIntro::Sound> TitleIntro::take_sounds() {
  std::vector<Sound> s;
  s.swap(sounds_);
  return s;
}

// ---------------------------------------------------------------- the menus

namespace {
// The language grid: three across, two down; the fourth language is twice as wide.
constexpr int kLangColumns = 3, kLangRows = 2;
constexpr int kLangCells[kLangRows][kLangColumns] = {{0, 1, 2}, {3, 3, 4}};
constexpr int kLangPlace[5][2] = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}};  // column, row of each language
}  // namespace

void TitleIntro::set_language_cursor(int language) {
  language = std::clamp(language, 0, 4);
  lang_x_ = kLangPlace[language][0];
  lang_y_ = kLangPlace[language][1];
}

void TitleIntro::show(Menu m) {
  next_ = m;
  busy_ = true;
  if (m == Menu::Start) transition_frame_ = 0;
  choice_ = Choice::None;
  chosen_ = false;
}

void TitleIntro::close_menus() {
  // The mode menu, its lines settling from scattered.
  busy_ = true;
  closing_ = true;
  transition_frame_ = 20;
  current_ = Menu::Mode;
  next_ = Menu::Mode;
  choice_ = Choice::None;
  chosen_ = false;
}

void TitleIntro::mode_jitter(int frame) {
  const int t = frame * 120 / 50;
  menu_jitter_[1] = t * t * 7 + 10;
}

void TitleIntro::update_menus(const Input& in, std::vector<SceneLine>& out) {
  const bool skipped = skipped_;
  skipped_ = false;
  auto draw = [&](int which, Movie& m) {
    lines_.animated(menu_models_[which], m.anim, m.clip.pose(), {}, menu_jitter_[which], out);
  };
  // Between menus: the one going zooms away, then the next zooms in (its clip backwards).
  if (busy_) {
    if (next_ == Menu::Start) {
      mode_jitter(transition_frame_++);  // the menu flies apart until the confirm sound ends
      if (!in.confirm_sound_playing) {
        busy_ = false;
        choice_ = Choice::Started;
        current_ = next_;
      }
    } else if (closing_) {
      mode_jitter(transition_frame_--);
      if (transition_frame_ == 0) {
        menu_jitter_[1] = 10;
        busy_ = closing_ = false;
      }
    } else if (current_ == Menu::None) {
      current_ = next_;
      busy_ = false;
    } else {
      drawn_ = true;
      Movie& s = scale_[current_ == Menu::Language ? 0 : 1];
      draw(current_ == Menu::Language ? 0 : 1, s);
      s.clip.advance(1.0 / TitleScreens::kFrameRate);
      if (s.clip.finished()) {
        reversed_ = !reversed_;
        current_ = next_;
        Movie& n = scale_[current_ == Menu::Language ? 0 : 1];
        n.clip.rewind(reversed_);
        n.clip.set_speed(reversed_ ? -1 : 1);
        busy_ = reversed_;
      }
    }
  } else {
    drawn_ = false;
  }

  const bool active = !busy_ && !chosen_ && !skipped;
  auto sounds = [&](bool moved) {
    if (moved) sounds_.push_back({0x06, 5});  // the cursor
    if (in.confirm) sounds_.push_back({0x00, 1});  // confirm
  };
  if (current_ == Menu::Language) {
    if (active) {
      const int from = kLangCells[lang_y_][lang_x_];
      const int dx = in.right ? 1 : in.left ? -1 : 0, dy = in.down ? 1 : in.up ? -1 : 0;
      lang_x_ = std::clamp(lang_x_ + dx, 0, kLangColumns - 1);
      lang_y_ = std::clamp(lang_y_ + dy, 0, kLangRows - 1);
      // Across the wide item: on to the next cell.
      if (dx && kLangCells[lang_y_][lang_x_] == from) lang_x_ = std::clamp(lang_x_ + dx, 0, kLangColumns - 1);
      const int item = kLangCells[lang_y_][lang_x_];
      sounds(item != from && (dx || dy));
      if (in.confirm) choice_ = Choice(int(Choice::German) + item);
      select_[0].clip.seek(item + 0.5);  // its pose for the item
      draw(0, select_[0]);
    }
  } else if (current_ == Menu::Mode) {
    if (active) {
      const int from = mode_index_;
      if (in.cancel) mode_index_ = 2;  // to "back"
      if (in.down) mode_index_ = std::min(mode_index_ + 1, 2);
      if (in.up) mode_index_ = std::max(mode_index_ - 1, 0);
      sounds(mode_index_ != from && (in.up || in.down));
      if (in.confirm) choice_ = Choice(int(Choice::Play) + mode_index_);
      select_[1].clip.seek(mode_index_ + 0.5);
    }
    if (!drawn_) draw(1, select_[1]);
  } else if (current_ == Menu::Start && !drawn_) {
    draw(1, select_[1]);
  }
}

}  // namespace vib
