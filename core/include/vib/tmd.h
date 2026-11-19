#pragma once
// PlayStation TMD models as used by Vib-Ribbon.
//
// The game draws nothing but lines:
//   0x40 flat line        drawn as is.
//   0x29 unlit flat quad  drawn as one line from its first vertex to its last.
//   0x21 unlit flat triangle with red = green = 0: never drawn. It is a marker.
//        Blue 0: the object is drawn only while this triangle faces the camera
//        (how Vibri's eyes disappear from behind). Blue 1: a second kind of
//        marker that the game records but never uses.
// On the supported disc every triangle is one of those two markers, and an
// object has at most one.

#include <cstdint>
#include <vector>

#include "vib/bytes.h"

namespace vib {

struct Vec3s {
  std::int16_t x = 0, y = 0, z = 0;
};

struct Rgb {
  std::uint8_t r = 0, g = 0, b = 0;
};

struct TmdLine {
  std::uint16_t a = 0, b = 0;  // vertex indices into the object's vertex list
  Rgb colour_a, colour_b;      // equal unless the line is gradated
  bool semi_transparent = false;
};

enum class FaceMarker { None, DrawOnlyWhenFacing, Unused };

struct TmdObject {
  std::vector<Vec3s> vertices;
  std::vector<TmdLine> lines;
  FaceMarker marker = FaceMarker::None;
  std::uint16_t marker_vertices[3] = {0, 0, 0};
  std::int32_t scale = 0;
};

struct TmdModel {
  std::vector<TmdObject> objects;
  std::size_t unsupported_primitives = 0;  // anything this loader skipped; 0 on the supported disc
};

TmdModel parse_tmd(ByteView data);

}  // namespace vib
