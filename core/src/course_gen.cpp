#include "vib/course_gen.h"

#include <algorithm>

#include "vib/bytes.h"

namespace vib {

std::vector<Block> make_blocks(std::span<const std::int16_t> stereo, int lead) {
  const std::int64_t frames = static_cast<std::int64_t>(stereo.size() / 2);  // stereo sample pairs at 44.1 kHz
  const std::int64_t count = std::max<std::int64_t>(0, (frames - lead) / (kBlockSize * 2));  // 256 pairs make one block
  std::vector<Block> blocks(static_cast<std::size_t>(count));
  for (std::int64_t b = 0; b < count; ++b)
    for (int i = 0; i < kBlockSize; ++i) {
      const std::int64_t pair = b * kBlockSize * 2 + std::int64_t(i) * 2 + lead;
      if (pair < 0 || pair >= frames) continue;  // silence before the music
      const int sum = int(stereo[std::size_t(pair) * 2]) + int(stereo[std::size_t(pair) * 2 + 1]);
      blocks[std::size_t(b)][std::size_t(i)] = static_cast<std::int16_t>(sum / 2);  // C++ division truncates toward zero
    }
  return blocks;
}

CourseTiming CourseTiming::like_original(std::uint32_t seed) {
  // Seven recorded rounds of the original began between 204 samples late and 462
  // early (44.1 kHz), with either decimation phase. Drawn evenly from a slightly wider range.
  constexpr int kEarliest = -240, kLatest = 500;
  CourseTiming t;
  std::uint32_t state = next_random(seed ^ 0x5EED1E55u);
  t.lead = kEarliest + static_cast<int>((std::uint64_t(state) * std::uint64_t(kLatest - kEarliest + 1)) >> 32);
  state = next_random(state);
  t.frame_period_q16 = CourseGenerator::kOriginalFramePeriodQ16;
  t.frame_phase_q16 = static_cast<std::int64_t>((std::uint64_t(state) * std::uint64_t(t.frame_period_q16)) >> 32);
  return t;
}

namespace {

constexpr std::int32_t kOne = 65536;       // 1.0 in 16.16
constexpr std::int32_t kMinLevel = 999999;  // quieter than this never counts as an onset

// level / background > other_level / other_background, without dividing
bool ratio_above(std::int64_t level, std::int64_t background, std::int64_t other_level, std::int64_t other_background) {
  return other_level * background < level * other_background;
}

std::int32_t fixed_from(double v) { return static_cast<std::int32_t>(v * 65536.0); }

// The entry in force at `time`: the last one starting at or before it (the first if none does).
template <typename T>
std::size_t find_segment(const std::vector<T>& list, std::int32_t time) {
  std::size_t lo = 0, hi = list.size();
  while (hi - lo > 1) {
    const std::size_t mid = (lo + hi) / 2;
    if (list[mid].start <= time) lo = mid;
    else hi = mid;
  }
  return lo;
}

}  // namespace

struct CourseGenerator::Selector {
  // Either a fixed pattern played round and round, or a seeded weighted draw.
  const FixedPattern* pattern = nullptr;
  std::size_t index = 0;
  const DistributionPattern* odds = nullptr;
  std::uint32_t seed = 0;

  int next() {
    if (pattern) {
      const int road = static_cast<int>(pattern->sequence[index++]);
      if (index >= pattern->sequence.size()) index = 0;
      return road;
    }
    seed = next_random(seed);
    const auto pick = static_cast<std::int32_t>((std::uint64_t(std::uint32_t(odds->max_prob)) * seed) >> 32) + 1;
    for (int road = 0; road < kObstacleKinds; ++road)
      if (odds->cumulative[std::size_t(road)] >= pick) return road;
    return kObstacleKinds - 1;  // malformed weights; the original asserts here
  }
};

struct CourseGenerator::Tempo {
  std::int32_t start = 0;
  std::uint32_t length = 0;
  std::int32_t pattern = 0;
  std::uint32_t seed = 0;
  int turn = 0;

  bool over(std::int32_t time) const { return std::uint32_t(time - start) > length; }

