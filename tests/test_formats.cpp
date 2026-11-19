// Format loaders on small hand-built inputs. No game data needed.

#include <cmath>
#include <initializer_list>

#include "test.h"
#include "vib/course_script.h"
#include "vib/motion.h"
#include "vib/pose.h"
#include "vib/sound_bank.h"
#include "vib/tmd.h"

using namespace vib;

namespace {
struct Builder {
  Bytes b;
  Builder& u8(std::uint8_t v) { b.push_back(v); return *this; }
  Builder& u16(std::uint16_t v) { u8(std::uint8_t(v)); return u8(std::uint8_t(v >> 8)); }
  Builder& u32(std::uint32_t v) { u16(std::uint16_t(v)); return u16(std::uint16_t(v >> 16)); }
  Builder& i16(std::int16_t v) { return u16(static_cast<std::uint16_t>(v)); }
  Builder& i32(std::int32_t v) { return u32(static_cast<std::uint32_t>(v)); }
};
}  // namespace

TEST(tmd_lines_and_fill) {
  // One object, 3 vertices, one white line 0-2 and one black unlit triangle.
  Builder t;
  t.u32(0x41).u32(0).u32(1);
  const std::uint32_t vert_top = 28, prim_top = 28 + 3 * 8;
  t.u32(vert_top).u32(3).u32(0).u32(0).u32(prim_top).u32(2).i32(0);
  for (std::int16_t v : {std::int16_t(-10), std::int16_t(0), std::int16_t(10)}) t.i16(v).i16(std::int16_t(v * 2)).i16(5).i16(0);
  t.u8(3).u8(2).u8(1).u8(0x40).u8(255).u8(255).u8(255).u8(0x40).u16(0).u16(2);
  t.u8(4).u8(3).u8(1).u8(0x21).u8(0).u8(0).u8(0).u8(0x21).u16(0).u16(1).u16(2).u16(0);
  const TmdModel m = parse_tmd(t.b);
  CHECK(m.objects.size() == 1);
  CHECK(m.unsupported_primitives == 0);
  CHECK(m.objects[0].vertices.size() == 3 && m.objects[0].vertices[2].y == 20);
  CHECK(m.objects[0].lines.size() == 1 && m.objects[0].lines[0].b == 2 && m.objects[0].lines[0].colour_a.g == 255);
  CHECK(m.objects[0].fills.size() == 1 && m.objects[0].fills[0].count == 3 && m.objects[0].fills[0].v[1] == 1);
}

TEST(tmd_rejects_bad_vertex_index) {
  Builder t;
  t.u32(0x41).u32(0).u32(1).u32(28).u32(1).u32(0).u32(0).u32(36).u32(1).i32(0);
  t.i16(0).i16(0).i16(0).i16(0);
  t.u8(3).u8(2).u8(1).u8(0x40).u8(255).u8(255).u8(255).u8(0x40).u16(0).u16(7);
  CHECK_THROWS(parse_tmd(t.b));
}

TEST(anm_frames_and_keys) {
  // Two frames: frame 0 keys object 3 with rotation and position, frame 1 is empty.
  Builder a;
  a.u16(0x8000).i16(30).u16(2);
  const std::uint16_t f0 = (6 + 3 * 2) / 2, f1 = std::uint16_t(f0 + (2 + 12) / 2);
  a.u16(f0).u16(f1).u16(f1);
  a.u8(3).u8(1 | 4).i16(1024).i16(0).i16(0).i16(1).i16(2).i16(3);
  const ModelAnimation m = parse_anm(a.b);
  CHECK(m.rate == 30);
  CHECK(m.frames.size() == 2 && m.frames[0].size() == 1 && m.frames[1].empty());
  CHECK(m.frames[0][0].object == 3 && m.frames[0][0].has_rotation && !m.frames[0][0].has_scale);
  CHECK(m.frames[0][0].position.z == 3);
  a.b.push_back(0);
  CHECK_THROWS(parse_anm(a.b));  // end offset no longer matches the size
}

TEST(anc_keys) {
  Builder c;
  c.u16(0x8000).i16(60).u16(1);
  for (int v = 1; v <= 8; ++v) c.i16(static_cast<std::int16_t>(v));
  const CameraAnimation cam = parse_anc(c.b);
  CHECK(cam.keys.size() == 1 && cam.keys[0].eye.x == 1 && cam.keys[0].target.z == 6 && cam.keys[0].fov == 8);
  c.b.pop_back();
  CHECK_THROWS(parse_anc(c.b));
}

