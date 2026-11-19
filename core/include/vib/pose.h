#pragma once
// Applying an animation frame to a model.
//
// Each object's vertices are scaled, then rotated about X, then Y, then Z, then
// translated: v' = Rz * Ry * Rx * S * v + p. Found by looking (the other order
// detaches Vibri's feet) and consistent with the original building its matrix
// with the PlayStation library's ZYX rotation followed by a column scale.
// PlayStation axes: X right, Y down, Z away from the camera.

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
