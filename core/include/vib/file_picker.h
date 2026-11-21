#pragma once
// Ours: choosing the player's own music in the game, drawn with the disc's line font
// and driven by the d-pad, so it works in a headset (owner's decisions). Two modes:
//   Folder: the first time, the player walks to their music folder and picks it.
//   Files:  in that folder, the player ticks one or more music files (and may step
//           into folders below it); the ticked files become the "CD" (own_cd.h).
// The app lists folders and files (platform code) and loads the music; this class
// only keeps the cursor and the ticks, and draws.

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "vib/scene.h"
#include "vib/settings_panel.h"

namespace vib {

class FilePicker {
 public:
  enum class Mode { Folder, Files };
  struct Entry {
    std::string name;  // UTF-8, as the file system has it
    bool folder = false;
  };
  enum class Action {
    None,
    Moved,      // the cursor moved
    Toggled,    // a file was ticked or unticked
    Enter,      // open entry() (a folder): the app lists it and calls show()
    Up,         // go to the folder above: the app lists it and calls show()
    UseFolder,  // Folder mode: this folder is the music folder
    Done,       // Files mode: the ticked files (ticked()) are the CD
    Cancel,     // back to the menus
  };

  // `font`: the settings panel, for its line font and words.
  FilePicker(SettingsPanel& font, int language) : font_(font), language_(language) {}

  // Shows a folder: `path` (UTF-8, shown shortened at the top) and what is in it
  // (folders first, then music files; the app sorts each). Ticks are cleared when
  // the folder is not the one shown before.
  void show(Mode mode, const std::string& path, std::vector<Entry> entries, bool can_go_up);
  Mode mode() const { return mode_; }
  const std::string& path() const { return path_; }

  // One step of input (the settings panel's Input). Returns what to do.
  Action tick(const SettingsPanel::Input& in, double seconds);
  // The entry under the cursor, for Enter.
  const Entry* entry() const;
  // The ticked files' names, in the order shown.
  std::vector<std::string> ticked() const;

  // The screen as a layer with its own camera (the settings panel's). `size`: as the
  // panel's (a headset uses 0.25: about 1 m away). `seed`: the text's jitter.
  Hud::Layer draw(float size, std::uint32_t seed) const;

  // The rows as shown (font codes), for tests: the action rows, then the entries.
  std::vector<std::string> rows() const;

 private:
  enum class Row { UseFolder, Done, Up, Entry };
  struct Line {
    Row row;
    int entry = -1;
  };
  std::vector<Line> lines() const;

  SettingsPanel& font_;
  int language_;
  Mode mode_ = Mode::Files;
  std::string path_;
  std::vector<Entry> entries_;
  bool can_go_up_ = false;
  std::set<std::string> ticked_;
  int cursor_ = 0, first_ = 0;
  int repeat_dir_ = 0;
  double repeat_timer_ = 0;
};

}  // namespace vib
