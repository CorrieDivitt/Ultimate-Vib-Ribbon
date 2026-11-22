#include "vib/save.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace vib {

namespace {
const char* const kCourseKeys[kCourseCount] = {"bronze", "silver", "gold", "one_track", "all_tracks",
                                               "speed1", "speed2", "speed3", "in_order"};

bool parse_bool(const std::string& v) { return v == "1" || v == "true" || v == "on" || v == "yes"; }
}  // namespace

bool is_speed_course(Course c) { return c == Course::Speed1 || c == Course::Speed2 || c == Course::Speed3; }

SaveData::SaveData() { reset_high_scores(); }

void SaveData::reset_high_scores() {
  for (int c = 0; c < kCourseCount; ++c)
    for (int i = 0; i < 3; ++i) high_scores[std::size_t(c)][std::size_t(i)] = course_info(Course(c)).high_scores[i];
}

int SaveData::record(Course course, int score) {
  // As the original: the first place the new result beats takes it, and the rest move down.
  auto& table = high_scores[std::size_t(course)];
  const bool fewer_is_better = is_speed_course(course);
  for (std::size_t i = 0; i < table.size(); ++i) {
    if (fewer_is_better ? score < table[i] : table[i] < score) {
      for (std::size_t j = table.size() - 1; j > i; --j) table[j] = table[j - 1];
      table[i] = score;
      return int(i) + 1;
    }
  }
  return 0;
}

double clamp_scroll_speed(double v) {
  if (!(v == v)) return 1.0;  // (not a number)
  return std::clamp(std::round(v / kScrollSpeedStep) * kScrollSpeedStep, kScrollSpeedMin, kScrollSpeedMax);
}

Options original_options() {
  Options o;
  o.smooth_animation = false;
  o.line_width = o.vr_line_width = 1.0;  // one pixel on the original's 288 lines
  o.vr_jitter_hold = 1;                  // a new jitter every frame
  o.jitter_scale = o.vr_jitter_scale = 1.0;
  o.vr_ahead_seconds = 8.0;   // what play builds ahead
  o.vr_behind_seconds = 2.0;  // the original erases passed parts about 4.25 lengths back: some 2 s
  o.audio_offset_ms = 0;
  o.perf_overlay = 0;
  o.jitter_hold = 0;  // the monitor: the original's own 50 Hz, shown as Off (owner, session 6)
  o.camera = o.vr_camera = 0;
  o.ahead_seconds = 8.0, o.behind_seconds = 2.0;
  o.picture_43 = true;
  return o;
}

void copy_look(const Options& from, Options& to) {
  to.smooth_animation = from.smooth_animation;
  to.line_width = from.line_width, to.vr_line_width = from.vr_line_width;
  to.vr_jitter_hold = from.vr_jitter_hold;
  to.jitter_scale = from.jitter_scale, to.vr_jitter_scale = from.vr_jitter_scale;
  to.vr_ahead_seconds = from.vr_ahead_seconds, to.vr_behind_seconds = from.vr_behind_seconds;
  to.glow = from.glow;
  to.colours = from.colours;
  to.scroll_speed = from.scroll_speed;
  to.score_numbers = from.score_numbers, to.timing_feedback = from.timing_feedback, to.fc_celebration = from.fc_celebration;
  to.vr_wrap = from.vr_wrap;
  to.jitter_hold = from.jitter_hold;
  to.camera = from.camera;  // (the monitor's; the headset keeps its view beside Vibri)
  to.ahead_seconds = from.ahead_seconds, to.behind_seconds = from.behind_seconds;
  to.picture_43 = from.picture_43;
}

void apply_original_look(Options& o) {
  Options original = original_options();
  original.glow = 0;
  original.colours.fill(0);
  original.scroll_speed = 1.0;
  original.score_numbers = original.timing_feedback = original.fc_celebration = false;
  original.vr_wrap = false;
  // In a headset the original's one pixel is far too thick on an eye's tall picture:
  // there it gives our headset default instead (owner, session 5).
  original.vr_line_width = Options{}.vr_line_width;
  copy_look(original, o);
}

