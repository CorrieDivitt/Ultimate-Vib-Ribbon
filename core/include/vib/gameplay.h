#pragma once
// The rules of play, with no graphics, sound or input devices: parts on the
// road, judging presses, Vibri's forms, score, courses. docs/SPEC.md section 4.
// Our own implementation of the original's behaviour.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "vib/course_gen.h"
#include "vib/fixed.h"

namespace vib {

enum class Course { Bronze, Silver, Gold, OneTrack, AllTracks, Speed1, Speed2, Speed3, InOrder };
constexpr int kCourseCount = 9;

struct CourseInfo {
  int tracks;            // -1: every track of the disc
  bool auto_play;        // the game plays itself
  int first_home_track;  // on the game's own disc, counting audio tracks from 1; 0: not played from it
  bool audio_cd;         // played from the player's audio CD (or own music)
  bool shuffle;
  float min_speed, max_speed;  // speed courses, beats per second
  int points;                  // speed courses: points that bring the top speed
  bool again;                  // "play again" restarts from the track that was lost
  int high_scores[3];          // defaults (speed courses: seconds)
};
const CourseInfo& course_info(Course c);

// The four actions, as bits. A pair of obstacles needs both of its bits.
enum Press : std::uint8_t { kPressBlock = 1, kPressPit = 2, kPressLoop = 4, kPressWave = 8 };
std::uint8_t road_presses(int road);  // road 0-9
constexpr bool is_pair(int road) { return road >= 4 && road <= 9; }

struct Part {
  int road = 0;
  std::int32_t time = 0;  // samples, as the sound is heard
  Fixed rate;             // road units per 64 samples
  bool screw = false;     // its piece leans as the camera passes
  Fixed position;         // part-lengths ahead of Vibri; negative is behind
  bool done = false, missed = false;
  std::uint8_t pressed = 0;
  int other = 0;  // a road that is not its own, for the demo's deliberate misses
};

struct RoadPiece {
  int road;
  Fixed position;  // along the road as drawn (Vibri's own place added)
  bool missed, leaning;
};

// What happened during one update, for sound and animation.
struct GameEvent {
  enum class Type {
    Hit,       // road
    Miss,      // road
    Stray,     // a press with no part to take it. road = the action pressed; value = bonus earned (0, 1, 2 or 4)
    FormUp,    // value = new form
    FormDown,  // value = new form (a miss in the best form)
    LevelUp,   // value = new level: one step worse within or across forms
    Camera,    // value = loops * 10 + camera id, from the course script
    CameraBreak,
    SelectOff,  // audio CDs: obstacles pause (seconds 52-59 of each minute)
    SelectOn,
  };
  Type type;
  int road = 0;
  int value = 0;
  std::int32_t time = 0;  // Hit and Miss: the part's time (samples, as heard), to show how early or late a press was
};

enum class Outcome { Playing, Clear, GameOver };

struct RoundSetup {
  Course course = Course::Bronze;
  int course_seconds = 1;      // length shown on the time meter
  int points = 0;              // carried in from the course's earlier tracks
  std::int32_t origin = 0;     // stream position times are measured from
  std::int32_t start = 0;      // parts before this are rejected
  int track_index = 0;         // within the course
  bool demo = false;
  // Samples by which parts are moved earlier to make up for display delay.
  // The original uses two frame periods, 882 samples at 49.759 Hz. A VR build
  // predicts display time itself, so the default is none.
  std::int32_t video_delay = 0;
  std::int32_t stream_delay_seconds = 8;  // look-ahead, whole seconds
};

class Gameplay {
 public:
  static constexpr int kMaxScore = 116279;
  static constexpr std::int32_t kOriginalVideoDelay = 882;
  static constexpr std::int32_t kKeepForRewind = 8 * kSampleRate;  // parts passed in the last 8 s stay known (a pause rewinds 2 or more)

  explicit Gameplay(float distance = 10.0f, float vibri_position = 1.25f, float beats_per_second = 2.0f);

  void seed_demo(std::uint32_t seed) { random_ = seed; }
  void start(const RoundSetup& setup);
  void add_event(const CourseEvent& e);
  void end_at(std::int32_t position);  // the music ends there
  void quiet(std::int32_t position);   // parts stop coming there

  // One step. `position`: where the heard music is, in samples. `pressed`: Press
  // bits that went down since the last step. `move_done`: Vibri's current move
  // animation is at least half over (presses are ignored until then).
  Outcome update(std::int32_t position, std::uint8_t pressed, bool move_done = true);

