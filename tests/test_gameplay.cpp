// The rules of play on hand-made courses. No game data needed.
#include <cmath>

#include "test.h"
#include "vib/gameplay.h"
#include "vib/scene.h"
#include "vib/title.h"

using namespace vib;

namespace {

constexpr int kLen = 11025;  // half-second parts
constexpr int kStep = 441;   // 50 updates per second

CourseEvent part(int time, int road, int length = kLen) {
  CourseEvent e;
  e.type = CourseEvent::Type::Part;
  e.time = time;
  e.value = road;
  e.length = length;
  e.scale = 65536;
  return e;
}

struct Run {
  Gameplay g;
  std::int32_t now = 0;
  int hits = 0, misses = 0, strays = 0;
  Outcome outcome = Outcome::Playing;
  explicit Run(Course c = Course::Bronze) {
    RoundSetup s;
    s.course = c;
    g.start(s);
  }
  void step(std::uint8_t pressed = 0) {
    outcome = g.update(now, pressed);
    for (const GameEvent& e : g.events()) {
      hits += e.type == GameEvent::Type::Hit;
      misses += e.type == GameEvent::Type::Miss;
      strays += e.type == GameEvent::Type::Stray;
    }
    now += kStep;
  }
  void run_to(std::int32_t t) {
    while (now < t) step();
  }
  // Runs up to `t` (a multiple of the step) and presses exactly there.
  void press_at(std::int32_t t, std::uint8_t pressed) {
    run_to(t);
    step(pressed);
  }
};

}  // namespace

TEST(fixed_point_matches_the_original_rounding) {
  CHECK(Fixed::from(0.2).raw() == 13107);
  CHECK(Fixed::whole(1).over(kLen >> 6).raw() == 65536 / 172);
  CHECK((Fixed::whole(3) * Fixed::from(0.5)).raw() == 98304);
  CHECK((Fixed::whole(1) / Fixed::whole(2)).raw() == 32768);
  CHECK(road_presses(0) == kPressBlock && road_presses(3) == kPressWave);
  CHECK(road_presses(4) == (kPressBlock | kPressPit) && road_presses(9) == (kPressLoop | kPressWave));
}

TEST(press_on_time_hits_and_scores) {
  Run r;
  r.g.add_event(part(44100, 0));
  r.press_at(44100, kPressBlock);
  CHECK(r.hits == 1 && r.misses == 0);
  CHECK(r.g.points() == 1 && r.g.chain() == 1 && r.g.combo() == 1);
}

TEST(hit_window_is_a_fifth_of_the_part_either_side) {
  // 0.2 of 11,025 samples is 2,205: inside at 1,764 early or late, outside at 2,646.
  for (int offset : {-1764, 1764}) {
    Run r;
    r.g.add_event(part(44100, 1));
    r.press_at(44100 + offset, kPressPit);
    CHECK(r.hits == 1);
  }
  Run early;
  early.g.add_event(part(44100, 1));
  early.press_at(44100 - 2646, kPressPit);
  CHECK(early.hits == 0 && early.strays == 1 && early.misses == 0);
  early.press_at(44100, kPressPit);  // the part can still be hit, and the matching stray added 1
  CHECK(early.hits == 1 && early.g.points() == 2);
  Run late;
  late.g.add_event(part(44100, 1));
  late.run_to(44100 + 2646);
  CHECK(late.misses == 1);
}

TEST(wrong_button_misses_at_once) {
  Run r;
  r.g.add_event(part(44100, 2));
  r.press_at(44100, kPressWave);
  CHECK(r.misses == 1 && r.hits == 0 && r.g.points() == 0);
}

TEST(pair_needs_both_buttons_and_scores_three) {
  Run r;
  r.g.add_event(part(44100, 5));  // block + loop
  r.press_at(44100 - 441, kPressBlock);
  CHECK(r.hits == 0 && r.misses == 0);
  r.step(kPressLoop);
  CHECK(r.hits == 1 && r.g.points() == 3);
}

