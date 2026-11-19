#pragma once
// The scene of a round as the original draws it: the road and its pieces, Vibri
// in her four forms with her move animations, the evolution meter, the game
// camera with its clips and transitions, and the jitter that makes every line
// shake. docs/SPEC.md sections 5 to 7. Our own implementation of the original's
// behaviour, with no graphics API: it produces 3D line segments and a camera.
//
// Coordinates are the PlayStation's: X right, Y down, Z along the road.

#include <array>
#include <cstdint>
#include <vector>

#include "vib/fixed.h"
#include "vib/gameplay.h"
#include "vib/motion.h"
#include "vib/pak.h"
#include "vib/pose.h"
#include "vib/tmd.h"

namespace vib {

struct SceneLine {
  Vec3f a, b;
  Rgb colour;
};

struct SceneView {
  Vec3f eye, target;  // world space; "up" on screen is -Y
  float fov_y = 0;    // vertical field of view in radians, for a 4:3 picture
};

// An animation played by time. The original steps a clip once per video frame by
// rate / refresh; this steps by elapsed seconds, which is the same thing at any
// refresh rate. The frame shown is the whole part of the position (no blending).
class Clip {
 public:
  Clip() = default;
  Clip(int frames, int rate, bool loop) : frames_(frames), rate_(rate > 0 ? rate : 60), loop_(loop) {}

  void rewind(bool backwards = false) {
    finished_ = false;
    position_ = backwards ? frames_ - 0.5 : 0.5;
  }
  void seek(double position) { position_ = position; }
  void set_speed(double speed) { speed_ = speed; }
  void advance(double seconds);

  int frame() const;
  int frames() const { return frames_; }
  double position() const { return position_; }
  double speed() const { return speed_; }
  bool finished() const { return finished_; }

 private:
  int frames_ = 0;
  double rate_ = 60, speed_ = 1, position_ = 0.5;
  bool loop_ = false, finished_ = false;
};

// Turns posed line models into world-space lines with the original's jitter.
// One shared generator, reseeded by the caller (the original's runs on forever;
// reseeding at its 50 Hz gives the same look).
class LineBuilder {
 public:
  struct Xform {
    Mat3 m;
    Vec3f t;
  };
  void seed(std::uint32_t s) { random_ = s; }
  void set_view(const SceneView& v) { view_ = v; }
  // One object: each vertex moved by a random offset in [-range, range) on each
  // axis, then `z_offset` added to z, then `xf` applied. With `join`, a line from
  // that point (in this object's local space) to its first vertex is added, and
  // `join` becomes its last vertex. Objects with a facing marker that faces away
  // from the view are left out.
  void object(const TmdObject& obj, const Xform& xf, int range, std::int32_t z_offset, Vec3f* join, std::vector<SceneLine>& out);
  // Every object keyed in `frame` of `anim`, placed at `origin`. `swap_first`: draw
  // this object wherever the clip asks for object 0 (the score marks); -1 for none.
  void animated(const TmdModel& model, const ModelAnimation& anim, int frame, Vec3f origin, int range, std::vector<SceneLine>& out,
                int swap_first = -1);

 private:
  std::int32_t offset(int range);
  std::uint32_t random_ = 0;
  SceneView view_;
};

// The overlays drawn over the scene, each through its own fixed camera: the score
// as a row of circling marks, the time meter, and the "Round 1/2" sign.
class Hud {
 public:
  // `language`: the archive's language prefix for text models, "02_" for English.
  explicit Hud(const Pak& game_pak, const char* language = "02_");

  // A round of the game's own courses begins; `index` 0 or 1 picks the sign.
  void start_round(int index, bool show_sign);

  struct Frame {
    double seconds = 0;
    std::uint32_t jitter_seed = 0;
    int score = 0;          // shown as marks; capped at 116,279
    int combo = 0;          // hits toward the next form: the marks circle faster
    double time_left = 1;   // 1 at the start of the round, 0 at its end
    bool show_score = true, show_time = true;
  };
  struct Layer {
    std::vector<SceneLine> lines;
    SceneView view;
  };
  void draw(const Frame& f, std::vector<Layer>& layers);