  // State after the last update.
  const std::vector<GameEvent>& events() const { return events_; }
  const std::vector<Part>& parts() const { return parts_; }
  // The road's pieces to draw. As the original: from just behind Vibri to its
  // horizon. `far` (for a headset, to look along the line): every part built so far
  // ahead (the course is made eight seconds ahead), and the ones passed within
  // keep_passed()'s time behind. Drawing only: the rules never see the passed ones.
  // With `far`, everything behind Vibri moves back together, at the speed of the
  // part she is passing at each moment (ours: in the original each moves back at its
  // own speed, but it is gone 4.25 lengths behind; on a long trail a slow part would
  // trail her while faster ones passed later slid back through it).
  std::vector<RoadPiece> road(bool far = false) const;
  void keep_passed(std::int32_t samples) { keep_passed_ = samples; }
  // The same road as it was (or will be) at an earlier `position`, from the parts
  // still known: for drawing a pause's run-up, while the rules stand still. Parts
  // passed in the last few seconds are kept for this whatever keep_passed() says.
  std::vector<RoadPiece> road_at(std::int32_t position, bool far = false) const;
  // Parts already judged whose times are in [from, to), oldest first: for the run-up
  // to replay Vibri's moves over them.
  std::vector<Part> judged(std::int32_t from, std::int32_t to) const;
  // The latest position at or before `at` with no known part within `margin`
  // samples of it (so Vibri does not start inside one), but not before `earliest`.
  std::int32_t clear_before(std::int32_t at, std::int32_t margin, std::int32_t earliest) const;
  // A part play has not been given yet (built further ahead, for drawing), placed
  // where play would put it at `position`; false if play would not take it.
  bool preview(const CourseEvent& e, std::int32_t position, RoadPiece& out) const;
  // How far the trail behind Vibri moved from `from` to `to` (samples, from <= to):
  // the road's distance at the speed of the part last passed at each moment.
  Fixed trail(std::int32_t from, std::int32_t to) const;
  int points() const { return points_; }
  int score() const { return points_ < kMaxScore ? points_ : kMaxScore; }
  int bonus() const;                 // end-of-round bonus for the current form
  int level() const { return level_; }
  int form() const { return level_ / 3; }   // 0 best (angel) to 3 (worm)
  int stage() const { return level_ % 3; }
  int combo() const { return combo_; }      // hits in a row toward the next form (18)
  int chain() const { return chain_; }      // hits in a row for scoring (10 at most)
  Fixed speed() const { return speed_; }    // beats per second, for drawing
  Fixed shake() const { return shake_; }    // 1 right after a miss, falling to 0
  bool near_part() const { return near_; }  // a part is at Vibri or was just hit
  bool time_up() const { return time_up_; }
  int seconds() const { return seconds_; }  // speed courses: time taken, once cleared
  int music_volume() const { return volume_; }  // 0-256; the demo fades out
  // Speed courses: the cue the course generator must use (period, select period); 0 when none.
  std::int32_t fixed_period() const { return fixed_period_; }

 private:
  // trail() from each time to `now`, with the speeds looked up once (for a whole road).
  std::function<Fixed(std::int32_t)> trail_of(std::int32_t now) const;
  void hit(Part& p);
  void miss(Part& p);
  int stray(int road, int part_road);
  void emit(GameEvent::Type t, int road = 0, int value = 0, std::int32_t time = 0) { events_.push_back({t, road, value, time}); }

  struct Interval {
    bool set = false;
    std::int32_t to = 0;
    bool over(std::int32_t t) const { return set && std::int32_t(std::uint32_t(t) - std::uint32_t(to)) >= 0; }
  };

  Fixed distance_, vibri_, beats_;
  RoundSetup setup_{};
  bool running_ = false, home_ = true, speed_course_ = false, auto_ = false, auto_play_ = false;
  std::vector<Part> parts_;
  std::vector<Part> passed_;     // gone from parts_, kept for drawing (oldest first)
  std::int32_t keep_passed_ = 0;
  std::int32_t position_ = 0;  // of the last update
  std::vector<GameEvent> events_, incoming_;
  Interval start_, end_, quiet_, fade_;
  // screw effect
  bool effect_set_ = false;
  Interval effect_start_, effect_end_;
  // score
  int points_ = 0, strays_ = 0, stray_bonus_ = 0, chain_ = 0, stray_roads_[2] = {0, 0};
  int last_base_ = 0, last_bonus_ = 0;
  // forms
  int level_ = 3, combo_ = 0, misses_ = 0;
  bool game_over_ = false;
  // speed
  Fixed speed_, max_speed_, range_min_, range_max_, range_speed_;
  int range_points_ = 0, range_scored_ = 0;
  std::int32_t ramp_position_ = 0;
  bool ramp_started_ = false;
  std::int32_t fixed_period_ = 0;
  // demo
  std::uint32_t random_ = 0;
  int demo_hits_ = 0, demo_tracks_ = 2;
  // per-frame outputs
  Fixed shake_, beat_period_;
  std::int32_t beat_time_ = 0, last_part_time_ = 0;
  bool new_part_ = false, near_ = false, time_up_ = false, no_select_ = false, camera_break_ = false;
  int seconds_ = 0, volume_ = 256;
};

}  // namespace vib
