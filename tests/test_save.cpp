// High scores and options. No game data needed.

#include "test.h"
#include "vib/save.h"

using namespace vib;

TEST(default_high_scores_are_the_originals) {
  SaveData d;
  CHECK((d.high_scores[int(Course::Bronze)] == std::array<int, 3>{6000, 4000, 2000}));
  CHECK((d.high_scores[int(Course::Gold)] == std::array<int, 3>{5500, 3500, 1500}));
  CHECK((d.high_scores[int(Course::Speed1)] == std::array<int, 3>{50, 60, 70}));
  CHECK(d.options.sound_effects && d.options.vibration && !d.options.fixed_courses);
}

TEST(recording_pushes_the_rest_down) {
  SaveData d;
  CHECK(d.record(Course::Bronze, 1000) == 0);
  CHECK(d.record(Course::Bronze, 5000) == 2);
  CHECK((d.high_scores[0] == std::array<int, 3>{6000, 5000, 4000}));
  CHECK(d.record(Course::Bronze, 6000) == 2);  // a tie does not beat the 6000, but beats the 5000
  CHECK(d.record(Course::Bronze, 7000) == 1);
  CHECK((d.high_scores[0] == std::array<int, 3>{7000, 6000, 6000}));
  // Speed courses keep seconds, fewer is better.
  CHECK(d.record(Course::Speed1, 55) == 2);
  CHECK((d.high_scores[int(Course::Speed1)] == std::array<int, 3>{50, 55, 60}));
}

TEST(save_text_round_trips_and_tolerates_old_files) {
  SaveData d;
  d.record(Course::Silver, 9999);
  d.options.vibration = false;
  d.options.fixed_courses = true;
  d.options.line_width = 0.375;
  d.options.audio_offset_ms = -12.5;
  const SaveData back = SaveData::from_text(d.to_text());
  CHECK(back.high_scores == d.high_scores);
  CHECK(!back.options.vibration && back.options.fixed_courses && back.options.sound_effects);
  CHECK(back.options.line_width == 0.375 && back.options.audio_offset_ms == -12.5);
  // Lines missing, unknown keys and Windows line ends: defaults fill in, nothing breaks.
  const SaveData partial = SaveData::from_text("vibration 0\r\nsomething_new 5\r\nhigh_scores.gold 1 2\r\n");
  CHECK(!partial.options.vibration && partial.options.line_width == 0.3);
  CHECK((partial.high_scores[int(Course::Gold)] == std::array<int, 3>{5500, 3500, 1500}));
}
