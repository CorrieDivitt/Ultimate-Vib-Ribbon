#pragma once
// The screens around the courses: the main menu, the game-disc course select, the
// options and the high scores, with Vibri at the side and the fades between them.
// Behaviour follows the original's Title and TitleDraw (read for behaviour; our own
// code), drawn from the TITLE models, clips and cameras on the player's disc.
//
// Not yet: the audio-CD and speed course menus, the track select, the CLEAR and
// AGAIN result sequences. Sounds (the menu music and Vibri's voice) are asked of
// the app as requests; it plays them from the gameplay sound bank.
//
// The original updates these screens once per video frame; tick() is one such
// frame (1/49.759 s), and the app calls it at that rate whatever the display does.

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "vib/game_text.h"
#include "vib/pak.h"
#include "vib/scene.h"

namespace vib {

class TitleScreens {
 public:
  enum class Screen { None, Main, PsjModeSelect, Option, PsjHiscore, Results, Clear, Again, AgainSelect, Done };
  enum class Result {
    None,
    PsjModes, UnknownModes, Options, Leave,          // main
    Bronze, Silver, Gold, PsjHiscores, PsjBack,      // game-disc course select
    OptionBack,                                      // options
    HiscoreBack,                                     // high scores
    ResultsDone,                                     // high scores shown after a course; AGAIN is over
    PlayAgain, AgainBack,                            // AGAIN SELECT: try the song again, or leave
  };

  // Buttons for one tick: which went down, and which are held (for key repeat).
  struct Input {
    bool up = false, down = false, left = false, right = false, confirm = false, cancel = false;
    bool held_up = false, held_down = false, held_left = false, held_right = false;
    bool voices_playing = false;  // the voice sequence is still sounding (Vibri's mouth moves until it ends)
    bool music_playing = false;   // the music sequence is still sounding
  };

  // Sounds the screens ask for, on two sequences as in the original: the music,
  // and Vibri's voice (with the confirm sound). Each sound is a number of the
  // gameplay bank, program * 16 + tone, played one after another. `stop` ends
  // that sequence instead.
  enum class Channel { Music, Voice };
  struct SoundRequest {
    Channel channel;
    std::vector<int> sounds;
    int volume = 0;
    bool stop = false;
  };

  static constexpr double kFrameRate = 49.759;

  // `language`: the original's number (1 English), for the models with words on them.
  explicit TitleScreens(const Pak& game_pak, int language = 1);

  // The score said in Japanese, as the original builds it from syllables: the
  // sounds in order, and the same laid out on a beat grid with rests first.
  struct SpokenNumber {
    std::vector<int> clips, timeline;
    int tempo = 0;  // 0, 1 or 2: the line lasts 6, 14 or 22 beats
  };
  static SpokenNumber say_number(int number, int form);

  // The game's words, read from the player's own MAIN_G.EXE. Without them the
  // screens are drawn without their text.
  void set_text(const GameText* text, int language = GameText::kEnglish) {
    text_ = text;
    text_language_ = language;
  }

  // Fades to `screen`; Screen::None switches at once with no fade.
  void go_to(Screen screen);
  Screen screen() const { return screen_; }
  // The fade has finished leaving for Screen::Done: the course can start.
  bool done() const { return screen_ == Screen::Done && fade_frame_ == 0 && fade_step_ == 0; }

  // One video frame. Returns what was chosen, if anything.
  Result tick(const Input& in);
  // The lines of the last tick, each layer with its own camera.
  const std::vector<Hud::Layer>& layers() const { return layers_; }
  // The requests since the last call, in order.
  std::vector<SoundRequest> take_sounds() { return std::exchange(sounds_, {}); }

