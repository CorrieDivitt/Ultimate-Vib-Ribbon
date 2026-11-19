// vibview: draws a model from your own Vib-Ribbon disc as lines and plays its animations.
//
//   vibview <disc.cue> [<pak> [<model.tmd> [<anim.anm>]]]
//   defaults: GAME/02_FILES.PAK  CHARA/PEELOO/MODEL.TMD  first .ANM beside the model
//
//   Left mouse drag: orbit     Wheel: zoom        R: reset view
//   Space: pause/resume        , and . : step one frame while paused
//   Left/Right: previous/next animation in the model's folder
//   Up/Down: line width        M: facing markers on/off
//   Esc: quit

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include "line_renderer.h"
#include "vib/disc_image.h"
#include "vib/iso9660.h"
#include "vib/motion.h"
#include "vib/pak.h"
#include "vib/pose.h"
#include "vib/tmd.h"

namespace {

using vibr::LineInstance;
using vibr::Mat4;
using vibr::V3;
using vibr::look_at;

// ---------------------------------------------------------------- scene

std::string upper(std::string s) {
  for (char& c : s) c = c == '\\' ? '/' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

// The viewer's world uses the PlayStation's own axes (X right, Y down, Z away),
// so nothing is mirrored; the camera simply treats -Y as up.
V3 to_world(const vib::Vec3f& v) { return {v.x, v.y, v.z}; }

V3 transform(const Mat4& m, V3 v) {
  return {m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12], m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13],
          m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14]};
}

// The original's rule for objects that carry a facing marker: project the marker
// triangle and hide the object when its screen winding is negative (Y down).
bool marker_faces_away(const Mat4& view, V3 a, V3 b, V3 c) {
  const V3 p[3] = {transform(view, a), transform(view, b), transform(view, c)};
  float x[3], y[3];
  for (int i = 0; i < 3; ++i) {
    if (p[i].z > -1.0f) return false;  // at or behind the camera: leave it drawn
    x[i] = p[i].x / -p[i].z;
    y[i] = -p[i].y / -p[i].z;
  }
  return (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]) < 0.0f;
}

void build_geometry(const vib::TmdModel& model, const std::vector<vib::PosedObject>& posed, const Mat4& view, bool use_markers,
                    std::vector<LineInstance>& lines, V3& lo, V3& hi) {
  lines.clear();
  lo = {1e9f, 1e9f, 1e9f};
  hi = {-1e9f, -1e9f, -1e9f};
  for (const auto& p : posed) {
    const vib::TmdObject& obj = model.objects[std::size_t(p.object)];
    for (const auto& v : p.vertices) {
      const V3 w = to_world(v);
      lo = {std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z)};
      hi = {std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z)};
    }
    if (use_markers && obj.marker == vib::FaceMarker::DrawOnlyWhenFacing &&
        marker_faces_away(view, to_world(p.vertices[obj.marker_vertices[0]]), to_world(p.vertices[obj.marker_vertices[1]]),
                          to_world(p.vertices[obj.marker_vertices[2]])))
      continue;
    for (const auto& l : obj.lines) {
      const V3 a = to_world(p.vertices[l.a]), b = to_world(p.vertices[l.b]);
      LineInstance li{{a.x, a.y, a.z}, {b.x, b.y, b.z}, {l.colour_a.r / 255.0f, l.colour_a.g / 255.0f, l.colour_a.b / 255.0f, 1.0f}};
      lines.push_back(li);
    }
  }
}

