#pragma once
// The line renderer shared by the desktop apps: Vulkan 1.0, one instanced
// screen-space quad per line segment, constant pixel width with a feathered edge.

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace vibr {

struct Mat4 {
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // column-major
};

inline Mat4 mul(const Mat4& a, const Mat4& b) {
  Mat4 r;
  for (int c = 0; c < 4; ++c)
    for (int row = 0; row < 4; ++row) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
      r.m[c * 4 + row] = s;
    }
  return r;
}

// Vulkan clip space: Y down, depth 0..1. The view space used here is Y up, looking down -Z.
inline Mat4 perspective(float fov_y, float aspect, float z_near, float z_far) {
  const float f = 1.0f / std::tan(fov_y * 0.5f);
  Mat4 r;
  r.m[0] = f / aspect;
  r.m[5] = -f;
  r.m[10] = z_far / (z_near - z_far);
  r.m[11] = -1;
  r.m[14] = z_near * z_far / (z_near - z_far);
  r.m[15] = 0;
  return r;
}

struct V3 {
  float x, y, z;
};
inline V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 norm(V3 a) {
  const float l = std::sqrt(dot(a, a));
  return l > 0 ? V3{a.x / l, a.y / l, a.z / l} : V3{0, 0, 1};
}

inline Mat4 look_at(V3 eye, V3 target, V3 up) {
  const V3 f = norm(sub(target, eye)), s = norm(cross(f, up)), u = cross(s, f);
  Mat4 r;
  r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z; r.m[12] = -dot(s, eye);
  r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z; r.m[13] = -dot(u, eye);
  r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z; r.m[14] = dot(f, eye);
  return r;
}

// One line segment, world space.
struct LineInstance {
  float a[3];
  float b[3];
  float colour[4];
};

// Lines seen through one camera. Layers are drawn in order, later ones on top.
struct Layer {
  const std::vector<LineInstance>* lines = nullptr;
  Mat4 view;
  float fov_y = 0.8f;
  // In a headset, > 0: fixed in the room where the view was last recentred, this
  // many metres a unit, instead of seen through the game's own camera; `view` then
  // takes the lines to that place's own space (y up, -z ahead, the head's height at
  // the recentre at 0). It stays put as the head turns. A monitor draws it as any other.
  float anchored = 0;
  // > 0: a glow pass (ours): the lines this many times as wide (from at least a pixel)
  // with an edge that fades all the way out, drawn under the layer it copies.
  float glow = 0;
};

// How the last frame went, as the display measured it (the performance overlay).
struct FrameTiming {
  double interval_ms = 0;  // since the frame before: shown (headset: display times), else handed over; 0 unknown
  double period_ms = 0;    // the display's frame period; 0 if it does not say
  double wait_ms = 0;      // in draw(), blocked until the display was ready for the frame
  double submit_ms = 0;    // the rest of draw(): copying the lines, recording, submitting, presenting
};

// Where the game's lines are shown: a window on the monitor, or a headset.
class Display {
 public:
  virtual ~Display() = default;
  virtual const char* gpu_name() const = 0;
  virtual void request_resize() {}
  virtual int height() const = 0;  // pixels of the picture (an eye's, in a headset)
  // One frame: the layers in order, each through its own camera.
  virtual void draw(const std::vector<Layer>& layers, float line_width) = 0;
  // A headset: put the scene in front of where the player is now looking.
  virtual void recentre() {}
  // A headset: how close the view stands to Vibri in play (1 = as set up, larger = closer).
  virtual float zoom() const { return 1.0f; }
  virtual void set_zoom(float) {}
  // The last draw()'s timing.
  virtual FrameTiming timing() const { return {}; }
  // A headset: the refresh rates it lets the game choose (empty: none), and asking
  // for one (0: the highest up to 120 Hz). Takes effect a frame or so later.
  virtual std::vector<float> refresh_rates() const { return {}; }
  virtual void set_refresh_rate(float) {}
  // A headset's own controllers (Quest Touch) send different gamepad buttons in play
  // and in the menus (session 5, owner): the game says which it is in, every frame.
  enum class InputMode { Menus, Play };
  virtual void set_input_mode(InputMode) {}
  // The headset controllers' sticks, -1 to 1 (up and right positive). In the menus the
  // left stick moves the cursor, so it reads 0 here; `held` is the cursor's held
  // direction from it (up, down, left, right) for the menus' repeat.
  struct ControllerSticks {
    float left_x = 0, left_y = 0, right_x = 0, right_y = 0;
    bool held[4] = {};
  };
  virtual ControllerSticks controller_sticks() const { return {}; }
  // The colour behind the lines, as shown on a monitor (0-1 each; the original's black).
  void set_background(float r, float g, float b) { background_[0] = r, background_[1] = g, background_[2] = b; }

 protected:
  float background_[3] = {0.0f, 0.0f, 0.0f};
};

class Renderer : public Display {
 public:
  explicit Renderer(SDL_Window* window);
  ~Renderer() override;
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  const char* gpu_name() const override;
  void request_resize() override;
  // Width / height of the drawable, or 0 while minimised.
  float aspect() const;
  int height() const override;  // pixels
  void draw(const std::vector<LineInstance>& lines, const Mat4& view, float fov_y, float line_width);
  void draw(const std::vector<Layer>& layers, float line_width) override;
  FrameTiming timing() const override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace vibr
