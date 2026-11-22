#pragma once
// On-screen touch controls for phones and tablets (task 4.20; ours, not the original's).
// Platform-free: the app turns its touch events into finger calls in picture units and
// draws the buttons from button_lines().
//
// Picture units: x across, y down, the picture's middle at 0, its top at y -1 and its
// bottom at 1; x runs from -aspect to aspect (aspect = width / height), so circles stay
// round.
//
// In play: four action buttons (two for each thumb, as the original's controller has
// them: block L1 above pit Down on the left, loop R1 above wave X on the right) and an
// options button at the top right. Pressing two at once makes a pair. A finger that
// starts away from the buttons drags the camera round (beside Vibri); two such fingers
// pinch to zoom. In the menus: a tap chooses (where it was tapped, for our settings'
// rows), a swipe moves the cursor, and the back button at the top left goes back.

#include <cstdint>
#include <map>
#include <vector>

#include "vib/scene.h"

namespace vib {

class TouchControls {
 public:
  enum class Style { Shapes = 0, PlayStation = 1 };  // obstacle shapes, or the original's L1, Down, R1, X
  enum class Mode { Play, Menus };
  enum Button { kBlock = 0, kPit, kLoop, kWave, kOptions, kBack, kButtonCount };
  struct Place {
    float x = 0, y = 0, r = 0;
  };

  // Lays the buttons out for a picture `aspect` wide (width / height).
  void layout(float aspect);
  float aspect() const { return aspect_; }
  const Place& place(Button b) const { return places_[b]; }
  void set_mode(Mode m);
  Mode mode() const { return mode_; }
  // Whether a button is drawn and can be pressed in the mode now.
  bool shown(Button b) const;
  // Held down by a finger now (drawn brighter).
  bool held(Button b) const;

  struct Result {
    std::uint8_t presses = 0;  // play: Press bits pressed by this event (vib/gameplay.h)
    bool options = false;      // the options button
    bool back = false;         // menus: the back button
    bool tap = false;          // menus: a tap away from the buttons, at tap_x, tap_y
    float tap_x = 0, tap_y = 0;
    int swipe = 0;             // menus: 1 up, 2 down, 3 left, 4 right
    float turn = 0;            // camera: across, in picture units (positive: dragged right)
    float zoom = 0;            // camera: log2 of the pinch's spread change (positive: spread apart)
  };
  Result down(std::int64_t finger, float x, float y, double seconds);
  Result move(std::int64_t finger, float x, float y, double seconds);
  Result up(std::int64_t finger, float x, float y, double seconds);
  void clear();  // every finger let go (the app went to the background)

  // A button's drawing at its place: the obstacle's shape or the PlayStation button's
  // mark (a ring with a cross for X, a ring with an arrow for Down; L1 and R1 are a ring
  // only: their letters are text, label() gives them). Lines in picture units scaled by
  // `unit` (the camera's half height), at depth `z`; grey `shade` 0-255.
  std::vector<SceneLine> button_lines(Button b, Style style, float unit, float z, std::uint8_t shade) const;
  static const char* label(Button b, Style style);  // "L1", "R1" for those; nullptr otherwise

 private:
  struct Finger {
    Button button = kButtonCount;  // the button it pressed, or none (a gesture)
    float x0 = 0, y0 = 0, x = 0, y = 0;
    double t0 = 0;
  };
  int hit(float x, float y) const;
  float spread() const;  // the two gesture fingers' distance apart (0 if not two)

  float aspect_ = 16.0f / 9.0f;
  Place places_[kButtonCount];
  Mode mode_ = Mode::Menus;
  std::map<std::int64_t, Finger> fingers_;
  float last_spread_ = 0;
};

}  // namespace vib
