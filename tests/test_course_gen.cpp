// The course generator on synthetic input. No game data needed.

#include <algorithm>
#include <vector>

#include "test.h"
#include "vib/course_gen.h"

using namespace vib;

namespace {

// One track: half-second parts, quarter-second selection windows, pattern Block, Pit, Block, Pit...
CourseScript tiny_script() {
  CourseScript s;
  s.round_tracks = {0};
  s.fixed.push_back(FixedPattern{{Obstacle::Block, Obstacle::Pit}});
  CourseTrack t;
  t.control.push_back(ControlSegment{0, 11025, 5512, 0, 0});
  t.patterns.push_back(PatternSegment{0, 0, PatternType::Fixed, 0});
  t.events.push_back(EventSegment{0, 0, 0, 0, 0, 23});
  s.tracks.push_back(t);
  return s;
}

// Silence with a loud 50 ms burst every `spacing` samples (at 22,050 Hz), as 44.1 kHz stereo.
std::vector<std::int16_t> bursts(int seconds, int spacing) {
  std::vector<std::int16_t> pcm(std::size_t(seconds) * 44100 * 2, 0);
  for (int t = spacing; t < seconds * kSampleRate; t += spacing)
    for (int i = 0; i < 1100; ++i) {
      const std::size_t pair = std::size_t(t + i) * 2;  // two 44.1 kHz pairs per analysis sample
      if (pair * 2 + 3 >= pcm.size()) break;
      const auto v = static_cast<std::int16_t>((i % 2) ? 20000 : -20000);
      pcm[pair * 2] = pcm[pair * 2 + 1] = pcm[pair * 2 + 2] = pcm[pair * 2 + 3] = v;
    }
  return pcm;
}

std::vector<CourseEvent> run(const CourseScript& script, const std::vector<std::int16_t>& pcm, int interval) {
  const std::vector<Block> blocks = make_blocks(pcm);
  CourseGenerator gen(script, interval);
  gen.start_round(1, 0);
  for (std::size_t b = 0; b < blocks.size(); ++b) gen.process(static_cast<std::int32_t>(b * kBlockSize), blocks[b]);
  return gen.events();
}

}  // namespace

TEST(blocks_average_and_truncate) {
  std::vector<std::int16_t> pcm(256 * 2 * 2, 0);
  pcm[0] = 3; pcm[1] = 4;      // (3 + 4) / 2 = 3
  pcm[4] = -3; pcm[5] = -4;    // (-3 - 4) / 2 = -3, truncating toward zero
  pcm[2] = 30000;              // odd pairs are skipped
  const auto blocks = make_blocks(pcm);
  CHECK(blocks.size() == 2);
  CHECK(blocks[0][0] == 3 && blocks[0][1] == -3 && blocks[0][2] == 0);
}

TEST(random_generator_constants) {
  CHECK(next_random(0) == 1013904223u);
  CHECK(next_random(1) == 1015568748u);
}

TEST(course_places_one_part_per_burst) {
  const CourseScript script = tiny_script();
  const int spacing = 26460;  // 1.2 s: more than twice the half-second part length, so every burst qualifies
  const auto events = run(script, bursts(13, spacing), 1);
  std::vector<CourseEvent> parts;
  for (const auto& e : events)
    if (e.type == CourseEvent::Type::Part) parts.push_back(e);
  CHECK(events.front().type == CourseEvent::Type::Camera && events.front().value == 23);
  CHECK(parts.size() >= 8 && parts.size() <= 10);
  for (std::size_t i = 0; i < parts.size(); ++i) {
    CHECK(parts[i].value == int(i % 2));  // Block, Pit, Block, ...
    CHECK(parts[i].length == 11025 && parts[i].scale == 65536);
    CHECK(parts[i].time % spacing < 1200);  // inside the burst that caused it
    CHECK(parts[i].time / spacing == int(i) + 1);  // one per burst, none skipped
  }
}

TEST(course_skips_onsets_closer_than_two_part_lengths) {
  // Bursts exactly two part-lengths apart are not "more than" two apart: every other one is dropped.
  const auto events = run(tiny_script(), bursts(12, kSampleRate), 1);
  int parts = 0, last = -100000;
  for (const auto& e : events)
    if (e.type == CourseEvent::Type::Part) {
      CHECK(e.time - last > 2 * 11025);
      last = e.time;
      ++parts;
    }
  CHECK(parts == 5);
}

TEST(course_is_repeatable) {
  const CourseScript script = tiny_script();
  const auto pcm = bursts(8, 15000);
  const auto a = run(script, pcm, 1), b = run(script, pcm, 1);
  CHECK(a.size() == b.size() && !a.empty());
  for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) CHECK(a[i].time == b[i].time && a[i].value == b[i].value);
}

