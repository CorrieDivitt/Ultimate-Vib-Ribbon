#include "vib/save.h"

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
  out << "fixed_courses " << (options.fixed_courses ? 1 : 0) << '\n';
  out << "smooth_animation " << (options.smooth_animation ? 1 : 0) << '\n';
  std::snprintf(buf, sizeof buf, "%.3f", options.line_width);
  out << "line_width " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.3f", options.vr_line_width);
  out << "vr_line_width " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.3f", options.vr_zoom);
  out << "vr_zoom " << buf << '\n';
  std::snprintf(buf, sizeof buf, "%.1f", options.audio_offset_ms);
  out << "audio_offset_ms " << buf << '\n';
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
    fields >> value;
    if (value.empty()) continue;
    const double number = std::atof(value.c_str());
    if (key == "sound_effects") d.options.sound_effects = parse_bool(value);
    else if (key == "vibration") d.options.vibration = parse_bool(value);
    else if (key == "fixed_courses") d.options.fixed_courses = parse_bool(value);
    else if (key == "smooth_animation") d.options.smooth_animation = parse_bool(value);
    else if (key == "line_width" && number > 0) d.options.line_width = number;
    else if (key == "vr_line_width" && number > 0) d.options.vr_line_width = number;
    else if (key == "vr_zoom" && number > 0) d.options.vr_zoom = number;
    else if (key == "audio_offset_ms") d.options.audio_offset_ms = number;
    else if (key == "music_volume" && number >= 0) d.options.music_volume = float(number);
    else if (key == "effects_volume" && number >= 0) d.options.effects_volume = float(number);
  }
  return d;
}

}  // namespace vib
