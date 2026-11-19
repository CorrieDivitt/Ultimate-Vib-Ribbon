// vibplay: Vib-Ribbon on a monitor, played from your own disc. The prototype of
// the game loop the VR builds will use: course generated eight seconds ahead of
// the music as in the original, rules from vibcore, presses judged at the moment
// they happened on the audio clock.
//
//   vibplay <disc.cue> [bronze|silver|gold | --song 1-6] [--fixed] [--seed N]
//           [--offset ms] [--sfx 0-1] [--music 0-1] [--width x]
//
//   Block  (L1/L2)             Left arrow, A          gamepad: left shoulder or trigger
//   Pit    (Up/Down)           Up or Down arrow, W, S gamepad: d-pad up or down
//   Loop   (R1/R2)             Right arrow, D         gamepad: right shoulder or trigger
//   Wave   (Triangle/Cross)    Space, J, K            gamepad: top or bottom face button
//   [ and ]: audio offset -/+ 5 ms     - and =: line width     Esc: quit

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "audio.h"
#include "line_renderer.h"
#include "vib/course_gen.h"
#include "vib/course_script.h"
#include "vib/disc_image.h"
#include "vib/gameplay.h"
#include "vib/iso9660.h"
#include "vib/known_discs.h"
#include "vib/pak.h"
#include "vib/scene.h"
#include "vib/sound_bank.h"

namespace {

using namespace vib;

constexpr std::int32_t kDelay = kLookAheadBlocks * kBlockSize;  // 8 s look-ahead, 22.05 kHz samples
constexpr double kOriginalRefresh = 49.759;                       // the PAL original's frame rate

// ---------------------------------------------------------------- sound effects

class Effects {
 public:
  Effects(Iso9660& iso, vibg::Audio& audio) : audio_(audio) {
    bank_ = parse_vh(iso.read("GAME/AUDIO/PSJ_SE.VH"));
    const Bytes vb = iso.read("GAME/AUDIO/PSJ_SE.VB");
    for (const ByteView s : split_vb(bank_, vb)) samples_.push_back(decode_spu_adpcm(s).pcm);
  }

  // A sound of the bank: program, tone; `volume` 0-127 as the game asks.
  void play(int program, int tone, int volume, int voice) {
    if (program >= int(bank_.programs.size())) return;
    const VabProgram& p = bank_.programs[std::size_t(program)];
    if (tone >= int(p.tones.size())) return;
    const VabTone& t = p.tones[std::size_t(tone)];
    if (t.sample < 1 || t.sample > int(samples_.size()) || t.volume == 0) return;
    // The game keys tone t at note t + 1. At the centre note the sample plays at
    // 44.1 kHz; each semitone below is a twelfth of an octave lower. The fine
    // centre is in 128ths of a semitone.
    const double semitones = double(tone + 1 - t.centre_note) - t.centre_fine / 128.0;
    const double rate = 44100.0 * std::pow(2.0, semitones / 12.0);
    // Volumes multiply as on the sound chip: bank x program, then tone, then the request.
    const int level = ((bank_.master_volume * p.volume) >> 7) * t.volume;
    const float gain = float(((level * volume) >> 7) / 16383.0);
    audio_.play_effect(&samples_[std::size_t(t.sample - 1)], rate, gain, voice);
  }
  void number(int n, int volume, int voice) { play(n >> 4, n & 15, volume, voice); }

