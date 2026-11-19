#pragma once
// SCRIPT/SYSTEM3.FSL: the course script that shapes every track's obstacle course.
// All times and lengths are audio samples at 22,050 Hz, counted from the start of
// the track. See docs/SPEC.md for how the game uses each field.

#include <array>
#include <cstdint>
#include <vector>

#include "vib/bytes.h"

namespace vib {

enum class Obstacle : std::uint8_t {
  Block, Pit, Loop, Wave,
  BlockPit, BlockLoop, BlockWave, PitLoop, PitWave, LoopWave,
};
constexpr int kObstacleKinds = 10;

// Played in order and repeated while its pattern segment is active.
struct FixedPattern {
  std::vector<Obstacle> sequence;
};

// Seeded weighted random choice over the ten obstacle kinds.
struct DistributionPattern {
  std::uint32_t count = 0;     // never read by the game
  std::int32_t seed = 0;
  std::int32_t max_prob = 0;   // denominator
  std::array<std::int32_t, kObstacleKinds> cumulative{};  // running totals, one per obstacle kind
};

struct ControlSegment {
  std::int32_t start = 0;
  std::int32_t period = 0;               // length of one road part, in samples
  std::int32_t select_period = 0;        // length of the window in which one onset is chosen
  std::int32_t period_slope = 0;         // change per 4096 samples; ignored in a track's last segment
  std::int32_t select_period_slope = 0;
};

enum class PatternType : std::int32_t { Break = 0, Fixed = 1, Distribution = 2 };

struct PatternSegment {
  std::int32_t start = 0;
  std::int32_t unknown = 0;
  PatternType type = PatternType::Break;  // Break: no obstacles are placed
  std::int32_t index = -1;                // into fixed or distribution patterns; -1 for none
};

struct EventSegment {
  std::int32_t start = 0;
  std::int32_t tempo_length = 0;    // > 0: road part lengths vary for this long
  std::int32_t tempo_pattern = 0;   // how they vary: 2, 3, 22 or anything else (see SPEC)
  std::int32_t reverse_length = 0;  // > 0: sends a "reverse" effect, which the game then ignores
  std::int32_t screw_length = 0;    // > 0: parts placed in this span lean as the camera passes
  std::int32_t camera = 0;          // >= 0: loops * 10 + camera id; negative: none
};

struct CourseTrack {
  std::vector<ControlSegment> control;
  std::vector<PatternSegment> patterns;
  std::vector<EventSegment> events;
};

struct CourseScript {
  // Which track each kind of play uses: the six fixed rounds, free-order songs
  // (none on this disc), and the medley used for audio CDs (track 6).
  std::vector<std::uint32_t> round_tracks, song_tracks, medley_tracks;
  std::vector<FixedPattern> fixed;
  std::vector<DistributionPattern> distributions;
  // Indexed by course: 0 Polaroid, 1 Sunny Day, 2 Laugh & Peace, 3 Universal Dance,
  // 4 Overflowing Emotions, 5 Roll Along, 6 custom CD. Resolved through the track index table.
  std::vector<CourseTrack> tracks;
};

CourseScript parse_course_script(ByteView data);

}  // namespace vib
