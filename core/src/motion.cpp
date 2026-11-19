#include "vib/motion.h"

namespace vib {
namespace {

constexpr std::uint16_t kMagic = 0x8000;

Vec3s vec_at(ByteView b, std::size_t off) { return Vec3s{read_i16(b, off), read_i16(b, off + 2), read_i16(b, off + 4)}; }

}  // namespace

ModelAnimation parse_anm(ByteView b) {
  require(read_u16(b, 0) == kMagic, "not an ANM file (bad magic)");
  ModelAnimation anim;
  anim.rate = read_i16(b, 2);
  const std::uint16_t frame_count = read_u16(b, 4);
  const std::size_t table = 6;
  require(table + (std::size_t(frame_count) + 1) * 2 <= b.size(), "ANM frame table runs past the end");
  require(std::size_t(read_u16(b, table + std::size_t(frame_count) * 2)) * 2 == b.size(), "ANM end offset does not match file size");

  anim.frames.resize(frame_count);
  for (std::uint16_t f = 0; f < frame_count; ++f) {
    std::size_t p = std::size_t(read_u16(b, table + std::size_t(f) * 2)) * 2;
    const std::size_t end = std::size_t(read_u16(b, table + (std::size_t(f) + 1) * 2)) * 2;
    require(p <= end && end <= b.size(), "ANM frame offsets out of order");
    while (p < end) {
      ModelKey k;
      k.object = b[p];
      const std::uint8_t flags = b[p + 1];
      require((flags & ~7u) == 0, "ANM keyframe has unknown flag bits");
      p += 2;
      if (flags & 1) { k.has_rotation = true; k.rotation = vec_at(b, p); p += 6; }
      if (flags & 2) { k.has_scale = true; k.scale = vec_at(b, p); p += 6; }
      if (flags & 4) { k.has_position = true; k.position = vec_at(b, p); p += 6; }
      anim.frames[f].push_back(k);
    }
    require(p == end, "ANM keyframes overrun their frame");
  }
  return anim;
}

CameraAnimation parse_anc(ByteView b) {
  require(read_u16(b, 0) == kMagic, "not an ANC file (bad magic)");
  CameraAnimation cam;
  cam.rate = read_i16(b, 2);
  const std::uint16_t count = read_u16(b, 4);
  require(6 + std::size_t(count) * 16 == b.size(), "ANC size does not match its keyframe count");
  cam.keys.resize(count);
  for (std::uint16_t i = 0; i < count; ++i) {
    const std::size_t at = 6 + std::size_t(i) * 16;
    cam.keys[i] = CameraKey{vec_at(b, at), vec_at(b, at + 6), read_i16(b, at + 12), read_i16(b, at + 14)};
  }
  return cam;
}

}  // namespace vib
