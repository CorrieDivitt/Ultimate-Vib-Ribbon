// The rules of play on hand-made courses. No game data needed.
#include <cmath>

#include "test.h"
#include "vib/course_wrap.h"
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

TEST(the_scroll_speed_moves_only_where_parts_are_drawn) {
  // The same course and presses at 1x, 2x and 0.5x: the same hits, points and events;
  // each part drawn at its distance from Vibri times the scroll; 1x is the original's.
  Run one, two, half, ten;
  ten.g.set_scroll(10.0);
  two.g.set_scroll(2.0);
  half.g.set_scroll(0.5);
  for (Run* r : {&one, &two, &half, &ten}) {
    r->g.add_event(part(44100, 0));
    r->g.add_event(part(66150, 3));
    r->g.add_event(part(88200, 1));
  }
  // Part way: compare where each would be drawn, then play on.
  for (Run* r : {&one, &two, &half, &ten}) r->run_to(22050);
  const auto a = one.g.road(true), b = two.g.road(true), c = half.g.road(true);
  CHECK(a.size() == 3 && b.size() == 3 && c.size() == 3);
  const Fixed vibri = Fixed::from(1.25);
  for (std::size_t i = 0; i < 3 && i < b.size() && i < c.size(); ++i) {
    const double d = (a[i].position - vibri).to_double();
    CHECK(std::abs((b[i].position - vibri).to_double() - 2 * d) < 1e-3);
    CHECK(std::abs((c[i].position - vibri).to_double() - 0.5 * d) < 1e-3);
  }
  // The original's horizon is on the drawn distance: a slow scroll shows more of what
  // play already knows, a fast one less.
  CHECK(half.g.road().size() >= one.g.road().size() && two.g.road().size() <= one.g.road().size());
  CHECK(ten.g.road(true).size() == 3 && std::abs((ten.g.road(true)[2].position - vibri).to_double() - 10 * (a[2].position - vibri).to_double()) < 1e-2);
  for (Run* r : {&one, &two, &half, &ten}) {
    r->press_at(44100, kPressBlock);
    r->press_at(66150, kPressWave);
    r->press_at(88200, kPressPit);
    r->run_to(44100 * 4);
  }
  CHECK(one.hits == 3 && two.hits == 3 && half.hits == 3 && ten.hits == 3 && ten.g.points() == one.g.points());
  CHECK(one.g.points() == two.g.points() && one.g.points() == half.g.points());
  CHECK(one.g.level() == two.g.level() && one.g.combo() == half.g.combo());
  // A preview is scaled the same way.
  Run p;
  p.g.set_scroll(2.0);
  p.run_to(44100);
  RoadPiece ahead;
  CHECK(p.g.preview(part(44100 * 3, 2), p.now, ahead));
  Run q;
  q.run_to(44100);
  RoadPiece plain;
  CHECK(q.g.preview(part(44100 * 3, 2), q.now, plain));
  CHECK(std::abs((ahead.position - vibri).to_double() - 2 * (plain.position - vibri).to_double()) < 1e-3);
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

TEST(slow_passed_parts_speed_up_to_the_least_trail_speed) {
  // On the far road a passed part slower than the least speed (here 2 lengths a
  // second, the normal part's: 11,025 samples a length) speeds up to it over a second
  // from Vibri's passing, instead of seeming to stay with her. Faster parts, and the
  // setting off, move as in the original.
  Run r;
  r.g.keep_passed(22050 * 60);
  r.g.add_event(part(22050 * 4, 0, kLen * 4));  // a quarter of the normal speed: 0.5 a second
  r.g.add_event(part(22050 * 6, 1));            // normal: 2 a second
  r.run_to(22050 * 12);
  auto place = [&](int road) {
    for (const RoadPiece& p : r.g.road(true))
      if (p.road == road) return (p.position - Fixed::from(1.25)).to_double();
    return 1e9;
  };
  const double slow_original = place(0), fast_original = place(1);
  const auto monitor_before = r.g.road(false);
  // Off: the original's own speeds (8 s after passing at 0.5 a second: 4 lengths back).
  CHECK(std::abs(slow_original + 4.0) < 0.1 && std::abs(fast_original + 12.0) < 0.1);
  r.g.set_trail_min_speed(2.0);
  // On: a second's ramp from 0.5 to 2 (1.25 lengths), then 7 s at 2 (14): 15.25 back.
  CHECK(std::abs(place(0) + 15.25) < 0.1);
  CHECK(place(1) == fast_original);  // already at the least speed: untouched
  // Continuous where Vibri passes it: a moment after, still right beside her.
  Part slow;
  slow.time = 22050 * 4;
  slow.rate = Fixed::whole(1).over((kLen * 4) >> 6);
  CHECK(std::abs(r.g.far_behind(slow, slow.time + 64).to_double()) < 0.01);
  // The monitor's road (not far) is the original's whatever the setting.
  const auto monitor_after = r.g.road(false);
  CHECK(monitor_after.size() == monitor_before.size());
  for (std::size_t i = 0; i < monitor_after.size(); ++i) CHECK(monitor_after[i].position == monitor_before[i].position);
}

TEST(the_course_wraps_round_the_player_and_fades_behind_them) {
  CourseWrap w;
  w.radius = 3000.0f;
  const float vz = 5000.0f;
  // Vibri (s = 0, on the road x = 0) stands on the ring straight ahead at first...
  Vec3f v = w.bend({0, 0, vz}, vz);
  CHECK(std::abs(v.x) < 1e-3f && std::abs(v.z - 3000.0f) < 1e-3f);
  // ...and every point of the road is the ring's radius from the middle, whatever its angle.
  for (float s : {-8000.0f, -1000.0f, 0.0f, 2500.0f, 9000.0f}) {
    const Vec3f p = w.bend({0, -100, vz + s}, vz);
    CHECK(std::abs(std::sqrt(p.x * p.x + p.z * p.z) - 3000.0f) < 0.5f && p.y == -100.0f);
  }
  // +x (towards the default view's side) is towards the middle.
  CHECK(std::sqrt(w.bend({200, 0, vz}, vz).x * w.bend({200, 0, vz}, vz).x + w.bend({200, 0, vz}, vz).z * w.bend({200, 0, vz}, vz).z) < 3000.0f);
  // Walking: a quarter of the ring passed puts her a quarter of the way round.
  w.advance(3.14159265 * 3000.0 / 2.0);
  v = w.bend({0, 0, vz}, vz);
  CHECK(std::abs(v.x - 3000.0f) < 1.0f && std::abs(v.z) < 1.0f);
  // The HUD carried whole: at her own place, the same as the bent road there.
  std::vector<SceneLine> hud(1);
  hud[0].a = {0, -1400, vz}, hud[0].b = {150, -1400, vz + 300};
  w.place(hud, vz);
  const Vec3f bent = w.bend({0, -1400, vz}, vz);
  CHECK(std::abs(hud[0].a.x - bent.x) < 1e-2f && std::abs(hud[0].a.z - bent.z) < 1e-2f);
  // Not mirrored: seen from the middle (looking along +z after a recentre), the road
  // ahead of her runs to the right, as play's +z does in the view beside her.
  CourseWrap w0;
  w0.radius = 3000.0f;
  CHECK(w0.bend({0, 0, vz + 300}, vz).x > 0);
  // Lines are split so they curve, fade towards the point opposite her, and are gone there.
  std::vector<SceneLine> road(1);
  road[0].a = {0, 0, vz - 9000}, road[0].b = {0, 0, vz + 9000};
  road[0].colour = {255, 255, 255};
  w0.bend_lines(road, vz);
  CHECK(road.size() > 50);
  float dimmest = 255, brightest = 0;
  for (const SceneLine& l : road) {
    dimmest = std::min(dimmest, float(l.colour.r));
    brightest = std::max(brightest, float(l.colour.r));
    CHECK(std::abs(l.a.z - (-3000.0f)) > 10.0f || std::abs(l.a.x) > 10.0f);  // nothing at the point behind the player
  }
  CHECK(brightest == 255.0f && dimmest < 60.0f);
  // Speed: eased, and kept while no part is ahead.
  CourseWrap w1;
  w1.follow(1000.0, 0.1);
  CHECK(w1.rate == 1000.0);
  w1.follow(2000.0, 0.5);
  CHECK(w1.rate > 1500.0 && w1.rate < 2000.0);
  w1.follow(0.0, 1.0);
  CHECK(w1.rate > 1500.0);
}

TEST(the_road_under_vibri_moves_at_the_next_parts_speed) {
  Run r;
  CHECK(r.g.ground_rate() == 0.0);
  r.g.add_event(part(44100, 0));
  r.step();
  CHECK(r.g.ground_rate() > 0.0);
  // One part length per its own length in samples.
  CHECK(std::abs(r.g.ground_rate() - double(r.g.parts()[0].rate.to_double()) / 64.0) < 1e-12);
}

TEST(vibris_walk_follows_the_parts_nearest_her) {
  // Two parts at different speeds (one twice as long a part, so half as fast). Near the
  // first the speed is the first's; near the second, the second's; between, a blend.
  Run r;
  r.g.add_event(part(44100, 0, kLen));
  r.g.add_event(part(44100 + 5 * kLen, 1, kLen * 2));
  r.step();
  const double fast = r.g.parts()[0].rate.to_double() / 64.0, slow = r.g.parts()[1].rate.to_double() / 64.0;
  CHECK(fast > slow * 1.9);
  r.run_to(44100 - kLen / 20);  // the first part almost at Vibri, the second far
  CHECK(std::abs(r.g.ground_rate_near(1.0) - fast) < 0.02 * fast);
  double last = r.g.ground_rate_near(1.0);
  bool smooth = true;
  for (int i = 0; i < 400 && r.g.parts().size() > 1; ++i) {  // as the first goes by: no jumps
    r.step();
    const double now = r.g.ground_rate_near(1.0);
    if (now > 0 && last > 0) smooth = smooth && std::abs(now - last) < 0.2 * fast;
    if (now > 0) last = now;
  }
  CHECK(smooth);
}