std::string SaveData::to_text() const {
  std::ostringstream out;
  out << "# Vib-Ribbon VR save file. One setting per line; delete a line to get its default back.\n";
  for (int c = 0; c < kCourseCount; ++c) {
    const auto& t = high_scores[std::size_t(c)];
    out << "high_scores." << kCourseKeys[c] << ' ' << t[0] << ' ' << t[1] << ' ' << t[2] << '\n';
  }
  char buf[64];
  out << "sound_effects " << (options.sound_effects ? 1 : 0) << '\n';
  out << "vibration " << (options.vibration ? 1 : 0) << '\n';
  out << "language " << options.language << '\n';
  out << "fixed_courses " << (options.fixed_courses ? 1 : 0) << '\n';
  out << "smooth_animation " << (options.smooth_animation ? 1 : 0) << '\n';
  out << "resume_countdown " << (options.resume_countdown ? 1 : 0) << '\n';
  out << "perf_overlay " << options.perf_overlay << '\n';
  out << "score_numbers " << (options.score_numbers ? 1 : 0) << '\n';
  std::snprintf(buf, sizeof buf, "%.2f", options.scroll_speed);
  out << "scroll_speed " << buf << '\n';
  out << "timing_feedback " << (options.timing_feedback ? 1 : 0) << '\n';
  out << "fc_celebration " << (options.fc_celebration ? 1 : 0) << '\n';
  out << "vr_wrap " << (options.vr_wrap ? 1 : 0) << '\n';
  out << "colours";
  for (const std::uint8_t c : options.colours) out << ' ' << int(c);
  out << '\n';
  std::snprintf(buf, sizeof buf, "%.2f", options.vr_wrap_radius);
  out << "vr_wrap_radius " << buf << '\n';
  out << "vr_classic " << (options.vr_classic ? 1 : 0) << '\n';
  std::snprintf(buf, sizeof buf, "%.2f", options.vr_classic_size);
  out << "vr_classic_size " << buf << '\n';
  if (!options.music_folder.empty()) out << "music_folder " << options.music_folder << '\n';  // (the rest of the line)
  if (!options.disc_path.empty()) out << "disc_path " << options.disc_path << '\n';           // (the same)
  std::snprintf(buf, sizeof buf, "%.3f", options.line_width);
  out << "line_width " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.3f", options.vr_line_width);
  out << "vr_line_width " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.3f", options.vr_zoom);
  out << "vr_zoom " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.3f", options.vr_menu_zoom);
  out << "vr_menu_zoom " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.1f", options.vr_behind_seconds);
  out << "vr_behind_seconds " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.1f", options.vr_ahead_seconds);
  out << "vr_ahead_seconds " << buf << '\n';
  out << "vr_jitter_hold " << options.vr_jitter_hold << '\n';
  std::snprintf(buf, sizeof buf, "%.2f", options.jitter_scale);
  out << "jitter_scale " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.2f", options.vr_jitter_scale);
  out << "vr_jitter_scale " << buf << '\n';
  out << "vr_refresh_hz " << options.vr_refresh_hz << '\n';
  std::snprintf(buf, sizeof buf, "%.1f", options.vr_angle);
  out << "vr_angle " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.1f", options.audio_offset_ms);
  out << "audio_offset_ms " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.1f", options.picture_offset_ms);
  out << "picture_offset_ms " << buf << '\n';
  out << "glow " << options.glow << '\n';
  out << "jitter_hold " << options.jitter_hold << '\n';
  out << "camera " << options.camera << '\n';
  out << "vr_camera " << options.vr_camera << '\n';
  std::snprintf(buf, sizeof buf, "%.3f", options.zoom);
  out << "zoom " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.1f", options.view_angle);
  out << "view_angle " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.1f", options.fov);
  out << "fov " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.1f", options.ahead_seconds);
  out << "ahead_seconds " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.1f", options.behind_seconds);
  out << "behind_seconds " << buf << '\n';
  out << "window_mode " << options.window_mode << '\n';
  out << "fullscreen_mode " << options.fullscreen_w << ' ' << options.fullscreen_h << ' ' << options.fullscreen_hz << '\n';
  out << "vsync " << (options.vsync ? 1 : 0) << '\n';
  out << "frame_limit " << options.frame_limit << '\n';
  out << "picture_43 " << (options.picture_43 ? 1 : 0) << '\n';
  out << "low_latency " << (options.low_latency ? 1 : 0) << '\n';
  std::snprintf(buf, sizeof buf, "%.3f", options.menu_zoom);
  out << "menu_zoom " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.1f", options.menu_angle);
  out << "menu_angle " << buf << '\n';
  out << "launcher_vr " << (options.launcher_vr ? 1 : 0) << '\n';
  out << "difficulty " << options.difficulty << '\n';
  out << "touch_style " << options.touch_style << '\n';
  out << "launcher_close " << (options.launcher_close ? 1 : 0) << '\n';
  std::snprintf(buf, sizeof buf, "%.2f", double(options.music_volume));
  out << "music_volume " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.2f", double(options.effects_volume));
  out << "effects_volume " << buf << '\n';
  return out.str();
}

