#pragma once
// What is kept between plays: the high scores of every course and the options.
// Stored as plain text (one "key value" pair per line) so it is easy to read,
// fix by hand and carry between PC and headset. Unknown keys are ignored and
// missing ones keep their defaults, so older and newer files both load.

#include <array>
#include <string>

#include "vib/gameplay.h"

namespace vib {

struct Options {
  // The original's two options.
  bool sound_effects = true;
  bool vibration = true;
  // Ours. Courses vary from play to play like the original unless this is set.
  bool fixed_courses = false;
  // Blend Vibri's, the menus' and the cameras' poses into each other. The original
  // steps through them (15 to 30 a second for most), so this is off by default.
  bool smooth_animation = false;
  double line_width = 0.3;     // relative to the original's one pixel on a 288-line picture
  double audio_offset_ms = 0;  // added to the estimated output latency
  float music_volume = 1.0f, effects_volume = 1.0f;
};

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
