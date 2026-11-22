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
    ScoreNumbers,  // score_numbers: the score in numbers too, during play
    FixedCourses,  // fixed_courses: consistent mode, the same course every play of a song
    EffectsVolume, // effects_volume: the sound effects' loudness, 0 to 10 tenths
    ScrollSpeed,   // scroll_speed: how fast the road is drawn coming, 0.5x to 10x (display only)
    TimingFeedback,// timing_feedback: each hit's offset in ms (- early, + late)
    FcCelebration, // fc_celebration: "Full Combo!" at the end of a song with no misses
    WrapCourse,    // vr_wrap: the course wrapped round the player (headset)
    WrapSize,      // vr_wrap_radius: that ring's radius, 1 to 10 m (headset)
    // Groups (owner's layout, session 4): confirm opens the group's own list
    // (set_group); B or its Back row returns to the list it was opened from.
    Visual,
    Modifiers,
    CustomColours,
    Back,          // the last row of every group: back to the list above
    // Custom colors: one per colour slot (vib/colours.h), in its order, then a reset.
    ColourRoad, ColourBlock, ColourPit, ColourLoop, ColourWave,
    ColourBlockPit, ColourBlockLoop, ColourBlockWave, ColourPitLoop, ColourPitWave, ColourLoopWave,
    ColourAngel, ColourRabbit, ColourFrog, ColourWorm, ColourBackground, ColourMenu,
    ResetColours,  // an action: every colour back to the original's
    Calibration,      // a group: the calibration tools
    SoundCalibration, // an action: the click track to set Sound timing by
    PictureTiming,    // picture_offset_ms: the picture drawn ahead of the sound, -300 to 300 ms
    PictureCalibration, // an action: silent flashes to set Picture timing by
    Glow,             // glow: off, faint, soft, medium, strong
    RefreshRate,      // vr_refresh_hz: Auto (the highest up to 120 Hz) or one the headset offers
  };
  enum class Action { None, Moved, Changed, Close, Recentre, QuitSong, LeaveTop, Calibrate, CalibratePicture };

  struct Input {
    bool up = false, down = false, left = false, right = false, confirm = false, cancel = false;
    bool held_left = false, held_right = false;  // for repeat while held
    bool held_up = false, held_down = false;     // (the file picker scrolls while these are held)
  };

  // `pak`: the game's own archive (for the font). `text`: the program's character
  // table; without it nothing is drawn but the panel still works.
  SettingsPanel(const Pak& game_pak, int language, const GameText* text);

  // The items in order. The cursor goes to the first, at the top level.
  void set_items(std::vector<Item> items);
  // The list a group item opens (a Back row is added at its end). Groups can hold groups.
  void set_group(Item group, std::vector<Item> items);
  // How deep in groups the cursor is (0: the top list), and the group open now.
  int depth() const { return int(stack_.size()); }
  int language() const { return language_; }
  Item open_group() const { return stack_.empty() ? Item::Continue : stack_.back().group; }
  // At the top (from the Options screen, where up from the first leaves the panel)
  // or not (in play, where the cursor wraps).
  void set_wraps(bool wraps) { wraps_ = wraps; }
  // The refresh rates the headset offers (whole Hz), for the Refresh rate item; with
  // none it only shows Auto.
  void set_refresh_rates(std::vector<int> rates) { refresh_rates_ = std::move(rates); }
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
  // The course selects' line at the bottom middle, "Scroll speed: 1.25x" (L2 and R2
  // change it there), dimmed as the hint is.
  Hud::Layer draw_scroll_speed(const Options& o, std::uint32_t seed);
  // "Scroll speed: 1.25x" in font codes, as the course selects and play show it.
  std::string scroll_line(const Options& o) const;
  // A hit's offset as shown, from press minus perfect in ms: "-12 ms" (early),
  // "+7 ms" (late), "0 ms".
  static std::string offset_text(double ms);
  // A scroll speed as shown: "1x", "1.25x", "0.5x".
  static std::string scroll_text(double speed);
  // `text` (font codes) large in the middle of the screen: the count-in's numbers.
  Hud::Layer draw_big(const std::string& text, std::uint32_t seed);
  // `text` (font codes) as lines centred on the origin, as if seen from -z (x right,
  // y down, z 0 the text's own plane), each character 72 x `scale` across, jittered by
  // `range` as the game's own text is: for a display to place where it wants (the
  // headset puts the score's number below Vibri).
  std::vector<SceneLine> text_lines(const std::string& text, float scale, std::uint32_t seed, int range);
  // The score in numbers on the monitor: centred near the bottom, just above the time
  // meter's strip, with the game's text jitter.
  Hud::Layer draw_score(int score, std::uint32_t seed);
  // "Full Combo!" `t` seconds into its celebration (it lasts kFullComboSeconds): the
  // letters pop in one by one, swelling past their size and settling, with a hop; two
  // bursts of rays fly out from behind; then the word rides a wave, and all of it
  // fades at the end. Lines centred on the origin as text_lines() gives them (seen
  // from -z, y down), the word about 1,500 across.
  static constexpr double kFullComboSeconds = 2.6;
  std::vector<SceneLine> full_combo(double t, std::uint32_t seed);
  // A whole course with no misses: bigger. "Full Course" pops in as above, then a
  // large "Combo!" slams down from oversize with a shake and a shockwave ring; three
  // wider bursts of rays and twinkling sparkles all round; about 4 s.
  static constexpr double kFullCourseComboSeconds = 4.0;
  std::vector<SceneLine> full_course_combo(double t, std::uint32_t seed);
  // Either, through the text camera, upper middle of the screen (the monitor).
  Hud::Layer draw_full_combo(double t, std::uint32_t seed, bool course = false);
  // The timing feedback on the monitor: `text` centred a little above the score's
  // number, dimmed by `brightness` (0-1).
  Hud::Layer draw_timing(const std::string& text, float brightness, std::uint32_t seed);
  // The panel as a layer of its own, with its own camera. `seed`: its jitter.
  Hud::Layer draw(const Options& o, bool vr, std::uint32_t seed);

  // The label and the value as shown (font codes), for tests and the console. Our own
  // settings show the original game's value after the name, "Line width (1.000)"
  // (original_options()), so players can find the original's look again; the
  // original's two options, the actions, Resume, Performance and Score in numbers show none.
  std::string label(Item item, bool vr = false) const;
  std::string value(Item item, const Options& o, bool vr) const;

 private:
  bool change(Item item, int direction, Options& o, bool vr);
  void write(const std::string& font_text, float x, float y, float scale, const Rgb* colour, int range, std::vector<SceneLine>& out);

 public:
  float text_width(const std::string& font_text, float scale = 1.0f) const;  // across, at size 1

 private:
  Hud::Layer start_layer(std::uint32_t seed);
  // One word of a celebration: its letters popping in from `start`, `stagger` apart,
  // each swelling to about 1.35 and settling with a hop, then riding a wave; centred
  // across, its middle at `y`; grey `shade` (0-1).
  void pop_word(const std::string& word, float scale, float y, double t, double start, double stagger, float shade,
                std::uint32_t seed, std::vector<SceneLine>& out);

  const GameText* text_;
  int language_;
  TmdModel font_;
  LineBuilder lines_;
  std::vector<Item> items_;
  struct Level {
    std::vector<Item> items;
    int cursor, first;
    Item group;  // the group this level opened (the one shown now is stack_.back().group)
  };
  std::vector<Level> stack_;  // the lists above the one shown, innermost last
  std::vector<std::pair<Item, std::vector<Item>>> groups_;
  bool is_group(Item item) const;
  void open(Item group);
  void close_group();
  int cursor_ = 0, first_ = 0, rows_ = 20;
  float top_ = 0.8f, size_ = 1.0f, label_x_ = -1600.0f, value_right_ = 1600.0f;
  bool active_ = true;
  bool wraps_ = true;
  std::vector<int> refresh_rates_;
  int repeat_dir_ = 0;
  double repeat_timer_ = 0;
  int scroll_dir_ = 0;  // up or down held: -1 or 1 (the list scrolls while held)
  double scroll_timer_ = 0;
};

}  // namespace vib