 private:
  vibg::Audio& audio_;
  SoundBank bank_;
  std::vector<std::vector<std::int16_t>> samples_;
};

// ---------------------------------------------------------------- input

struct PressEvent {
  std::uint8_t bits;
  std::uint64_t ns;
};

std::uint8_t key_bits(SDL_Keycode k) {
  switch (k) {
    case SDLK_LEFT: case SDLK_A: return kPressBlock;
    case SDLK_UP: case SDLK_DOWN: case SDLK_W: case SDLK_S: return kPressPit;
    case SDLK_RIGHT: case SDLK_D: return kPressLoop;
    case SDLK_SPACE: case SDLK_J: case SDLK_K: return kPressWave;
    default: return 0;
  }
}

std::uint8_t pad_bits(Uint8 button) {
  switch (button) {
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return kPressBlock;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return kPressPit;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return kPressLoop;
    case SDL_GAMEPAD_BUTTON_NORTH: case SDL_GAMEPAD_BUTTON_SOUTH: return kPressWave;
    default: return 0;
  }
}

// ---------------------------------------------------------------- one song

struct Options {
  std::string cue;
  Course course = Course::Bronze;
  int song = 0;  // 1-6 for a single song; 0 = the course's two
  bool fixed = false;
  std::uint32_t seed = 0;
  bool seed_given = false;
  double offset_ms = 0, line_width = 1.0;
  float sfx = 1.0f, music = 1.0f;
};

enum class Result { Clear, GameOver, Quit };

struct Session {
  const Options& opt;
  DiscImage& disc;
  const CourseScript& script;
  Scene& scene;
  vibg::Audio& audio;
  Effects& fx;
  SDL_Window* window;
  vibr::Renderer& renderer;
  double line_width;
  std::map<SDL_JoystickID, SDL_Gamepad*> pads;
  std::map<SDL_JoystickID, std::uint8_t> triggers;  // which triggers are held, per pad
};

Result play_song(Session& s, int song, int track_index, int& points, std::uint32_t seed) {
  // Timing: like the original, a little different on every play, unless --fixed.
  const CourseTiming timing = s.opt.fixed ? CourseTiming::consistent() : CourseTiming::like_original(seed);
  const std::vector<std::int16_t> pcm = s.disc.read_audio_track(song + 1);
  const std::vector<Block> blocks = make_blocks(pcm, timing.lead);
  const auto total = static_cast<std::int32_t>(blocks.size() * kBlockSize);

  // What you hear: the 8 s the original spends analysing ahead in silence, then the
  // song, lined up with what the analysis sees (the lead shifts both together).
  std::vector<std::int16_t> out(std::size_t(kDelay) * 2 * 2, 0);
  const std::int64_t frames = std::int64_t(pcm.size() / 2);
  for (std::int64_t f = std::max<std::int64_t>(0, timing.lead); f < frames; ++f) {
    out.push_back(pcm[std::size_t(f) * 2]);
    out.push_back(pcm[std::size_t(f) * 2 + 1]);
  }
  if (timing.lead < 0) out.insert(out.begin() + std::ptrdiff_t(kDelay) * 4, std::size_t(-timing.lead) * 2, 0);

  CourseGenerator gen(s.script);
  gen.set_timing(timing);
  gen.start_round(song, 0);
  Gameplay game;
  RoundSetup setup;
  setup.course = s.opt.song ? Course::Bronze : s.opt.course;
  setup.course_seconds = total / kSampleRate;
  setup.points = points;
  setup.track_index = track_index;
  game.start(setup);
  s.scene.reset(true, false);

  std::printf("\nSong %d: %d s, %s course (lead %d, frame phase %.0f)\n", song, total / kSampleRate,
              s.opt.fixed ? "fixed" : "original-style", timing.lead, double(timing.frame_phase_q16) / 65536.0);
  s.audio.play_music(std::move(out));
  s.fx.play(0, 11, 63, 4);  // the round's opening sound

  std::size_t next_block = 0, fed = 0;
  bool was_selecting = gen.selecting(), ended = false;
  std::int32_t last_position = -kDelay;
  std::uint64_t last_ns = SDL_GetTicksNS();
  const std::uint64_t start_ns = last_ns;
  double title_timer = 0;
  std::vector<PressEvent> presses;
  std::vector<SceneLine> scene_lines;
  std::vector<vibr::LineInstance> lines;
  Result result = Result::Clear;

  auto handle = [&](const std::vector<GameEvent>& events) {
    bool form_up = false, hit = false, miss = false;
    for (const GameEvent& e : events) {
      switch (e.type) {
        case GameEvent::Type::Hit:
          s.fx.number(16 + e.road, 127, 3);
          s.scene.set_move(e.road, false);
          hit = true;
          break;
        case GameEvent::Type::Stray: {
          const int n = e.value == 1 ? 31 : e.value == 2 ? 28 : e.value == 4 ? 29 : 27;
          s.fx.number(n, 100, 3);
          s.scene.set_move(e.road, true);
          break;
        }
        case GameEvent::Type::Miss:
          s.fx.play(1, 10, 127, 2);
          miss = true;
          break;
        case GameEvent::Type::FormUp:
          s.fx.play(1, 14, 127, 4);
          s.scene.play_evo();
          form_up = true;
          break;
        case GameEvent::Type::Camera: s.scene.camera().event(e.value); break;
        case GameEvent::Type::CameraBreak: s.scene.camera().cut(); break;
        default: break;
      }
    }
    if (miss) s.scene.show_evo(0);
    else if (hit && !form_up && game.form() != 0) s.scene.show_evo(game.combo());
  };

  for (;;) {
    presses.clear();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_EVENT_QUIT: return Result::Quit;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED: s.renderer.request_resize(); break;
        case SDL_EVENT_KEY_DOWN:
          if (e.key.repeat) break;
          if (e.key.key == SDLK_ESCAPE) return Result::Quit;
          if (e.key.key == SDLK_LEFTBRACKET || e.key.key == SDLK_RIGHTBRACKET) {
            s.audio.adjust_offset(e.key.key == SDLK_LEFTBRACKET ? -5 : 5);
            std::printf("audio offset %+.0f ms (latency now %.0f ms)\n", s.audio.offset_ms(), s.audio.latency_ms());
          } else if (e.key.key == SDLK_MINUS || e.key.key == SDLK_EQUALS) {
            s.line_width = std::clamp(s.line_width + (e.key.key == SDLK_MINUS ? -0.25 : 0.25), 0.25, 4.0);
          } else if (const std::uint8_t b = key_bits(e.key.key)) {
            presses.push_back({b, e.key.timestamp});
          }
          break;
        case SDL_EVENT_GAMEPAD_ADDED:
          if (SDL_Gamepad* g = SDL_OpenGamepad(e.gdevice.which)) {
            s.pads[e.gdevice.which] = g;
            std::printf("Gamepad: %s\n", SDL_GetGamepadName(g));
          }
          break;
        case SDL_EVENT_GAMEPAD_REMOVED:
          if (auto it = s.pads.find(e.gdevice.which); it != s.pads.end()) {
            SDL_CloseGamepad(it->second);
            s.pads.erase(it);
          }
          break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) return Result::Quit;
          if (const std::uint8_t b = pad_bits(e.gbutton.button)) presses.push_back({b, e.gbutton.timestamp});
          break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
          if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
            const std::uint8_t bit = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 1 : 2;
            std::uint8_t& held = s.triggers[e.gaxis.which];
            if (e.gaxis.value > 16000 && !(held & bit)) {  // pulled past halfway: a press
              held = std::uint8_t(held | bit);
              presses.push_back({bit == 1 ? std::uint8_t(kPressBlock) : std::uint8_t(kPressLoop), e.gaxis.timestamp});
            } else if (e.gaxis.value < 8000) {
              held = std::uint8_t(held & ~bit);
            }
          }
          break;
        default: break;
      }
    }

    const std::uint64_t now_ns = SDL_GetTicksNS();
    const double dt = double(now_ns - last_ns) * 1e-9;
    last_ns = now_ns;
    // Heard position, 22.05 kHz samples from the song's start (negative during the 8 s ahead).
    auto heard_at = [&](std::uint64_t ns) {
      return static_cast<std::int32_t>(std::floor(s.audio.heard_frame_at(now_ns, ns) / 2.0)) - kDelay;
    };
    const std::int32_t heard = std::max(heard_at(now_ns), last_position);

    // The analysis runs eight seconds ahead of what is heard.
    while (next_block < blocks.size() && std::int32_t(next_block * kBlockSize) <= heard + kDelay) {
      const auto t = static_cast<std::int32_t>(next_block * kBlockSize);
      gen.process(t, blocks[next_block++]);
      for (; fed < gen.events().size(); ++fed) game.add_event(gen.events()[fed]);
      if (was_selecting && !gen.selecting()) game.quiet(t);
      was_selecting = gen.selecting();
    }
    if (!ended && next_block >= blocks.size()) {
      game.end_at(total);
      ended = true;
    }

    // Presses first, each judged where the music was when it happened.
    Outcome outcome = Outcome::Playing;
    std::stable_sort(presses.begin(), presses.end(), [](const PressEvent& a, const PressEvent& b) { return a.ns < b.ns; });
    for (const PressEvent& p : presses) {
      const std::int32_t at = std::clamp(heard_at(p.ns), last_position, heard);
      outcome = game.update(at, p.bits, s.scene.move_done(0.5));
      last_position = at;
      handle(game.events());
      if (outcome != Outcome::Playing) break;
    }
    if (outcome == Outcome::Playing) {
      outcome = game.update(heard, 0, s.scene.move_done(0.5));
      last_position = heard;
      handle(game.events());
    }

    // Draw. The jitter changes at the original's 50 Hz, whatever the display does.
    Scene::Frame f;
    f.seconds = dt;
    f.jitter_seed = next_random(static_cast<std::uint32_t>(double(now_ns - start_ns) * 1e-9 * kOriginalRefresh) * 2654435761u);
    f.speed = game.speed();
    f.road = game.road();
    f.still = game.near_part();
    f.shake = game.shake();
    f.form = game.form();
    f.stage = game.stage();
    SceneView view;
    s.scene.draw(f, scene_lines, view);
    lines.clear();
    for (const SceneLine& l : scene_lines)
      lines.push_back({{l.a.x, l.a.y, l.a.z}, {l.b.x, l.b.y, l.b.z}, {l.colour.r / 255.0f, l.colour.g / 255.0f, l.colour.b / 255.0f, 1.0f}});
    const vibr::Mat4 view_matrix = vibr::look_at({view.eye.x, view.eye.y, view.eye.z}, {view.target.x, view.target.y, view.target.z}, {0, -1, 0});
    // The original's lines are one pixel on a 288-line picture.
    const float width = float(s.line_width * std::max(1, s.renderer.height()) / 288.0);
    s.renderer.draw(lines, view_matrix, view.fov_y, width);

    title_timer -= dt;
    if (title_timer <= 0) {
      static const char* forms[] = {"angel", "rabbit", "frog", "worm"};
      char title[160];
      std::snprintf(title, sizeof title, "Vib-Ribbon  song %d  score %d  %s  %s", song, game.score(), forms[game.form()],
                    heard < 0 ? "(analysing ahead)" : "");
      SDL_SetWindowTitle(s.window, title);
      title_timer = 0.25;
    }

    if (outcome == Outcome::Clear) {
      std::printf("Song %d clear: score %d + end bonus %d\n", song, game.score(), game.bonus());
      if (game.score() == 0) return Result::GameOver;  // the original counts 0 as a loss
      points = game.score() + game.bonus();
      result = Result::Clear;
      break;
    }
    if (outcome == Outcome::GameOver) {
      std::printf("Game over on song %d: score %d\n", song, game.score());
      result = Result::GameOver;
      break;
    }
  }
  return result;
}