TEST(strays_score_by_how_they_differ) {
  Run r;
  r.g.add_event(part(88200, 0));
  r.press_at(22050, kPressLoop);   // different road, single part coming: 2
  r.press_at(33075, kPressLoop);   // repeat of an earlier stray: 1
  r.press_at(44100, kPressWave);   // a third is not counted
  r.press_at(88200, kPressBlock);
  CHECK(r.strays == 3 && r.hits == 1 && r.g.points() == 1 + 3);
  Run p;
  p.g.add_event(part(88200, 4));   // block + pit
  p.press_at(22050, kPressWave);   // different road, pair coming: 4
  p.press_at(88200, kPressBlock | kPressPit);
  CHECK(p.g.points() == 3 + 4);
}

TEST(chain_grows_to_ten_and_eighteen_hits_change_form) {
  Run r;
  for (int i = 0; i < 20; ++i) r.g.add_event(part(44100 + i * 22050, 0));
  int expected = 0;
  for (int i = 0; i < 20; ++i) {
    r.press_at(44100 + i * 22050, kPressBlock);
    const bool best = i >= 17;  // the 18th hit lifts Vibri to the best form, which scores double
    expected += (1 + std::min(i, 10)) * (best ? 2 : 1);
    CHECK(r.g.points() == expected);
  }
  CHECK(r.hits == 20 && r.g.form() == 0 && r.g.level() == 0);
  CHECK(r.g.bonus() == expected * 30 / 100);
}

TEST(a_miss_in_the_best_form_drops_to_rabbit) {
  Run r;
  for (int i = 0; i < 19; ++i) r.g.add_event(part(44100 + i * 22050, 0));
  for (int i = 0; i < 18; ++i) r.press_at(44100 + i * 22050, kPressBlock);
  CHECK(r.g.form() == 0);
  r.run_to(44100 + 19 * 22050);
  CHECK(r.misses == 1 && r.g.level() == 3 && r.g.chain() == 0);
}

TEST(every_third_miss_is_a_level_and_level_eleven_ends_the_game) {
  Run r;
  for (int i = 0; i < 27; ++i) r.g.add_event(part(44100 + i * 22050, 3));
  r.run_to(44100 + 2 * 22050 + 11025);
  CHECK(r.misses == 3 && r.g.level() == 4);
  r.run_to(44100 + 23 * 22050 + 11025);
  CHECK(r.misses == 24 && r.g.level() == 11 && r.g.form() == 3);
  while (r.outcome == Outcome::Playing && r.now < 44100 + 40 * 22050) r.step();
  CHECK(r.misses == 27 && r.outcome == Outcome::GameOver);
}

TEST(round_clears_after_the_end_once_parts_are_gone) {
  Run r;
  r.g.add_event(part(44100, 0));
  r.g.end_at(66150);
  r.g.add_event(part(88200, 0));  // after the end: dropped
  r.press_at(44100, kPressBlock);
  while (r.outcome == Outcome::Playing && r.now < 200000) r.step();
  CHECK(r.outcome == Outcome::Clear && r.now >= 66150 && r.now < 70000 && r.hits == 1 && r.misses == 0);
}

TEST(auto_play_hits_everything_and_demo_misses_now_and_then) {
  Run a(Course::InOrder);
  for (int i = 0; i < 30; ++i) a.g.add_event(part(44100 + i * 22050, i % 10));
  a.run_to(44100 + 31 * 22050);
  CHECK(a.hits == 30 && a.misses == 0);

  Gameplay g;
  g.seed_demo(12345);
  RoundSetup s;
  s.demo = true;
  g.start(s);
  for (int i = 0; i < 60; ++i) g.add_event(part(44100 + i * 22050, 0));
  int hits = 0, misses = 0;
  for (std::int32_t t = 0; t < 44100 + 61 * 22050; t += kStep) {
    g.update(t, kPressWave);  // the player's buttons are ignored in the demo
    for (const GameEvent& e : g.events()) {
      hits += e.type == GameEvent::Type::Hit;
      misses += e.type == GameEvent::Type::Miss;
    }
  }
  CHECK(hits + misses == 60 && misses >= 3 && misses <= 12);
}

