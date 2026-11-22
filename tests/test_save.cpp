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
  d.options.colours[0] = 3;
  d.options.picture_offset_ms = -45;
  d.options.glow = 3;
  d.options.colours[15] = 4;
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
  {  // the classic screen (headset): off by default, 3 m wide, 1.5 to 8 m
    SaveData c;
    c.options.vr_classic = true;
    c.options.vr_classic_size = 4.5;
    const SaveData cb = SaveData::from_text(c.to_text());
    CHECK(cb.options.vr_classic && cb.options.vr_classic_size == 4.5 && !SaveData().options.vr_classic && SaveData().options.vr_classic_size == 3.0);
    CHECK(SaveData::from_text("vr_classic_size 20\n").options.vr_classic_size == 8.0);
  }
  {  // task 4.16: the monitor's view, wobble and window
    SaveData m;
    m.options.jitter_hold = 2, m.options.camera = 1, m.options.vr_camera = 0, m.options.zoom = 0.5, m.options.view_angle = -60;
    m.options.fov = 75, m.options.ahead_seconds = kWholeSongSeconds, m.options.behind_seconds = 30, m.options.window_mode = 2;
    m.options.fullscreen_w = 2560, m.options.fullscreen_h = 1440, m.options.fullscreen_hz = 144, m.options.vsync = false;
    m.options.disc_path = "F:/Vib Ribbon/disc.cue";
    m.options.frame_limit = 144, m.options.picture_43 = true, m.options.low_latency = true, m.options.menu_zoom = 2.0, m.options.menu_angle = 30;
    const Options b = SaveData::from_text(m.to_text()).options;
    CHECK(b.jitter_hold == 2 && b.camera == 1 && b.vr_camera == 0 && b.zoom == 0.5 && b.view_angle == -60 && b.fov == 75);
    CHECK(b.ahead_seconds == kWholeSongSeconds && b.behind_seconds == 30 && b.window_mode == 2 && b.fullscreen_w == 2560 &&
          b.fullscreen_h == 1440 && b.fullscreen_hz == 144 && !b.vsync && b.frame_limit == 144 && b.picture_43 && b.disc_path == "F:/Vib Ribbon/disc.cue");
    CHECK(SaveData::from_text("disc_path D:/Games/Vib Ribbon (Europe).cue\n").options.disc_path == "D:/Games/Vib Ribbon (Europe).cue");
    CHECK(SaveData().options.difficulty == 1 && SaveData::from_text("difficulty 2\n").options.difficulty == 2 &&
          SaveData::from_text("difficulty 7\n").options.difficulty == 1);
    CHECK(b.low_latency && b.menu_zoom == 2.0 && b.menu_angle == 30 && !SaveData().options.low_latency && SaveData().options.menu_zoom == 1.0);
    const Options d0 = SaveData().options;  // both start beside Vibri (owner); the monitor's wobble is the original's 50 Hz
    CHECK(d0.jitter_hold == 0 && d0.camera == 1 && d0.vr_camera == 1 && d0.fov == 60 && d0.ahead_seconds == 8 && d0.behind_seconds == 2);
    CHECK(d0.window_mode == 0 && d0.fullscreen_w == 0 && d0.vsync && d0.frame_limit == 0 && !d0.picture_43);
    CHECK(SaveData::from_text("fov 500\nahead_seconds 1\njitter_hold 9\ncamera 4\nwindow_mode 7\n").options.fov == 110.0);
    const Options bad = SaveData::from_text("ahead_seconds 1\njitter_hold 9\ncamera 4\nwindow_mode 7\nfullscreen_mode 1 2\n").options;
    CHECK(bad.ahead_seconds == 8.0 && bad.jitter_hold == 0 && bad.camera == 1 && bad.window_mode == 0 && bad.fullscreen_w == 0);
  }
  CHECK(back.options.glow == 3 && SaveData().options.glow == 0 && SaveData::from_text("glow 9\n").options.glow == 0);
  CHECK(back.options.picture_offset_ms == -45 && SaveData().options.picture_offset_ms == 0 &&
        SaveData::from_text("picture_offset_ms 900\n").options.picture_offset_ms == 300.0);
  CHECK(back.options.colours == d.options.colours && SaveData().options.colours == (std::array<std::uint8_t, kColourSlotCount>{}));
  CHECK(SaveData::from_text("colours 2 9\n").options.colours[0] == 2 && SaveData::from_text("colours 2 9\n").options.colours[1] == 8);
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

TEST(original_look_sets_the_look_and_leaves_the_rest) {
  Options o;
  o.glow = 3, o.colours[2] = 4, o.scroll_speed = 2.5, o.vr_wrap = true, o.timing_feedback = true, o.vr_line_width = 0.2;
  o.audio_offset_ms = 35, o.picture_offset_ms = -60, o.language = 3, o.vr_zoom = 2.0;
  o.camera = 1, o.jitter_hold = 2, o.ahead_seconds = 60, o.behind_seconds = 30;
  const Options before = o;
  apply_original_look(o);
  CHECK(o.glow == 0 && o.colours[2] == 0 && o.scroll_speed == 1.0 && !o.vr_wrap && !o.timing_feedback && !o.smooth_animation);
  CHECK(o.line_width == 1.0 && o.vr_line_width == Options{}.vr_line_width && o.vr_jitter_hold == 1 && o.vr_ahead_seconds == 8.0 && o.vr_behind_seconds == 2.0);
  CHECK(o.audio_offset_ms == 35 && o.picture_offset_ms == -60 && o.language == 3 && o.vr_zoom == 2.0);  // (not the look)
  CHECK(o.camera == 0 && o.jitter_hold == 1 && o.ahead_seconds == 8.0 && o.behind_seconds == 2.0 && o.picture_43);  // (the monitor's)
  CHECK(o.vr_camera == 1);  // (the headset keeps its view beside Vibri)
  copy_look(before, o);
  CHECK(o.glow == 3 && o.colours[2] == 4 && o.scroll_speed == 2.5 && o.vr_wrap && o.timing_feedback && o.vr_line_width == 0.2);
  CHECK(o.camera == 1 && o.jitter_hold == 2 && o.ahead_seconds == 60 && o.behind_seconds == 30 && !o.picture_43);
}
