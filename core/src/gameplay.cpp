#include "vib/gameplay.h"

#include <algorithm>

#include "vib/bytes.h"

namespace vib {

namespace {

const CourseInfo kCourses[kCourseCount] = {
    {2, false, 1, false, false, 0, 0, 0, true, {6000, 4000, 2000}},
    {2, false, 3, false, false, 0, 0, 0, true, {6000, 4000, 2000}},
    {2, false, 5, false, false, 0, 0, 0, true, {5500, 3500, 1500}},
    {1, false, 0, true, false, 0, 0, 0, false, {6500, 4500, 2500}},
    {-1, false, 0, true, true, 0, 0, 0, false, {50000, 35000, 20000}},
    {1, false, 0, true, false, 1.5f, 3.5f, 210, false, {50, 60, 70}},
    {1, false, 0, true, false, 1.5f, 5.0f, 300, false, {55, 65, 75}},
    {1, false, 0, true, false, 1.5f, 6.2f, 372, false, {65, 75, 85}},
    {-1, true, 0, true, false, 0, 0, 0, false, {0, 0, 0}},
};

const int kEndBonusPercent[4] = {30, 20, 15, 0};  // by final form
const Fixed kWindow = Fixed::from(0.2);            // how far before or past Vibri a part can be pressed for
constexpr int kStepSamples = 443;                  // one frame of the original (49.759 Hz) in samples

int first_of(int road) { return road < 4 ? road : road >= 9 ? 2 : road >= 7 ? 1 : 0; }
int second_of(int road) { return road < 4 ? road : road >= 9 ? road - 6 : road >= 7 ? road - 5 : road - 3; }

// Square root in 4.12 fixed point, as used for the speed curve. The original
// calls a PlayStation library routine; this is the exact integer root, which
// may differ from it in the last bit.
std::int32_t sqrt_4_12(std::int32_t a) {
  if (a <= 0) return 0;
  const std::uint64_t n = std::uint64_t(a) << 12;
  std::uint64_t r = 0, bit = std::uint64_t(1) << 40;
  std::uint64_t rem = n;
  while (bit > rem) bit >>= 2;
  while (bit) {
    if (rem >= r + bit) {
      rem -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
    bit >>= 2;
  }
  return static_cast<std::int32_t>(r);
}

}  // namespace

const CourseInfo& course_info(Course c) { return kCourses[static_cast<int>(c)]; }

std::uint8_t road_presses(int road) {
  require(road >= 0 && road <= 9, "road number out of range");
  return static_cast<std::uint8_t>((1 << first_of(road)) | (1 << second_of(road)));
}

Gameplay::Gameplay(float distance, float vibri_position, float beats_per_second)
    : distance_(Fixed::from(distance)), vibri_(Fixed::from(vibri_position)), beats_(Fixed::from(beats_per_second)) {
  speed_ = beats_;
}

void Gameplay::start(const RoundSetup& s) {
  setup_ = s;
  running_ = true;
  start_ = {true, s.start};
  end_ = quiet_ = fade_ = {};
  demo_tracks_ = 2;
  level_ = 3;
  demo_hits_ = 0;
  combo_ = misses_ = 0;
  beat_time_ = 0;
  last_part_time_ = s.origin;
  beat_period_ = Fixed::from(1.0 / (beats_.to_double()));
  new_part_ = game_over_ = false;
  parts_.clear();
  passed_.clear();
  events_.clear();
  incoming_.clear();
  points_ = s.points;
  strays_ = chain_ = last_base_ = last_bonus_ = 0;
  const CourseInfo& info = course_info(s.course);
  auto_ = info.auto_play;
  auto_play_ = info.auto_play || s.demo;
  home_ = static_cast<int>(s.course) < static_cast<int>(Course::OneTrack);
  speed_course_ = s.course == Course::Speed1 || s.course == Course::Speed2 || s.course == Course::Speed3;
  max_speed_ = Fixed::from(info.max_speed);
  range_points_ = 0;
  fixed_period_ = 0;
  if (speed_course_) {
    range_min_ = Fixed::from(info.min_speed);
    range_max_ = max_speed_;
    range_points_ = info.points;
    range_scored_ = 0;
    range_speed_ = range_min_;
    speed_ = range_speed_;
  } else {
    speed_ = beats_;
  }
  ramp_started_ = false;
  no_select_ = camera_break_ = false;
  seconds_ = 0;
  time_up_ = false;
  effect_set_ = false;
  effect_start_ = effect_end_ = {};
  shake_ = Fixed();
  near_ = false;
  volume_ = 256;
}

void Gameplay::end_at(std::int32_t position) { end_ = {true, position}; }

void Gameplay::quiet(std::int32_t position) {
  quiet_ = {true, position};
  if (setup_.demo && --demo_tracks_ == 0) {
    fade_ = quiet_;
    end_ = {true, position + kSampleRate * 8};
  }
}

void Gameplay::add_event(const CourseEvent& e) {
  if (!running_) return;
  switch (e.type) {
    case CourseEvent::Type::Part: {
      if (!start_.over(e.time) || end_.over(e.time)) return;
      const std::int32_t length =
          speed_course_ ? e.length : static_cast<std::int32_t>((std::int64_t(e.scale) * e.length) >> 16);
      if ((length >> 6) <= 0) return;  // shorter than 64 samples cannot be placed
      Part p;
      p.road = e.value;
      p.time = e.time - setup_.video_delay;
      p.rate = Fixed::whole(1).over(length >> 6);
      p.screw = effect_set_ && effect_start_.over(e.time) && !effect_end_.over(e.time);
      const int first = first_of(e.value), second = second_of(e.value);
      for (int i = 1; i < 4; ++i) {
        const int other = (first + i) % 4;
        if (other != first && other != second) {
          p.other = other;
          break;
        }
      }
      parts_.push_back(p);
      break;
    }
    case CourseEvent::Type::Effect:
      if (!speed_course_ && e.value == static_cast<int>(CourseEvent::Effect::Screw)) {
        // A new screw effect takes over only where the running one has ended.
        const Interval s{true, e.time}, en{true, e.time + e.length};
        if (!effect_set_) {
          effect_start_ = s;
          effect_end_ = en;
        } else {
          if (effect_end_.over(s.to)) effect_start_ = s;
          if (effect_end_.over(en.to)) effect_end_ = en;
        }
        effect_set_ = true;
      }
      break;
    case CourseEvent::Type::Camera:
      incoming_.push_back({GameEvent::Type::Camera, 0, e.value});
      break;
  }
}

int Gameplay::stray(int road, int part_road) {
  if (strays_ >= 2) return 0;
  if (strays_ == 0) stray_bonus_ = 0;
  bool again = false;
  for (int i = 0; i < strays_; ++i) again = again || road == stray_roads_[i];
  const bool two = is_pair(part_road);
  const bool match = two ? (road == first_of(part_road) || road == second_of(part_road)) : road == part_road;
  const int result = (again || match) ? 1 : two ? 4 : 2;
  stray_bonus_ += result;
  stray_roads_[strays_++] = road;
  return result;
}

void Gameplay::hit(Part& p) {
  p.done = true;
  emit(GameEvent::Type::Hit, p.road);
  int f = level_ / 3;
  if (!auto_ && !speed_course_ && f != 0) {
    if (++combo_ == 18) {
      --f;
      combo_ = 0;
      level_ = f * 3;
      misses_ = 0;
      emit(GameEvent::Type::FormUp, 0, f);
    }
  }
  const int times = f == 0 ? 2 : 1;  // the best form scores double
  const int base = is_pair(p.road) ? 3 : 1;
  const int bonus = strays_ != 0 ? stray_bonus_ : 0;
  points_ += (base + bonus + chain_) * times;
  last_base_ = base;
  last_bonus_ = bonus;
  chain_ = std::min(chain_ + 1, 10);
  strays_ = 0;
  if (range_points_ != 0) {  // speed courses: speed follows the points, without chain or doubling
    range_scored_ += base + bonus;
    const std::int32_t share = sqrt_4_12((range_scored_ << 12) / range_points_);
    range_speed_ = range_min_ + Fixed::raw(static_cast<std::int32_t>((std::int64_t((range_max_ - range_min_).raw()) * share) >> 12));
  }
}

void Gameplay::miss(Part& p) {
  p.done = p.missed = true;
  emit(GameEvent::Type::Miss, p.road);
  combo_ = 0;
  const int f = level_ / 3;
  if (f == 0) {
    misses_ = 0;
    level_ = 3;
    emit(GameEvent::Type::FormDown, 0, 1);
  } else if (++misses_ == 3) {
    misses_ = 0;
    if (level_ < 11) {
      ++level_;
      emit(GameEvent::Type::LevelUp, 0, level_);
    } else {
      game_over_ = true;
    }
  }
  strays_ = 0;
  chain_ = 0;
}

int Gameplay::bonus() const { return points_ * kEndBonusPercent[level_ / 3] / 100; }

bool Gameplay::preview(const CourseEvent& e, std::int32_t position, RoadPiece& out) const {
  // As add_event() makes the part and update() places it.
  if (!running_ || e.type != CourseEvent::Type::Part || !start_.over(e.time) || end_.over(e.time)) return false;
  const std::int32_t length = speed_course_ ? e.length : static_cast<std::int32_t>((std::int64_t(e.scale) * e.length) >> 16);
  if ((length >> 6) <= 0) return false;
  const Fixed rate = speed_course_ ? speed_.over(kSampleRate >> 6) : Fixed::whole(1).over(length >> 6);
  out = {e.value, rate.times((e.time - setup_.video_delay - position) >> 6) + vibri_, false, false};
  return true;
}

std::vector<RoadPiece> Gameplay::road(bool far) const {
  std::vector<RoadPiece> out;
  if (far)
    for (const Part& p : passed_) out.push_back({p.road, p.position + vibri_, p.missed, p.screw});
  const Fixed limit = distance_ - vibri_ + Fixed::from(3.0);
  for (const Part& p : parts_) {
    if (!far && p.position >= limit) break;  // parts_ is sorted by position
    out.push_back({p.road, p.position + vibri_, p.missed, p.screw});
  }
  return out;
}

Outcome Gameplay::update(std::int32_t position, std::uint8_t pressed, bool move_done) {
  events_ = std::move(incoming_);  // camera changes that arrived since the last update
  incoming_.clear();
  if (!running_) return Outcome::Playing;
  Outcome outcome = Outcome::Playing;
  if (setup_.demo) pressed = 0;
  pressed &= 15;

  // Where every part is now. In a speed course they all come at one speed.
  const Fixed common = speed_course_ ? speed_.over(kSampleRate >> 6) : Fixed();
  for (Part& p : parts_) p.position = (common.raw() != 0 ? common : p.rate).times((p.time - position) >> 6);
  std::stable_sort(parts_.begin(), parts_.end(), [](const Part& a, const Part& b) { return a.position < b.position; });
  const Fixed behind = Fixed::whole(-3) - vibri_;
  const auto gone = std::find_if(parts_.begin(), parts_.end(), [&](const Part& p) { return p.position >= behind; });
  if (keep_passed_ > 0) {
    // Kept for drawing only, moving on as the rest do, until they are far enough behind.
    passed_.insert(passed_.end(), parts_.begin(), gone);
    for (Part& p : passed_) p.position = (common.raw() != 0 ? common : p.rate).times((p.time - position) >> 6);
    passed_.erase(passed_.begin(), std::find_if(passed_.begin(), passed_.end(), [&](const Part& p) { return position - p.time <= keep_passed_; }));
  }
  parts_.erase(parts_.begin(), gone);

  // The parts around Vibri.
  Part *current = nullptr, *passed = nullptr, *next = nullptr, *pending = nullptr;
  auto before = [&](std::int32_t a, std::int32_t b) {
    return std::uint32_t(a - setup_.origin) < std::uint32_t(b - setup_.origin);
  };
  for (Part& p : parts_) {
    if (!current && p.position >= -kWindow && p.position < kWindow) current = &p;
    if (!passed && Fixed::whole(-1) < p.position && p.position <= Fixed()) passed = &p;
    if (!(p.position < Fixed::whole(-1)) && (!next || before(p.time, next->time))) next = &p;
    if (!p.done && (!pending || before(p.time, pending->time))) pending = &p;
  }

  if (pressed != 0 && move_done) {
    if (current) {
      if (!current->done) {
        current->pressed = static_cast<std::uint8_t>(current->pressed | pressed);
        const std::uint8_t wanted = road_presses(current->road);
        if (current->pressed == wanted) hit(*current);
        else if ((current->pressed & ~wanted) && !auto_) miss(*current);
      }
    } else {
      for (int road = 0; road < 4; ++road)
        if (pressed & (1 << road)) {
          emit(GameEvent::Type::Stray, road, pending ? stray(road, pending->road) : 0);
          break;
        }
    }
  }

  // Parts the game plays itself, and parts that got past.
  for (Part& p : parts_) {
    if (auto_play_ && !p.done && !(Fixed() < p.position)) {
      if (setup_.demo) {
        if (demo_hits_ == 0) {
          random_ = next_random(random_);
          demo_hits_ = static_cast<int>((std::uint64_t(random_) * 14) >> 32) + 5;
        }
        if (--demo_hits_ != 0) {
          hit(p);
        } else {
          if (!auto_) miss(p);
          emit(GameEvent::Type::Stray, p.other, 0);
        }
      } else {
        hit(p);
      }
    }
    if (p.position < -kWindow && !p.done && !auto_) miss(p);
  }

  const bool clear_of_parts = !passed && !current;
  if (clear_of_parts) {
    if (end_.over(position)) {
      if (speed_course_) {
        time_up_ = true;
        outcome = Outcome::GameOver;
      } else {
        outcome = Outcome::Clear;
      }
    } else if (game_over_) {
      outcome = Outcome::GameOver;
    }
  }

  const int seconds = (position - setup_.start) / kSampleRate;
  if (!speed_course_) {
    if (!home_) {
      const bool was = no_select_;
      no_select_ = (seconds + setup_.stream_delay_seconds) % 60 >= 52;
      if (no_select_ != was) emit(no_select_ ? GameEvent::Type::SelectOff : GameEvent::Type::SelectOn);
      const bool was_break = camera_break_;
      camera_break_ = seconds % 60 >= 52;
      if (camera_break_ && !was_break) emit(GameEvent::Type::CameraBreak);
    } else if (quiet_.over(position)) {
      quiet_ = {};
      emit(GameEvent::Type::CameraBreak);
    }
  }

  volume_ = 256;
  if (fade_.set && end_.set) {
    const std::int32_t length = end_.to - fade_.to, t = position - fade_.to;
    if (t > 0 && t <= length) volume_ = ((length - t) << 8) / length;
    else if (t > length) volume_ = 0;
  }

  if (speed_course_) {
    // The original grows the speed by 0.2% per video frame; here per frame's worth
    // of music, so the result does not depend on the display's refresh rate.
    if (!ramp_started_) {
      ramp_started_ = true;
      ramp_position_ = position;
    }
    while (position - ramp_position_ >= kStepSamples) {
      ramp_position_ += kStepSamples;
      if (speed_ < range_speed_) speed_ = speed_ + speed_ * Fixed::from(0.002);
    }
    fixed_period_ = static_cast<std::int32_t>((std::int64_t(kSampleRate) << 16) / range_speed_.raw());
    if (clear_of_parts && speed_ >= max_speed_) {
      seconds_ = seconds;
      outcome = Outcome::Clear;
    }
  } else if (next) {
    speed_ = next->rate.times(kSampleRate >> 6);
  }

  if (current && current->time != last_part_time_) {
    new_part_ = true;
    last_part_time_ = current->time;
  }
  const bool passed_done = passed && passed->done;
  const bool was_missed = passed_done && passed->missed;
  near_ = current != nullptr || (passed_done && !passed->missed);
  if (new_part_ && was_missed) {
    new_part_ = false;
    beat_time_ = position;
    beat_period_ = passed->rate;
  }
  shake_ = Fixed::whole(1) - beat_period_.times((position - beat_time_) >> 6);
  if (shake_ < Fixed() || Fixed::whole(1) < shake_) shake_ = Fixed();
  return outcome;
}

}  // namespace vib
