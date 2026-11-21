// High scores and options. No game data needed.

#include "test.h"
#include "vib/save.h"

using namespace vib;

TEST(default_high_scores_are_the_originals) {
  SaveData d;
  CHECK((d.high_scores[int(Course::Bronze)] == std::array<int, 3>{6000, 4000, 2000}));
  CHECK((d.high_scores[int(Course::Gold)] == std::array<int, 3>{5500, 3500, 1500}));
  CHECK((d.high_scores[int(Course::Speed1)] == std::array<int, 3>{50, 60, 70}));
  CHECK(d.options.sound_effects && d.options.vibration && !d.options.fixed_courses && !d.options.smooth_animation);
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
  d.options.smooth_animation = true;
  d.options.line_width = 0.375;
  d.options.vr_line_width = 0.125;
  d.options.vr_zoom = 1.75;
  d.options.vr_angle = -30;
  d.options.language = 3;
  d.options.vr_jitter_hold = 2;
  d.options.jitter_scale = 0;
  d.options.vr_jitter_scale = 0.7;
  d.options.vr_refresh_hz = 120;
  d.options.audio_offset_ms = -12.5;
  d.options.perf_overlay = 2;
  d.options.score_numbers = true;
  d.options.scroll_speed = 2.75;
  d.options.timing_feedback = true;
  d.options.fc_celebration = true;
  d.options.vr_wrap = true;
  d.options.vr_wrap_radius = 4.5;
  d.options.music_folder = "C:/Users/Me/My Music/Vib Ribbon";
  const SaveData back = SaveData::from_text(d.to_text());
  CHECK(back.high_scores == d.high_scores);
  CHECK(!back.options.vibration && back.options.fixed_courses && back.options.smooth_animation && back.options.sound_effects);
  CHECK(back.options.line_width == 0.375 && back.options.vr_line_width == 0.125 && back.options.vr_zoom == 1.75 && back.options.vr_angle == -30 && back.options.language == 3 && back.options.vr_jitter_hold == 2 && back.options.jitter_scale == 0 && back.options.vr_jitter_scale == 0.7 &&
        back.options.vr_refresh_hz == 120 && back.options.audio_offset_ms == -12.5 && back.options.perf_overlay == 2 && back.options.score_numbers && back.options.music_folder == "C:/Users/Me/My Music/Vib Ribbon");
  CHECK(back.options.scroll_speed == 2.75 && SaveData().options.scroll_speed == 1.0);
  CHECK(back.options.timing_feedback && !SaveData().options.timing_feedback);
  CHECK(back.options.fc_celebration && !SaveData().options.fc_celebration);
  CHECK(back.options.vr_wrap && back.options.vr_wrap_radius == 4.5 && !SaveData().options.vr_wrap && SaveData().options.vr_wrap_radius == 3.0);
  CHECK(SaveData::from_text("vr_wrap_radius 50\n").options.vr_wrap_radius == 10.0);
  CHECK(SaveData::from_text("scroll_speed 12\n").options.scroll_speed == 10.0 && SaveData::from_text("scroll_speed 9\n").options.scroll_speed == 9.0 && SaveData::from_text("scroll_speed 0.1\n").options.scroll_speed == 0.5);
  CHECK(SaveData::from_text("scroll_speed 1.3\n").options.scroll_speed == 1.25);  // on a step
  CHECK(SaveData().options.music_folder.empty());
  CHECK(SaveData::from_text("vr_behind_seconds 0\n").options.vr_behind_seconds == 2.0);  // 2 s at least
  CHECK(SaveData().options.perf_overlay == 0 && SaveData::from_text("perf_overlay 7\n").options.perf_overlay == 0);  // off unless set
  // Lines missing, unknown keys and Windows line ends: defaults fill in, nothing breaks.
  const SaveData partial = SaveData::from_text("vibration 0\r\nsomething_new 5\r\nhigh_scores.gold 1 2\r\n");
  CHECK(!partial.options.vibration && partial.options.line_width == 0.3);
  CHECK((partial.high_scores[int(Course::Gold)] == std::array<int, 3>{5500, 3500, 1500}));
}