TEST(speed_course_speeds_up_with_points_and_clears_at_the_top) {
  Run r(Course::Speed1);
  CHECK(r.g.speed() == Fixed::from(1.5f));
  int t = 44100, n = 0;
  while (r.outcome == Outcome::Playing && n < 400) {
    r.g.add_event(part(t, 0));
    // In a speed course all parts move at the current speed, so press when the part arrives.
    while (r.now < t && r.outcome == Outcome::Playing) r.step();
    if (r.outcome != Outcome::Playing) break;
    r.step(kPressBlock);
    t += 22050;
    ++n;
  }
  CHECK(r.outcome == Outcome::Clear);
  CHECK(n >= 210 && n <= 230);  // 210 points bring the top speed, one point per single part
  CHECK(r.g.speed() >= Fixed::from(3.5f));
  CHECK(r.g.fixed_period() == int((std::int64_t(22050) << 16) / Fixed::from(3.5f).raw()));
}

TEST(score_marks_count_rows_of_shapes) {
  // The score is the index of its row of marks among all rows of 7 marks whose
  // shapes (0-14) never rise along the row.
  CHECK((Hud::marks(0) == std::array<int, 7>{0, 0, 0, 0, 0, 0, 0}));
  CHECK((Hud::marks(1) == std::array<int, 7>{1, 0, 0, 0, 0, 0, 0}));
  CHECK((Hud::marks(8) == std::array<int, 7>{2, 0, 0, 0, 0, 0, 0}));
  CHECK((Hud::marks(116279) == std::array<int, 7>{14, 14, 14, 14, 14, 14, 14}));
  CHECK((Hud::marks(500000) == Hud::marks(116279)));
  int last = -1;
  for (int score = 0; score < 3000; ++score) {  // every row valid, and each one different
    const auto m = Hud::marks(score);
    for (std::size_t i = 1; i < m.size(); ++i) CHECK(m[i] <= m[i - 1]);
    int key = 0;
    for (int v : m) key = key * 16 + v;
    CHECK(key != last);
    last = key;
  }
}

TEST(score_said_in_japanese) {
  using V = std::vector<int>;
  auto clips = [](int n) { return TitleScreens::say_number(n, 0).clips; };
  CHECK(clips(0).empty());
  CHECK((clips(3) == V{67}));                 // san
  CHECK((clips(10) == V{89, 91}));            // juu, said last, then a rest
  CHECK((clips(12) == V{81, 66}));            // juu-ni
  CHECK((clips(300) == V{67, 83}));           // san-byaku
  CHECK((clips(600) == V{71, 78, 85}));       // rop-pyaku
  CHECK((clips(1000) == V{86}));              // sen
  CHECK((clips(3000) == V{67, 87}));          // san-zen
  CHECK((clips(10000) == V{64, 65, 88}));     // ichi-man
  const auto line = TitleScreens::say_number(1234, 0);
  // 11 beats of words in a 14-beat line: three rests, then the six clips.
  CHECK(line.tempo == 1 && line.timeline.size() == 9 && line.timeline.front() == 91 && line.timeline[3] == 86);
}

TEST(smooth_animation_blends_between_poses) {
  // Stepped, like the original: always one whole pose.
  Clip c(4, 20, false);
  c.rewind();
  c.advance(0.025);  // half a pose on
  CHECK(c.pose().a == 1 && c.pose().b == 1 && c.pose().t == 0.0f);
  Clip::set_smooth(true);
  // Smooth: half way from pose 0 to pose 1, and exactly pose 0 just after a rewind.
  Clip::Pose p = c.pose();
  CHECK(p.a == 0 && p.b == 1 && std::abs(p.t - 0.5f) < 1e-4f);
  c.rewind();
  CHECK(c.pose().a == 0 && c.pose().t == 0.0f);
  // A clip that does not loop holds its last pose; one that loops blends back to the first.
  c.advance(10);
  CHECK(c.pose().a == 3 && c.pose().t == 0.0f);
  Clip l(4, 20, true);
  l.rewind();
  l.advance(0.175);  // 3.5 poses on
  p = l.pose();
  CHECK(p.a == 3 && p.b == 0 && std::abs(p.t - 0.5f) < 1e-4f);
  Clip::set_smooth(false);
}

