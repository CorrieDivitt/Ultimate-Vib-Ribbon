#pragma once
// Ours (not the original's): drawing the course wrapped round a headset player.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "vib/scene.h"

namespace vib {

// Ours, task 4.11, headset only: the course wrapped round the player. The player
// stands at the middle of a level ring (`radius` game units); the road runs round it,
// Vibri walking it as the road under her moves (so the pieces near her stand still in
// the room and the player turns to follow her). Ahead of her the road curves away
// round one side, behind her the passed road round the other; both fade out as they
// come round to the point opposite her, behind the player while they face her.
// Play's world (x across, y down, z along the road, Vibri at vibri_z) maps onto the
// ring: along the road is round it (angle phi + s / radius), +x is towards the middle.
// The HUD around Vibri (score, meter, numbers, celebrations) is placed as usual with
// the middle as the viewer, then carried rigidly to her place on the ring.
struct CourseWrap {
  static constexpr float kEye = 500.0f;  // the road this far below the eye (0.5 m)
  float radius = 3000.0f;
  double phi = 0;   // Vibri's angle round the ring (0: straight ahead after a recentre)
  double rate = 0;  // the road's speed under her, units a second (eased)
  bool rate_known = false;

  // `units` of road passed under her (negative while a rewind runs it back).
  void advance(double units) { phi = std::remainder(phi + units / double(radius), 2.0 * 3.14159265358979); }
  SceneView view() const {
    SceneView v;
    v.eye = {0.0f, -kEye, 0.0f};
    v.target = {0.0f, -kEye, 1000.0f};
    v.fov_y = 1.0f;  // (the headset shows its own field of view)
    return v;
  }
  // The ring's middle in play's world, for the HUD's facing().
  Vec3f eye_play(float vibri_z) const { return {radius, -kEye, vibri_z}; }

  static Vec3f inward(double t) { return {float(-std::sin(t)), 0.0f, float(-std::cos(t))}; }
  static Vec3f along(double t) { return {float(std::cos(t)), 0.0f, float(-std::sin(t))}; }
  static Vec3f turn(Vec3f v, double t) {  // a direction in play's frame to the ring's at angle t
    const Vec3f n = inward(t), a = along(t);
    return {v.x * n.x + v.z * a.x, v.y, v.x * n.z + v.z * a.z};
  }
  Vec3f bend(Vec3f p, float vibri_z) const {
    const double t = phi + double(p.z - vibri_z) / double(radius);
    const double r = double(radius - p.x);
    return {float(r * std::sin(t)), p.y, float(r * std::cos(t))};
  }
  // Lines round the ring: split into pieces of at most 150 units so they curve, dimmed
  // over the last 35% of each half of the ring and gone at the point opposite her.
  void bend_lines(std::vector<SceneLine>& lines, float vibri_z) const {
    const double half = 3.14159265358979 * double(radius), fade = 0.35 * half;
    std::vector<SceneLine> out;
    out.reserve(lines.size() + lines.size() / 4);
    auto lerp = [](Vec3f a, Vec3f b, float k) { return Vec3f{a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k}; };
    for (const SceneLine& l : lines) {
      const int n = std::clamp(int(std::ceil(std::abs(l.b.z - l.a.z) / 150.0f)), 1, 400);
      for (int k = 0; k < n; ++k) {
        const float k0 = float(k) / float(n), k1 = float(k + 1) / float(n);
        SceneLine p = l;
        p.a = lerp(l.a, l.b, k0), p.b = lerp(l.a, l.b, k1);
        const double far = std::max(std::abs(double(p.a.z - vibri_z)), std::abs(double(p.b.z - vibri_z)));
        const double bright = std::clamp((half - far) / fade, 0.0, 1.0);
        if (bright <= 0.0) continue;
        p.jitter_a = lerp(l.jitter_a, l.jitter_b, k0), p.jitter_b = lerp(l.jitter_a, l.jitter_b, k1);
        p.vertex_a = k == 0 ? l.vertex_a : 0u;
        p.vertex_b = k == n - 1 ? l.vertex_b : 0u;
        const double ta = phi + double(p.a.z - vibri_z) / double(radius), tb = phi + double(p.b.z - vibri_z) / double(radius);
        p.jitter_a = turn(p.jitter_a, ta), p.jitter_b = turn(p.jitter_b, tb);
        p.a = bend(p.a, vibri_z), p.b = bend(p.b, vibri_z);
        p.colour = {std::uint8_t(l.colour.r * bright), std::uint8_t(l.colour.g * bright), std::uint8_t(l.colour.b * bright)};
        out.push_back(p);
      }
    }
    lines = std::move(out);
  }
  // HUD lines (placed with eye_play as the viewer) carried whole to her place on the ring.
  void place(std::vector<SceneLine>& lines, float vibri_z) const {
    const Vec3f n = inward(phi), a = along(phi);
    const Vec3f c{float(double(radius) * std::sin(phi)), 0.0f, float(double(radius) * std::cos(phi))};
    auto at = [&](Vec3f p) {
      const float sz = p.z - vibri_z;
      return Vec3f{c.x + p.x * n.x + sz * a.x, p.y, c.z + p.x * n.z + sz * a.z};
    };
    for (SceneLine& l : lines) {
      l.a = at(l.a), l.b = at(l.b);
      l.jitter_a = turn(l.jitter_a, phi), l.jitter_b = turn(l.jitter_b, phi);
    }
  }
  // Follow the road's speed under her (units a second), eased over about half a second
  // so a change of speed between parts does not jerk the ring.
  void follow(double target, double seconds) {
    if (target <= 0) return;  // (no part ahead: keep going as before)
    if (!rate_known) rate = target, rate_known = true;
    rate += (target - rate) * (1.0 - std::exp(-std::max(0.0, seconds) / 0.5));
  }
};

}  // namespace vib
