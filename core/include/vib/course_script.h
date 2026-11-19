#pragma once
// SCRIPT/SYSTEM3.FSL: the course script that shapes every track's obstacle course.
// Field names follow the open-ribbon documentation. Times are integers compared
// against the music position; the unit is very probably audio samples at 22,050 Hz
// (the custom-CD track's last segment starts at exactly 74:00 in that unit).

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
  std::uint32_t count = 0;     // 1 in all but the last entry; meaning unknown
  std::int32_t seed = 0;
  std::int32_t max_prob = 0;   // denominator
  std::array<std::int32_t, kObstacleKinds> cumulative{};  // running totals, one per obstacle kind
};

struct ControlSegment {
  std::int32_t start = 0;
  std::int32_t base_speed = 0;          // larger is slower
  std::int32_t base_shadow_period = 0;  // minimum gap between obstacles
  std::int32_t delta_speed = 0;         // change per 4096 time units
  std::int32_t delta_shadow_period = 0;
};

enum class PatternType : std::int32_t { Break = 0, Fixed = 1, Distribution = 2 };

struct PatternSegment {
  std::int32_t start = 0;
  std::int32_t unknown = 0;
  PatternType type = PatternType::Break;  // Break: song break, camera circles Vibri
  std::int32_t index = -1;                // into fixed or distribution patterns; -1 for none
};

struct EventSegment {
  std::int32_t start = 0;
  std::int32_t random_speed_duration = 0;
  std::int32_t random_speed_mode = 0;
  std::int32_t unused = 0;
  std::int32_t flip_duration = 0;
  std::int32_t camera_event = 0;  // camera index = value % 10, loop count = value / 10
};

struct CourseTrack {
  std::vector<ControlSegment> control;
  std::vector<PatternSegment> patterns;
  std::vector<EventSegment> events;
};

struct CourseScript {
  std::array<std::uint32_t, 3> header_unknown{};  // probably official / other / custom-CD track counts
  std::vector<std::uint32_t> header_track_list;
  std::vector<FixedPattern> fixed;
  std::vector<DistributionPattern> distributions;
  // Indexed by course: 0 Polaroid, 1 Sunny Day, 2 Laugh & Peace, 3 Universal Dance,
  // 4 Overflowing Emotions, 5 Roll Along, 6 custom CD. Resolved through the track index table.
  std::vector<CourseTrack> tracks;
};

CourseScript parse_course_script(ByteView data);

}  // namespace vib
