// The on-screen touch controls (task 4.20).
#include "test.h"
#include "vib/gameplay.h"
#include "vib/touch_controls.h"

using namespace vib;

TEST(touch_buttons_press_their_actions_and_pairs) {
  TouchControls t;
  t.layout(16.0f / 9.0f);
  t.set_mode(TouchControls::Mode::Play);
  using B = TouchControls;
  const auto& block = t.place(B::kBlock);
  const auto& wave = t.place(B::kWave);
  CHECK(block.x < 0 && t.place(B::kLoop).x > 0 && block.y < t.place(B::kPit).y);  // left thumb: block above pit
  CHECK(t.down(1, block.x, block.y, 0.0).presses == kPressBlock && t.held(B::kBlock));
  CHECK(t.down(2, wave.x + wave.r, wave.y, 0.01).presses == kPressWave);  // (the edge counts too)
  t.up(1, block.x, block.y, 0.1);
  CHECK(!t.held(B::kBlock) && t.held(B::kWave));
  CHECK(t.down(3, 0.0f, 0.0f, 0.2).presses == 0);  // the middle: no button
  const auto& opt = t.place(B::kOptions);
  CHECK(t.down(4, opt.x, opt.y, 0.3).options);
  CHECK(!t.shown(B::kBack));  // (only in the menus)
}

TEST(touch_gestures_turn_zoom_tap_and_swipe) {
  TouchControls t;
  t.layout(2.0f);
  t.set_mode(TouchControls::Mode::Play);
  t.down(1, 0.0f, -0.3f, 0.0);
  CHECK(t.move(1, 0.2f, -0.3f, 0.1).turn > 0.19f);  // one finger in play: drag turns
  t.down(2, 0.4f, -0.3f, 0.2);                      // a second: pinch
  CHECK(t.move(2, 0.8f, -0.3f, 0.3).zoom > 0.5f);   // spread apart: zoom in
  t.clear();
  t.set_mode(TouchControls::Mode::Menus);
  t.down(1, 0.1f, 0.1f, 1.0);
  auto r = t.up(1, 0.11f, 0.1f, 1.1);
  CHECK(r.tap && r.tap_x == 0.11f);
  t.down(1, 0.0f, 0.5f, 2.0);
  CHECK(t.move(1, 0.0f, 0.0f, 2.1).turn == 0.0f);  // (one finger in the menus does not turn)
  CHECK(t.up(1, 0.0f, 0.0f, 2.2).swipe == 1);       // up
  t.down(1, 0.0f, 0.0f, 3.0);
  CHECK(t.up(1, 0.4f, 0.05f, 3.2).swipe == 4);      // right
  t.down(1, 0.0f, 0.0f, 4.0);
  CHECK(!t.up(1, 0.0f, 0.0f, 4.9).tap);             // held too long: not a tap
  const auto& back = t.place(TouchControls::kBack);
  CHECK(t.shown(TouchControls::kBack) && t.down(5, back.x, back.y, 5.0).back);
  CHECK(t.down(6, t.place(TouchControls::kBlock).x, t.place(TouchControls::kBlock).y, 5.0).presses == 0);  // (no play buttons in the menus)
}

TEST(touch_buttons_draw_in_both_styles) {
  TouchControls t;
  t.layout(16.0f / 9.0f);
  for (int b = 0; b < TouchControls::kButtonCount; ++b)
    for (const auto style : {TouchControls::Style::Shapes, TouchControls::Style::PlayStation}) {
      const auto lines = t.button_lines(TouchControls::Button(b), style, 1000.0f, 4000.0f, 200);
      CHECK(lines.size() >= 24);  // at least its ring
      const auto& p = t.place(TouchControls::Button(b));
      for (const SceneLine& l : lines) CHECK(std::abs(l.a.x / 1000.0f - p.x) <= p.r * 1.01f && l.a.z == 4000.0f);
    }
  CHECK(std::string(TouchControls::label(TouchControls::kBlock, TouchControls::Style::PlayStation)) == "L1");
  CHECK(TouchControls::label(TouchControls::kWave, TouchControls::Style::PlayStation) == nullptr);
  CHECK(TouchControls::label(TouchControls::kBlock, TouchControls::Style::Shapes) == nullptr);
}