TEST(silence_gives_no_parts) {
  const auto events = run(tiny_script(), std::vector<std::int16_t>(44100 * 2 * 5, 0), 1);
  for (const auto& e : events) CHECK(e.type != CourseEvent::Type::Part);
}

TEST(lead_moves_the_music_against_the_round_start) {
  std::vector<std::int16_t> pcm(256 * 2 * 3, 0);
  pcm[0] = pcm[1] = 100;  // the first 44.1 kHz sample
  pcm[6] = pcm[7] = 300;  // the fourth
  CHECK(make_blocks(pcm, 0)[0][0] == 100);
  CHECK(make_blocks(pcm, 3)[0][0] == 300);   // three samples skipped, the other decimation phase
  const auto late = make_blocks(pcm, -2);    // one analysis sample of silence first
  CHECK(late[0][0] == 0 && late[0][1] == 100);
  CHECK(make_blocks(pcm, -2).size() == 3 && make_blocks(pcm, 3).size() == 2);
}

TEST(original_style_timing_is_seeded_and_in_range) {
  const CourseTiming a = CourseTiming::like_original(7), b = CourseTiming::like_original(7);
  CHECK(a.lead == b.lead && a.frame_phase_q16 == b.frame_phase_q16);
  bool differs = false;
  for (std::uint32_t seed = 0; seed < 200; ++seed) {
    const CourseTiming t = CourseTiming::like_original(seed);
    CHECK(t.lead >= -240 && t.lead <= 500);
    CHECK(t.frame_period_q16 == CourseGenerator::kOriginalFramePeriodQ16);
    CHECK(t.frame_phase_q16 >= 0 && t.frame_phase_q16 < t.frame_period_q16);
    differs = differs || t.lead != a.lead;
  }
  CHECK(differs);
  const CourseTiming c = CourseTiming::consistent();
  CHECK(c.lead == 0 && c.frame_period_q16 == 0);
}

TEST(random_picks_take_every_number_before_any_twice) {
  for (std::uint32_t seed : {0u, 1u, 0xCBF633B1u, 123456789u}) {
    std::vector<int> p = random_picks(0, 9, 10, seed);
    CHECK(p.size() == 10);
    std::vector<int> sorted = p;
    std::sort(sorted.begin(), sorted.end());
    for (int i = 0; i < 10; ++i) CHECK(sorted[std::size_t(i)] == i);
    // More picks than numbers: each round of ten is again all ten.
    p = random_picks(5, 9, 12, seed);
    CHECK(p.size() == 12);
    std::vector<int> first(p.begin(), p.begin() + 5), second(p.begin() + 5, p.begin() + 10);
    std::sort(first.begin(), first.end());
    std::sort(second.begin(), second.end());
    CHECK(first == std::vector<int>({5, 6, 7, 8, 9}) && second == first);
    for (int v : p) CHECK(v >= 5 && v <= 9);
    CHECK(random_picks(0, 9, 10, seed) == random_picks(0, 9, 10, seed));  // the same seed, the same picks
  }
  CHECK(random_picks(0, 9, 10, 1) != random_picks(0, 9, 10, 2));
}

TEST(disc_key_from_the_table_of_contents) {
  CHECK(disc_key(0, 0) == 0xCBF633B1u);
  CHECK(disc_key(1, 3) == ((128u | 3u) ^ 0xCBF633B1u));
}

TEST(the_effects_follow_the_musics_loudness) {
  // 1,000 blocks: silence, then 400 loud ones (every sample 8,000), then silence again.
  std::vector<Block> blocks(1000);
  for (std::size_t i = 300; i < 700; ++i) blocks[i].fill(8000);
  const auto level = loudness_levels(blocks);
  CHECK(level.size() == blocks.size());
  // A loud block: 128 x 8,000 = 1,024,000, >> 14 = 62. Block 500's window is blocks 301
  // to 700: 399 loud and one silent, 24,738 / 400 = 61, x 5/4 = 76.
  CHECK(level[500] == 76);
  CHECK(level[150] == 0);            // (the first 200 have none: not yet 400 blocks)
  CHECK(level[250] > 0 && level[250] < 76);  // (coming up: the window reaches the loud part)
  CHECK(level[950] == 0);            // (silence again, far enough on)
  // Centred: rising before the loud part as much as it falls after it.
  CHECK(level[300] == level[699] || std::abs(int(level[300]) - int(level[699])) <= 1);
  // Never over 100.
  for (auto& b : blocks) b.fill(32000);
  for (const auto v : loudness_levels(blocks)) CHECK(v <= 100);
  // The volume: 10% in silence, all of it at 100, in between in proportion.
  CHECK(loud_volume(127, 0) == 12 && loud_volume(127, 100) == 127 && loud_volume(100, 50) == 55);
}