int run(int argc, char** argv) {
  if (argc < 2) {
    std::fputs("usage: vibview <disc.cue> [<pak> [<model.tmd> [<anim.anm>]]]\n"
               "  defaults: GAME/02_FILES.PAK CHARA/PEELOO/MODEL.TMD and the first animation beside the model\n",
               stderr);
    return 2;
  }
  const std::string pak_path = argc > 2 ? argv[2] : "GAME/02_FILES.PAK";
  const std::string model_name = upper(argc > 3 ? argv[3] : "CHARA/PEELOO/MODEL.TMD");

  vib::DiscImage disc = vib::DiscImage::open(vib::path_from_utf8(argv[1]));
  vib::Iso9660 iso(disc);
  const vib::Pak pak(iso.read(pak_path));
  const vib::TmdModel model = vib::parse_tmd(pak.view(model_name));

  // Animations that live in the same folder as the model.
  const std::string folder = model_name.substr(0, model_name.rfind('/') + 1);
  std::vector<std::string> anim_names;
  for (const auto& e : pak.entries()) {
    const std::string n = upper(e.name);
    if (n.size() > 4 && n.compare(0, folder.size(), folder) == 0 && n.compare(n.size() - 4, 4, ".ANM") == 0 &&
        n.find('/', folder.size()) == std::string::npos)
      anim_names.push_back(n);
  }
  int anim_index = anim_names.empty() ? -1 : 0;
  if (argc > 4) {
    const std::string want = upper(argv[4]);
    const auto it = std::find(anim_names.begin(), anim_names.end(), want);
    if (it == anim_names.end()) anim_names.push_back(want), anim_index = int(anim_names.size()) - 1;
    else anim_index = int(it - anim_names.begin());
  }

  if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
  SDL_Window* window = SDL_CreateWindow("vibview", 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());

  {
    vibr::Renderer renderer(window);
    std::printf("GPU: %s\nModel: %s (%zu objects), %zu animations in its folder\n", renderer.gpu_name(), model_name.c_str(),
                model.objects.size(), anim_names.size());

    vib::ModelAnimation anim;
    auto load_anim = [&]() {
      anim = {};
      if (anim_index >= 0) anim = vib::parse_anm(pak.view(anim_names[std::size_t(anim_index)]));
      const std::string title = "vibview  " + model_name + "  " + (anim_index >= 0 ? anim_names[std::size_t(anim_index)] : std::string("(no animation)")) +
                                "  rate " + std::to_string(anim.rate) + "  frames " + std::to_string(anim.frames.size());
      SDL_SetWindowTitle(window, title.c_str());
      std::printf("%s\n", title.c_str());
    };
    load_anim();

    std::vector<LineInstance> lines;
    V3 lo{}, hi{};
    float yaw = 0.5f, pitch = 0.15f, distance = 1000.0f, line_width = 2.5f;
    V3 centre{0, 0, 0};
    bool framed = false, paused = false, running = true, use_markers = true;
    double frame_pos = 0;
    Uint64 last = SDL_GetTicksNS();

    while (running) {
      SDL_Event e;
      while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) running = false;
        else if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || e.type == SDL_EVENT_WINDOW_RESIZED) renderer.request_resize();
        else if (e.type == SDL_EVENT_MOUSE_MOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
          yaw -= e.motion.xrel * 0.008f;
          pitch = std::clamp(pitch + e.motion.yrel * 0.008f, -1.5f, 1.5f);
        } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
          distance *= std::pow(0.9f, e.wheel.y);
        } else if (e.type == SDL_EVENT_KEY_DOWN) {
          const int frames = int(anim.frames.size());
          switch (e.key.key) {
            case SDLK_ESCAPE: running = false; break;
            case SDLK_SPACE: paused = !paused; break;
            case SDLK_R: framed = false; yaw = 0.5f; pitch = 0.15f; break;
            case SDLK_M: use_markers = !use_markers; std::printf("facing markers %s\n", use_markers ? "on" : "off"); break;
            case SDLK_UP: line_width = std::min(line_width + 0.5f, 12.0f); break;
            case SDLK_DOWN: line_width = std::max(line_width - 0.5f, 1.0f); break;
            case SDLK_PERIOD: if (paused && frames) frame_pos = double((int(frame_pos) + 1) % frames); break;
            case SDLK_COMMA: if (paused && frames) frame_pos = double((int(frame_pos) + frames - 1) % frames); break;
            case SDLK_RIGHT:
            case SDLK_LEFT:
              if (!anim_names.empty()) {
                const int n = int(anim_names.size());
                anim_index = (anim_index + (e.key.key == SDLK_RIGHT ? 1 : n - 1)) % n;
                frame_pos = 0;
                load_anim();
              }
              break;
            default: break;
          }
        }
      }

      const Uint64 now = SDL_GetTicksNS();
      const double dt = double(now - last) * 1e-9;
      last = now;
      // The header's rate field is treated as frames per second (unconfirmed).
      const double fps = anim.rate > 0 ? double(anim.rate) : 30.0;
      if (!paused && !anim.frames.empty()) frame_pos = std::fmod(frame_pos + dt * fps, double(anim.frames.size()));

      const std::vector<vib::PosedObject> posed =
          anim.frames.empty() ? vib::rest_pose(model) : vib::pose(model, anim.frames[std::min(std::size_t(frame_pos), anim.frames.size() - 1)]);
      // Orbit around the model. PlayStation Y points down, so "up" is -Y and yaw 0 looks along +Z.
      const float fov = 0.8f;
      auto camera = [&]() {
        const V3 eye{centre.x + distance * std::cos(pitch) * std::sin(yaw), centre.y - distance * std::sin(pitch),
                     centre.z - distance * std::cos(pitch) * std::cos(yaw)};
        return look_at(eye, centre, {0, -1, 0});
      };
      Mat4 view = camera();
      build_geometry(model, posed, view, use_markers, lines, lo, hi);
      if (!framed && lo.x <= hi.x) {
        centre = {(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
        const float size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1.0f});
        distance = size * 1.6f;
        framed = true;
        view = camera();
        build_geometry(model, posed, view, use_markers, lines, lo, hi);
      }
      renderer.draw(lines, view, fov, line_width);
    }
  }
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "vibview: %s\n", e.what());
    return 1;
  }
}
