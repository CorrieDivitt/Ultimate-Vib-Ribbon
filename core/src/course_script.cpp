#include "vib/course_script.h"

#include <string>

namespace vib {
namespace {

struct Reader {
  ByteView b;
  std::size_t p = 0;
  std::uint32_t u32() { const auto v = read_u32(b, p); p += 4; return v; }
  std::int32_t i32() { const auto v = read_i32(b, p); p += 4; return v; }
};

template <typename T, typename F>
std::vector<std::vector<T>> read_segment_tables(Reader& r, std::uint32_t tables, F read_one) {
  std::vector<std::vector<T>> out(tables);
  for (auto& table : out) {
    const std::uint32_t n = r.u32();
    require(n < 100000, "FSL segment count is implausible");
    table.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) table.push_back(read_one(r));
    for (std::size_t i = 1; i < table.size(); ++i)
      require(table[i - 1].start <= table[i].start, "FSL segments are not sorted by start time");
  }
  return out;
}

}  // namespace

CourseScript parse_course_script(ByteView b) {
  Reader r{b};
  const std::uint32_t control_tables = r.u32();
  const std::uint32_t pattern_tables = r.u32();
  const std::uint32_t event_tables = r.u32();
  const std::uint32_t track_count = r.u32();
  const std::uint32_t fixed_count = r.u32();
  const std::uint32_t dist_count = r.u32();
  require(control_tables < 64 && pattern_tables < 64 && event_tables < 64 && track_count < 64 && fixed_count < 1024 &&
              dist_count < 1024,
          "FSL header counts are implausible");

  CourseScript s;
  const std::uint32_t rounds = r.u32(), songs = r.u32(), medleys = r.u32();
  require(rounds < 64 && songs < 64 && medleys < 64, "FSL header track lists are implausible");
  for (std::uint32_t i = 0; i < rounds; ++i) s.round_tracks.push_back(r.u32());
  for (std::uint32_t i = 0; i < songs; ++i) s.song_tracks.push_back(r.u32());
  for (std::uint32_t i = 0; i < medleys; ++i) s.medley_tracks.push_back(r.u32());
  for (const auto* list : {&s.round_tracks, &s.song_tracks, &s.medley_tracks})
    for (std::uint32_t t : *list) require(t < track_count, "FSL header names a missing track");

  s.fixed.resize(fixed_count);
  for (auto& f : s.fixed) {
    const std::uint32_t n = r.u32();
    require(n < 1024, "FSL fixed pattern is implausibly long");
    for (std::uint32_t i = 0; i < n; ++i) {
      const std::uint32_t v = r.u32();
      require(v < kObstacleKinds, "FSL fixed pattern has unknown obstacle " + std::to_string(v));
      f.sequence.push_back(static_cast<Obstacle>(v));
    }
  }

  s.distributions.resize(dist_count);
  for (auto& d : s.distributions) {
    d.count = r.u32();
    d.seed = r.i32();
    d.max_prob = r.i32();
    for (auto& c : d.cumulative) c = r.i32();
  }

  const auto control = read_segment_tables<ControlSegment>(r, control_tables, [](Reader& x) {
    ControlSegment c;
    c.start = x.i32(); c.period = x.i32(); c.select_period = x.i32();
    c.period_slope = x.i32(); c.select_period_slope = x.i32();
    return c;
  });
  const auto patterns = read_segment_tables<PatternSegment>(r, pattern_tables, [](Reader& x) {
    PatternSegment p;
    p.start = x.i32(); p.unknown = x.i32();
    const std::int32_t type = x.i32();
    require(type >= 0 && type <= 2, "FSL pattern segment has unknown type");
    p.type = static_cast<PatternType>(type);
    p.index = x.i32();
    return p;
  });
  const auto events = read_segment_tables<EventSegment>(r, event_tables, [](Reader& x) {
    EventSegment e;
    e.start = x.i32(); e.tempo_length = x.i32(); e.tempo_pattern = x.i32();
    e.reverse_length = x.i32(); e.screw_length = x.i32(); e.camera = x.i32();
    return e;
  });

  s.tracks.resize(track_count);
  for (auto& t : s.tracks) {
    const std::uint32_t c = r.u32(), p = r.u32(), e = r.u32();
    require(c < control.size() && p < patterns.size() && e < events.size(), "FSL track index out of range");
    t.control = control[c];
    t.patterns = patterns[p];
    t.events = events[e];
  }
  require(r.p == b.size(), "FSL has " + std::to_string(b.size() - r.p) + " unread bytes");

  for (const auto& t : s.tracks)
    for (const auto& p : t.patterns) {
      if (p.type == PatternType::Fixed) require(p.index >= 0 && std::size_t(p.index) < s.fixed.size(), "FSL fixed pattern index out of range");
      if (p.type == PatternType::Distribution)
        require(p.index >= 0 && std::size_t(p.index) < s.distributions.size(), "FSL distribution index out of range");
    }
  return s;
}

}  // namespace vib