  // What the screens show and change.
  bool sound_effects = true, vibration = true;
  std::array<std::array<int, 3>, 3> psj_high_scores{};  // bronze, silver, gold
  // For the results screens: the course just played (0-2), its place in the high
  // scores (1-3, 0 for none) and the score. Screen::Again is game over: Vibri's
  // reaction, the score, then the high scores if it made them.
  int results_course = 0, results_rank = 1, results_score = 0;
  // Screen::Clear (a course cleared): the last song's score and end bonus, and
  // Vibri's form at the end (0 best to 3), which picks the comment's sign.
  int results_bonus = 0, results_form = 1;

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

  int load_model(const Pak& pak, int language, const char* name);
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
  void say(std::vector<int> sounds, int volume, Vibri speaking);
  // Writes `text` with the line font where the original's results screens do, as a
  // layer of its own. `jitter` in the geometry unit's fixed point, as the screens pass it.
  void draw_text(const std::string& text, int jitter);
  const std::string& words(GameText::List list, int index) const;
  void play_music();
  void show_number(Model& m, const std::array<int, 6>& slots, int first_digit_object, int value);

  Result main_menu(const Input& in);
  Result psj_mode_select(const Input& in);
  Result options(const Input& in);
  Result psj_hiscore(const Input& in, int frame);
  Result again(const Input& in);
  Result clear(const Input& in);

  Result again_select(const Input& in);

  LineBuilder lines_;
  const GameText* text_ = nullptr;
  int text_language_ = GameText::kEnglish;
  TmdModel font_;
  std::vector<Model> models_;
  std::vector<Movie> movies_;
  std::vector<Camera> cameras_;
  std::vector<Hud::Layer> layers_;
  std::vector<SoundRequest> sounds_;
  SceneView view_;  // the camera most recently shown, as the original's one view

  // models
  int m_vibri_ = 0, m_main_ = 0, m_psj_ = 0, m_option_ = 0, m_hiscore_ = 0, m_end_ = 0, m_again_ = 0;
  // movies
  std::array<int, 6> vibri_movies_{};
  std::array<int, 4> main_steps_{};
  std::array<int, 5> psj_steps_{};
  int psj_open_ = 0, psj_close_ = 0;
  std::array<int, 3> option_steps_{};
  int option_open_ = 0, option_close_ = 0;
  int hi_open_ = 0, hi_loop_ = 0, hi_left_ = 0, hi_right_ = 0, hi_exit_ = 0;
  std::array<int, 4> over_ = {};          // the GAME OVER sign's clips
  int gov_fall_ = 0;                      // Vibri falls
  std::array<int, 7> gov_ = {};           // her reactions: 03a 03b 03c 05 06a 06b 07
  std::array<int, 2> again_steps_ = {};
  int again_close_ = 0;
  // CLEAR
  int m_song_ = 0, clear_sign_ = 0, clear_title_ = 0, clear_bonus_vibri_ = 0;
  std::array<int, 6> clr_ = {};        // Vibri: v04 (waits, loops), v05 (counts), v12 v14 v16 v15 (comments)
  std::array<int, 10> song_ = {};      // the medal steps, Vibri v01 ... v08
  int song_medal_ = 0, song_ten_ = 0;  // the medal and its score
  std::array<int, 10> song_cams_ = {}; // the medal steps' cameras
  int entering_frame_ = 0, repeat_ = 0, step_ = 0, medal_ = 2, pass_score_ = 2500;
  bool medal_shown_ = false;
  SpokenNumber spoken_;
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
  int result_frame_ = 0, leave_frame_ = 0, hiscore_frame_ = 0, comment_ = 0;
  std::uint32_t random_ = 12345;
  Cursor again_cursor_{0, 2, 1};
  double speed_ = 1.0;
  bool entering_ = false, leaving_ = false;
  Vibri vibri_ = Vibri::Hidden;
  int vibri_movie_ = 0, vibri_frame_ = 0;
  bool voice_playing_ = false, music_playing_ = false;
  // key repeat
  int repeat_state_ = 0, repeat_timer_ = 0;
  std::uint32_t frame_count_ = 0;
};

}  // namespace vib
