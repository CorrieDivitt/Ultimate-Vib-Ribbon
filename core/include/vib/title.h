#pragma once
// The screens around the courses: the main menu, the game-disc course select, the
// options and the high scores, with Vibri at the side and the fades between them.
// Behaviour follows the original's Title and TitleDraw (read for behaviour; our own
// code), drawn from the TITLE models, clips and cameras on the player's disc.
//
// Not yet: the audio-CD and speed course menus, the track select, the CLEAR and
// AGAIN result sequences, the menu music and Vibri's voice (requested sounds are
// listed so the app can play them once that exists).
//
// The original updates these screens once per video frame; tick() is one such
// frame (1/49.759 s), and the app calls it at that rate whatever the display does.

#include <array>
#include <cstdint>
#include <vector>

#include "vib/pak.h"
#include "vib/scene.h"

namespace vib {

class TitleScreens {
 public:
  enum class Screen { None, Main, PsjModeSelect, Option, PsjHiscore, Results, Done };
  enum class Result {
    None,
    PsjModes, UnknownModes, Options, Leave,          // main
    Bronze, Silver, Gold, PsjHiscores, PsjBack,      // game-disc course select
    OptionBack,                                      // options
    HiscoreBack,                                     // high scores
    ResultsDone,                                     // high scores shown after a course
  };

  // Buttons for one tick: which went down, and which are held (for key repeat).
  struct Input {
    bool up = false, down = false, left = false, right = false, confirm = false, cancel = false;
    bool held_up = false, held_down = false, held_left = false, held_right = false;
  };

  // A sound the screen asks for: a sequence of the title sound bank and its volume.
  // Collected for later; nothing plays them yet.
  struct SoundRequest {
    int sequence, volume;
    bool music;
  };

  static constexpr double kFrameRate = 49.759;

  explicit TitleScreens(const Pak& game_pak, const char* language = "02_");

  // Fades to `screen`; Screen::None switches at once with no fade.
  void go_to(Screen screen);
  Screen screen() const { return screen_; }
  // The fade has finished leaving for Screen::Done: the course can start.
  bool done() const { return screen_ == Screen::Done && fade_frame_ == 0 && fade_step_ == 0; }

  // One video frame. Returns what was chosen, if anything.
  Result tick(const Input& in);
  // The lines of the last tick, each layer with its own camera.
  const std::vector<Hud::Layer>& layers() const { return layers_; }
  std::vector<SoundRequest>& sounds() { return sounds_; }

  // What the screens show and change.
  bool sound_effects = true, vibration = true;
  std::array<std::array<int, 3>, 3> psj_high_scores{};  // bronze, silver, gold
  // For Screen::Results: the course just played (0-2) and its place (1-3).
  int results_course = 0, results_rank = 1;

 private:
  enum class Vibri { Lying, LyingSpeaking, Sitting, SittingSpeaking, Standing, StandingSpeaking, Paused, Hidden };
  enum Fade { kNone, kIn, kOut };

  struct Model {
    TmdModel tmd;
    LineBuilder::Look look;
    void reset_look();
    void swap(int replaced, int shown) { look.swap[std::size_t(replaced)] = shown; }
    void jitter_all(int range) { std::fill(look.jitter.begin(), look.jitter.end(), range); }
    void hide(int object, bool hidden);
  };
  struct Movie {
    int model;
    ModelAnimation anim;
    Clip clip;
  };
  struct Camera {
    CameraAnimation anim;
    Clip clip;
  };
  // A list of items, top to bottom, with the clip that carries the cursor from
  // each item to the next.
  struct Cursor {
    int index = 0, count = 1, back = -1;  // back: the item cancel jumps to
  };
  struct MenuRead {
    bool confirm = false, up = false, down = false, left = false, right = false, back = false, changed = false;
  };

  int load_model(const Pak& pak, const char* language, const char* name);
  int load_movie(const Pak& pak, int model, const char* name, bool loop = false);
  int load_camera(const Pak& pak, const char* name, bool loop = false);
  MenuRead read(Cursor& c, const Input& in, bool wrap, bool active);
  void update_fade();
  void rewind_movies();
  void set_camera(int cue);
  void update_camera(int cue);
  SceneView camera_view(int camera);
  void draw_movie(int movie, double speed, std::vector<SceneLine>& out);
  void draw_menu_movie(int movie, std::vector<SceneLine>& out);
  void draw_vibri(int jitter, std::vector<SceneLine>& out);
  void say(int sequence, int volume, Vibri speaking);
  void show_number(Model& m, const std::array<int, 6>& slots, int first_digit_object, int value);

  Result main_menu(const Input& in);
  Result psj_mode_select(const Input& in);
  Result options(const Input& in);
  Result psj_hiscore(const Input& in, int frame);

  LineBuilder lines_;
  std::vector<Model> models_;
  std::vector<Movie> movies_;
  std::vector<Camera> cameras_;
  std::vector<Hud::Layer> layers_;
  std::vector<SoundRequest> sounds_;
  SceneView view_;  // the camera most recently shown, as the original's one view

  // models
  int m_vibri_ = 0, m_main_ = 0, m_psj_ = 0, m_option_ = 0, m_hiscore_ = 0;
  // movies
  std::array<int, 6> vibri_movies_{};
  std::array<int, 4> main_steps_{};
  std::array<int, 5> psj_steps_{};
  int psj_open_ = 0, psj_close_ = 0;
  std::array<int, 3> option_steps_{};
  int option_open_ = 0, option_close_ = 0;
  int hi_open_ = 0, hi_loop_ = 0, hi_left_ = 0, hi_right_ = 0, hi_exit_ = 0;
  // cameras
  int cam_left_ = 0, cam_right_ = 0, cam_zoom_ = 0, cam_menu_ = 0;

  // screen flow and fades
  Screen screen_ = Screen::None, next_ = Screen::None, previous_ = Screen::None;
  int fade_ = kNone, fade_frame_ = 0, fade_step_ = 0;
  bool arrived_ = true;
  int fade_jitter() const { return fade_frame_ * 200 * 4096; }

  // screen state
  Cursor main_cursor_{0, 4, 3}, psj_cursor_{0, 5, 4}, option_cursor_{0, 3, 2}, back_cursor_{0, 2, 1};
  int menu_movie_ = 0, camera_cue_ = -1, wait_frame_ = 0, hiscore_course_ = 0, results_frame_ = 0;
  double speed_ = 1.0;
  bool entering_ = false, leaving_ = false;
  Vibri vibri_ = Vibri::Hidden;
  int vibri_movie_ = 0, vibri_frame_ = 0, voice_frames_ = 0;
  // key repeat
  int repeat_state_ = 0, repeat_timer_ = 0;
  std::uint32_t frame_count_ = 0;
};

}  // namespace vib
