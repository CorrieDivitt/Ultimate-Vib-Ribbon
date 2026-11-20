#pragma once
// Turning music into an obstacle course: the audio front end, onset detection,
// peak picking and the course script, as described in docs/SPEC.md sections 1-3.
// Our own implementation of the original's behaviour; all arithmetic is integer
// so results are identical on every platform.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "vib/course_script.h"

namespace vib {

constexpr int kSampleRate = 22050;  // of the mono analysis stream; all course times use it
constexpr int kBlockSize = 128;
constexpr int kLookAheadBlocks = 1379;  // 8,000 ms rounded up to whole blocks

using Block = std::array<std::int16_t, kBlockSize>;

// 44.1 kHz stereo (interleaved L, R) to 22.05 kHz mono blocks: every second
// stereo sample, left and right averaged with truncation toward zero.
// A final partial block is dropped. `lead` moves the music against the round's
// start, in 44.1 kHz samples: positive skips that many, negative puts silence first.
std::vector<Block> make_blocks(std::span<const std::int16_t> stereo_44100, int lead = 0);

// The original does not produce exactly the same course twice: where the music
// starts against the round's start, and how audio blocks fall into video frames,
// differ a little on every play (docs/HANDOFF.md section 2d). A CourseTiming holds
// those two accidents.
struct CourseTiming {
  int lead = 0;                        // for make_blocks
  std::int64_t frame_period_q16 = 0;   // 0: consult the script every block
  std::int64_t frame_phase_q16 = 0;

  // One fixed course per song, the same on every play.
  static CourseTiming consistent() { return {}; }
  // As the original behaves: a start offset and frame phase drawn, from `seed`,
  // out of the ranges measured on the original. A different seed on each play
  // gives the original's play-to-play variation.
  static CourseTiming like_original(std::uint32_t seed);
};

struct CourseEvent {
  enum class Type { Part, Camera, Effect };
  enum class Effect { Screw, Reverse, Tempo };
  Type type = Type::Part;
  std::int32_t time = 0;    // samples from the start of the stream
  std::int32_t value = 0;   // Part: road 0-9. Camera: loops * 10 + id. Effect: an Effect.
  std::int32_t length = 0;  // Part: the cue's period. Effect: its duration.
  std::int32_t scale = 0;   // Part: tempo scale, 16.16
};

// An audio CD's key, from its table of contents: where the lead-out starts (in
// sectors) and the last track's number.
inline std::uint32_t disc_key(std::uint32_t lead_out_sector, std::uint32_t last_track) {
  return ((lead_out_sector << 7) | last_track) ^ 0xCBF633B1u;
}

// `count` numbers from `lo` to `hi`, at random from `seed`: all different until
// every number has come up, then the same again.
std::vector<int> random_picks(int lo, int hi, int count, std::uint32_t seed);

// The 32-bit generator used everywhere in the game.
inline std::uint32_t next_random(std::uint32_t seed) { return seed * 1664525u + 1013904223u; }

class CourseGenerator {
 public:
  // `cue_interval_blocks`: how often the script is consulted. 1 = every block
  // (fully deterministic). The original consults it once per video frame.
  explicit CourseGenerator(const CourseScript& script, int cue_interval_blocks = 1);
  ~CourseGenerator();

  // Consult the script once per video frame instead, as the original does: every
  // block that arrives during one frame uses the cue fetched for the first of them.
  // `period_q16` is the frame length in samples times 65536 (the PAL original:
  // 22050 / 49.759 Hz), `phase_q16` where in a frame the round's first block falls.
  void set_frame_timing(std::int64_t period_q16, std::int64_t phase_q16) {
    frame_period_ = period_q16;
    frame_phase_ = phase_q16;
  }
  // One PAL frame (314 lines of 3406 video clock ticks at 53.203425 MHz) in samples, times 65536.
  static constexpr std::int64_t kOriginalFramePeriodQ16 = 29048467;  // 443.244 samples
  void set_timing(const CourseTiming& t) { set_frame_timing(t.frame_period_q16, t.frame_phase_q16); }

  // Begins a round on the game's own disc: `round` counts from 1 and selects the
  // script track; `start_time` is the stream position of the track's first sample.
  void start_round(int round, std::int32_t start_time, std::uint32_t script_seed = 0);

  // An audio CD (or the player's own music, taken as one): every track uses the
  // script's medley track, each from its own place in it. The tracks are put in an
  // order, and the script's spare time is shared out between them as gaps, both at
  // random from the disc's key, so the same disc always gives the same courses.
  // `track_samples`: each track's whole seconds at 22,050 Hz, in disc order.
  void prepare_disc(const std::vector<std::int32_t>& track_samples, std::uint32_t disc_key);
  // Begins disc track `track` (from 1); `start_time` as for start_round.
  void start_track(int track, std::int32_t start_time);
  // Where each disc track starts in the medley script (after prepare_disc).
  const std::vector<std::int32_t>& track_offsets() const { return disc_starts_; }

  // While false the script places no obstacles (gameplay turns it off for seconds
  // 52 to 59 of every minute of an audio CD).
  void set_select(bool on) { select_ = on; }

  // Feeds the next block, whose first sample is at stream position `time`.
  void process(std::int32_t time, const Block& block);

  // The script currently asks for obstacles. When this turns false the original
  // tells gameplay that parts have stopped coming (Gameplay::quiet).
  bool selecting() const { return current_cue_.selector != nullptr; }

  // Events generated so far, in the order the original would emit them.
  std::vector<CourseEvent>& events() { return events_; }

 private:
  struct Selector;
  struct Tempo;
  struct Cue {
    std::int32_t period = kSampleRate;
    std::int32_t select_period = kSampleRate / 10;
    std::shared_ptr<Selector> selector;
    std::shared_ptr<Tempo> tempo;
  };
  struct Pick {
    Cue cue;
    std::int32_t time = 0;
    std::int32_t level = 0, background = 0;
  };

  Cue next_cue(std::int32_t time);
  void begin(std::int32_t start_time);
  void send_effects();
  void send_effect(const EventSegment& e, bool tempo, bool reverse, bool screw, bool camera);
  void end_window(std::int32_t time);
  void take_candidate();
  void place();

  std::vector<int> disc_order_;            // disc track (from 0) -> its slot in the medley
  std::vector<std::int32_t> disc_starts_;  // disc track (from 0) -> script offset
  std::uint32_t disc_key_ = 0;
  bool select_ = true;
  const CourseScript& script_;
  int cue_interval_;
  int blocks_since_cue_ = 0;
  std::int64_t frame_period_ = 0, frame_phase_ = 0, last_frame_ = -1;
  Cue current_cue_;

  // script position
  bool playing_ = false;
  const CourseTrack* track_ = nullptr;
  int round_ = 1;
  std::uint32_t script_seed_ = 0;
  std::int32_t start_time_ = 0, offset_ = 0, end_ = 0, now_ = 0;
  std::size_t effect_index_ = 0;
  std::vector<std::shared_ptr<Selector>> fixed_selectors_, distribution_selectors_;
  std::shared_ptr<Tempo> tempo_;

  // onset detection
  static constexpr int kHistory = 32, kRecent = 4;
  std::array<Block, kHistory> history_{};
  int history_index_ = 0;
  std::int32_t short_level_ = 0, long_level_ = 0;
  std::int32_t window_start_ = 0;
  std::uint32_t window_ = 0;

  // peak picking
  std::optional<Pick> pending_, candidate_;
  std::int32_t pending_scale_ = 65536, pending_length_ = 0;

  std::vector<CourseEvent> events_;
};

}  // namespace vib
