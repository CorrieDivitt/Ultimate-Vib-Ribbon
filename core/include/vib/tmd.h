#pragma once
// PlayStation TMD models as used by Vib-Ribbon.
//
// The game's models are almost entirely line primitives (mode 0x40: flat colour,
// two vertex indices). A few unlit flat triangles (0x21) and quads (0x29) appear
// on the characters, always black: they are fills that hide the lines behind a
// character, so a renderer must draw them into the depth buffer.

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

struct TmdFill {
  std::uint16_t v[4] = {0, 0, 0, 0};
  int count = 3;  // 3 for a triangle, 4 for a quad (PlayStation order: 0,1,2 and 1,3,2)
  Rgb colour;
  bool semi_transparent = false;
};

struct TmdObject {
  std::vector<Vec3s> vertices;
  std::vector<TmdLine> lines;
  std::vector<TmdFill> fills;
  std::int32_t scale = 0;
};

struct TmdModel {
  std::vector<TmdObject> objects;
  std::size_t unsupported_primitives = 0;  // anything this loader skipped; 0 on the supported disc
};

TmdModel parse_tmd(ByteView data);

}  // namespace vib
