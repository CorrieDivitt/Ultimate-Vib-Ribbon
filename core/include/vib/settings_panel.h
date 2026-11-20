#pragma once
// Our settings panel (not the original's): a list of the player's settings written
// with the line font from the player's disc. The View button opens it anywhere: in
// play it freezes the song and shows it over the frozen course (the original has no
// pause; its Start still asks "again?"); in the menus it shows beside a preview of
// play, Vibri on a stretch of road, so every setting can be seen working.
//
// The labels are our own words, in the five languages of the PAL menus. The font's
// accented letters are reached as the game's own text reaches them ("~" and a
// letter); to_font_text() turns ordinary UTF-8 into that form.

#include <cstdint>
#include <string>
#include <vector>

#include "vib/game_text.h"
#include "vib/pak.h"
#include "vib/save.h"
#include "vib/scene.h"
#include "vib/tmd.h"

namespace vib {

// UTF-8 to the line font's codes: plain ASCII as it is, each accented letter the
// font has as "~" and its code letter. Characters the font lacks become '?'.
std::string to_font_text(const std::string& utf8);

class SettingsPanel {
 public:
  enum class Item {
    Continue,      // close the panel (play goes on)
    WobbleSize,    // jitter_scale / vr_jitter_scale, 0 to 2
    WobbleHold,    // vr_jitter_hold, 1 to 3 (headset)
    Smooth,        // smooth_animation (monitor; always on in a headset)
    LineWidth,     // line_width / vr_line_width
    Zoom,          // vr_zoom (headset, play's)
    ViewAngle,     // vr_angle (headset)
    SeeAhead,      // vr_ahead_seconds (headset)
    SeeBehind,     // vr_behind_seconds (headset)
    Recentre,      // an action (headset)
    SoundEffects,  // the original's two options
    Vibration,
    SoundTiming,   // audio_offset_ms
    QuitSong,      // an action: leave the song, as Start does
    ResumeStyle,   // resume_countdown: after a pause, rewind a little or count in
    Performance,   // perf_overlay: off, one line, detailed
  };
  enum class Action { None, Moved, Changed, Close, Recentre, QuitSong, LeaveTop };

  struct Input {
    bool up = false, down = false, left = false, right = false, confirm = false, cancel = false;
    bool held_left = false, held_right = false;  // for repeat while held
  };

  // `pak`: the game's own archive (for the font). `text`: the program's character
  // table; without it nothing is drawn but the panel still works.
  SettingsPanel(const Pak& game_pak, int language, const GameText* text);

  // The items in order. The cursor goes to the first.
  void set_items(std::vector<Item> items);
  // At the top (from the Options screen, where up from the first leaves the panel)
  // or not (in play, where the cursor wraps).
  void set_wraps(bool wraps) { wraps_ = wraps; }
  void home() { cursor_ = 0, first_ = 0; }
  Item current() const { return items_.empty() ? Item::Continue : items_[std::size_t(cursor_)]; }

  // One step, `seconds` since the last (for repeat while left or right is held).
  // Changes the options in place. `vr`: which of each setting is in use.
  Action tick(const Input& in, double seconds, Options& o, bool vr);

  // Where and how many rows: `top` the first row's height on screen (0 the middle,
  // 1 the top edge, -1 the bottom), `rows` the most shown at once (the list scrolls).
  // `size`: everything this much nearer and smaller, the same on screen (in a
  // headset 1 is 4 m away; play's pause uses 0.25, in front of Vibri).
  void set_place(float top, int rows, float size = 1.0f) { top_ = top, rows_ = rows, size_ = size; }
  // Shown without the cursor and all dimmed (the Options screen's own items have it).
  void set_active(bool active) { active_ = active; }
  // Where the labels start and the values end, across (at size 1, the screen's
  // edges are about -3080 and 3080).
  void set_columns(float label_x, float value_right) { label_x_ = label_x, value_right_ = value_right; }
  // Just a dimmed "More options (Select)" at the first row's place: the Options
  // screen's hint that View opens ours.
  Hud::Layer draw_hint(std::uint32_t seed);
  // `text` (font codes) large in the middle of the screen: the count-in's numbers.
  Hud::Layer draw_big(const std::string& text, std::uint32_t seed);
  // The panel as a layer of its own, with its own camera. `seed`: its jitter.
  Hud::Layer draw(const Options& o, bool vr, std::uint32_t seed);

  // The label and the value as shown (font codes), for tests and the console.
  std::string label(Item item) const;
  std::string value(Item item, const Options& o, bool vr) const;

 private:
  bool change(Item item, int direction, Options& o, bool vr);
  void write(const std::string& font_text, float x, float y, float scale, const Rgb* colour, int range, std::vector<SceneLine>& out);
  float text_width(const std::string& font_text, float scale) const;
  Hud::Layer start_layer(std::uint32_t seed);

  const GameText* text_;
  int language_;
  TmdModel font_;
  LineBuilder lines_;
  std::vector<Item> items_;
  int cursor_ = 0, first_ = 0, rows_ = 20;
  float top_ = 0.8f, size_ = 1.0f, label_x_ = -1600.0f, value_right_ = 1600.0f;
  bool active_ = true;
  bool wraps_ = true;
  int repeat_dir_ = 0;
  double repeat_timer_ = 0;
};

}  // namespace vib
