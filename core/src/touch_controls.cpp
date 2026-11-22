#include "vib/touch_controls.h"

#include <algorithm>
#include <cmath>

#include "vib/gameplay.h"

namespace vib {

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kTapMove = 0.06f, kSwipeMove = 0.15f;  // picture units (a twelfth of the height: a swipe)
constexpr double kTapSeconds = 0.5;
}  // namespace

void TouchControls::layout(float aspect) {
  aspect_ = std::max(1.0f, aspect);
  const float a = aspect_, r = 0.21f;
  // Two buttons for each thumb, on an arc: the upper one nearer the edge, as the
  // original's shoulder buttons sit above its d-pad and face buttons.
  places_[kBlock] = {-a + 0.30f, 0.20f, r};
  places_[kPit] = {-a + 0.66f, 0.62f, r};
  places_[kLoop] = {a - 0.30f, 0.20f, r};
  places_[kWave] = {a - 0.66f, 0.62f, r};
  places_[kOptions] = {a - 0.16f, -0.84f, 0.11f};
  places_[kBack] = {-a + 0.16f, -0.84f, 0.11f};
}

void TouchControls::set_mode(Mode m) {
  if (m == mode_) return;
  mode_ = m;
  clear();  // (a finger held across the change would act in the wrong place)
}

bool TouchControls::shown(Button b) const {
  if (b == kOptions) return true;
  if (b == kBack) return mode_ == Mode::Menus;
  return mode_ == Mode::Play;
}

bool TouchControls::held(Button b) const {
  return std::any_of(fingers_.begin(), fingers_.end(), [b](const auto& f) { return f.second.button == b; });
}

int TouchControls::hit(float x, float y) const {
  int best = -1;
  float best_d = 0;
  for (int b = 0; b < kButtonCount; ++b) {
    if (!shown(Button(b))) continue;
    const Place& p = places_[b];
    const float d = std::hypot(x - p.x, y - p.y);
    if (d <= p.r * 1.3f && (best < 0 || d < best_d)) best = b, best_d = d;  // (a little larger than drawn)
  }
  return best;
}

float TouchControls::spread() const {
  const Finger* g[2] = {};
  int n = 0;
  for (const auto& [id, f] : fingers_)
    if (f.button == kButtonCount && n < 2) g[n++] = &f;
  return n == 2 ? std::hypot(g[0]->x - g[1]->x, g[0]->y - g[1]->y) : 0.0f;
}

TouchControls::Result TouchControls::down(std::int64_t finger, float x, float y, double seconds) {
  Result r;
  Finger f;
  f.x0 = f.x = x, f.y0 = f.y = y, f.t0 = seconds;
  const int b = hit(x, y);
  if (b >= 0) {
    f.button = Button(b);
    if (b == kBlock) r.presses = kPressBlock;
    if (b == kPit) r.presses = kPressPit;
    if (b == kLoop) r.presses = kPressLoop;
    if (b == kWave) r.presses = kPressWave;
    if (b == kOptions) r.options = true;
    if (b == kBack) r.back = true;
  }
  fingers_[finger] = f;
  last_spread_ = spread();
  return r;
}

TouchControls::Result TouchControls::move(std::int64_t finger, float x, float y, double) {
  Result r;
  auto it = fingers_.find(finger);
  if (it == fingers_.end() || it->second.button != kButtonCount) return r;  // (fingers on buttons stay put)
  Finger& f = it->second;
  const float dx = x - f.x;
  f.x = x, f.y = y;
  int gestures = 0;
  for (const auto& [id, g] : fingers_) gestures += g.button == kButtonCount;
  if (gestures >= 2) {  // two fingers: pinch zooms, and both dragging across turns (half each)
    const float s = spread();
    if (last_spread_ > 0.02f && s > 0.02f) r.zoom = std::log2(s / last_spread_);
    last_spread_ = s;
    r.turn = dx * 0.5f;
  } else if (mode_ == Mode::Play) {  // one finger in play: drag across turns
    r.turn = dx;
  }
  return r;
}

TouchControls::Result TouchControls::up(std::int64_t finger, float x, float y, double seconds) {
  Result r;
  auto it = fingers_.find(finger);
  if (it == fingers_.end()) return r;
  const Finger f = it->second;
  fingers_.erase(it);
  last_spread_ = spread();
  int gestures = 0;
  for (const auto& [id, g] : fingers_) gestures += g.button == kButtonCount;
  if (f.button != kButtonCount || mode_ != Mode::Menus || gestures > 0) return r;
  const float dx = x - f.x0, dy = y - f.y0, moved = std::hypot(dx, dy);
  if (moved < kTapMove && seconds - f.t0 < kTapSeconds) {
    r.tap = true, r.tap_x = x, r.tap_y = y;
  } else if (moved >= kSwipeMove) {
    if (std::abs(dy) >= std::abs(dx)) r.swipe = dy < 0 ? 1 : 2;
    else r.swipe = dx < 0 ? 3 : 4;
  }
  return r;
}

