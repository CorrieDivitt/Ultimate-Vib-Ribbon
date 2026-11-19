#pragma once
// Model animations (.ANM) and camera animations (.ANC).
//
// Both start with the same 6-byte header: int16 0x8000, int16 rate, uint16 count.
// The rate is the clip's frames per second (1, 10, 15, 20, 30 or 60 on the
// supported disc). The original steps a clip by rate / display rate each video
// frame, starts it half a frame in, and shows the whole-numbered frame: no
// blending between frames.

#include <cstdint>
#include <vector>

#include "vib/bytes.h"
#include "vib/tmd.h"

namespace vib {

// One object's transform in one frame. An object with no keyframe in a frame is
// not drawn in that frame. A key without a group uses the identity value for it
// (no rotation, scale 1, position 0).
struct ModelKey {
  std::uint8_t object = 0;
  bool has_rotation = false, has_scale = false, has_position = false;
  Vec3s rotation;  // 4096 = one full turn
  Vec3s scale;     // 4096 = 1.0
  Vec3s position;  // model units
};

struct ModelAnimation {
  std::int16_t rate = 0;
  std::vector<std::vector<ModelKey>> frames;
};

struct CameraKey {
  Vec3s eye;          // viewpoint (vpx, vpy, vpz)
  Vec3s target;       // reference point (vrx, vry, vrz)
  std::int16_t twist; // roll about the view direction, passed to the PlayStation library as is
  std::int16_t fov;   // vertical field of view on a 4:3 picture, 4096 = full turn
};

struct CameraAnimation {
  std::int16_t rate = 0;
  std::vector<CameraKey> keys;
};

ModelAnimation parse_anm(ByteView data);
CameraAnimation parse_anc(ByteView data);

}  // namespace vib
