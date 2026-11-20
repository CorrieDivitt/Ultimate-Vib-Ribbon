#include "vib/perf_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vib {

namespace {

constexpr double kTau = 6.28318530717958647692;

double ms_between(std::uint64_t a, std::uint64_t b) { return double(b - a) * 1e-6; }

}  // namespace

void FrameStats::add(const FrameSample& f) {
  frames_.push_back(f);
  while (!frames_.empty() && ms_between(frames_.front().ns, f.ns) > kLateSeconds * 1000.0) frames_.pop_front();
}

void FrameStats::add_press(double offset_ms) {
  presses_.push_back(offset_ms);
  while (presses_.size() > kPresses) presses_.pop_front();
}

void FrameStats::add_audio(std::uint64_t ns, double heard_seconds) {
  if (!audio_.empty() && (ns < audio_.back().ns || heard_seconds < audio_.back().heard)) audio_.clear();  // started again
  audio_.push_back({ns, heard_seconds});
  while (audio_.size() > 1 && ms_between(audio_.front().ns, ns) > kWindowSeconds * 1000.0) audio_.pop_front();
}

std::vector<double> FrameStats::intervals(std::uint64_t now_ns, double seconds) const {
  std::vector<double> out;
  for (const FrameSample& f : frames_)
    if (f.ns <= now_ns && ms_between(f.ns, now_ns) <= seconds * 1000.0 && f.interval_ms > 0) out.push_back(f.interval_ms);
  return out;
}

FrameStats::Summary FrameStats::summary(std::uint64_t now_ns) const {
  Summary s;
  std::vector<double> recent;
  double update = 0, build = 0, submit = 0, wait = 0;
  double period = 0;
  for (const FrameSample& f : frames_) {
    if (f.ns > now_ns || ms_between(f.ns, now_ns) > kWindowSeconds * 1000.0) continue;
    ++s.frames;
    if (f.interval_ms > 0) recent.push_back(f.interval_ms);
    update += f.update_ms, build += f.build_ms, submit += f.submit_ms, wait += f.wait_ms;
    if (f.period_ms > 0) period = f.period_ms;
  }
  if (!frames_.empty()) s.lines = frames_.back().lines;
  if (s.frames > 0) {
    const double n = double(s.frames);
    s.update_ms = update / n, s.build_ms = build / n, s.submit_ms = submit / n, s.wait_ms = wait / n;
  }
  if (!recent.empty()) {
    double sum = 0;
    for (const double v : recent) sum += v, s.worst_ms = std::max(s.worst_ms, v);
    s.average_ms = sum / double(recent.size());
    if (period <= 0) {  // the display does not say: the median interval stands for it
      std::vector<double> sorted = recent;
      std::nth_element(sorted.begin(), sorted.begin() + std::ptrdiff_t(sorted.size() / 2), sorted.end());
      period = sorted[sorted.size() / 2];
    }
  }
  s.target_ms = period;
  s.rate_hz = period > 0 ? 1000.0 / period : double(s.frames);
  if (period > 0)
    for (const FrameSample& f : frames_)
      if (f.ns <= now_ns && ms_between(f.ns, now_ns) <= kLateSeconds * 1000.0 && f.interval_ms > kLateFactor * period) ++s.late;
  // The sound clock against the steady clock, over the samples kept.
  if (audio_.size() >= 2) {
    const Audio &a = audio_.front(), &b = audio_.back();
    const double wall = double(b.ns - a.ns) * 1e-9;
    if (wall >= kWindowSeconds * 0.5 && ms_between(b.ns, now_ns) < 250.0) {
      s.audio = true;
      s.drift_ms = ((b.heard - a.heard) - wall) / wall * 1000.0;
    }
  }
  s.presses.assign(presses_.begin(), presses_.end());
  return s;
}

std::vector<std::string> perf_rows(const FrameStats::Summary& s, int level, double latency_ms) {
  std::vector<std::string> rows;
  if (level <= 0) return rows;
  char buf[160];
  // (The font has no colon, slash, plus or percent sign: spaces and words instead.)
  std::snprintf(buf, sizeof buf, "%.0f Hz   %.1f ms   worst %.1f   late %d", s.rate_hz, s.average_ms, s.worst_ms, s.late);
  rows.push_back(buf);
  if (level < 2) return rows;
  std::snprintf(buf, sizeof buf, "CPU ms   game %.2f   lines %.2f   draw %.2f   wait %.2f", s.update_ms, s.build_ms, s.submit_ms, s.wait_ms);
  rows.push_back(buf);
  std::snprintf(buf, sizeof buf, "Lines %d   target %.2f ms   late in last %.0f s", s.lines, s.target_ms, FrameStats::kLateSeconds);
  rows.push_back(buf);
  std::string sound = "Sound";
  if (latency_ms >= 0) {
    std::snprintf(buf, sizeof buf, "   latency %.0f ms", latency_ms);
    sound += buf;
  }
  if (s.audio) {
    std::snprintf(buf, sizeof buf, "   drift %.1f ms in 1 s", s.drift_ms);
    sound += buf;
  } else {
    sound += "   (no music)";
  }
  rows.push_back(sound);
  std::string presses = "Presses ms (- early)";
  if (s.presses.empty()) presses += "   none yet";
  for (const double p : s.presses) {
    std::snprintf(buf, sizeof buf, "   %.0f", p);
    presses += std::string(buf) == "   -0" ? "   0" : buf;
  }
  rows.push_back(presses);
  return rows;
}