int run(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto value = [&]() -> const char* {
      if (i + 1 >= argc) throw std::runtime_error(a + " needs a value");
      return argv[++i];
    };
    if (a == "bronze") opt.course = Course::Bronze;
    else if (a == "silver") opt.course = Course::Silver;
    else if (a == "gold") opt.course = Course::Gold;
    else if (a == "--song") opt.song = std::clamp(std::atoi(value()), 1, 6);
    else if (a == "--fixed") opt.fixed = true;
    else if (a == "--seed") opt.seed = std::uint32_t(std::strtoul(value(), nullptr, 10)), opt.seed_given = true;
    else if (a == "--offset") opt.offset_ms = std::atof(value());
    else if (a == "--sfx") opt.sfx = float(std::atof(value()));
    else if (a == "--music") opt.music = float(std::atof(value()));
    else if (a == "--width") opt.line_width = std::atof(value());
    else if (opt.cue.empty() && a.size() > 0 && a[0] != '-') opt.cue = a;
    else throw std::runtime_error("unknown argument: " + a);
  }
  if (opt.cue.empty()) {
    std::fputs("usage: vibplay <disc.cue> [bronze|silver|gold | --song 1-6] [--fixed] [--seed N]\n"
               "               [--offset ms] [--sfx 0-1] [--music 0-1] [--width x]\n",
               stderr);
    return 2;
  }
  if (!opt.seed_given) opt.seed = static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());

  DiscImage disc = DiscImage::open(path_from_utf8(opt.cue));
  Iso9660 iso(disc);
  const Pak pak(iso.read("GAME/02_FILES.PAK"));
  const CourseScript script = parse_course_script(pak.view("SCRIPT/SYSTEM3.FSL"));
  Scene scene(pak);

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
  SDL_Window* window = SDL_CreateWindow("Vib-Ribbon", 1200, 900, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
  int code = 0;
  {
    vibr::Renderer renderer(window);
    vibg::Audio audio(opt.offset_ms);
    audio.set_volumes(opt.music, opt.sfx);
    Effects fx(iso, audio);
    std::printf("GPU: %s\nCourses: %s\n", renderer.gpu_name(), opt.fixed ? "fixed (the same every play)" : "vary from play to play, like the original");
    Session s{opt, disc, script, scene, audio, fx, window, renderer, opt.line_width, {}, {}};

    std::vector<int> songs;
    if (opt.song) songs = {opt.song};
    else songs = {int(opt.course) * 2 + 1, int(opt.course) * 2 + 2};
    std::uint32_t seed = opt.seed;
    int points = 0;
    for (std::size_t i = 0; i < songs.size(); ++i) {
      const Result r = play_song(s, songs[i], int(i), points, seed);
      seed = next_random(seed);
      if (r == Result::Quit) break;
      if (r == Result::GameOver) {
        std::printf("Game over.\n");
        break;
      }
      if (i + 1 == songs.size()) std::printf("Course clear! Total %d\n", points);
    }
    for (auto& [id, pad] : s.pads) SDL_CloseGamepad(pad);
  }
  SDL_DestroyWindow(window);
  SDL_Quit();
  return code;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "vibplay: %s\n", e.what());
    return 1;
  }
}
