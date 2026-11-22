#pragma once
// The game in a headset through OpenXR and Vulkan (task 1.8, the stereo spike).
//
// Every layer the game draws is seen from where its own camera stands, placed at
// the headset's starting position and scaled to metres, so the player looks
// around each scene in stereo. Controller buttons arrive as gamepad button events
// (SDL), which the game already understands, mapped by the mode the game is in
// (set_input_mode; owner's layout, session 5):
//   play:  left trigger block, right trigger loop, X / Y / left grip pit,
//          A / B / right grip wave, menu button start (pause), stick clicks L3 / R3;
//          the sticks are read by the game (left x: view angle, right y: zoom or
//          circle size, right x flicked: scroll speed).
//   menus: left stick the d-pad, A or right trigger confirm, B or left trigger back,
//          menu button start (settings), stick clicks L3 / R3; right stick y zooms.
#include <memory>

#include "line_renderer.h"

namespace vibr {

class XrDisplay : public Display {
 public:
  // `metres_per_unit`: the size of one unit of the original's world.
  // `refresh_hz`: the refresh rate to ask the headset for (0: the highest it offers),
  // where the runtime lets a program choose (XR_FB_display_refresh_rate).
  explicit XrDisplay(float metres_per_unit, float refresh_hz = 0);
  ~XrDisplay() override;
  XrDisplay(const XrDisplay&) = delete;
  XrDisplay& operator=(const XrDisplay&) = delete;

  const char* gpu_name() const override;
  int height() const override;
  // Waits for the headset's next frame, then draws it. Also polls the session and
  // the controllers; a closed session sends SDL_EVENT_QUIT.
  void draw(const std::vector<Layer>& layers, float line_width) override;
  void recentre() override;
  float zoom() const override;
  void set_zoom(float z) override;
  FrameTiming timing() const override;
  std::vector<float> refresh_rates() const override;
  void set_refresh_rate(float hz) override;
  void set_input_mode(InputMode mode) override;
  ControllerSticks controller_sticks() const override;

  const char* runtime_name() const;
  void set_scale(float metres_per_unit);
  float scale() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace vibr