  // The seven mark shapes (0-14, never rising along the row) for a score.
  static std::array<int, 7> marks(int score);
  static constexpr int kMaxScore = 116279;

 private:
  LineBuilder lines_;
  TmdModel mark_model_, time_model_, sign_model_;
  ModelAnimation score_anim_, time_anim_, sign_anims_[2];
  CameraAnimation time_camera_, sign_camera_;
  Clip score_, time_, sign_;
  int sign_index_ = 0;
  bool show_sign_ = false;
};

// The game camera: a camera clip played some number of times, then a transition
// clip to the next camera, as the course script's camera events ask.
class GameCamera {
 public:
  explicit GameCamera(const Pak& pak);
  // `fast`: clips run at 1.3 (the original passes "this is a course of the game's own
  // disc"); `hold`: a camera clip keeps looping (speed courses).
  void reset(bool fast, bool hold);
  void event(int value);  // loops * 10 + camera id
  void cut();             // a break: on to the next camera now
  // The current frame's view, then the clips advance by `seconds`.
  SceneView update(double seconds, Vec3f origin);
  int side() const { return in_transition_ ? next_side_ : side_; }

 private:
  struct Camera {
    int id, group;
    CameraAnimation anim;
    Clip clip;
  };
  struct Transition {
    int from, to;
    CameraAnimation same_anim, switch_anim;
    Clip same_clip, switch_clip;
  };
  void take_next(int side);
  void start_base();
  void start_transition();

  std::vector<Camera> cameras_;
  std::vector<Transition> transitions_;
  const CameraAnimation* anim_ = nullptr;
  Clip* clip_ = nullptr;
  bool fast_ = false, hold_ = false, waiting_ = true, in_transition_ = false, flip_ = false;
  int camera_ = 0, loops_done_ = 0, loops_ = 1, side_ = 0;
  int next_camera_ = 0, next_loops_ = 1, next_side_ = 0, event_camera_ = 0, event_loops_ = 1;
};

class Scene {
 public:
  static constexpr int kForms = 4, kMoves = 11, kRunning = 10;

  explicit Scene(const Pak& game_pak, float road_length = 10.0f, float vibri_position = 1.25f);

  void reset(bool home_course, bool speed_course);
  // Vibri starts the move for `road` (0-9); `alternate`: its stumbling version, used
  // for stray presses.
  void set_move(int road, bool alternate);
  // The current move has gone `fraction` of the way (or she is just running).
  bool move_done(double fraction) const;
  void show_evo(int frame);
  void play_evo();
  GameCamera& camera() { return camera_; }

  struct Frame {
    double seconds = 0;          // since the last frame drawn
    std::uint32_t jitter_seed = 0;  // change it at the original's 50 Hz for its look
    Fixed speed;                 // parts per second
    std::vector<RoadPiece> road;
    bool still = false;          // a part is at Vibri: nothing jitters but missed pieces
    Fixed shake;                 // 1 just after a miss, down to 0
    int form = 1, stage = 0;
  };
  // Builds the frame's lines and view, then advances every clip.
  void draw(const Frame& f, std::vector<SceneLine>& lines, SceneView& view);

 private:
  struct Model {
    TmdModel tmd;
    int jitter = 0;
  };
  struct Move {
    ModelAnimation anim;
    Clip clip;
  };
  Move& current() { return moves_[std::size_t(form_)][std::size_t(move_)][alternate_ ? 1 : 0]; }
  const Move& current() const { return moves_[std::size_t(form_)][std::size_t(move_)][alternate_ ? 1 : 0]; }

  Fixed length_, start_, offset_;
  TmdModel road_, point_, evo_model_;
  ModelAnimation evo_anim_;
  Clip evo_;
  int evo_frame_ = 0, evo_jitter_ = 0;
  std::vector<TmdModel> forms_;
  std::vector<std::vector<std::vector<Move>>> moves_;  // [form][move][version]
  GameCamera camera_;
  int form_ = 0, move_ = kRunning;
  bool alternate_ = false;
  LineBuilder lines_;
  SceneView view_;
};

}  // namespace vib