PerfOverlay::PerfOverlay(const Pak& game_pak, int language, const GameText* text) : text_(text) {
  font_ = parse_tmd(game_pak.view(localized(game_pak, "FONT/FONT.TMD", language)));
}

Hud::Layer PerfOverlay::draw(const std::vector<std::string>& rows, const std::vector<double>& graph_ms, double target_ms, bool vr,
                             float aspect) {
  Hud::Layer layer;
  // A camera at the origin looking along +z (y down), as the original's text. In a
  // headset the layer is drawn fixed to the head, a unit a millimetre, so the
  // camera's field of view only matters on the monitor.
  constexpr float kDepth = 1000.0f;
  layer.view.eye = {0, 0, 0};
  layer.view.target = {0, 0, kDepth};
  layer.view.fov_y = float(682 * kTau / 4096.0);  // 60 degrees
  lines_.set_view(layer.view);
  lines_.seed(1);

  // Where the block starts and how big it is. Headset: low left, 1 m away (about
  // 7 to 29 degrees left and 11 to 21 degrees down), small; monitor: the top left corner.
  float x0, y0, scale;
  if (vr) {
    x0 = -560.0f, y0 = 200.0f, scale = 0.12f;
  } else {
    const float half_h = kDepth * std::tan(0.5f * layer.view.fov_y);
    scale = 0.14f;
    x0 = -half_h * std::max(0.5f, aspect) + 30.0f, y0 = -half_h + 40.0f;
  }
  const float advance = 72.0f * scale, pitch = 200.0f * scale, k = 0.8f * scale;
  const Rgb text_colour{200, 200, 200};
  if (text_) {
    LineBuilder::Xform xf;
    xf.m.m = {{{k, 0, 0}, {0, k, 0}, {0, 0, k}}};
    for (std::size_t r = 0; r < rows.size(); ++r) {
      bool second = false;
      int column = 0;
      for (const char ch : rows[r]) {
        const auto c = static_cast<unsigned char>(ch);
        const bool this_second = second;
        second = false;
        if (c == '~') {
          second = true;
          continue;
        }
        if (c != ' ') {
          const int object = text_->object_for(c, this_second);
          if (object >= 0 && object < int(font_.objects.size())) {
            xf.t = {x0 + advance * float(column), y0 + pitch * float(r), kDepth};
            lines_.object(font_.objects[std::size_t(object)], xf, 0, 0, nullptr, layer.lines, &text_colour);
          }
        }
        ++column;
      }
    }
  }
  if (!graph_ms.empty() && target_ms > 0) {
    // A bar a frame, its height the frame's time: the target at half height, two
    // targets or more at the top. Late frames (1.5 targets or more) in red.
    const float width = 2600.0f * scale, height = 420.0f * scale;
    const float left = x0, bottom = y0 + pitch * (float(rows.size()) - 0.5f) + height + 20.0f * scale;
    const float step = width / float(std::max<std::size_t>(graph_ms.size(), 60));
    auto line = [&](float ax, float ay, float bx, float by, Rgb colour) {
      SceneLine l;
      l.a = {ax, ay, kDepth}, l.b = {bx, by, kDepth}, l.colour = colour;
      layer.lines.push_back(l);
    };
    line(left, bottom, left + width, bottom, {90, 90, 90});
    line(left, bottom - height * 0.5f, left + width, bottom - height * 0.5f, {90, 90, 90});
    for (std::size_t i = 0; i < graph_ms.size(); ++i) {
      const double v = graph_ms[i];
      const float h = height * float(std::min(1.0, v / (2.0 * target_ms)));
      const float x = left + step * (float(i) + 0.5f);
      const bool late = v > FrameStats::kLateFactor * target_ms;
      line(x, bottom, x, bottom - std::max(h, 1.0f * scale), late ? Rgb{255, 80, 80} : Rgb{170, 170, 170});
    }
  }
  return layer;
}

}  // namespace vib
