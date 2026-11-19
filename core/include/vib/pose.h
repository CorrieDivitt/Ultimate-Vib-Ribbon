#pragma once
// Applying an animation frame to a model.
//
// Observed on the supported disc (session 2): each object's vertices are scaled,
// then rotated about X, then Y, then Z, then translated: v' = Rz * Ry * Rx * S * v + p.
// The other common order leaves Vibri's feet detached from her legs. Not yet
// confirmed against the game's own code (task 1.7). PlayStation Y points down.

#include <array>
#include <vector>

#include "vib/motion.h"
#include "vib/tmd.h"

namespace vib {

struct Vec3f {
  float x = 0, y = 0, z = 0;
};

struct Mat3 {
  std::array<std::array<float, 3>, 3> m{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  Vec3f apply(const Vec3f& v) const;
};

// 4096 units = one full turn.
Mat3 rotation_xyz(const Vec3s& rotation);

struct PosedObject {
  int object = 0;
  std::vector<Vec3f> vertices;  // in model space, PlayStation axes
};

// Returns the objects that have a key in this frame, transformed.
std::vector<PosedObject> pose(const TmdModel& model, const std::vector<ModelKey>& frame);

// Every object untransformed, for models that are drawn without animation.
std::vector<PosedObject> rest_pose(const TmdModel& model);

}  // namespace vib
