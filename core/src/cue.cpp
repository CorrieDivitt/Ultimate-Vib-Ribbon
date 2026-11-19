#include "vib/cue.h"

#include <cctype>
#include <sstream>

#include "vib/bytes.h"

namespace vib {
namespace {

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

bool starts_with_ci(std::string_view s, std::string_view prefix) {
  if (s.size() < prefix.size()) return false;
  for (std::size_t i = 0; i < prefix.size(); ++i)
    if (std::toupper(static_cast<unsigned char>(s[i])) != std::toupper(static_cast<unsigned char>(prefix[i]))) return false;
  return true;
}

std::vector<std::string> split_words(std::string_view s) {
  std::vector<std::string> out;
  std::string cur;
  bool quoted = false;
  for (char c : s) {
    if (c == '"') { quoted = !quoted; continue; }
    if (!quoted && std::isspace(static_cast<unsigned char>(c))) {
      if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

}  // namespace

std::uint32_t msf_to_frames(std::string_view msf) {
  unsigned m = 0, s = 0, f = 0;
  char c1 = 0, c2 = 0;
  std::istringstream in{std::string(msf)};
  in >> m >> c1 >> s >> c2 >> f;
  require(in && !in.fail() && c1 == ':' && c2 == ':' && s < 60 && f < 75, "bad MSF time '" + std::string(msf) + "'");
  return (m * 60 + s) * kFramesPerSecond + f;
}

CueSheet parse_cue(std::string_view text, const std::filesystem::path& base_dir) {
  CueSheet sheet;
  std::filesystem::path current_file;
  std::size_t pos = 0;
  while (pos < text.size()) {
    std::size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    const std::string_view line = trim(text.substr(pos, end - pos));
    pos = end + 1;
    if (line.empty()) continue;
    const auto words = split_words(line);
    if (starts_with_ci(line, "FILE")) {
      require(words.size() >= 2, "CUE FILE line without a name");
      current_file = path_from_utf8(words[1]);
      if (current_file.is_relative()) current_file = base_dir / current_file;
    } else if (starts_with_ci(line, "TRACK")) {
      require(words.size() >= 3, "CUE TRACK line incomplete");
      require(!current_file.empty(), "CUE TRACK before any FILE");
      CueTrack t;
      t.number = std::stoi(words[1]);
      t.file = current_file;
      const std::string& mode = words[2];
      if (starts_with_ci(mode, "AUDIO")) t.mode = TrackMode::Audio;
      else if (starts_with_ci(mode, "MODE2/2352")) t.mode = TrackMode::Mode2Raw;
      else if (starts_with_ci(mode, "MODE1/2352")) t.mode = TrackMode::Mode1Raw;
      else throw FormatError("unsupported CUE track mode " + mode + " (only raw 2352-byte images are supported)");
      sheet.tracks.push_back(t);
    } else if (starts_with_ci(line, "INDEX")) {
      require(!sheet.tracks.empty() && words.size() >= 3, "CUE INDEX outside a track");
      const int idx = std::stoi(words[1]);
      const std::uint32_t frames = msf_to_frames(words[2]);
      if (idx == 0) sheet.tracks.back().index00 = frames;
      else if (idx == 1) sheet.tracks.back().index01 = frames;
    }
    // REM, PREGAP, POSTGAP, FLAGS, CATALOG etc. are ignored.
  }
  require(!sheet.tracks.empty(), "CUE sheet has no tracks");
  return sheet;
}

CueSheet load_cue(const std::filesystem::path& cue_path) {
  const Bytes raw = read_file(cue_path);
  return parse_cue(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()), cue_path.parent_path());
}

}  // namespace vib