SaveData SaveData::from_text(const std::string& text) {
  SaveData d;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::istringstream fields(line);
    std::string key, value;
    fields >> key;
    if (key.rfind("high_scores.", 0) == 0) {
      const std::string name = key.substr(12);
      for (int c = 0; c < kCourseCount; ++c)
        if (name == kCourseKeys[c]) {
          std::array<int, 3> t{};
          if (fields >> t[0] >> t[1] >> t[2]) d.high_scores[std::size_t(c)] = t;
        }
      continue;
    }
    if (key == "colours") {  // one palette choice per slot; missing ones stay 0
      int c = 0;
      for (std::size_t k = 0; k < std::size_t(kColourSlotCount) && fields >> c; ++k)
        d.options.colours[k] = std::uint8_t(std::clamp(c, 0, 8));
      continue;
    }
    if (key == "fullscreen_mode") {  // width, height, refresh rate (0 0 0: the desktop's)
      int w = 0, h = 0, hz = 0;
      if (fields >> w >> h >> hz && w >= 0 && h >= 0 && hz >= 0 && w <= 16384 && h <= 16384 && hz <= 1000)
        d.options.fullscreen_w = w, d.options.fullscreen_h = h, d.options.fullscreen_hz = hz;
      continue;
    }
    if (key == "music_folder" || key == "disc_path") {  // a path: the rest of the line, spaces and all
      std::string rest;
      std::getline(fields, rest);
      const std::size_t start = rest.find_first_not_of(" \t");
      if (start != std::string::npos) (key == "disc_path" ? d.options.disc_path : d.options.music_folder) = rest.substr(start);
      continue;
    }
    fields >> value;
    if (value.empty()) continue;
    const double number = std::atof(value.c_str());
    if (key == "sound_effects") d.options.sound_effects = parse_bool(value);
    else if (key == "vibration") d.options.vibration = parse_bool(value);
    else if (key == "fixed_courses") d.options.fixed_courses = parse_bool(value);
    else if (key == "language" && number >= -1 && number <= 4) d.options.language = int(number);
    else if (key == "smooth_animation") d.options.smooth_animation = parse_bool(value);
    else if (key == "resume_countdown") d.options.resume_countdown = parse_bool(value);
    else if (key == "perf_overlay" && number >= 0 && number <= 2) d.options.perf_overlay = int(number);
    else if (key == "score_numbers") d.options.score_numbers = parse_bool(value);
    else if (key == "timing_feedback") d.options.timing_feedback = parse_bool(value);
    else if (key == "fc_celebration") d.options.fc_celebration = parse_bool(value);
    else if (key == "vr_wrap") d.options.vr_wrap = parse_bool(value);
    else if (key == "vr_wrap_radius" && number > 0) d.options.vr_wrap_radius = std::clamp(number, 1.0, 10.0);
    else if (key == "vr_classic") d.options.vr_classic = number != 0;
    else if (key == "vr_classic_size" && number > 0) d.options.vr_classic_size = std::clamp(number, 1.5, 8.0);
    else if (key == "scroll_speed" && number > 0) d.options.scroll_speed = clamp_scroll_speed(number);
    else if (key == "line_width" && number > 0) d.options.line_width = number;
    else if (key == "vr_line_width" && number > 0) d.options.vr_line_width = number;
    else if (key == "vr_zoom" && number > 0) d.options.vr_zoom = number;
    else if (key == "vr_angle") d.options.vr_angle = number;
    else if (key == "vr_jitter_hold" && number >= 1 && number <= 3) d.options.vr_jitter_hold = int(number);
    else if (key == "jitter_scale" && number >= 0 && number <= 2) d.options.jitter_scale = number;
    else if (key == "vr_jitter_scale" && number >= 0 && number <= 2) d.options.vr_jitter_scale = number;
    else if (key == "vr_refresh_hz" && number >= 0 && number <= 240) d.options.vr_refresh_hz = int(number);
    else if (key == "vr_menu_zoom" && number > 0) d.options.vr_menu_zoom = number;
    else if (key == "vr_behind_seconds" && number >= 0) d.options.vr_behind_seconds = std::max(2.0, number);  // (2 s at least)
    else if (key == "vr_ahead_seconds" && number >= 0) d.options.vr_ahead_seconds = number;
    else if (key == "audio_offset_ms") d.options.audio_offset_ms = number;
    else if (key == "glow" && number >= 0 && number <= 4) d.options.glow = int(number);
    else if (key == "picture_offset_ms") d.options.picture_offset_ms = std::clamp(number, -300.0, 300.0);
    else if (key == "jitter_hold" && number >= 0 && number <= 3) d.options.jitter_hold = int(number);
    else if (key == "camera" && (number == 0 || number == 1)) d.options.camera = int(number);
    else if (key == "vr_camera" && (number == 0 || number == 1)) d.options.vr_camera = int(number);
    else if (key == "zoom" && number > 0) d.options.zoom = std::clamp(number, 1.0 / 36.0, 4.0);
    else if (key == "view_angle") d.options.view_angle = std::remainder(number, 360.0);
    else if (key == "fov" && number > 0) d.options.fov = std::clamp(number, 30.0, 110.0);
    else if (key == "ahead_seconds" && number >= 0) d.options.ahead_seconds = std::max(8.0, number);
    else if (key == "behind_seconds" && number >= 0) d.options.behind_seconds = std::max(2.0, number);
    else if (key == "window_mode" && number >= 0 && number <= 2) d.options.window_mode = int(number);
    else if (key == "vsync") d.options.vsync = parse_bool(value);
    else if (key == "frame_limit" && number >= 0 && number <= 1000) d.options.frame_limit = int(number);
    else if (key == "picture_43") d.options.picture_43 = parse_bool(value);
    else if (key == "low_latency") d.options.low_latency = parse_bool(value);
    else if (key == "launcher_vr") d.options.launcher_vr = parse_bool(value);
    else if (key == "difficulty" && number >= 0 && number <= 2) d.options.difficulty = int(number);
    else if (key == "touch_style" && (number == 0 || number == 1)) d.options.touch_style = int(number);
    else if (key == "launcher_close") d.options.launcher_close = parse_bool(value);
    else if (key == "menu_zoom" && number > 0) d.options.menu_zoom = std::clamp(number, 1.0 / 36.0, 4.0);
    else if (key == "menu_angle") d.options.menu_angle = std::remainder(number, 360.0);
    else if (key == "music_volume" && number >= 0) d.options.music_volume = float(number);
    else if (key == "effects_volume" && number >= 0) d.options.effects_volume = float(number);
  }
  return d;
}

}  // namespace vib