TEST(pose_order_is_x_then_y_then_z) {
  // Quarter turn about X then about Z: (0,1,0) -> (0,0,1) -> stays (0,0,1).
  // The opposite order would give (-1,0,0) -> (-1,0,0).
  const Mat3 m = rotation_xyz(Vec3s{1024, 0, 1024});
  const Vec3f v = m.apply({0, 1, 0});
  CHECK(std::fabs(v.x) < 1e-5f && std::fabs(v.y) < 1e-5f && std::fabs(v.z - 1) < 1e-5f);
}

TEST(course_script_minimal) {
  Builder s;
  s.u32(1).u32(1).u32(1).u32(1).u32(1).u32(1);  // one of everything
  s.u32(1).u32(0).u32(0).u32(0);                // header unknowns, list of one
  s.u32(2).u32(0).u32(9);                       // fixed pattern: Block, LoopWave
  s.u32(1).i32(11).i32(3);
  for (int i = 0; i < 10; ++i) s.i32(i < 2 ? 0 : 3);
  s.u32(2).i32(0).i32(8819).i32(26459).i32(0).i32(0).i32(22050).i32(8000).i32(20000).i32(0).i32(0);
  s.u32(1).i32(0).i32(0).i32(1).i32(0);
  s.u32(1).i32(0).i32(0).i32(2).i32(0).i32(0).i32(15);
  s.u32(0).u32(0).u32(0);
  const CourseScript c = parse_course_script(s.b);
  CHECK(c.fixed.size() == 1 && c.fixed[0].sequence[1] == Obstacle::LoopWave);
  CHECK(c.distributions[0].seed == 11 && c.distributions[0].cumulative[9] == 3);
  CHECK(c.tracks.size() == 1 && c.tracks[0].control.size() == 2 && c.tracks[0].control[1].start == 22050);
  CHECK(c.tracks[0].patterns[0].type == PatternType::Fixed && c.tracks[0].events[0].camera_event == 15);
  s.b.push_back(0);
  CHECK_THROWS(parse_course_script(s.b));  // trailing bytes
}

TEST(spu_adpcm_decoding) {
  // Block 1: filter 0, shift 12, every nibble 1 -> every sample 1.
  // Block 2: filter 1, shift 12, nibbles 0 -> each sample = previous * 60/64, rounded.
  // Block 2 also carries the end flag without repeat, so the sample does not loop.
  Bytes b(32, 0);
  b[0] = 0x0C;
  for (int i = 2; i < 16; ++i) b[std::size_t(i)] = 0x11;
  b[16] = 0x1C;
  b[17] = 0x01;
  const DecodedSample d = decode_spu_adpcm(b);
  CHECK(d.pcm.size() == 56);
  CHECK(d.pcm[0] == 1 && d.pcm[27] == 1);
  CHECK(d.pcm[28] == 1);  // (1*60 + 32) >> 6 = 1
  CHECK(d.loop_start == -1);
  // A negative nibble: 0xF is -1 before shifting, so shift 12 gives -1.
  Bytes n(16, 0);
  n[0] = 0x0C;
  n[2] = 0x0F;
  CHECK(decode_spu_adpcm(n).pcm[0] == -1);
}

TEST(vh_layout_and_vb_split) {
  // One program with one tone pointing at sample 1 of 1, 32 bytes long.
  Bytes vh(32 + 128 * 16 + 1 * 16 * 32 + 256 * 2, 0);
  vh[0] = 'p'; vh[1] = 'B'; vh[2] = 'A'; vh[3] = 'V';
  vh[4] = 7;
  vh[18] = 1;   // programs
  vh[20] = 1;   // tones
  vh[22] = 1;   // samples
  vh[32] = 1;   // program 0 has one tone
  const std::size_t tone = 32 + 128 * 16;
  vh[tone + 4] = 60;  // centre note
  vh[tone + 22] = 1;  // sample 1
  const std::size_t table = tone + 16 * 32;
  vh[table + 2] = 4;  // sample 1 is 4 * 8 = 32 bytes
  const SoundBank bank = parse_vh(vh);
  CHECK(bank.programs.size() == 1 && bank.programs[0].tones.size() == 1);
  CHECK(bank.programs[0].tones[0].centre_note == 60 && bank.programs[0].tones[0].sample == 1);
  CHECK(bank.sample_sizes.size() == 1 && bank.sample_sizes[0] == 32);
  CHECK(split_vb(bank, Bytes(32, 0)).size() == 1);
  CHECK_THROWS(split_vb(bank, Bytes(48, 0)));
  vh.push_back(0);
  CHECK_THROWS(parse_vh(vh));
}