  std::int32_t next(std::int32_t time) {
    static const std::int32_t longer = fixed_from(1.25), shorter = fixed_from(0.5), middle = fixed_from(1 / 1.5);
    if (over(time)) return kOne;
    auto below_ten = [&] {
      seed = next_random(seed);
      return static_cast<int>((std::uint64_t(seed) * 10) >> 32);
    };
    switch (pattern) {
      case 2:
        return below_ten() < 5 ? longer : shorter;
      case 3: {
        const int r = below_ten();
        return r < 3 ? longer : r >= 7 ? shorter : middle;
      }
      case 22:
        if (++turn >= 2) turn = 0;
        return turn == 0 ? longer : shorter;
      default:
        if (++turn >= 3) turn = 0;
        return turn == 0 ? longer : turn == 1 ? middle : shorter;
    }
  }
};

CourseGenerator::CourseGenerator(const CourseScript& script, int cue_interval_blocks)
    : script_(script), cue_interval_(std::max(1, cue_interval_blocks)) {
  fixed_selectors_.resize(script.fixed.size());
  distribution_selectors_.resize(script.distributions.size());
}

CourseGenerator::~CourseGenerator() = default;

void CourseGenerator::start_round(int round, std::int32_t start_time, std::uint32_t script_seed) {
  require(round >= 1 && std::size_t(round) <= script_.round_tracks.size(), "no such round in the course script");
  for (auto& s : fixed_selectors_) s.reset();
  for (auto& s : distribution_selectors_) s.reset();
  tempo_.reset();
  round_ = round;
  script_seed_ = script_seed;
  start_time_ = start_time;
  offset_ = 0;
  playing_ = true;
  effect_index_ = 0;
  track_ = &script_.tracks[script_.round_tracks[std::size_t(round - 1)]];
  require(!track_->control.empty() && !track_->patterns.empty(), "course track has no segments");
  end_ = std::max(track_->patterns.back().start, track_->control.back().start);
  if (!track_->events.empty()) end_ = std::max(end_, track_->events.back().start);
  now_ = offset_;

  // Catch up: effects still running at the starting point, and the latest camera.
  const EventSegment* camera = nullptr;
  for (; effect_index_ < track_->events.size(); ++effect_index_) {
    const EventSegment& e = track_->events[effect_index_];
    if (now_ < e.start) break;
    send_effect(e, now_ < e.tempo_length + e.start, now_ < e.reverse_length + e.start, now_ < e.screw_length + e.start, false);
    if (e.camera >= 0) camera = &e;
  }
  if (camera) send_effect(*camera, false, false, false, true);

  // The detector starts afresh with the track.
  pending_.reset();
  candidate_.reset();
  window_start_ = start_time;
  window_ = 0;
  for (auto& b : history_) b.fill(0);
  short_level_ = long_level_ = 0;
  blocks_since_cue_ = 0;
  last_frame_ = frame_period_ > 0 ? frame_phase_ / frame_period_ : -1;
  current_cue_ = next_cue(start_time);
}

void CourseGenerator::send_effect(const EventSegment& e, bool tempo, bool reverse, bool screw, bool camera) {
  const std::int32_t time = start_time_ + e.start - offset_;
  auto effect = [&](CourseEvent::Effect kind, std::int32_t length) {
    CourseEvent ev;
    ev.type = CourseEvent::Type::Effect;
    ev.time = time;
    ev.value = static_cast<std::int32_t>(kind);
    ev.length = length;
    events_.push_back(ev);
  };
  if (tempo && e.tempo_length > 0) {
    tempo_ = std::make_shared<Tempo>();
    tempo_->start = time;
    tempo_->length = std::uint32_t(e.tempo_length);
    tempo_->pattern = e.tempo_pattern;
    tempo_->seed = std::uint32_t(e.start);
    effect(CourseEvent::Effect::Tempo, e.tempo_length);
  }
  if (reverse && e.reverse_length > 0) effect(CourseEvent::Effect::Reverse, e.reverse_length);
  if (screw && e.screw_length > 0) effect(CourseEvent::Effect::Screw, e.screw_length);
  if (camera && e.camera >= 0) {
    CourseEvent ev;
    ev.type = CourseEvent::Type::Camera;
    ev.time = time;
    ev.value = e.camera;
    events_.push_back(ev);
  }
}

void CourseGenerator::send_effects() {
  while (effect_index_ < track_->events.size()) {
    const EventSegment& e = track_->events[effect_index_];
    if (now_ < e.start) break;
    send_effect(e, true, true, true, true);
    ++effect_index_;
  }
}

CourseGenerator::Cue CourseGenerator::next_cue(std::int32_t time) {
  if (!playing_) return Cue{};
  now_ = std::min(std::max(0, time - start_time_) + offset_, end_);
  send_effects();

  const PatternSegment& part = track_->patterns[find_segment(track_->patterns, now_)];
  std::shared_ptr<Selector> selector;
  if (part.type == PatternType::Fixed) {
    auto& slot = fixed_selectors_[std::size_t(part.index)];
    if (!slot) {
      slot = std::make_shared<Selector>();
      slot->pattern = &script_.fixed[std::size_t(part.index)];
    }
    selector = slot;
  } else if (part.type == PatternType::Distribution) {
    auto& slot = distribution_selectors_[std::size_t(part.index)];
    if (!slot) {
      slot = std::make_shared<Selector>();
      slot->odds = &script_.distributions[std::size_t(part.index)];
      slot->seed = std::uint32_t(slot->odds->seed) * (100u * script_seed_ + std::uint32_t(round_));
    }
    selector = slot;
  }

  const std::size_t si = find_segment(track_->control, now_);
  const ControlSegment& speed = track_->control[si];
  Cue cue;
  if (si == track_->control.size() - 1) {  // the last entry does not ramp
    cue.period = speed.period;
    cue.select_period = speed.select_period;
  } else {
    const std::int32_t since = now_ - speed.start;
    cue.period = speed.period + ((speed.period_slope * since) >> 12);
    cue.select_period = speed.select_period + ((speed.select_period_slope * since) >> 12);
  }
  cue.period = std::max(1, cue.period);
  cue.select_period = std::max(1, cue.select_period);
  cue.selector = std::move(selector);
  cue.tempo = tempo_;
  return cue;
}

void CourseGenerator::place() {
  CourseEvent ev;
  ev.type = CourseEvent::Type::Part;
  ev.time = pending_->time;
  ev.value = pending_->cue.selector->next();
  ev.length = pending_->cue.period;
  ev.scale = pending_scale_;
  events_.push_back(ev);
  pending_.reset();
}

void CourseGenerator::take_candidate() {
  pending_ = std::move(candidate_);
  candidate_.reset();
  pending_scale_ = pending_->cue.tempo ? pending_->cue.tempo->next(pending_->time) : kOne;
  // 32-bit product, as in the original: it wraps for very long parts under a tempo effect.
  pending_length_ = static_cast<std::int32_t>(std::uint32_t(pending_scale_) * std::uint32_t(pending_->cue.period)) >> 16;
}

void CourseGenerator::end_window(std::int32_t time) {
  if (candidate_) {
    if (pending_) {
      if (candidate_->time - pending_->time > 2 * pending_length_) {
        place();  // far enough apart: the earlier one stands
        take_candidate();
      } else if (ratio_above(candidate_->level, candidate_->background, static_cast<std::int32_t>(3u * std::uint32_t(pending_->level)) / 2,
                             pending_->background)) {
        take_candidate();  // too close, but much louder: it replaces the earlier one
      } else {
        candidate_.reset();
      }
    } else {
      take_candidate();
    }
  }
  if (pending_ && time - pending_->time > 2 * pending_length_) place();
}

void CourseGenerator::process(std::int32_t time, const Block& block) {
  if (frame_period_ > 0) {
    const std::int64_t frame = (std::int64_t(time - start_time_) * 65536 + frame_phase_) / frame_period_;
    if (frame != last_frame_) {
      current_cue_ = next_cue(time);
      last_frame_ = frame;
    }
  } else {
    if (blocks_since_cue_ >= cue_interval_) {
      current_cue_ = next_cue(time);
      blocks_since_cue_ = 0;
    }
    ++blocks_since_cue_;
  }
  const Cue& cue = current_cue_;

  history_[std::size_t(history_index_)] = block;
  history_index_ = (history_index_ + 1) % kHistory;
  const Block& recent = history_[std::size_t((history_index_ + kHistory - kRecent) % kHistory)];
  const Block& oldest = history_[std::size_t(history_index_)];

  std::int32_t short_level = short_level_, long_level = long_level_;
  for (int i = 0; i < kBlockSize;) {
    const std::int32_t now = time + i;
    if (std::uint32_t(now - window_start_) >= window_) {
      window_start_ += std::int32_t(window_);
      window_ = std::uint32_t(candidate_ ? candidate_->cue.select_period : cue.select_period);
      end_window(window_start_);
      while (std::uint32_t(now - window_start_) >= window_) {
        window_start_ += std::int32_t(window_);
        end_window(window_start_);
      }
    }
    const int end = static_cast<int>(std::min<std::int64_t>(kBlockSize, std::int64_t(window_start_) + window_ - time));
    int best = -1;
    std::int32_t best_short = 0, best_long = 1;
    for (; i < end; ++i) {
      const std::int32_t s = block[std::size_t(i)], r = recent[std::size_t(i)], o = oldest[std::size_t(i)];
      const std::int32_t power = s * s;
      short_level += (power >> 9) - ((r * r) >> 9);
      long_level += (power >> 12) - ((o * o) >> 12);
      if (short_level > kMinLevel && ratio_above(short_level, long_level, best_short, best_long)) {
        best = i;
        best_short = short_level;
        best_long = long_level;
      }
    }
    if (cue.selector && best >= 0) {
      if (candidate_ && !ratio_above(best_short, best_long, candidate_->level, candidate_->background)) continue;
      Pick pick;
      pick.cue = cue;
      pick.time = time + best;
      pick.level = best_short;
      pick.background = best_long;
      candidate_ = std::move(pick);
    }
  }
  short_level_ = short_level;
  long_level_ = long_level;
}

}  // namespace vib
