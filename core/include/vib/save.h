#pragma once
// What is kept between plays: the high scores of every course and the options.
// Stored as plain text (one "key value" pair per line) so it is easy to read,
// fix by hand and carry between PC and headset. Unknown keys are ignored and
// missing ones keep their defaults, so older and newer files both load.

#include <array>
#include <cstdint>
#include <string>

#include "vib/gameplay.h"

namespace vib {

// vr_ahead_seconds or vr_behind_seconds at or above this: the whole song (the course
// drawn to its end, or kept from its start), so the line runs on into the horizon.
constexpr double kWholeSongSeconds = 10000.0;

// The scroll speed's range and step (Options::scroll_speed).
constexpr double kScrollSpeedMin = 0.5, kScrollSpeedMax = 10.0, kScrollSpeedStep = 0.25;
// Into the range, on the nearest step.
double clamp_scroll_speed(double v);

// Custom colours: how many slots (see vib/colours.h for what each is).
constexpr int kColourSlotCount = 17;

// The headset's line width moves in these steps (owner), from one step to 1.0.
constexpr double kHeadsetLineStep = 0.005;

struct Options {
  // The language of the menus and words: 0 German, 1 English, 2 Spanish, 3 French,
  // 4 Italian; -1 until the player has chosen one (the title then asks first).
  int language = -1;
  // The original's two options.
  bool sound_effects = true;
  bool vibration = true;
  // Ours. Courses vary from play to play like the original unless this is set.
  bool fixed_courses = false;
  // Blend Vibri's, the menus' and the cameras' poses into each other. The original
  // steps through them (15 to 30 a second for most), so this is off by default.
  bool smooth_animation = false;
  double line_width = 0.3;     // relative to the original's one pixel on a 288-line picture
  double vr_line_width = 0.05;  // the same, in a headset (an eye's picture is much taller; owner's default)
  double vr_zoom = 1.0;         // in a headset, how close the view stands to Vibri in play (larger: closer)
  double vr_behind_seconds = 30.0;  // in a headset, how long passed parts stay on the line, 2 s at least (kWholeSongSeconds: all of them)
  double vr_ahead_seconds = 30.0;   // and how far ahead it is drawn (play itself builds 8 s ahead; kWholeSongSeconds: to the end)
  double vr_menu_zoom = 1.0;    // in a headset, the menus' cameras moved towards what they look at (larger: closer)
  int vr_jitter_hold = 3;       // in a headset the jitter changes every this many of its frames (1-3; the owner's choice)
  double jitter_scale = 1.0;    // how far the lines wobble, times the original's (0: not at all)
  double vr_jitter_scale = 1.0; // the same in a headset
  int vr_refresh_hz = 0;        // the headset's refresh rate to ask for; 0: the highest it offers up to 120 Hz
  bool resume_countdown = false;  // after a pause: count in at the same place, instead of rewinding a little
  int perf_overlay = 0;           // ours: the performance overlay, 0 off, 1 one line, 2 detailed
  bool score_numbers = false;     // ours: the score in numbers as well as the marks, during play
  // Ours, as DDR's and StepMania's speed modifier: how fast the road is drawn coming
  // towards Vibri (0.5 to 10 in steps of 0.25). Display only: every part still reaches
  // her at the same moment and is judged the same; the music is untouched.
  double scroll_speed = 1.0;
  // Ours, as StepMania's offset display: each hit shows how early (-) or late (+) the
  // press was, in milliseconds, for a moment near Vibri.
  bool timing_feedback = false;
  // Ours: "Full Combo!" pops up at the end of a song cleared without a miss.
  bool fc_celebration = false;
  // Ours, headset only: the course wrapped round the player in a ring of this radius
  // (metres), Vibri walking it, the new road coming round one side and the old going
  // round the other, both fading out behind the player.
  bool vr_wrap = false;
  double vr_wrap_radius = 3.0;
  // Ours, headset only (task 4.2): the game exactly as on a monitor (the original's
  // cameras and HUD), shown on a flat 4:3 screen this many metres wide, 3 m in front.
  bool vr_classic = false;
  double vr_classic_size = 3.0;
  // Ours: custom colours, a palette choice per slot (vib/colours.h); all 0 is the
  // original's white lines on black.
  std::array<std::uint8_t, kColourSlotCount> colours{};
  std::string music_folder;       // ours: the player's music folder (UTF-8), picked the first time; empty until then
  double vr_angle = 45.0;       // and where around her, degrees: 0 behind, 90 to her right, -90 to her left
  double audio_offset_ms = 0;  // added to the estimated output latency
  // Ours: the picture drawn this far ahead of what is heard (ms; negative: behind), to
  // make up for a display's own delay. Judging stays on the sound.
  double picture_offset_ms = 0;
  // Ours: a soft halo round every line: 0 off (the original), 1 faint to 4 strong.
  int glow = 0;
  // Ours, task 4.16 (the Ultimate PC version): the headset's enhancements on a monitor.
  // How the lines wobble on the monitor: 0 at the original's 50 Hz (scaled by
  // jitter_scale), 1-3 afresh every this many of the monitor's frames, as in a headset.
  int jitter_hold = 0;
  // Play's camera: 0 the original's, 1 beside Vibri (the headset's view; the score
  // above her and the meter and numbers below). The monitor starts with the original's,
  // a headset with ours.
  int camera = 0;
  int vr_camera = 1;
  // On the monitor with the camera beside Vibri: how close (larger: closer), where
  // round her (degrees, as vr_angle) and how wide the view is (degrees, vertical).
  double zoom = 1.0;
  double view_angle = 45.0;
  double fov = 60.0;
  // On the monitor: how far ahead the road is drawn and how long passed parts stay
  // (as vr_ahead_seconds and vr_behind_seconds). 8 s and 2 s are the original's.
  double ahead_seconds = 8.0;
  double behind_seconds = 2.0;
  // The monitor's window: 0 a window, 1 borderless over the whole screen, 2 fullscreen
  // in the display mode below (0 x 0 at 0 Hz: the desktop's).
  int window_mode = 0;
  int fullscreen_w = 0, fullscreen_h = 0, fullscreen_hz = 0;
  bool vsync = true;      // off: frames as fast as they come (no tearing where the driver offers it)
  int frame_limit = 0;    // frames a second at most (0: none)
  bool picture_43 = false;  // the original's 4:3 picture with bars at the sides, instead of the whole window
  float music_volume = 1.0f, effects_volume = 1.0f;
};

// What the original game itself would have for each of our settings (its one-pixel
// lines, its jitter changing every frame, nothing smoothed, 8 s of course ahead and
// about 2 s behind, no sound offset, no overlay). The settings show these after each name, so a
// player can find the original's look again; they are not our defaults. Settings
// with no counterpart in the original (zoom, view angle, the pause's resume) keep ours.
Options original_options();

// The look settings (lines, wobble, smoothing, horizon, glow, colours, scroll speed and
// our display extras; the headset's lines go to our default 0.05) set to the original's in one go (Visual > Original look, for
// comparing with the original), and copied from one set of options to another (its
// Restore). Timing, sound, language, controls and the like are left alone.
void apply_original_look(Options& o);
void copy_look(const Options& from, Options& to);

struct SaveData {
  // Three per course, best first. Speed courses keep seconds: fewer is better.
  std::array<std::array<int, 3>, kCourseCount> high_scores{};
  Options options;

  SaveData();  // the original's default tables
  // Enters a result; returns its place (1-3), or 0 if it did not make the table.
  int record(Course course, int score);
  void reset_high_scores();

  std::string to_text() const;
  static SaveData from_text(const std::string& text);
};

bool is_speed_course(Course c);

}  // namespace vib