TEST(far_road_keeps_passed_parts_for_drawing_only) {
  // Two parts; once both have gone by, the original's road has neither, the far one
  // still shows them (behind Vibri) for as long as it is asked to, and play is the same.
  Run plain, kept;
  kept.g.keep_passed(8 * 22050);
  for (Run* r : {&plain, &kept}) {
    r->g.add_event(part(44100, 0));
    r->g.add_event(part(55125, 1));
    r->press_at(44100, kPressBlock);
    r->press_at(55125, kPressPit);
    r->run_to(44100 * 4);
  }
  CHECK(plain.hits == 2 && kept.hits == 2 && plain.g.points() == kept.g.points());
  CHECK(plain.g.road().empty() && kept.g.road().empty());
  const auto far = kept.g.road(true);
  CHECK(far.size() == 2);
  CHECK(far.size() == 2 && far[0].position < Fixed() && far[0].position < far[1].position);
  CHECK(plain.g.road(true).empty());
  kept.run_to(44100 * 4 + 8 * 22050 + 22050);
  CHECK(kept.g.road(true).empty());  // gone once 8 s behind
}

TEST(a_part_built_further_ahead_is_drawn_where_play_will_put_it) {
  Run r;
  r.run_to(44100);
  const CourseEvent e = part(44100 * 10, 2);
  RoadPiece ahead;
  CHECK(r.g.preview(e, r.now, ahead));
  r.g.add_event(e);
  r.step();  // play places it at the same position (the step's start)
  const auto road = r.g.road(true);
  CHECK(road.size() == 1 && road[0].road == 2 && road[0].position.raw() == ahead.position.raw());
  CourseEvent camera;
  camera.type = CourseEvent::Type::Camera;
  CHECK(!r.g.preview(camera, r.now, ahead));
}

TEST(a_joint_stays_joined_when_jittered_afresh) {
  // Two pieces, the first jittering hard (a missed piece), joined as the road is.
  TmdObject piece;
  piece.vertices = {{0, 0, 0}, {0, -100, 300}, {0, 0, 600}};
  piece.lines = {TmdLine{0, 1, {255, 255, 255}, {255, 255, 255}}, TmdLine{1, 2, {255, 255, 255}, {255, 255, 255}}};
  LineBuilder b;
  b.seed(1234);
  std::vector<SceneLine> out;
  Vec3f join{0, 0, -600};
  b.object(piece, {}, 300, 0, &join, out);  // hard
  b.object(piece, {}, 5, 600, &join, out);  // calm
  // The second piece's join line starts at the first piece's last vertex: the same
  // vertex number, and the same place once each end's own jitter is taken off.
  const SceneLine* last_of_first = nullptr;
  const SceneLine* joint = nullptr;
  for (const SceneLine& l : out) {
    if (l.vertex_b == 3) last_of_first = &l;  // vertices 1-3 are the first piece's
    if (l.vertex_a == 3 && l.vertex_b == 4) joint = &l;
  }
  CHECK(last_of_first && joint);
  if (last_of_first && joint) {
    const Vec3f p{last_of_first->b.x - last_of_first->jitter_b.x, last_of_first->b.y - last_of_first->jitter_b.y,
                  last_of_first->b.z - last_of_first->jitter_b.z};
    const Vec3f q{joint->a.x - joint->jitter_a.x, joint->a.y - joint->jitter_a.y, joint->a.z - joint->jitter_a.z};
    CHECK(std::abs(p.x - q.x) < 0.01f && std::abs(p.y - q.y) < 0.01f && std::abs(p.z - q.z) < 0.01f);
    CHECK(joint->shake_a == last_of_first->shake_b && joint->shake_a > 200.0f);
  }
}

