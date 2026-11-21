#pragma once
// Ours (not the original's): custom colours. Every line keeps its own brightness and
// is tinted by the colour chosen for what it belongs to (SceneLine::part): the road
// line, each of the ten obstacles, Vibri in each of her four forms. The background
// has a palette of its own (dark colours, so the lines still show). White lines on
// black, the original's look, is the default.

#include <array>
#include <cstdint>
#include <cstdlib>

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
  kColourMenu = 16,        // the menus' and our settings' items (added last: saves keep)
  kColourSlots = 17,
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
  if (part == line_part::kMenu) return kColourMenu;
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

inline Rgb background_colour(const std::array<std::uint8_t, kColourSlots>& choices);

// A light background (white): lines would vanish into it.
inline bool light_background(const std::array<std::uint8_t, kColourSlots>& choices);

// A line's colour as shown: its custom colour if it has one (tinted); every other line
// (the menus, the settings, text, the HUD, the ring) keeps its own, inverted on a light
// background, so the settings' chosen row (white) and other rows (grey) never match it.
inline Rgb shown_colour(Rgb colour, std::uint8_t part, const std::array<std::uint8_t, kColourSlots>& choices) {
  if (part == line_part::kMenu) {
    // Menu items in the chosen colour (the chosen row full, the others dimmer, as
    // their own brightness is kept), but never the background's own colour: then it
    // is darkened (light background) or lightened (dark), so it still shows.
    Rgb c = colour;
    if (!light_background(choices) || choices[std::size_t(kColourMenu)] != 0) c = tinted(colour, part, choices);
    else c = {std::uint8_t(255 - colour.r), std::uint8_t(255 - colour.g), std::uint8_t(255 - colour.b)};  // (white on white: inverted, as before)
    const Rgb b = background_colour(choices);
    auto lum = [](Rgb x) { return (299 * x.r + 587 * x.g + 114 * x.b) / 1000; };
    // Only when it is the background's own colour, near enough (owner: black menus
    // turned grey on every dark background; now only black on black, white on white).
    const int dr = int(c.r) - b.r, dg = int(c.g) - b.g, db = int(c.b) - b.b;
    if (dr * dr + dg * dg + db * db < 40 * 40) {
      const bool light = lum(b) > 128;
      c = light ? Rgb{std::uint8_t(c.r * 35 / 100), std::uint8_t(c.g * 35 / 100), std::uint8_t(c.b * 35 / 100)}
                : Rgb{std::uint8_t(255 - (255 - c.r) * 35 / 100), std::uint8_t(255 - (255 - c.g) * 35 / 100), std::uint8_t(255 - (255 - c.b) * 35 / 100)};
    }
    return c;
  }
  if (colour_slot(part) >= 0) return tinted(colour, part, choices);
  if (!light_background(choices)) return colour;
  return {std::uint8_t(255 - colour.r), std::uint8_t(255 - colour.g), std::uint8_t(255 - colour.b)};
}

inline Rgb background_colour(const std::array<std::uint8_t, kColourSlots>& choices) {
  const int choice = choices[std::size_t(kColourBackground)];
  return kBackgroundPalette[std::size_t(choice >= 0 && choice < kBackgroundColours ? choice : 0)];
}

inline bool light_background(const std::array<std::uint8_t, kColourSlots>& choices) {
  const Rgb b = background_colour(choices);
  return 299 * b.r + 587 * b.g + 114 * b.b > 128 * 1000;  // (perceived brightness over half)
}

}  // namespace vib
