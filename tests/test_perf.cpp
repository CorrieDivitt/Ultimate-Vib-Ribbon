// The performance overlay's numbers (task 4.6): frame statistics and the rows' text.
#include <cmath>
#include <string>

#include "test.h"
#include "vib/perf_overlay.h"

using namespace vib;

namespace {

constexpr std::uint64_t kMs = 1'000'000;

// `n` frames `interval` ms apart, ending at `end` ms; the display says its period.
std::uint64_t frames(FrameStats& st, int n, double interval, std::uint64_t start_ms, double period = 0) {
  std::uint64_t t = start_ms * kMs;
  for (int i = 0; i < n; ++i) {
    t += std::uint64_t(interval * double(kMs));
    FrameSample f;
    f.ns = t, f.interval_ms = interval, f.period_ms = period, f.update_ms = 0.5, f.build_ms = 0.25, f.lines = 100 + i;
    st.add(f);
  }
  return t;
}

bool in_font(const std::string& s) {
  for (const char c : s) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == ',' ||
                    c == '-' || c == '(' || c == ')';
    if (!ok) return false;
  }
  return true;
}

}  // namespace

TEST(frame_stats_average_worst_and_rate_from_the_displays_period) {
  FrameStats st;
  std::uint64_t t = frames(st, 79, 12.5, 0, 12.5);
  // One slow frame: 30 ms (more than 1.5 periods): late.
  FrameSample slow;
  slow.ns = t += 30 * kMs, slow.interval_ms = 30, slow.period_ms = 12.5;
  st.add(slow);
  const FrameStats::Summary s = st.summary(t);
  CHECK(std::abs(s.rate_hz - 80.0) < 1e-9);
  CHECK(s.worst_ms == 30.0);
  CHECK(s.late == 1);
  CHECK(s.frames >= 79);
  CHECK(std::abs(s.update_ms - 0.5 * double(s.frames - 1) / double(s.frames)) < 1e-9);  // the slow frame had none
  CHECK(s.average_ms > 12.5 && s.average_ms < 13.0);
}

TEST(frame_stats_late_frames_count_for_ten_seconds_and_averages_for_one) {
  FrameStats st;
  std::uint64_t t = frames(st, 1, 40, 0, 12.5);  // late
  t = frames(st, 400, 12.5, t / kMs, 12.5);      // 5 s of good frames
  CHECK(st.summary(t).late == 1 && st.summary(t).worst_ms == 12.5);
  t = frames(st, 480, 12.5, t / kMs, 12.5);  // 6 s more: the late one is over 10 s ago
  CHECK(st.summary(t).late == 0);
  CHECK(st.intervals(t).size() == 81);  // (both ends of the second)
}

TEST(frame_stats_without_a_period_take_the_median_interval) {
  FrameStats st;
  std::uint64_t t = frames(st, 59, 1000.0 / 60.0, 0);
  FrameSample hitch;
  hitch.ns = t += 50 * kMs, hitch.interval_ms = 50;
  st.add(hitch);
  const FrameStats::Summary s = st.summary(t);
  CHECK(std::abs(s.target_ms - 1000.0 / 60.0) < 1e-9);
  CHECK(std::abs(s.rate_hz - 60.0) < 1e-6);
  CHECK(s.late == 1);
}

TEST(frame_stats_sound_clock_drift_and_presses) {
  FrameStats st;
  // A sound clock running 1 ms a second fast, sampled every 10 ms for a second.
  for (int i = 0; i <= 100; ++i) st.add_audio(std::uint64_t(i) * 10 * kMs, double(i) * 0.010 * 1.001);
  FrameStats::Summary s = st.summary(1000 * kMs);
  CHECK(s.audio && std::abs(s.drift_ms - 1.0) < 1e-6);
  CHECK(!st.summary(2000 * kMs).audio);  // stale: no music now
  st.add_audio(1010 * kMs, 0.0);         // started again: the history goes
  CHECK(!st.summary(1010 * kMs).audio);
  for (int i = 0; i < 7; ++i) st.add_press(double(i) - 3.0);
  s = st.summary(1010 * kMs);
  CHECK(s.presses.size() == FrameStats::kPresses && s.presses.front() == -1.0 && s.presses.back() == 3.0);
}

TEST(perf_rows_use_only_the_fonts_characters) {
  FrameStats st;
  const std::uint64_t t = frames(st, 80, 12.5, 0, 12.5);
  st.add_press(-12.4);
  st.add_press(-0.2);
  st.add_press(7.0);
  FrameStats::Summary s = st.summary(t);
  CHECK(perf_rows(s, 0, 40).empty());
  const auto one = perf_rows(s, 1, 40);
  CHECK(one.size() == 1 && one[0] == "80 Hz   12.5 ms   worst 12.5   late 0");
  const auto all = perf_rows(s, 2, 40);
  CHECK(all.size() == 5);
  for (const std::string& r : all) CHECK(in_font(r));
  CHECK(all[4] == "Presses ms (- early)   -12   0   7");
  CHECK(all[3] == "Sound   latency 40 ms   (no music)");
  CHECK(perf_rows(s, 2, -1)[3] == "Sound   (no music)");
}
