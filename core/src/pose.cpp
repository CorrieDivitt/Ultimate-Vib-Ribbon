#include "vib/pose.h"

#include <cmath>

namespace vib {
namespace {

constexpr float kTau = 6.28318530717958647692f;

Mat3 mul(const Mat3& a, const Mat3& b) {
  Mat3 r;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
  return r;
}

Mat3 axis(int which, float radians) {
  const float c = std::cos(radians), s = std::sin(radians);
  Mat3 r;
  if (which == 0) r.m = {{{1, 0, 0}, {0, c, -s}, {0, s, c}}};
  if (which == 1) r.m = {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}};
  if (which == 2) r.m = {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
  return r;
}

float angle(std::int16_t units) { return float(units) * kTau / 4096.0f; }

}  // namespace

Vec3f Mat3::apply(const Vec3f& v) const {
  return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
          m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
}

Mat3 rotation_xyz(const Vec3s& r) {
  return mul(axis(2, angle(r.z)), mul(axis(1, angle(r.y)), axis(0, angle(r.x))));
}

std::vector<PosedObject> pose(const TmdModel& model, const std::vector<ModelKey>& frame) {
  std::vector<PosedObject> out;
  for (const ModelKey& k : frame) {
    if (k.object >= model.objects.size()) continue;
    const Mat3 rot = k.has_rotation ? rotation_xyz(k.rotation) : Mat3{};
    const Vec3f scale = k.has_scale ? Vec3f{k.scale.x / 4096.0f, k.scale.y / 4096.0f, k.scale.z / 4096.0f} : Vec3f{1, 1, 1};
    const Vec3f pos = k.has_position ? Vec3f{float(k.position.x), float(k.position.y), float(k.position.z)} : Vec3f{};
    PosedObject p;
    p.object = k.object;
    for (const Vec3s& v : model.objects[k.object].vertices) {
      const Vec3f r = rot.apply({v.x * scale.x, v.y * scale.y, v.z * scale.z});
      p.vertices.push_back({r.x + pos.x, r.y + pos.y, r.z + pos.z});
    }
    out.push_back(std::move(p));
  }
  return out;
}

std::vector<PosedObject> rest_pose(const TmdModel& model) {
  std::vector<PosedObject> out;
  for (std::size_t o = 0; o < model.objects.size(); ++o) {
    PosedObject p;
    p.object = static_cast<int>(o);
    for (const Vec3s& v : model.objects[o].vertices) p.vertices.push_back({float(v.x), float(v.y), float(v.z)});
    out.push_back(std::move(p));
  }
  return out;
}

}  // namespace vib
