#pragma once
// Ours (not the original's): custom colours. Every line keeps its own brightness and
// is tinted by the colour chosen for what it belongs to (SceneLine::part): the road
// line, each of the ten obstacles, Vibri in each of her four forms. The background
// has a palette of its own (dark colours, so the lines still show). White lines on
// black, the original's look, is the default.

#include <array>
#include <cstdint>

#include "vib/save.h"
#include "vib/scene.h"
#include "vib/tmd.h"

namespace vib {

// The colour slots (Options::colours), in the order the settings list them.
enum ColourSlot : int {
  kColourRoad = 0,
  kColourObstacle = 1,     // + road 0-9 (block, pit, loop, wave, then the pairs)
  kColourForm = 11,        // + form 0-3 (angel, rabbit, frog, worm)
  kColourBackground = 15,
  kColourSlots = 16,
};
static_assert(kColourSlots == kColourSlotCount, "Options::colours has a choice per slot");

// The lines' palette: white (the original), red, orange, yellow, green, cyan, blue,
// pink, black (owner: black and white in every palette; added last so saves keep).
constexpr int kLineColours = 9;
constexpr std::array<Rgb, kLineColours> kLinePalette = {{
    {255, 255, 255}, {255, 70, 70}, {255, 150, 40}, {255, 230, 60},
    {80, 235, 90}, {60, 225, 235}, {80, 130, 255}, {255, 110, 210}, {0, 0, 0},
}};
// The background's: black (the original), navy, purple, dark green, grey, white.
constexpr int kBackgroundColours = 6;
constexpr std::array<Rgb, kBackgroundColours> kBackgroundPalette = {{
    {0, 0, 0}, {10, 18, 60}, {40, 12, 58}, {8, 42, 22}, {48, 48, 52}, {255, 255, 255},
}};

// How many colours a slot can take.
constexpr int colour_choices(int slot) { return slot == kColourBackground ? kBackgroundColours : kLineColours; }

// The slot a scene line's colour comes from; -1 for lines that keep theirs (the
// evolution ring, anything untagged).
constexpr int colour_slot(std::uint8_t part) {
  if (part == line_part::kRoad) return kColourRoad;
  if (part >= line_part::kObstacle && part < line_part::kObstacle + 10) return kColourObstacle + (part - line_part::kObstacle);
  if (part >= line_part::kVibri && part < line_part::kVibri + 4) return kColourForm + (part - line_part::kVibri);
  return -1;
}

// A line's colour tinted by its slot's choice (`choices`: Options::colours): each
// channel times the tint's, so a line's own brightness (dimmed, hidden) is kept.
inline Rgb tinted(Rgb colour, std::uint8_t part, const std::array<std::uint8_t, kColourSlots>& choices) {
  const int slot = colour_slot(part);
  if (slot < 0) return colour;
  const int choice = choices[std::size_t(slot)];
  if (choice <= 0 || choice >= kLineColours) return colour;  // white: as it is
  const Rgb t = kLinePalette[std::size_t(choice)];
  return {std::uint8_t(colour.r * t.r / 255), std::uint8_t(colour.g * t.g / 255), std::uint8_t(colour.b * t.b / 255)};
}

inline Rgb background_colour(const std::array<std::uint8_t, kColourSlots>& choices) {
  const int choice = choices[std::size_t(kColourBackground)];
  return kBackgroundPalette[std::size_t(choice >= 0 && choice < kBackgroundColours ? choice : 0)];
}

}  // namespace vib
