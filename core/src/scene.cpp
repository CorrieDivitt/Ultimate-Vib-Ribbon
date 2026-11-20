#include "vib/scene.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

#include "vib/bytes.h"
#include "vib/course_gen.h"

namespace vib {

namespace {

constexpr double kTau = 6.28318530717958647692;

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

Vec3f add(Vec3f a, Vec3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3f sub(Vec3f a, Vec3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3f cross(Vec3f a, Vec3f b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(Vec3f a, Vec3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3f norm(Vec3f a) {
  const float l = std::sqrt(dot(a, a));
  return l > 0 ? Vec3f{a.x / l, a.y / l, a.z / l} : Vec3f{0, 0, 1};
}

// The original's rule for objects with a facing marker: project the marker triangle
// and leave the object out when its screen winding (Y down) is negative.
bool faces_away(const SceneView& v, Vec3f a, Vec3f b, Vec3f c) {
  const Vec3f f = norm(sub(v.target, v.eye));
  const Vec3f s = norm(cross(f, {0, -1, 0}));  // screen right
  const Vec3f u = cross(s, f);                  // screen up
  float x[3], y[3];
  const Vec3f p[3] = {a, b, c};
  for (int i = 0; i < 3; ++i) {
    const Vec3f d = sub(p[i], v.eye);
    const float z = dot(d, f);
    if (z < 1.0f) return false;  // at or behind the camera: leave it drawn
    x[i] = dot(d, s) / z;
    y[i] = -dot(d, u) / z;  // screen Y points down
  }
  return (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]) < 0.0f;
}

Mat3 rotation_z(int units) {
  const double a = units * kTau / 4096.0;
  const float c = float(std::cos(a)), s = float(std::sin(a));
  Mat3 m;
  m.m = {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
  return m;
}

}  // namespace

// ---------------------------------------------------------------- Clip

void Clip::advance(double seconds) {
  // As the original: a clip that does not loop is finished once it cannot move,
  // which includes running at speed 0.
  if (frames_ <= 0 || seconds <= 0) return;
  const double step = rate_ * speed_ * seconds;
  if (loop_) {
    position_ += step;
    position_ = std::fmod(position_, double(frames_));
    if (position_ < 0) position_ += frames_;
    finished_ = false;
    return;
  }
  double next = position_ + step;
  if (next < 0) next = 0;
  else if (next >= frames_) next = std::nextafter(double(frames_), 0.0);
  if (next == position_) {  // could not move: at the end (or the start, going backwards)
    finished_ = true;
    return;
  }
  position_ = next;
  finished_ = false;
}

int Clip::frame() const {
  if (frames_ <= 0) return 0;
  return std::clamp(static_cast<int>(std::floor(position_)), 0, frames_ - 1);
}

Clip::Pose Clip::pose() const {
  Pose p;
  p.a = p.b = frame();
  if (!smooth_ || frames_ <= 1) return p;
  // Pose n is shown exactly at position n + 0.5 (where rewind() puts it), and the
  // clip moves towards the next one in between.
  double at = position_ - 0.5;
  if (loop_) {
    at = std::fmod(at, double(frames_));
    if (at < 0) at += frames_;
    p.a = std::clamp(static_cast<int>(std::floor(at)), 0, frames_ - 1);
    p.b = (p.a + 1) % frames_;
  } else {
    at = std::clamp(at, 0.0, double(frames_ - 1));
    p.a = std::clamp(static_cast<int>(std::floor(at)), 0, frames_ - 1);
    p.b = std::min(p.a + 1, frames_ - 1);
  }
  p.t = static_cast<float>(at - p.a);
  if (p.a == p.b) p.t = 0;
  return p;
}

SceneView camera_at(const CameraAnimation& anim, Clip::Pose pose, bool flip) {
  SceneView v;
  if (anim.keys.empty()) return v;
  const int last = int(anim.keys.size()) - 1;
  const CameraKey& a = anim.keys[std::size_t(std::clamp(pose.a, 0, last))];
  const CameraKey& b = anim.keys[std::size_t(std::clamp(pose.b, 0, last))];
  const float t = pose.t, m = flip ? -1.0f : 1.0f;
  auto mix = [t](int x, int y) { return float(x) + t * float(y - x); };
  v.eye = {m * mix(a.eye.x, b.eye.x), mix(a.eye.y, b.eye.y), mix(a.eye.z, b.eye.z)};
  v.target = {m * mix(a.target.x, b.target.x), mix(a.target.y, b.target.y), mix(a.target.z, b.target.z)};
  v.fov_y = float(mix(a.fov, b.fov) * kTau / 4096.0);
  return v;
}

// ---------------------------------------------------------------- GameCamera

namespace {
enum Group { kBack, kSide, kFront };
struct CameraName {
  int id;
  const char* name;
  int group;
};
const CameraName kCameraNames[] = {
    {1, "B01", kBack}, {2, "B02", kBack}, {3, "S01", kSide}, {4, "S02", kSide}, {5, "F01", kFront}, {6, "F02", kFront}, {7, "F03", kFront},
};
struct TransitionName {
  const char* same;
  const char* switched;
  int from, to;
};
const TransitionName kTransitionNames[] = {
    {"TV_BS", "TA_BS", kBack, kSide},  {"TV_BF", "TA_BF", kBack, kFront}, {"TV_SF", "TA_SF", kSide, kFront},
    {"TV_BB", "TA_BB", kBack, kBack},  {"TV_SS", "TA_SS", kSide, kSide},  {"TV_FF", "TA_FF", kFront, kFront},
};
constexpr int kDefaultLoops = 2;
}  // namespace

GameCamera::GameCamera(const Pak& pak) {
  auto load = [&](const char* name) { return parse_anc(pak.view(std::string("ROAD/") + name + ".ANC")); };
  for (const auto& c : kCameraNames) {
    Camera cam{c.id, c.group, load(c.name), {}};
    cam.clip = Clip(int(cam.anim.keys.size()), cam.anim.rate, false);
    cameras_.push_back(std::move(cam));
  }
  for (const auto& t : kTransitionNames) {
    Transition tr{t.from, t.to, load(t.same), load(t.switched), {}, {}};
    tr.same_clip = Clip(int(tr.same_anim.keys.size()), tr.same_anim.rate, false);
    tr.switch_clip = Clip(int(tr.switch_anim.keys.size()), tr.switch_anim.rate, false);
    transitions_.push_back(std::move(tr));
  }
  reset(false, false);
}

void GameCamera::take_next(int side) {
  next_side_ = side;
  next_camera_ = event_camera_;
  next_loops_ = event_loops_;
}

void GameCamera::start_base() {
  camera_ = next_camera_;
  side_ = next_side_;
  loops_done_ = 0;
  loops_ = next_loops_;
  Camera& c = cameras_[std::size_t(camera_)];
  anim_ = &c.anim;
  clip_ = &c.clip;
  in_transition_ = false;
  flip_ = side_ != 0;
  clip_->rewind();
  // However many loops a camera gets, together they last as long as two.
  clip_->set_speed((fast_ ? 1.3 : 1.0) * next_loops_ / 2.0);
}

void GameCamera::start_transition() {
  const int from = cameras_[std::size_t(camera_)].group, to = cameras_[std::size_t(next_camera_)].group;
  int found = -1;
  bool reverse = false;
  for (std::size_t i = 0; i < transitions_.size(); ++i)
    if (transitions_[i].from == from && transitions_[i].to == to) found = int(i);
  if (found < 0) {
    reverse = true;
    for (std::size_t i = 0; i < transitions_.size(); ++i)
      if (transitions_[i].from == to && transitions_[i].to == from) found = int(i);
  }
  require(found >= 0, "no camera transition between these groups");
  const bool switch_side = side_ != next_side_;
  Transition& t = transitions_[std::size_t(found)];
  anim_ = switch_side ? &t.switch_anim : &t.same_anim;
  clip_ = switch_side ? &t.switch_clip : &t.same_clip;
  in_transition_ = true;
  clip_->rewind(reverse);
  clip_->set_speed(reverse ? -1.0 : 1.0);
  flip_ = (side_ != 0) != (reverse && switch_side);
}

void GameCamera::reset(bool fast, bool hold) {
  fast_ = fast;
  hold_ = hold;
  waiting_ = true;
  event_camera_ = 2;  // the first side camera, once
  event_loops_ = 1;
  take_next(1);
  start_base();
  cut();
}

void GameCamera::event(int value) {
  int loops = value / 10;
  const int id = value % 10;
  if (loops <= 0) loops = kDefaultLoops;
  int camera = 0;
  for (std::size_t i = 0; i < cameras_.size(); ++i)
    if (cameras_[i].id == id) camera = int(i);
  event_camera_ = camera;
  event_loops_ = loops;
  if (waiting_) {
    waiting_ = false;
    take_next(0);
    start_base();
    take_next(0);
    start_transition();
  }
}

void GameCamera::cut() {
  take_next(fast_ ? 0 : side_ ^ 1);
  start_transition();
}

SceneView GameCamera::update(double seconds, Vec3f origin) {
  SceneView v;
  if (anim_ && !anim_->keys.empty()) {
    v = camera_at(*anim_, clip_->pose(), flip_);
    v.eye = add(origin, v.eye);
    v.target = add(origin, v.target);
  }
  clip_->advance(seconds);
  if (clip_->finished()) {
    if (!in_transition_) {
      if (hold_ || ++loops_done_ < loops_) clip_->rewind();
    } else {
      start_base();
    }
  }
  return v;
}

// ---------------------------------------------------------------- Scene

namespace {
const char* const kFormDirs[Scene::kForms] = {"CHARA/SUPER/", "CHARA/PEELOO/", "CHARA/FROG/", "CHARA/SNAKE/"};
// For each road (and running), the clip and its alternate per form; "" = the same clip again.
const char* const kMoveFiles[Scene::kMoves][Scene::kForms][2] = {
    {{"N01_J", "N01_J_F"}, {"N01_J", "N01_J_F"}, {"N01_J", "N01_J_F"}, {"N01_J", "N01_J_F"}},
    {{"N02_H", ""}, {"N02_H", ""}, {"N02_H", ""}, {"N02_H", "N02_H_F"}},
    {{"N03_L", "N03_L_F"}, {"N03_L", "N03_L_F"}, {"N03_L", "N03_L_F"}, {"N03_L", "N03_L_F"}},
    {{"N04_W", ""}, {"N04_W", ""}, {"N04_W", "N04_W_F"}, {"N04_W", "N04_W_F"}},
    {{"N05_JH", ""}, {"N05_JH", ""}, {"N05_JH", ""}, {"N05_JH", ""}},
    {{"N06_JL", ""}, {"N06_JL", ""}, {"N06_JL", ""}, {"N06_JL", ""}},
    {{"N07_JW", ""}, {"N07_JW", ""}, {"N07_JW", ""}, {"N07_JW", ""}},
    {{"N08_HL", ""}, {"N08_HL", ""}, {"N08_HL", ""}, {"N08_HL", ""}},
    {{"N09_HW", ""}, {"N09_HW", ""}, {"N09_HW", ""}, {"N09_HW", ""}},
    {{"N10_LW", ""}, {"N10_LW", ""}, {"N10_LW", ""}, {"N10_LW", ""}},
    {{"N00_SKIP", ""}, {"N00_SKIP", ""}, {"N00_SKIP", ""}, {"N00_SKIP", ""}},
};
const int kEvoEnds[Scene::kForms] = {58, 23, 19, 0};  // last frame of the evolution clip per form
}  // namespace

Scene::Scene(const Pak& pak, float road_length, float vibri_position)
    : length_(Fixed::from(road_length)), start_(Fixed::from(vibri_position)), camera_(pak) {
  offset_ = -length_.over(2);
  road_ = parse_tmd(pak.view("ROAD/ROAD.TMD"));
  require(road_.objects.size() == 10, "ROAD.TMD should hold one object per road");
  point_ = parse_tmd(pak.view("ROAD/POINT.TMD"));
  evo_model_ = parse_tmd(pak.view("METERS/EVO.TMD"));
  evo_anim_ = parse_anm(pak.view("METERS/EVO.ANM"));
  evo_ = Clip(int(evo_anim_.frames.size()), evo_anim_.rate, false);
  moves_.resize(kForms);
  for (int f = 0; f < kForms; ++f) {
    forms_.push_back(parse_tmd(pak.view(std::string(kFormDirs[f]) + "MODEL.TMD")));
    moves_[std::size_t(f)].resize(kMoves);
    for (int m = 0; m < kMoves; ++m)
      for (int a = 0; a < 2; ++a) {
        const char* file = kMoveFiles[m][f][a];
        Move mv;
        if (*file) {
          mv.anim = parse_anm(pak.view(std::string(kFormDirs[f]) + upper(file) + ".ANM"));
          mv.clip = Clip(int(mv.anim.frames.size()), mv.anim.rate, m == kRunning);
        } else {
          mv = moves_[std::size_t(f)][std::size_t(m)][0];
        }
        moves_[std::size_t(f)][std::size_t(m)].push_back(std::move(mv));
      }
  }
  reset(false, false);
}

void Scene::reset(bool home_course, bool speed_course) {
  alternate_ = false;
  move_ = kRunning;
  form_ = 0;
  current().clip.rewind();
  evo_frame_ = 0;
  evo_.rewind();
  evo_.set_speed(0);
  camera_.reset(home_course, speed_course);
}

void Scene::set_move(int road, bool alternate) {
  alternate_ = alternate;
  move_ = road;
  current().clip.rewind();
}

bool Scene::move_done(double fraction) const {
  if (move_ == kRunning) return true;
  const Clip& c = current().clip;
  return c.position() >= fraction * c.frames();
}

void Scene::show_evo(int frame) {
  evo_frame_ = frame;
  if (evo_.speed() == 0 || evo_.finished()) {
    evo_jitter_ = 10;
    evo_.set_speed(0);
    evo_.seek(frame);
  }
}

void Scene::play_evo() {
  evo_frame_ = 0;
  evo_jitter_ = 5;
  evo_.set_speed(1);
}

std::int32_t LineBuilder::offset(int range) {
  // An offset in [-range, range), from the shared generator, as the original makes it.
  random_ = next_random(random_);
  if (range <= 0) return 0;
  int bits = 0;
  for (std::uint32_t v = std::uint32_t(range) * 2; v; v >>= 1) ++bits;
  const std::uint32_t mask = bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1;
  std::int32_t d = static_cast<std::int32_t>((random_ >> (32 - bits)) & mask) - range;
  if (d >= range) d -= range * 2;
  return d;
}

void LineBuilder::object(const TmdObject& obj, const Xform& xf, int range, std::int32_t z_offset, Vec3f* join,
                         std::vector<SceneLine>& out, const Rgb* colour_override) {
  auto world = [&](Vec3f v) { return add(xf.m.apply(v), xf.t); };
  if (obj.marker == FaceMarker::DrawOnlyWhenFacing) {
    auto vtx = [&](int i) {
      const Vec3s& v = obj.vertices[obj.marker_vertices[i]];
      return world({float(v.x), float(v.y), float(v.z)});
    };
    if (faces_away(view_, vtx(0), vtx(1), vtx(2))) return;
  }
  std::vector<Vec3f> work(obj.vertices.size()), shake(obj.vertices.size());
  // The object's size in the world, for its jitter range there.
  auto length = [](float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); };
  const float scale = (length(xf.m.m[0][0], xf.m.m[1][0], xf.m.m[2][0]) + length(xf.m.m[0][1], xf.m.m[1][1], xf.m.m[2][1]) +
                       length(xf.m.m[0][2], xf.m.m[1][2], xf.m.m[2][2])) / 3.0f;
  const float reach = float(std::max(range, 0)) * scale;
  const std::uint32_t first = next_vertex_;
  next_vertex_ += std::uint32_t(obj.vertices.size());
  for (std::size_t i = 0; i < obj.vertices.size(); ++i) {
    const std::int32_t dx = offset(range), dy = offset(range), dz = offset(range);
    const Vec3s& v = obj.vertices[i];
    work[i] = {float(v.x + dx), float(v.y + dy), float(v.z + z_offset + dz)};
    shake[i] = xf.m.apply({float(dx), float(dy), float(dz)});
  }
  for (const TmdLine& l : obj.lines)
    out.push_back({world(work[l.a]), world(work[l.b]), colour_override ? *colour_override : l.colour_a, shake[l.a], shake[l.b],
                   first + l.a, first + l.b, reach, reach});
  if (join && !work.empty()) {
    // A line from the previous object's last vertex (kept in its own local
    // coordinates, as the original does) to this object's first.
    const Rgb colour = obj.lines.empty() ? Rgb{255, 255, 255} : obj.lines.front().colour_a;
    // Its start is the previous object's last vertex, jitter and all: say so, so a
    // display that jitters afresh moves both ends of the joint together.
    out.push_back({world(*join), world(work.front()), colour, join_vertex_ ? join_jitter_ : Vec3f{}, shake.front(),
                   join_vertex_ ? join_vertex_ : first, first, join_vertex_ ? join_shake_ : 0.0f, reach});
    *join = work.back();
    join_vertex_ = first + std::uint32_t(obj.vertices.size()) - 1;
    join_shake_ = reach;
    join_jitter_ = shake.back();
  }
}

void LineBuilder::animated(const TmdModel& model, const ModelAnimation& anim, Clip::Pose frame, Vec3f origin, int range,
                           std::vector<SceneLine>& out, int swap_first) {
  Look look;
  if (swap_first >= 0) {
    look.swap.resize(model.objects.size());
    for (std::size_t i = 0; i < look.swap.size(); ++i) look.swap[i] = int(i);
    look.swap[0] = swap_first;
  }
  animated(model, anim, frame, origin, range, out, look);
}

void LineBuilder::animated(const TmdModel& model, const ModelAnimation& anim, Clip::Pose frame, Vec3f origin, int range,
                           std::vector<SceneLine>& out, const Look& look) {
  if (anim.frames.empty()) return;
  const int last = int(anim.frames.size()) - 1;
  const std::vector<ModelKey>& next = anim.frames[std::size_t(std::clamp(frame.b, 0, last))];
  for (const ModelKey& k : anim.frames[std::size_t(std::clamp(frame.a, 0, last))]) {
    const std::size_t slot = k.object;
    const std::size_t index = slot < look.swap.size() ? std::size_t(look.swap[slot]) : slot;
    if (index >= model.objects.size()) continue;
    // Between two poses: the same object's key in the next one, if it has one.
    const ModelKey* to = nullptr;
    if (frame.t > 0)
      for (const ModelKey& n : next)
        if (n.object == k.object) {
          to = &n;
          break;
        }
    Xform xf;
    Mat3 rot;
    Vec3f scale{1, 1, 1}, position{};
    if (!to) {
      if (k.has_rotation) rot = rotation_xyz(k.rotation);
      if (k.has_scale) scale = {k.scale.x / 4096.0f, k.scale.y / 4096.0f, k.scale.z / 4096.0f};
      if (k.has_position) position = {float(k.position.x), float(k.position.y), float(k.position.z)};
    } else {
      const float t = frame.t;
      // Each angle turns the short way round (4096 units = a full turn).
      auto turn = [t](int a, int b) {
        int d = (b - a) % 4096;
        if (d > 2048) d -= 4096;
        if (d < -2048) d += 4096;
        return float(a) + t * float(d);
      };
      auto mix = [t](float a, float b) { return a + t * (b - a); };
      const Vec3s r0 = k.has_rotation ? k.rotation : Vec3s{}, r1 = to->has_rotation ? to->rotation : Vec3s{};
      if (k.has_rotation || to->has_rotation)
        rot = rotation_xyz(Vec3f{turn(r0.x, r1.x), turn(r0.y, r1.y), turn(r0.z, r1.z)});
      const Vec3s one{4096, 4096, 4096};
      const Vec3s s0 = k.has_scale ? k.scale : one, s1 = to->has_scale ? to->scale : one;
      scale = {mix(s0.x, s1.x) / 4096.0f, mix(s0.y, s1.y) / 4096.0f, mix(s0.z, s1.z) / 4096.0f};
      const Vec3s p0 = k.has_position ? k.position : Vec3s{}, p1 = to->has_position ? to->position : Vec3s{};
      position = {mix(p0.x, p1.x), mix(p0.y, p1.y), mix(p0.z, p1.z)};
    }
    for (std::size_t r = 0; r < 3; ++r) {
      xf.m.m[r][0] = rot.m[r][0] * scale.x;
      xf.m.m[r][1] = rot.m[r][1] * scale.y;
      xf.m.m[r][2] = rot.m[r][2] * scale.z;
    }
    xf.t = add(origin, position);
    // The look follows the object drawn: it keeps its jitter and colour wherever it is shown.
    const int r = index < look.jitter.size() ? look.jitter[index] : range;
    const Rgb* c = index < look.colour.size() && look.colour[index] ? &*look.colour[index] : nullptr;
    object(model.objects[index], xf, r, 0, nullptr, out, c);
  }
}

void Scene::draw(const Frame& f, std::vector<SceneLine>& out, SceneView& view) {
  out.clear();
  lines_.seed(f.jitter_seed);
  if (f.form != form_) {
    // The move goes on in the new form's version.
    const double at = current().clip.position();
    form_ = std::clamp(f.form, 0, kForms - 1);
    Clip& to = current().clip;
    if (at < to.frames()) to.seek(at);
    else to.rewind();
  }
  const Fixed speed_jitter = Fixed::whole(14).times(f.stage) + Fixed::whole(10);
  const Fixed jitter = f.still ? Fixed() : speed_jitter;

  // The camera follows Vibri's place on the road.
  // Road units to world z (600 each). In 64 bits: the original's 32-bit product
  // wraps beyond about 54 road lengths, which only the headset's long road reaches.
  auto to_z = [](Fixed u) { return static_cast<std::int32_t>((std::int64_t(u.raw()) * 600) >> 16); };
  const auto vibri_z = static_cast<float>(to_z(offset_ + start_));
  vibri_z_ = vibri_z;
  view_ = camera_.update(f.seconds, {0, 0, vibri_z});
  view = view_;
  lines_.set_view(view_);

  // The road: a far point behind, every piece, a far point ahead, all joined up.
  Fixed before = -length_.times(4), after = length_.times(5);
  // (When more of the road is drawn than the original shows, the line runs on past it.)
  if (!f.road.empty()) {
    if (f.road.front().position - length_ < before) before = f.road.front().position - length_;
    if (after < f.road.back().position + length_) after = f.road.back().position + length_;
  }
  Vec3f join{0, 0, float(to_z(before))};
  const int side = camera_.side();
  auto piece = [&](const TmdObject& obj, Fixed position, bool missed, int lean) {
    LineBuilder::Xform xf;
    xf.m = lean ? rotation_z(lean) : Mat3{};
    const int range = (missed ? speed_jitter.times(5) : jitter * Fixed::from(1.5)).integer();
    lines_.object(obj, xf, range, to_z(offset_ + position), &join, out);
  };
  piece(point_.objects.front(), before, false, 0);
  for (const RoadPiece& p : f.road) {
    int lean = 0;
    if (p.leaning) {
      const Fixed tenth = Fixed::from(409.6);
      const Fixed distance = (p.position - start_ - Fixed::from(0.5)).times(512);
      if (Fixed() < distance) {
        const auto scaled = static_cast<std::int32_t>((std::int64_t(distance.raw()) * tenth.raw()) >> 25);
        lean = static_cast<std::int16_t>(scaled / length_.raw()) * 8;
      }
      if (!side) lean = -lean;
    }
    piece(road_.objects[std::size_t(p.road)], p.position, p.missed, lean);
  }
  piece(point_.objects.front(), after, false, 0);

  // Vibri, shaking harder for a while after a miss.
  const Fixed vibri_jitter = Fixed() < f.shake ? jitter + f.shake * jitter.times(8) : jitter;
  Move& mv = current();
  mv.clip.set_speed((f.speed * Fixed::from(0.5)).to_double());
  lines_.animated(forms_[std::size_t(form_)], mv.anim, mv.clip.pose(), {0, 0, vibri_z}, vibri_jitter.integer(), out);
  mv.clip.advance(f.seconds);
  if (mv.clip.finished()) {  // on to running
    alternate_ = false;
    move_ = kRunning;
    current().clip.rewind();
  }

  // The evolution meter rides with her.
  lines_.animated(evo_model_, evo_anim_, evo_.pose(), {0, 0, vibri_z}, evo_jitter_, out);
  evo_.advance(f.seconds);
  if (evo_.speed() > 0 && kEvoEnds[form_] < evo_.position()) {
    evo_.set_speed(0);
    evo_.seek(evo_frame_);
  }
}

// ---------------------------------------------------------------- Hud

namespace {
// For mark i (0 = first) and shape n: how many rows the marks from i on make with
// shapes below n. Combinations with repetition; built rather than tabulated.
std::array<std::array<std::uint32_t, 16>, 7> mark_rows() {
  std::array<std::array<std::uint32_t, 16>, 7> rows{};
  for (int i = 6; i >= 0; --i)
    for (int n = 0; n < 16; ++n) {
      // rows[i][n] = C(n + (7 - i), 7 - i): rows of 7 - i marks, each at most n
      std::uint64_t c = 1;
      const int k = 7 - i;
      for (int j = 1; j <= k; ++j) c = c * std::uint64_t(n + j) / std::uint64_t(j);
      rows[std::size_t(i)][std::size_t(n)] = static_cast<std::uint32_t>(c);
    }
  return rows;
}

SceneView fixed_view(const CameraAnimation& cam) {
  SceneView v;
  if (cam.keys.empty()) return v;
  const CameraKey& k = cam.keys.front();
  v.eye = {float(k.eye.x), float(k.eye.y), float(k.eye.z)};
  v.target = {float(k.target.x), float(k.target.y), float(k.target.z)};
  v.fov_y = float(k.fov * kTau / 4096.0);
  return v;
}
}  // namespace

std::array<int, 7> Hud::marks(int score) {
  static const auto rows = mark_rows();
  std::uint32_t left = std::uint32_t(std::clamp(score, 0, kMaxScore));
  std::array<int, 7> out{};
  for (std::size_t i = 0; i < 7; ++i) {
    int shape = 0;
    while (shape < 16 && left >= rows[i][std::size_t(shape)]) ++shape;
    if (shape) left -= rows[i][std::size_t(shape - 1)];
    out[i] = shape;
  }
  return out;
}

Hud::Hud(const Pak& pak, int language) {
  mark_model_ = parse_tmd(pak.view("FONT/MARK.TMD"));
  score_anim_ = parse_anm(pak.view("METERS/SCORE.ANM"));
  score_ = Clip(int(score_anim_.frames.size()), score_anim_.rate, true);
  time_model_ = parse_tmd(pak.view("METERS/TIME.TMD"));
  time_anim_ = parse_anm(pak.view("METERS/TIME.ANM"));
  time_camera_ = parse_anc(pak.view("METERS/TIME.ANC"));
  time_ = Clip(int(time_anim_.frames.size()), time_anim_.rate, false);
  sign_model_ = parse_tmd(pak.view(localized(pak, "MESSAGE/ROUND.TMD", language)));
  sign_anims_[0] = parse_anm(pak.view("MESSAGE/ROUND01.ANM"));
  sign_anims_[1] = parse_anm(pak.view("MESSAGE/ROUND02.ANM"));
  sign_camera_ = parse_anc(pak.view("MESSAGE/ROUND.ANC"));
}

void Hud::start_round(int index, bool show_sign) {
  sign_index_ = std::clamp(index, 0, 1);
  show_sign_ = show_sign;
  sign_ = Clip(int(sign_anims_[sign_index_].frames.size()), sign_anims_[sign_index_].rate, false);
  sign_.rewind();
}

void Hud::draw(const Frame& f, std::vector<Layer>& layers) {
  layers.clear();
  lines_.seed(f.jitter_seed);
  if (f.show_score) {
    // The marks ride one looping clip, 20 frames apart, the first one moving it on.
    // It runs backwards, faster as the run of hits grows: 1 + 9 x (run / 18)^2.
    Layer l;
    l.view.eye = {0, 0, -2906};
    l.view.target = {0, 0, 0};
    l.view.fov_y = float(294 * kTau / 4096.0);  // LookAlongZ with fov 1.625: half-angle 147
    lines_.set_view(l.view);
    const auto m = marks(f.score);
    const double run = f.combo / 18.0;
    score_.set_speed(-(1.0 + 9.0 * run * run));
    const double start = score_.position();
    const int frames = score_.frames();
    for (std::size_t i = 0; i < m.size(); ++i) {
      const double at = std::fmod(start + 20.0 * double(i), double(frames));
      lines_.animated(mark_model_, score_anim_, static_cast<int>(at), {}, 4, l.lines, m[i]);
    }
    score_.advance(f.seconds);
    layers.push_back(std::move(l));
  }
  if (f.show_time) {
    Layer l;
    l.view = fixed_view(time_camera_);
    lines_.set_view(l.view);
    const double gone = std::clamp(1.0 - f.time_left, 0.0, 1.0);
    lines_.animated(time_model_, time_anim_, static_cast<int>(gone * (time_.frames() - 1)), {}, 3, l.lines);
    layers.push_back(std::move(l));
  }
  if (show_sign_) {
    Layer l;
    l.view = fixed_view(sign_camera_);
    lines_.set_view(l.view);
    lines_.animated(sign_model_, sign_anims_[sign_index_], sign_.pose(), {}, 3, l.lines);
    sign_.advance(f.seconds);
    layers.push_back(std::move(l));
  }
}

}  // namespace vib
