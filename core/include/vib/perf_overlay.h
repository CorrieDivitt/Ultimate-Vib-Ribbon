#pragma once
// Ours (task 4.6): the performance overlay. How steadily frames reach the display,
// how long the game's own work takes in each, how the sound clock runs, and how
// early or late the last presses were, kept over the last few seconds and written
// with the disc's line font in a corner of the view (in a headset, fixed in the
// room up and to the right of where the view was recentred, so it does not follow the head).
//
// FrameStats and perf_rows() are platform-free; the app measures and feeds them.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "vib/game_text.h"
#include "vib/pak.h"
#include "vib/scene.h"
#include "vib/tmd.h"

namespace vib {

// One frame as the display showed it.
struct FrameSample {
  std::uint64_t ns = 0;     // when it was measured (any steady clock, nanoseconds)
  double interval_ms = 0;   // since the frame before, as the display counts it
  double period_ms = 0;     // the display's own frame period; 0 if it does not say
  // The game's time on the processor in this frame, by part: its rules and sound
  // (update), making the lines (build), handing them to the GPU (submit), and
  // waiting for the display's next frame (wait).
  double update_ms = 0, build_ms = 0, submit_ms = 0, wait_ms = 0;
  int lines = 0;  // drawn
};

class FrameStats {
 public:
  static constexpr double kWindowSeconds = 1.0;  // averages and the worst frame
  static constexpr double kLateSeconds = 10.0;   // late frames are counted this far back
  static constexpr double kLateFactor = 1.5;     // a frame this many periods after the one before was late
  static constexpr std::size_t kPresses = 5;

  void add(const FrameSample& f);
  // A press that hit its part: how far from the part's own time (minus = early).
  void add_press(double offset_ms);
  // The sound clock (seconds of music heard) at a moment of the same steady clock as
  // the frames. Forget the history whenever the music starts again or stops.
  void add_audio(std::uint64_t ns, double heard_seconds);
  void reset_audio() { audio_.clear(); }
  void reset() { frames_.clear(), audio_.clear(), presses_.clear(); }

  struct Summary {
    int frames = 0;           // in the last second
    double rate_hz = 0;       // the display's rate (from its period, else frames counted)
    double target_ms = 0;     // its frame period (or the median interval, if it gives none)
    double average_ms = 0, worst_ms = 0;
    int late = 0;             // in the last 10 s
    double update_ms = 0, build_ms = 0, submit_ms = 0, wait_ms = 0;  // averages over the last second
    int lines = 0;            // the last frame's
    bool audio = false;       // the sound clock ran for at least half of the last second
    double drift_ms = 0;      // sound clock minus steady clock, ms per second
    std::vector<double> presses;  // the last few, oldest first
  };
  Summary summary(std::uint64_t now_ns) const;
  // The intervals of the last `seconds`, oldest first (the graph).
  std::vector<double> intervals(std::uint64_t now_ns, double seconds = kWindowSeconds) const;

 private:
  struct Audio {
    std::uint64_t ns;
    double heard;
  };
  std::deque<FrameSample> frames_;  // the last 10 s
  std::deque<Audio> audio_;         // the last second
  std::deque<double> presses_;
};

// The overlay's rows as the line font's text: `level` 1 one line, 2 everything.
// `latency_ms` < 0: the sound's output latency is not known (left out).
// Only characters the font has: letters, digits, space, . , - ( ).
std::vector<std::string> perf_rows(const FrameStats::Summary& s, int level, double latency_ms);

class PerfOverlay {
 public:
  // In a headset the layer is anchored in the room at the recentred place, at this
  // scale: its camera's 1000 units ahead are 1.5 m (the app marks the layer anchored).
  static constexpr float kAnchorMetresPerUnit = 0.0015f;

  // `pak`: the game's own archive (the font); `text`: the program's character table
  // (without it no text is drawn, the graph still is).
  PerfOverlay(const Pak& game_pak, int language, const GameText* text);

  // The rows and, if `graph_ms` is not empty, a frame-time graph under them (a bar
  // a frame, the target period at half height, late frames in red).
  // `vr`: up and to the right of the recentred view, 1.5 m away; else the monitor's top left, for a
  // picture `aspect` wide per unit of height.
  Hud::Layer draw(const std::vector<std::string>& rows, const std::vector<double>& graph_ms, double target_ms, bool vr, float aspect);

 private:
  const GameText* text_;
  TmdModel font_;
  LineBuilder lines_;
};

}  // namespace vib
