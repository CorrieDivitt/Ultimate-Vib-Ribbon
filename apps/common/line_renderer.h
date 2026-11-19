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

class Renderer {
 public:
  explicit Renderer(SDL_Window* window);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  const char* gpu_name() const;
  void request_resize();
  // Width / height of the drawable, or 0 while minimised.
  float aspect() const;
  int height() const;  // pixels
  void draw(const std::vector<LineInstance>& lines, const Mat4& view, float fov_y, float line_width);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace vibr
