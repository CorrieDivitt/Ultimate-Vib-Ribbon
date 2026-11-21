#pragma once
// The kiosk movie: the original's attract film (the "demo" of the title's menu, and
// every fourth demo when the title is left alone). Three parts, each two camera
// shots over Vibri, her road, its pieces and, in the first, the frog and the snake,
// with the kiosk's music. Played from the player's own disc (KIOSK/<language>FILES.PAK,
// KIOSK/AUDIO/KIOSK.VH), one tick per frame of the original's 50 Hz display.
//
// Behaviour written from reading revib's KioskScene and its elements; our own code,
// the original's timings.
#include <cstdint>
#include <vector>

#include "vib/pak.h"
#include "vib/scene.h"

namespace vib {

class KioskMovie {
 public:
  KioskMovie(const Pak& kiosk_pak, int language);

  struct Input {
    bool confirm = false;                // a button: the movie ends (once the confirm sound has)
    bool music_playing = false;          // the kiosk's music (voice 0) is still heard
    bool confirm_sound_playing = false;  // the confirm sound (voice 1)
  };
  void start();
  void tick(const Input& in);
  bool finished() const { return finished_; }

  const std::vector<Hud::Layer>& layers() const { return layers_; }
  struct Sound {
    int number, voice;  // program * 16 + tone in the kiosk's bank
  };
  std::vector<Sound> take_sounds();

 private:
  enum class Jitter { Constant, Falling, Rising };
  struct JitterCue {
    int model, time, length, amount;
    Jitter kind;
  };
  struct Movie {
    int model, frame;
    ModelAnimation anim;
    Clip clip;
  };
  struct Element {  // clips played one after the other on the element's models
    std::vector<TmdModel> models;
    std::vector<Movie> movies;
    std::vector<JitterCue> jitter;
    std::vector<int> model_jitter;
    int frame = 0, cue = 0;
    int scatter = 20, scatter_step = -1;
    bool vibri = false;  // Vibri's own: tagged with her part for custom colours
    bool done() const;
  };
  struct Shot {
    int frame;
    CameraAnimation anim;
    Clip clip;
  };

  void update_element(Element& e, std::vector<SceneLine>& out);

  LineBuilder lines_;
  std::vector<Element> elements_;
  std::vector<Shot> shots_;
  int shot_frame_ = 0, shot_ = 0;
  SceneView view_;
  bool music_started_ = false, leaving_ = false, finished_ = false;
  std::uint32_t seed_ = 7;
  std::vector<Sound> sounds_;
  std::vector<Hud::Layer> layers_;
};

}  // namespace vib
