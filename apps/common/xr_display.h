#pragma once
// The game in a headset through OpenXR and Vulkan (task 1.8, the stereo spike).
//
// Every layer the game draws is seen from where its own camera stands, placed at
// the headset's starting position and scaled to metres, so the player looks
// around each scene in stereo. Controller buttons arrive as gamepad button events
// (SDL), which the game already understands:
//   left trigger = block, right trigger = loop, X or Y = pit, A = wave and confirm,
//   B = back, menu button = start, left thumbstick = the d-pad in the menus.

#include <memory>

#include "line_renderer.h"

namespace vibr {

class XrDisplay : public Display {
 public:
  // `metres_per_unit`: the size of one unit of the original's world.
  explicit XrDisplay(float metres_per_unit);
  ~XrDisplay() override;
  XrDisplay(const XrDisplay&) = delete;
  XrDisplay& operator=(const XrDisplay&) = delete;

  const char* gpu_name() const override;
  int height() const override;
  // Waits for the headset's next frame, then draws it. Also polls the session and
  // the controllers; a closed session sends SDL_EVENT_QUIT.
  void draw(const std::vector<Layer>& layers, float line_width) override;

  const char* runtime_name() const;
  void set_scale(float metres_per_unit);
  float scale() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace vibr
