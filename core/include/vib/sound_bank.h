#pragma once
// PlayStation sound banks: a .VH header (VAB layout, magic "pBAV") describing
// programs and tones, and a .VB body holding the ADPCM samples back to back.
// On the supported disc: GAME/AUDIO/PSJ_SE (gameplay effects), TITLE/AUDIO/TITLE
// and KIOSK/AUDIO/KIOSK.

#include <cstdint>
#include <vector>

#include "vib/bytes.h"

namespace vib {

struct VabTone {
  std::uint8_t priority = 0, mode = 0, volume = 0, pan = 0;
  std::uint8_t centre_note = 0, centre_fine = 0;  // the game plays tone t at note t + 1; equal to the centre means base rate
  std::uint8_t note_min = 0, note_max = 0;
  std::uint16_t adsr1 = 0, adsr2 = 0;  // SPU envelope registers
  std::int16_t program = 0;
  std::int16_t sample = 0;  // 1-based index into SoundBank::samples
};

struct VabProgram {
  std::uint8_t tone_count = 0, volume = 0, priority = 0, mode = 0, pan = 0;
  std::vector<VabTone> tones;
};

struct SoundBank {
  std::uint32_t version = 0;
  std::uint8_t master_volume = 0, master_pan = 0;
  std::vector<VabProgram> programs;     // only the programs the header declares
  std::vector<std::uint32_t> sample_sizes;  // bytes of ADPCM per sample, in VB order
};

SoundBank parse_vh(ByteView vh);

// Splits the VB body into one ADPCM block per sample. Throws if the sizes do not add up.
std::vector<ByteView> split_vb(const SoundBank& bank, ByteView vb);

struct DecodedSample {
  std::vector<std::int16_t> pcm;  // mono; plays at 44,100 Hz when keyed at the centre note
  int loop_start = -1;            // sample index, or -1 if the sample does not loop
};

// PlayStation SPU ADPCM: 16-byte blocks of 28 samples each.
DecodedSample decode_spu_adpcm(ByteView adpcm);

}  // namespace vib
