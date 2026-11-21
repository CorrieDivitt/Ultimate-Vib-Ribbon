#pragma once
// Ours (not the original's): sound calibration. A steady click track plays as the
// music; the player presses on each click; each press is placed by the same clock play
// judges hits with (what is being heard, after the estimated output latency and the
// Sound timing setting), so the average offset is what Sound timing should change by.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace vib {

class TapCalibration {
 public:
  static constexpr int kRate = 44100;           // frames a second (the music's)
  static constexpr int kLeadFrames = kRate * 2;  // silence before the first click
  static constexpr int kPeriod = kRate / 2;      // 120 clicks a minute
  static constexpr int kWarmUp = 4;              // clicks to get the beat before presses count
  static constexpr int kWanted = 16;             // presses that make a result
  static constexpr double kReachMs = 220.0;      // a press further than this from any click is ignored
  static constexpr int kClicks = kWarmUp + kWanted * 3;  // enough for a few misses

  // Where click k starts (frames from the start of the track).
  static std::int64_t click_frame(int k) { return kLeadFrames + std::int64_t(k) * kPeriod; }
  // The track's length (frames): the clicks and a second after the last.
  static std::int64_t length_frames() { return click_frame(kClicks - 1) + kRate; }

  // A press at `heard` (frames of the track being heard). Returns its offset in ms from
  // the nearest click (- early, + late) if it counts.
  std::optional<double> press(double heard) {
    if (done()) return std::nullopt;
    const double k = std::round((heard - double(kLeadFrames)) / double(kPeriod));
    if (k < kWarmUp || k >= kClicks) return std::nullopt;
    const int click = int(k);
    if (click == last_click_) return std::nullopt;  // (one press a click)
    const double ms = (heard - double(click_frame(click))) * 1000.0 / kRate;
    if (std::abs(ms) > kReachMs) return std::nullopt;
    last_click_ = click;
    offsets_.push_back(ms);
    return ms;
  }
  int count() const { return int(offsets_.size()); }
  bool done() const { return count() >= kWanted; }
  // The track has run out (the result is what was gathered, if anything).
  static bool over(double heard) { return heard > double(length_frames()); }

  // The middle of the presses (median): steadier than the mean against a stray one.
  double average_ms() const {
    if (offsets_.empty()) return 0;
    std::vector<double> v = offsets_;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
  }
  // How far presses fall from that middle, on average (ms): how steady the player was.
  double spread_ms() const {
    if (offsets_.empty()) return 0;
    const double m = average_ms();
    double sum = 0;
    for (const double o : offsets_) sum += std::abs(o - m);
    return sum / double(offsets_.size());
  }
  // The Sound timing to use: late presses (the sound reached the player later than the
  // game thought) need more latency allowed for. In 5 ms steps, as the setting.
  double suggested_ms(double current_ms) const {
    const double v = current_ms + average_ms();
    return std::clamp(std::round(v / 5.0) * 5.0, -500.0, 500.0);
  }

 private:
  std::vector<double> offsets_;
  int last_click_ = -1;
};

}  // namespace vib
