#include "vib/tmd.h"

#include <string>

namespace vib {
namespace {

constexpr std::uint32_t kTmdId = 0x41;
constexpr std::uint32_t kFlagFixP = 1;  // object-table offsets are absolute

// Primitive mode byte: bits 7-5 are the code (1 polygon, 2 line, 3 sprite).
// Polygons: bit 4 gradation, bit 3 quad, bit 2 textured, bit 1 semi-transparent, bit 0 no lighting maths.
// Lines:    bit 4 gradation, bit 1 semi-transparent.
int code_of(std::uint8_t mode) { return mode >> 5; }

Rgb rgb_at(ByteView b, std::size_t off) { return Rgb{b[off], b[off + 1], b[off + 2]}; }

}  // namespace

TmdModel parse_tmd(ByteView b) {
  require(read_u32(b, 0) == kTmdId, "not a TMD file (bad id)");
  const std::uint32_t flags = read_u32(b, 4);
  const std::uint32_t object_count = read_u32(b, 8);
  require(object_count < 4096 && 12 + std::size_t(object_count) * 28 <= b.size(), "TMD object table is corrupt");
  const std::size_t base = (flags & kFlagFixP) ? 0 : 12;

  TmdModel model;
  model.objects.resize(object_count);
  for (std::uint32_t o = 0; o < object_count; ++o) {
    const std::size_t entry = 12 + std::size_t(o) * 28;
    const std::size_t vert_top = base + read_u32(b, entry + 0);
    const std::uint32_t vert_count = read_u32(b, entry + 4);
    const std::size_t prim_top = base + read_u32(b, entry + 16);
    const std::uint32_t prim_count = read_u32(b, entry + 20);
    TmdObject& obj = model.objects[o];
    obj.scale = read_i32(b, entry + 24);

    require(vert_count < 65536 && vert_top + std::size_t(vert_count) * 8 <= b.size(), "TMD vertices out of range");
    obj.vertices.resize(vert_count);
    for (std::uint32_t v = 0; v < vert_count; ++v) {
      const std::size_t at = vert_top + std::size_t(v) * 8;
      obj.vertices[v] = Vec3s{read_i16(b, at), read_i16(b, at + 2), read_i16(b, at + 4)};
    }

    auto vertex = [&](std::size_t at) {
      const std::uint16_t i = read_u16(b, at);
      require(i < vert_count, "TMD primitive uses vertex " + std::to_string(i) + " of " + std::to_string(vert_count));
      return i;
    };

    std::size_t p = prim_top;
    for (std::uint32_t k = 0; k < prim_count; ++k) {
      require(p + 4 <= b.size(), "TMD primitives run past the end");
      const std::uint8_t ilen = b[p + 1];
      const std::uint8_t flag = b[p + 2];
      const std::uint8_t mode = b[p + 3];
      const std::size_t body = p + 4;
      const std::size_t next = body + std::size_t(ilen) * 4;
      require(next <= b.size(), "TMD primitive runs past the end");
      const bool gradated = (mode & 0x10) != 0;
      const bool semi = (mode & 0x02) != 0;

      if (code_of(mode) == 2) {  // line
        TmdLine line;
        line.semi_transparent = semi;
        line.colour_a = rgb_at(b, body);
        if (gradated && ilen == 3) {
          line.colour_b = rgb_at(b, body + 4);
          line.a = vertex(body + 8);
          line.b = vertex(body + 10);
          obj.lines.push_back(line);
        } else if (!gradated && ilen == 2) {
          line.colour_b = line.colour_a;
          line.a = vertex(body + 4);
          line.b = vertex(body + 6);
          obj.lines.push_back(line);
        } else {
          ++model.unsupported_primitives;
        }
      } else if (code_of(mode) == 1 && (flag & 1) && !gradated && !(mode & 0x04)) {
        // Unlit, flat-coloured, untextured polygon: colour word, then vertex indices.
        const Rgb colour = rgb_at(b, body);
        const bool quad = (mode & 0x08) != 0;
        if (ilen != 3) {
          ++model.unsupported_primitives;
        } else if (quad) {  // drawn as a single line, first vertex to last
          TmdLine line;
          line.colour_a = line.colour_b = colour;
          line.semi_transparent = semi;
          line.a = vertex(body + 4);
          line.b = vertex(body + 10);
          obj.lines.push_back(line);
        } else if (colour.r == 0 && colour.g == 0 && colour.b <= 1 && obj.marker == FaceMarker::None) {
          obj.marker = colour.b == 0 ? FaceMarker::DrawOnlyWhenFacing : FaceMarker::Unused;
          for (int i = 0; i < 3; ++i) obj.marker_vertices[i] = vertex(body + 4 + std::size_t(i) * 2);
        } else {
          ++model.unsupported_primitives;  // a triangle that is not a marker, or a second marker: not seen on the disc
        }
      } else {
        ++model.unsupported_primitives;
      }
      p = next;
    }
  }
  return model;
}

}  // namespace vib
