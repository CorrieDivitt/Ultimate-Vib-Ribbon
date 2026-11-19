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
#include "vib/scene.h"

namespace vib {

class TitleIntro {
 public:
  explicit TitleIntro(const Pak& title_pak);

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
  // Sounds started this frame (program * 16 + tone in the title's bank, TITLE.VH).
  std::vector<int> take_sounds();
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
  std::vector<int> sounds_;
  int time_ = 0;
  bool skippable_ = true, leaving_ = false;
  int leave_frames_ = 0;
  std::uint32_t seed_ = 1;
  std::vector<Hud::Layer> layers_;
};

}  // namespace vib
