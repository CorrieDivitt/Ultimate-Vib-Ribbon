#include "vib/kiosk_movie.h"

#include <algorithm>
#include <string>

#include "vib/course_gen.h"   // next_random
#include "vib/title.h"        // TitleScreens::kFrameRate
#include "vib/title_intro.h"  // TitleIntro::game_frames

namespace vib {
namespace {

constexpr double kTick = 1.0 / TitleScreens::kFrameRate;
constexpr int kDefaultJitter = 6;

// When the parts and their second shots begin, in 30ths of a second.
constexpr int kShot2[3] = {480, 1496, 2515};
constexpr int kPart[3] = {0, 938, 2104};
constexpr int kEnd = 3025;

int frames(int thirtieths) { return TitleIntro::game_frames(thirtieths); }

}  // namespace

bool KioskMovie::Element::done() const {
  // Its last clip has played to the end.
  return cue == int(movies.size()) - 1 && movies.back().clip.finished();
}

KioskMovie::KioskMovie(const Pak& pak, int language) {
  auto file = [&](const std::string& name) { return pak.view(localized(pak, "MOVIE/" + name, language)); };
  auto element = [&](std::vector<std::string> models, std::vector<std::pair<std::string, std::pair<int, int>>> cues,
                     std::vector<JitterCue> jitter) {
    Element e;
    for (const std::string& m : models) e.models.push_back(parse_tmd(file(m)));
    for (const auto& [name, at] : cues) {
      Movie mv{at.second, frames(at.first), parse_anm(file(name)), {}};
      mv.clip = Clip(int(mv.anim.frames.size()), mv.anim.rate, false);
      e.movies.push_back(std::move(mv));
    }
    e.jitter = std::move(jitter);
    e.model_jitter.assign(e.models.size(), kDefaultJitter);
    elements_.push_back(std::move(e));
  };
  using J = Jitter;
  // Vibri.
  element({"KIOSK2/MODEL.TMD"},
          {{"KIOSK1/K1_V01.ANM", {kPart[0], 0}}, {"KIOSK1/K1_V02.ANM", {kShot2[0], 0}}, {"KIOSK2/K2_V01.ANM", {kPart[1], 0}},
           {"KIOSK2/K2_V02.ANM", {kShot2[1], 0}}, {"KIOSK3/K3_V01.ANM", {kPart[2], 0}}, {"KIOSK3/K3_V02.ANM", {kShot2[2], 0}}},
          {{0, 0, 20, 1600, J::Falling}, {0, 480, 120, 40, J::Constant}, {0, kPart[1] - 21, 20, 1600, J::Rising},
           {0, kPart[1], 20, 1600, J::Falling}, {0, kPart[2] - 21, 20, 600, J::Rising}, {0, kPart[2], 20, 1600, J::Falling},
           {0, kEnd - 21, 40, 1000, J::Rising}});
  // Her road, one per part.
  element({"KIOSK1/K1_RD.TMD", "KIOSK2/K2_RD.TMD", "KIOSK3/K3_RD.TMD"},
          {{"KIOSK1/K1_R01.ANM", {kPart[0], 0}}, {"KIOSK1/K1_R02.ANM", {kShot2[0], 0}}, {"KIOSK2/K2_R01.ANM", {kPart[1], 1}},
           {"KIOSK2/K2_R02.ANM", {kShot2[1], 1}}, {"KIOSK3/K3_R01.ANM", {kPart[2], 2}}, {"KIOSK3/K3_R02.ANM", {kShot2[2], 2}}},
          {{0, 0, 20, 1600, J::Falling}, {0, 480, 120, 40, J::Constant}, {0, 600, 30, 60, J::Falling},
           {0, 630, 30, 60, J::Falling}, {0, kPart[1] - 21, 20, 1600, J::Rising}, {1, kPart[1], 20, 1600, J::Falling},
           {1, kPart[2] - 21, 20, 600, J::Rising}, {2, kPart[2], 20, 1600, J::Falling}, {2, kEnd - 21, 40, 1000, J::Rising}});
  // The road's pieces (with words on them: one set per language).
  element({"KIOSK1/K1_PT.TMD", "KIOSK2/K2_PT.TMD", "KIOSK3/K3_PT.TMD"},
          {{"KIOSK1/K1_P01.ANM", {kPart[0], 0}}, {"KIOSK1/K1_P02.ANM", {kShot2[0], 0}}, {"KIOSK2/K2_P01.ANM", {kPart[1], 1}},
           {"KIOSK2/K2_P02.ANM", {kShot2[1], 1}}, {"KIOSK3/K3_P01.ANM", {kPart[2], 2}}, {"KIOSK3/K3_P02.ANM", {kShot2[2], 2}}},
          {{0, kPart[1] - 21, 20, 1600, J::Rising}, {1, kPart[1], 20, 1600, J::Falling}, {1, kPart[2] - 21, 20, 600, J::Rising},
           {2, kPart[2], 20, 1600, J::Falling}, {2, 3030, 20, 1000, J::Rising}});
  // The frog and the snake, in the first part's second shot.
  element({"KIOSK1/FROG.TMD"}, {{"KIOSK1/K1_F02.ANM", {kShot2[0], 0}}}, {{0, 600, 30, 60, J::Falling}});
  element({"KIOSK1/SNAKE.TMD"}, {{"KIOSK1/K1_S02.ANM", {kShot2[0], 0}}}, {{0, 630, 30, 60, J::Falling}});
  // The camera: two shots a part.
  const char* shots[6] = {"KIOSK1/K1_01.ANC", "KIOSK1/K1_02.ANC", "KIOSK2/K2_01.ANC",
                          "KIOSK2/K2_02.ANC", "KIOSK3/K3_01.ANC", "KIOSK3/K3_02.ANC"};
  const int at[6] = {kPart[0], kShot2[0], kPart[1], kShot2[1], kPart[2], kShot2[2]};
  for (int i = 0; i < 6; ++i) {
    Shot s{frames(at[i]), parse_anc(file(shots[i])), {}};
    s.clip = Clip(int(s.anim.keys.size()), s.anim.rate, false);
    shots_.push_back(std::move(s));
  }
  start();
}

void KioskMovie::start() {
  for (Element& e : elements_) {
    e.frame = e.cue = 0;
    e.scatter = 20;
    e.scatter_step = -1;
    for (Movie& m : e.movies) m.clip.rewind();
    std::fill(e.model_jitter.begin(), e.model_jitter.end(), kDefaultJitter);
  }
  for (Shot& s : shots_) s.clip.rewind();
  shot_frame_ = shot_ = 0;
  music_started_ = leaving_ = finished_ = false;
  sounds_.clear();
  layers_.clear();
}

void KioskMovie::update_element(Element& e, std::vector<SceneLine>& out) {
  if (e.frame == 0) {
    e.scatter = 0;
    e.scatter_step = 1;
  }
  // The jitter cues, in order: each sets its model to the jitter worked out so far.
  int jitter = kDefaultJitter;
  for (const JitterCue& c : e.jitter) {
    const int start = frames(c.time), end = frames(c.time + c.length);
    if (e.frame >= start && e.frame <= end) {
      if (c.kind == Jitter::Constant) {
        jitter = c.amount;
      } else {
        const int t = c.kind == Jitter::Falling ? end - e.frame : e.frame - start;
        jitter = end > start ? t * c.amount / (end - start) : 0;
      }
    }
    e.model_jitter[std::size_t(c.model)] = jitter;
  }
  // Leaving: the lines scatter more and more.
  if (leaving_ || e.scatter != 0) {
    e.scatter += e.scatter_step;
    const int s = e.scatter * 60 / 50;
    for (int& j : e.model_jitter) j = jitter + s * s * 8;
    if (!leaving_ && e.scatter == 0) e.scatter_step = 1;
  }
  if (e.movies.empty()) return;
  const bool draw = e.frame >= e.movies[std::size_t(e.cue)].frame;
  if (e.cue + 1 < int(e.movies.size()) && e.movies[std::size_t(e.cue) + 1].frame == e.frame) ++e.cue;
  if (e.done()) return;  // its last clip over: no longer drawn
  if (draw) {
    Movie& m = e.movies[std::size_t(e.cue)];
    lines_.animated(e.models[std::size_t(m.model)], m.anim, m.clip.pose(), {}, e.model_jitter[std::size_t(m.model)], out);
    m.clip.advance(kTick);
  }
  ++e.frame;
}

void KioskMovie::tick(const Input& in) {
  layers_.clear();
  if (finished_) return;
  if (!music_started_) {
    sounds_.push_back({0x100, 0});  // the music, from the start
    music_started_ = true;
  }
  // A button: the confirm sound, the lines scatter, and the movie is over once it has sounded.
  if (!leaving_ && in.confirm) {
    leaving_ = true;
    sounds_.push_back({0x00, 1});
  }
  if (leaving_ && !in.confirm_sound_playing && !in.confirm) {
    finished_ = true;
    return;
  }

  lines_.seed(seed_);
  seed_ = next_random(seed_);
  Hud::Layer layer;
  // The camera: each shot from its time on, the last holding its end.
  if (shot_ + 1 < int(shots_.size()) && shots_[std::size_t(shot_) + 1].frame == shot_frame_) ++shot_;
  Shot& shot = shots_[std::size_t(shot_)];
  if (shot_frame_ >= shot.frame) {
    view_ = camera_at(shot.anim, shot.clip.pose());
    shot.clip.advance(kTick);
  }
  ++shot_frame_;
  layer.view = view_;
  lines_.set_view(layer.view);
  for (Element& e : elements_) update_element(e, layer.lines);
  layers_.push_back(std::move(layer));

  // Over when every part has played out and the music has ended.
  bool all = shot_ == int(shots_.size()) - 1 && shots_.back().clip.finished();
  for (const Element& e : elements_) all = all && e.done();
  if (all && !in.music_playing && music_started_ && sounds_.empty()) finished_ = true;
}

std::vector<KioskMovie::Sound> KioskMovie::take_sounds() {
  std::vector<Sound> s;
  s.swap(sounds_);
  return s;
}

}  // namespace vib