void TouchControls::clear() {
  fingers_.clear();
  last_spread_ = 0;
}

const char* TouchControls::label(Button b, Style style) {
  if (style != Style::PlayStation) return nullptr;
  return b == kBlock ? "L1" : b == kLoop ? "R1" : nullptr;
}

std::vector<SceneLine> TouchControls::button_lines(Button b, Style style, float unit, float z, std::uint8_t shade) const {
  std::vector<SceneLine> out;
  const Place& p = places_[b];
  const Rgb c{shade, shade, shade};
  auto line = [&](float ax, float ay, float bx, float by) {  // (in the button's own units: -1 to 1 across its ring)
    SceneLine l;
    l.a = {(p.x + ax * p.r) * unit, (p.y + ay * p.r) * unit, z};
    l.b = {(p.x + bx * p.r) * unit, (p.y + by * p.r) * unit, z};
    l.colour = c;
    out.push_back(l);
  };
  auto ring = [&](float radius, int sides) {
    for (int i = 0; i < sides; ++i) {
      const float t0 = 2 * kPi * float(i) / float(sides), t1 = 2 * kPi * float(i + 1) / float(sides);
      line(radius * std::cos(t0), radius * std::sin(t0), radius * std::cos(t1), radius * std::sin(t1));
    }
  };
  if (b == kOptions) {  // three bars
    for (const float y : {-0.4f, 0.0f, 0.4f}) line(-0.55f, y, 0.55f, y);
    ring(1.0f, 24);
    return out;
  }
  if (b == kBack) {  // an arrow pointing left
    line(0.55f, 0.0f, -0.55f, 0.0f), line(-0.55f, 0.0f, -0.1f, -0.42f), line(-0.55f, 0.0f, -0.1f, 0.42f);
    ring(1.0f, 24);
    return out;
  }
  ring(1.0f, 32);
  if (style == Style::PlayStation) {
    if (b == kWave) line(-0.42f, -0.42f, 0.42f, 0.42f), line(-0.42f, 0.42f, 0.42f, -0.42f);  // X (the cross)
    if (b == kPit) {  // Down: an arrow
      line(0.0f, -0.55f, 0.0f, 0.5f), line(0.0f, 0.5f, -0.38f, 0.1f), line(0.0f, 0.5f, 0.38f, 0.1f);
    }
    return out;  // (L1 and R1: their letters, as text)
  }
  // The obstacles, as the road meets them: a line across with the obstacle on it.
  const float base = 0.35f;
  switch (b) {
    case kBlock:  // a square bump
      line(-0.75f, base, -0.3f, base), line(-0.3f, base, -0.3f, -0.4f), line(-0.3f, -0.4f, 0.3f, -0.4f);
      line(0.3f, -0.4f, 0.3f, base), line(0.3f, base, 0.75f, base);
      break;
    case kPit:  // a dip
      line(-0.75f, -0.1f, -0.3f, -0.1f), line(-0.3f, -0.1f, 0.0f, 0.55f), line(0.0f, 0.55f, 0.3f, -0.1f), line(0.3f, -0.1f, 0.75f, -0.1f);
      break;
    case kLoop: {  // a loop sitting on the line
      line(-0.75f, base, 0.75f, base);
      const float cy = base - 0.4f;
      for (int i = 0; i < 20; ++i) {
        const float t0 = 2 * kPi * float(i) / 20.0f, t1 = 2 * kPi * float(i + 1) / 20.0f;
        line(0.4f * std::cos(t0), cy + 0.4f * std::sin(t0), 0.4f * std::cos(t1), cy + 0.4f * std::sin(t1));
      }
      break;
    }
    case kWave:  // a wave
      for (int i = 0; i < 16; ++i) {
        const float x0 = -0.75f + 1.5f * float(i) / 16.0f, x1 = -0.75f + 1.5f * float(i + 1) / 16.0f;
        line(x0, 0.32f * std::sin(x0 * 2.3f * kPi), x1, 0.32f * std::sin(x1 * 2.3f * kPi));
      }
      break;
    default: break;
  }
  return out;
}

}  // namespace vib
