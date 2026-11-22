#pragma once
// The title: the camera flying through the intro's lines, then Vibri, the title
// and the logo, waiting for a button. Played from the player's own disc
// (TITLE/FILES.PAK), one tick per frame of the original's 50 Hz display.
//
// Behaviour written from reading revib's TitleScene and its elements; our own
// code, the original's timings.
#include <array>
#include <cstdint>
#include <vector>

#include "vib/pak.h"
#include "vib/game_text.h"
#include "vib/scene.h"

namespace vib {

class TitleIntro {
 public:
  // `language`: the original's number (1 English), for the mode menu's words.
  explicit TitleIntro(const Pak& title_pak, int language = 1);

  // The European version's menus at the title (the original's LanguageSelect): the
  // five languages, then "play / demo / back". Shown over the title's own scene.
  enum class Menu { None, Language, Mode, Start };
  enum class Choice { None, German, English, Spanish, French, Italian, Play, Demo, Back, Exit, Started };
  struct Input {
    bool up = false, down = false, left = false, right = false, confirm = false, cancel = false;
    bool confirm_sound_playing = false;  // the confirm sound (voice 1) is still heard
  };
  void show(Menu m);
  // Ours: a fourth item on the mode menu, below "back", that closes the game (Choice::Exit),
  // its word built from the line font (`font`, the game archive's FONT/FONT.TMD, with the
  // character table in `text`) at the size of the original's words. Without it the menu is
  // the original's three items.
  void add_exit_item(const TmdModel& font, const GameText& text, int language);
  bool has_exit_item() const { return exit_item_; }
  // The language menu's cursor on language `language` (0 German .. 4 Italian).
  void set_language_cursor(int language);
  // Back from play: the mode menu, its lines gathering in.
  void close_menus();
  Choice choice() const { return choice_; }
  // One frame with the menus' input (the intro's skip is any button, as before).
  void tick(const Input& in);

  // From the start, or (`at_title`) straight to the title, its lines gathering in,
  // as the original does when a course hands back to it.
  void start(bool at_title);
  // One frame. `confirm`: a button pressed this frame (skips the intro).
  void tick(bool confirm);
  // The lines scatter away; over() once they have.
  void leave();
  bool intro_over() const;  // the title is showing: a button now leaves
  bool leaving() const { return leaving_; }
  bool over() const;

  const std::vector<Hud::Layer>& layers() const { return layers_; }
  // Sounds started this frame (program * 16 + tone in the title's bank, TITLE.VH),
  // with the voice each plays on.
  struct Sound {
    int number, voice;
  };
  std::vector<Sound> take_sounds();
  // The original's frame count of a time in 30ths of a second.
  static int game_frames(int thirtieths);

 private:
  struct ModelCue {
    int movie, model, frame, label;
  };
  struct Movie {
    ModelAnimation anim;
    Clip clip;
  };
  struct Element {  // models played one after the other
    std::vector<TmdModel> models;
    std::vector<Movie> movies;
    std::vector<ModelCue> cues;
    int frame = 0, cue = 0;
    int scatter = 20, scatter_step = -1;
  };
  struct CameraCue {
    CameraAnimation anim;
    Clip clip;
    int frame, label;
  };
  struct SoundCue {
    int sound, frame, label;
  };

  void seek_title();
  void update_menus(const Input& in, std::vector<SceneLine>& out);
  void mode_jitter(int frame);
  Movie load_menu_movie(const Pak& pak, const std::string& name);
  void update_element(Element& e, std::vector<SceneLine>& out);
  int scatter_jitter(Element& e);

  LineBuilder lines_;
  Element lines_element_, vibri_;
  std::vector<CameraCue> cameras_;
  int camera_frame_ = 0, camera_cue_ = 0;
  TmdModel logo_model_;
  ModelAnimation logo_anim_;
  int logo_frame_ = 0, logo_scatter_ = 20, logo_scatter_step_ = -1;
  std::vector<SoundCue> sound_cues_;
  int sound_frame_ = 0, sound_cue_ = 0;
  std::vector<Sound> sounds_;
  // the menus
  TmdModel menu_models_[2];  // language, mode
  Movie select_[2], scale_[2];
  int menu_jitter_[2] = {10, 10};
  Menu current_ = Menu::None, next_ = Menu::None;
  Choice choice_ = Choice::None;
  bool busy_ = false, closing_ = false, drawn_ = false, reversed_ = false, chosen_ = false;
  int transition_frame_ = 0;
  int lang_x_ = 1, lang_y_ = 0, mode_index_ = 0;
  bool exit_item_ = false;
  int time_ = 0;
  bool skippable_ = true, leaving_ = false, skipped_ = false;
  int leave_frames_ = 0;
  std::uint32_t seed_ = 1;
  std::vector<Hud::Layer> layers_;
};

}  // namespace vib
