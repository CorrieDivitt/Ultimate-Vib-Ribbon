#include "vib/title_intro.h"
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

TitleIntro::TitleIntro(const Pak& pak) {
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
  layers_.clear();
  if (time_ >= game_frames(kTitleTime)) skippable_ = false;
  if (skippable_ && confirm) seek_title();
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
    sounds_.push_back(sound_cues_[std::size_t(sound_cue_++)].sound);
  ++sound_frame_;

  if (time_ < kTimeLimit) ++time_;
  layers_.push_back(std::move(layer));
}

void TitleIntro::leave() { leaving_ = true; }

bool TitleIntro::intro_over() const { return time_ > game_frames(kTitleTime); }

bool TitleIntro::over() const { return leaving_ && leave_frames_ >= kLeaveFrames; }

std::vector<int> TitleIntro::take_sounds() {
  std::vector<int> s;
  s.swap(sounds_);
  return s;
}

}  // namespace vib