TEST(the_road_as_it_was_two_seconds_ago_for_a_pause_run_up) {
  // A pause's run-up draws the road at earlier places while the rules stand still.
  Run r;
  r.g.add_event(part(44100, 0));
  r.g.add_event(part(66150, 1));
  r.g.add_event(part(88200, 2));
  r.press_at(44100, kPressBlock);
  r.run_to(55125);
  const auto then = r.g.road();
  const std::int32_t at = r.now - kStep;  // the last update's position
  r.run_to(55125 + 2 * 22050);
  const auto again = r.g.road_at(at);
  CHECK(then.size() == again.size());
  for (std::size_t i = 0; i < then.size() && i < again.size(); ++i)
    CHECK(then[i].road == again[i].road && then[i].position.raw() == again[i].position.raw());
  // Now (no pause) it is play's own road.
  const auto now = r.g.road(), same = r.g.road_at(r.now - kStep);
  CHECK(now.size() == same.size());
}

TEST(after_a_pause_vibri_starts_clear_of_parts_and_replays_what_was_judged) {
  Run r;
  for (int i = 1; i <= 8; ++i) r.g.add_event(part(22050 * i, i % 4));  // one a second
  r.press_at(22050, road_presses(1));
  r.run_to(22050 * 5 + 11025);
  // Back 2 s from 5.5 s is 3.5 s: half way between parts, clear with a 0.4 s margin.
  CHECK(r.g.clear_before(22050 * 7 / 2, 22050 * 2 / 5, 0) == 22050 * 7 / 2);
  // From 3.1 s the part at 3 s is too near: it moves back to 2.6 s.
  CHECK(r.g.clear_before(22050 * 31 / 10, 22050 * 2 / 5, 0) == 22050 * 3 - 22050 * 2 / 5);
  // Never before the earliest allowed.
  CHECK(r.g.clear_before(22050 * 31 / 10, 22050 * 2 / 5, 22050 * 3) == 22050 * 3);
  const auto done = r.g.judged(0, 22050 * 5 + 11025);
  CHECK(done.size() == 5);  // 1 to 5 s, every one judged (the first hit, the rest missed)
  CHECK(!done.empty() && !done[0].missed && done.back().missed && done[0].time < done.back().time);
}

TEST(the_trail_behind_vibri_keeps_its_order_with_mixed_speeds) {
  // A slow part (four times the length: a quarter of the speed), then two at the
  // normal speed. Once all three are passed, each moving back at its own speed would
  // leave the slow one just behind Vibri with the fast ones sliding back through it
  // (the owner saw this on the gold course). The far road moves the trail back as one.
  Run r;
  r.g.keep_passed(44100 * 60);
  r.g.add_event(part(44100 * 2, 0, kLen * 4));  // slow
  r.g.add_event(part(44100 * 6, 1));
  r.g.add_event(part(44100 * 8, 2));
  r.run_to(44100 * 12);
  const auto road = r.g.road(true);
  CHECK(road.size() == 3);
  // By position, furthest behind first: the first passed is furthest back.
  CHECK(road[0].road == 0 && road[1].road == 1 && road[2].road == 2);
  for (const RoadPiece& p : road) CHECK(p.position < Fixed::from(1.25));  // all behind her
  // Between the last two (88,200 samples apart, 4 s at 22.05 kHz, at the normal speed: 8 lengths).
  CHECK(std::abs((road[2].position - road[1].position).to_double() - 8.0) < 0.05);
  // The same, drawn for an earlier moment (a pause's run-up), and the trail's own measure.
  const auto earlier = r.g.road_at(44100 * 10, true);
  CHECK(earlier.size() == 3 && earlier[0].road == 0 && earlier[1].road == 1 && earlier[2].road == 2);
  CHECK(std::abs(r.g.trail(44100 * 6, 44100 * 8).to_double() - 8.0) < 0.05);
  // Before the second part the trail moves at the slow one's speed: a quarter.
  CHECK(std::abs(r.g.trail(44100 * 2, 44100 * 4).to_double() - 2.0) < 0.05);
  // The monitor's road (not far) is unchanged: passed parts are not in it.
  CHECK(r.g.road(false).empty());
}
