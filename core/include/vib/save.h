#pragma once
// What is kept between plays: the high scores of every course and the options.
// Stored as plain text (one "key value" pair per line) so it is easy to read,
// fix by hand and carry between PC and headset. Unknown keys are ignored and
// missing ones keep their defaults, so older and newer files both load.

#include <array>
#include <string>

#include "vib/gameplay.h"

namespace vib {

// vr_ahead_seconds or vr_behind_seconds at or above this: the whole song (the course
// drawn to its end, or kept from its start), so the line runs on into the horizon.
constexpr double kWholeSongSeconds = 10000.0;

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
  double vr_line_width = 0.15;  // the same, in a headset (an eye's picture is much taller)
  double vr_zoom = 1.0;         // in a headset, how close the view stands to Vibri in play (larger: closer)
  double vr_behind_seconds = 30.0;  // in a headset, how long passed parts stay on the line, 2 s at least (kWholeSongSeconds: all of them)
  double vr_ahead_seconds = 30.0;   // and how far ahead it is drawn (play itself builds 8 s ahead; kWholeSongSeconds: to the end)
  double vr_menu_zoom = 1.0;    // in a headset, the menus' cameras moved towards what they look at (larger: closer)
  int vr_jitter_hold = 3;       // in a headset the jitter changes every this many of its frames (1-3; the owner's choice)
  double jitter_scale = 1.0;    // how far the lines wobble, times the original's (0: not at all)
  double vr_jitter_scale = 1.0; // the same in a headset
  int vr_refresh_hz = 0;        // the headset's refresh rate to ask for; 0: the highest it offers
  bool resume_countdown = false;  // after a pause: count in at the same place, instead of rewinding a little
  int perf_overlay = 0;           // ours: the performance overlay, 0 off, 1 one line, 2 detailed
  bool score_numbers = false;     // ours: the score in numbers as well as the marks, during play
  std::string music_folder;       // ours: the player's music folder (UTF-8), picked the first time; empty until then
  double vr_angle = 45.0;       // and where around her, degrees: 0 behind, 90 to her right, -90 to her left
  double audio_offset_ms = 0;  // added to the estimated output latency
  float music_volume = 1.0f, effects_volume = 1.0f;
};

// What the original game itself would have for each of our settings (its one-pixel
// lines, its jitter changing every frame, nothing smoothed, 8 s of course ahead and
// about 2 s behind, no sound offset, no overlay). The settings show these after each name, so a
// player can find the original's look again; they are not our defaults. Settings
// with no counterpart in the original (zoom, view angle, the pause's resume) keep ours.
Options original_options();

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
