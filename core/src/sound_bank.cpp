#include "vib/sound_bank.h"

#include <algorithm>
#include <string>

namespace vib {
namespace {

constexpr std::size_t kHeaderSize = 32;
constexpr std::size_t kProgramSlots = 128;
constexpr std::size_t kProgramSize = 16;
constexpr std::size_t kTonesPerProgram = 16;
constexpr std::size_t kToneSize = 32;
constexpr std::size_t kSampleTableEntries = 256;

}  // namespace

SoundBank parse_vh(ByteView b) {
  require(b.size() >= kHeaderSize && b[0] == 'p' && b[1] == 'B' && b[2] == 'A' && b[3] == 'V', "not a VH sound bank header");
  SoundBank bank;
  bank.version = read_u32(b, 4);
  const std::uint16_t program_count = read_u16(b, 18);
  const std::uint16_t sample_count = read_u16(b, 22);
  bank.master_volume = b[24];
  bank.master_pan = b[25];
  require(program_count <= kProgramSlots && sample_count < kSampleTableEntries, "VH counts are out of range");

  const std::size_t tones_at = kHeaderSize + kProgramSlots * kProgramSize;
  const std::size_t table_at = tones_at + std::size_t(program_count) * kTonesPerProgram * kToneSize;
  require(table_at + kSampleTableEntries * 2 == b.size(), "VH size does not match its program count");

  for (std::size_t p = 0; p < program_count; ++p) {
    const std::size_t at = kHeaderSize + p * kProgramSize;
    VabProgram prog;
    prog.tone_count = b[at];
    prog.volume = b[at + 1];
    prog.priority = b[at + 2];
    prog.mode = b[at + 3];
    prog.pan = b[at + 4];
    require(prog.tone_count <= kTonesPerProgram, "VH program has too many tones");
    for (std::size_t t = 0; t < prog.tone_count; ++t) {
      const std::size_t ta = tones_at + (p * kTonesPerProgram + t) * kToneSize;
      VabTone tone;
      tone.priority = b[ta];
      tone.mode = b[ta + 1];
      tone.volume = b[ta + 2];
      tone.pan = b[ta + 3];
      tone.centre_note = b[ta + 4];
      tone.centre_fine = b[ta + 5];
      tone.note_min = b[ta + 6];
      tone.note_max = b[ta + 7];
      tone.adsr1 = read_u16(b, ta + 16);
      tone.adsr2 = read_u16(b, ta + 18);
      tone.program = read_i16(b, ta + 20);
      tone.sample = read_i16(b, ta + 22);
      require(tone.sample >= 0 && tone.sample <= sample_count, "VH tone points at a missing sample");
      prog.tones.push_back(tone);
    }
    bank.programs.push_back(std::move(prog));
  }

  // Entry 0 is always 0; entry i (1-based) is sample i's size divided by 8.
  for (std::size_t i = 1; i <= sample_count; ++i) bank.sample_sizes.push_back(std::uint32_t(read_u16(b, table_at + i * 2)) * 8);
  return bank;
}

std::vector<ByteView> split_vb(const SoundBank& bank, ByteView vb) {
  std::vector<ByteView> out;
  std::size_t at = 0;
  for (const std::uint32_t size : bank.sample_sizes) {
    require(at + size <= vb.size(), "VB is shorter than its VH says");
    out.push_back(vb.subspan(at, size));
    at += size;
  }
  require(at == vb.size(), "VB has " + std::to_string(vb.size() - at) + " bytes the VH does not describe");
  return out;
}

DecodedSample decode_spu_adpcm(ByteView b) {
  static constexpr int kF0[5] = {0, 60, 115, 98, 122};
  static constexpr int kF1[5] = {0, 0, -52, -55, -60};
  DecodedSample out;
  int s1 = 0, s2 = 0;
  for (std::size_t block = 0; block + 16 <= b.size(); block += 16) {
    int shift = b[block] & 0x0F;
    if (shift > 12) shift = 9;  // what the SPU does with reserved shift values
    const int filter = std::min(b[block] >> 4, 4);
    const std::uint8_t flags = b[block + 1];
    if ((flags & 4) && out.loop_start < 0) out.loop_start = static_cast<int>(out.pcm.size());
    for (int i = 0; i < 28; ++i) {
      const std::uint8_t byte = b[block + 2 + std::size_t(i) / 2];
      const int nibble = (i & 1) ? (byte >> 4) : (byte & 0x0F);
      const int raw = static_cast<std::int16_t>(static_cast<std::uint16_t>(nibble << 12)) >> shift;
      int s = raw + ((s1 * kF0[filter] + s2 * kF1[filter] + 32) >> 6);
      s = std::clamp(s, -32768, 32767);
      out.pcm.push_back(static_cast<std::int16_t>(s));
      s2 = s1;
      s1 = s;
    }
    if (flags & 1) {  // end of sample: bit 1 set means jump to the loop start
      if (!(flags & 2)) out.loop_start = -1;
      break;
    }
  }
  return out;
}

}  // namespace vib
