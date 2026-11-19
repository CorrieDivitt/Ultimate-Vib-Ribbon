// vibplay: Vib-Ribbon on a monitor, played from your own disc. The prototype of
// the game loop the VR builds will use: course generated eight seconds ahead of
// the music as in the original, rules from vibcore, presses judged at the moment
// they happened on the audio clock.
//
//   vibplay <disc.cue> [bronze|silver|gold | --song 1-6] [--fixed] [--seed N]
//
// With no course or song named it opens on the original's menus: game-disc
// courses, options and high scores (menus: arrows or d-pad to move, Enter, Space
// or Cross to choose, Esc, Backspace or Circle to go back). Esc during a course
// goes back to the menus.
//           [--offset ms] [--sfx 0-1] [--music 0-1] [--width x]
//
//   Block  (L1/L2)             Left arrow, A          gamepad: left shoulder or trigger
//   Pit    (Up/Down)           Up or Down arrow, W, S gamepad: d-pad up or down
//   Loop   (R1/R2)             Right arrow, D         gamepad: right shoulder or trigger
//   Wave   (Triangle/Cross)    Space, J, K            gamepad: top or bottom face button
//   [ and ]: audio offset -/+ 5 ms     - and =: line width     Esc: quit
//
// High scores and options (sound effects, vibration, fixed courses, line width,
// audio offset, volumes) live in save.txt in the per-user folder SDL picks, printed
// at start. Offset and width changes made while playing are saved there. Options on
// the command line apply to that run only.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
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
#include "vib/save.h"
#include "vib/pak.h"
#include "vib/scene.h"
#include "vib/sound_bank.h"
#include "vib/title.h"

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
    for (const ByteView s : split_vb(bank_, vb)) {
      DecodedSample d = decode_spu_adpcm(s);
      samples_.push_back(std::move(d.pcm));
      loops_.push_back(d.loop_start);
    }
  }

  // A sound of the bank: program, tone; `volume` 0-127 as the game asks.
  // `after`: start the moment that voice ends (or at once if it is silent).
  void play(int program, int tone, int volume, int voice, int after = -1) {
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
    audio_.play_effect(&samples_[std::size_t(t.sample - 1)], rate, gain, voice, loops_[std::size_t(t.sample - 1)], after);
  }
  void number(int n, int volume, int voice, int after = -1) { play(n >> 4, n & 15, volume, voice, after); }
  bool is_playing(int voice) { return audio_.is_playing(voice); }
  void stop(int voice) { audio_.stop(voice); }

 private:
  vibg::Audio& audio_;
  SoundBank bank_;
  std::vector<std::vector<std::int16_t>> samples_;
  std::vector<int> loops_;
};

// Sounds played one after another on two voices taken in turn (`voice` and
// `voice` + 12), each queued to start as the one before it ends, as the
// original's menus play Vibri's words and their music.
class SoundSequence {
 public:
  SoundSequence(Effects& fx, int voice) : fx_(fx), voice_(voice), current_(voice) {}
  void stop() {
    if (!playing_) return;
    fx_.stop(voice_);
    fx_.stop(voice_ + 12);
    playing_ = false;
  }
  void play(std::vector<int> sounds, int volume) {
    stop();
    sounds_ = std::move(sounds);
    volume_ = volume;
    previous_ = -1;
    next_ = 0;
    playing_ = !sounds_.empty();
  }
  // Once a frame: as soon as the next voice is free, queue the next sound behind the last.
  void update() {
    while (playing_) {
      if (next_ < sounds_.size()) {
        if (fx_.is_playing(current_)) return;
        fx_.number(sounds_[next_++], volume_, current_, previous_);
        previous_ = current_;
        current_ = (current_ + 12) % 24;
      } else {
        playing_ = fx_.is_playing(previous_ >= 0 ? previous_ : current_);
        return;
      }
    }
  }
  bool playing() const { return playing_; }

 private:
  Effects& fx_;
  int voice_, current_, previous_ = -1;
  std::vector<int> sounds_;
  std::size_t next_ = 0;
  int volume_ = 0;
  bool playing_ = false;
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

// Command line. Anything given here overrides the saved options for this run only.
struct Args {
  std::string cue;
  Course course = Course::Bronze;
  int song = 0;  // 1-6 for a single song; 0 = the course's two
  bool fixed = false;
  std::uint32_t seed = 0;
  bool seed_given = false;
  bool course_given = false;  // a course or song on the command line: play it straight away
  std::optional<double> offset_ms, line_width;
  std::optional<float> sfx, music;
};

enum class Result { Clear, GameOver, Abort, Quit };  // Abort: back to the menus; Quit: the window closed

struct Session {
  const Args& opt;
  SaveData& save;
  bool fixed;
  DiscImage& disc;
  const CourseScript& script;
  Scene& scene;
  Hud& hud;
  vibg::Audio& audio;
  Effects& fx;
  SDL_Window* window;
  vibr::Renderer& renderer;
  double line_width;
  std::map<SDL_JoystickID, SDL_Gamepad*> pads;
  std::map<SDL_JoystickID, std::uint8_t> triggers;  // which triggers are held, per pad
  Course course = Course::Bronze;  // being played
  int song = 0;                    // 1-6 when practising one song
};

Result play_song(Session& s, int song, int track_index, int& points, std::uint32_t seed) {
  // Timing: like the original, a little different on every play, unless --fixed.
  const CourseTiming timing = s.fixed ? CourseTiming::consistent() : CourseTiming::like_original(seed);
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
  setup.course = s.song ? Course::Bronze : s.course;
  setup.course_seconds = total / kSampleRate;
  setup.points = points;
  setup.track_index = track_index;
  game.start(setup);
  s.scene.reset(true, false);
  s.hud.start_round(track_index, !s.song);  // the "Round 1/2" sign belongs to the two-song courses

  std::printf("\nSong %d: %d s, %s course (lead %d, frame phase %.0f)\n", song, total / kSampleRate,
              s.fixed ? "fixed" : "original-style", timing.lead, double(timing.frame_phase_q16) / 65536.0);
  s.audio.play_music(std::move(out));
  s.fx.play(0, 11, 63, 4);  // the round's opening sound (the original plays it whatever the option)

  std::size_t next_block = 0, fed = 0;
  bool was_selecting = gen.selecting(), ended = false;
  std::int32_t last_position = -kDelay;
  std::uint64_t last_ns = SDL_GetTicksNS();
  const std::uint64_t start_ns = last_ns;
  double title_timer = 0;
  std::vector<PressEvent> presses;
  std::vector<SceneLine> scene_lines;
  std::vector<vibr::LineInstance> lines;
  std::vector<Hud::Layer> hud_layers;
  std::vector<std::vector<vibr::LineInstance>> hud_lines;
  Result result = Result::Clear;

  // The sound effects option silences gameplay's sounds only, as in the original
  // (the menus' music and voice are not effects).
  const bool effects_on = s.save.options.sound_effects;
  auto handle = [&](const std::vector<GameEvent>& events) {
    bool form_up = false, hit = false, miss = false;
    for (const GameEvent& e : events) {
      switch (e.type) {
        case GameEvent::Type::Hit:
          if (effects_on) s.fx.number(16 + e.road, 127, 3);
          s.scene.set_move(e.road, false);
          hit = true;
          break;
        case GameEvent::Type::Stray: {
          const int n = e.value == 1 ? 31 : e.value == 2 ? 28 : e.value == 4 ? 29 : 27;
          if (effects_on) s.fx.number(n, 100, 3);
          s.scene.set_move(e.road, true);
          break;
        }
        case GameEvent::Type::Miss:
          if (effects_on) s.fx.play(1, 10, 127, 2);
          if (s.save.options.vibration)
            for (auto& [id, pad] : s.pads) SDL_RumbleGamepad(pad, 0x6000, 0xC000, 200);
          miss = true;
          break;
        case GameEvent::Type::FormUp:
          if (effects_on) s.fx.play(1, 14, 127, 4);
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
          if (e.key.key == SDLK_ESCAPE) return Result::Abort;
          if (e.key.key == SDLK_LEFTBRACKET || e.key.key == SDLK_RIGHTBRACKET) {
            s.audio.adjust_offset(e.key.key == SDLK_LEFTBRACKET ? -5 : 5);
            s.save.options.audio_offset_ms = s.audio.offset_ms();
            std::printf("audio offset %+.0f ms (latency now %.0f ms)\n", s.audio.offset_ms(), s.audio.latency_ms());
          } else if (e.key.key == SDLK_MINUS || e.key.key == SDLK_EQUALS) {
            s.line_width = std::clamp(s.line_width + (e.key.key == SDLK_MINUS ? -0.125 : 0.125), 0.125, 4.0);
            s.save.options.line_width = s.line_width;
            std::printf("line width %.3f (saved)\n", s.line_width);
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
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK || e.gbutton.button == SDL_GAMEPAD_BUTTON_START) return Result::Abort;
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
    auto convert = [](const std::vector<SceneLine>& in, std::vector<vibr::LineInstance>& dst) {
      dst.clear();
      for (const SceneLine& l : in)
        dst.push_back({{l.a.x, l.a.y, l.a.z}, {l.b.x, l.b.y, l.b.z}, {l.colour.r / 255.0f, l.colour.g / 255.0f, l.colour.b / 255.0f, 1.0f}});
    };
    auto matrix = [](const SceneView& v) {
      return vibr::look_at({v.eye.x, v.eye.y, v.eye.z}, {v.target.x, v.target.y, v.target.z}, {0, -1, 0});
    };
    convert(scene_lines, lines);
    std::vector<vibr::Layer> layers{{&lines, matrix(view), view.fov_y}};

    // The meters and the sign, each through its own camera, over the scene.
    Hud::Frame h;
    h.seconds = dt;
    h.jitter_seed = next_random(f.jitter_seed);
    h.score = game.score();
    h.combo = game.combo();
    const int seconds = heard >= 0 ? heard / kSampleRate : 0;  // whole seconds, as the original counts
    h.time_left = 1.0 - double(seconds) / std::max(1, setup.course_seconds);
    s.hud.draw(h, hud_layers);
    hud_lines.resize(hud_layers.size());
    for (std::size_t i = 0; i < hud_layers.size(); ++i) {
      convert(hud_layers[i].lines, hud_lines[i]);
      layers.push_back({&hud_lines[i], matrix(hud_layers[i].view), hud_layers[i].view.fov_y});
    }
    // The original's lines are one pixel on a 288-line picture.
    const float width = float(s.line_width * std::max(1, s.renderer.height()) / 288.0);
    s.renderer.draw(layers, width);

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
      points = game.score();
      result = Result::GameOver;
      break;
    }
  }
  return result;
}

int run(int argc, char** argv) {
  Args opt;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto value = [&]() -> const char* {
      if (i + 1 >= argc) throw std::runtime_error(a + " needs a value");
      return argv[++i];
    };
    if (a == "bronze") opt.course = Course::Bronze, opt.course_given = true;
    else if (a == "silver") opt.course = Course::Silver, opt.course_given = true;
    else if (a == "gold") opt.course = Course::Gold, opt.course_given = true;
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
  Hud hud(pak);

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
  SDL_Window* window = SDL_CreateWindow("Vib-Ribbon", 1200, 900, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
  // Saved high scores and options, in the per-user folder SDL picks for the platform.
  std::string save_path;
  if (char* pref = SDL_GetPrefPath("VibRibbonVR", "VibRibbonVR")) {
    save_path = std::string(pref) + "save.txt";
    SDL_free(pref);
  }
  SaveData save;
  if (!save_path.empty()) {
    std::ifstream in(path_from_utf8(save_path));
    if (in) {
      std::stringstream text;
      text << in.rdbuf();
      save = SaveData::from_text(text.str());
    }
    std::printf("Save file: %s\n", save_path.c_str());
  }
  auto write_save = [&]() {
    if (save_path.empty()) return;
    std::ofstream out(path_from_utf8(save_path), std::ios::binary);
    out << save.to_text();
  };

  int code = 0;
  {
    vibr::Renderer renderer(window);
    vibg::Audio audio(opt.offset_ms.value_or(save.options.audio_offset_ms));
    audio.set_volumes(opt.music.value_or(save.options.music_volume), opt.sfx.value_or(save.options.effects_volume));
    Effects fx(iso, audio);
    const bool fixed = opt.fixed || save.options.fixed_courses;
    std::printf("GPU: %s\nCourses: %s\n", renderer.gpu_name(), fixed ? "fixed (the same every play)" : "vary from play to play, like the original");
    Session s{opt, save, fixed, disc, script, scene, hud, audio, fx, window, renderer,
              opt.line_width.value_or(save.options.line_width), {}, {}};
    std::uint32_t seed = opt.seed;
    int last_rank = 0;

    // One course from start to end (or one song as practice). Returns false if the
    // window was closed.
    auto play_course = [&](Course course, int song) -> bool {
      s.course = course;
      s.song = song;
      std::vector<int> songs;
      if (song) songs = {song};
      else songs = {int(course) * 2 + 1, int(course) * 2 + 2};
      int points = 0;
      bool finished = false;  // the course ended, cleared or lost, rather than being left
      for (std::size_t i = 0; i < songs.size(); ++i) {
        const Result r = play_song(s, songs[i], int(i), points, seed);
        seed = next_random(seed);
        if (r == Result::Quit) return false;
        if (r == Result::Abort) break;
        if (r == Result::GameOver) {
          std::printf("Game over.\n");
          finished = true;
          break;
        }
        if (i + 1 == songs.size()) {
          std::printf("Course clear! Total %d\n", points);
          finished = true;
        }
      }
      last_rank = 0;
      // High scores belong to whole courses, as in the original (one song is practice).
      if (finished && !song) {
        static const char* places[] = {"", "1st", "2nd", "3rd"};
        static const char* names[] = {"Bronze", "Silver", "Gold"};
        last_rank = save.record(course, points);
        const auto& table = save.high_scores[std::size_t(course)];
        std::printf("%s high scores: %d, %d, %d%s%s\n", names[int(course)], table[0], table[1], table[2], last_rank ? "   new " : "",
                    places[last_rank]);
        write_save();
      }
      return true;
    };

    if (opt.song || opt.course_given) {
      play_course(opt.course, opt.song);  // straight into play, as before
    } else {
      // The menus, run at the original's 50 frames a second whatever the display does.
      TitleScreens title(pak);
      using S = TitleScreens::Screen;
      using R = TitleScreens::Result;
      title.sound_effects = save.options.sound_effects;
      title.vibration = save.options.vibration;
      auto show_scores = [&]() {
        for (std::size_t c = 0; c < 3; ++c) title.psj_high_scores[c] = save.high_scores[c];
      };
      show_scores();
      title.go_to(S::Main);
      // The menus' two sound sequences: music on voices 0 and 12, Vibri's voice on 1 and 13.
      SoundSequence music(fx, 0), voices(fx, 1);
      auto play_requests = [&]() {
        for (TitleScreens::SoundRequest& q : title.take_sounds()) {
          SoundSequence& seq = q.channel == TitleScreens::Channel::Music ? music : voices;
          if (q.stop) seq.stop();
          else seq.play(std::move(q.sounds), q.volume);
        }
      };
      TitleScreens::Input pressed, held;
      std::vector<std::vector<vibr::LineInstance>> menu_lines;
      std::uint64_t last_ns = SDL_GetTicksNS();
      double behind = 0;
      bool course_chosen = false, running = true;
      Course chosen = Course::Bronze;
      while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
          auto press = [&](int which, bool down) {
            bool* p[6] = {&pressed.up, &pressed.down, &pressed.left, &pressed.right, &pressed.confirm, &pressed.cancel};
            bool* h[4] = {&held.up, &held.down, &held.left, &held.right};
            if (down) *p[which] = true;
            if (which < 4) *h[which] = down;
          };
          switch (e.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_RESIZED: renderer.request_resize(); break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
              if (e.key.repeat) break;
              const bool down = e.type == SDL_EVENT_KEY_DOWN;
              switch (e.key.key) {
                case SDLK_UP: case SDLK_W: press(0, down); break;
                case SDLK_DOWN: case SDLK_S: press(1, down); break;
                case SDLK_LEFT: case SDLK_A: press(2, down); break;
                case SDLK_RIGHT: case SDLK_D: press(3, down); break;
                case SDLK_RETURN: case SDLK_SPACE: case SDLK_Z: press(4, down); break;
                case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_X: press(5, down); break;
                default: break;
              }
              break;
            }
            case SDL_EVENT_GAMEPAD_ADDED:
              if (SDL_Gamepad* g = SDL_OpenGamepad(e.gdevice.which)) s.pads[e.gdevice.which] = g;
              break;
            case SDL_EVENT_GAMEPAD_REMOVED:
              if (auto it = s.pads.find(e.gdevice.which); it != s.pads.end()) {
                SDL_CloseGamepad(it->second);
                s.pads.erase(it);
              }
              break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP: {
              const bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
              switch (e.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_UP: press(0, down); break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN: press(1, down); break;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT: press(2, down); break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: press(3, down); break;
                case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_START: press(4, down); break;
                case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_NORTH: press(5, down); break;
                default: break;
              }
              break;
            }
            default: break;
          }
        }
        if (!running) break;
        const std::uint64_t now_ns = SDL_GetTicksNS();
        behind = std::min(behind + double(now_ns - last_ns) * 1e-9, 0.1);
        last_ns = now_ns;
        while (behind >= 1.0 / TitleScreens::kFrameRate) {
          behind -= 1.0 / TitleScreens::kFrameRate;
          TitleScreens::Input in = pressed;
          in.held_up = held.up, in.held_down = held.down, in.held_left = held.left, in.held_right = held.right;
          pressed = {};
          in.voices_playing = voices.playing();
          const R r = title.tick(in);
          switch (r) {
            case R::PsjModes: title.go_to(S::PsjModeSelect); break;
            case R::UnknownModes: std::printf("Courses from your own music come later.\n"); break;
            case R::Options: title.go_to(S::Option); break;
            case R::Leave: running = false; break;
            case R::Bronze: case R::Silver: case R::Gold:
              chosen = r == R::Bronze ? Course::Bronze : r == R::Silver ? Course::Silver : Course::Gold;
              course_chosen = true;
              title.go_to(S::Done);
              break;
            case R::PsjHiscores: title.go_to(S::PsjHiscore); break;
            case R::PsjBack: title.go_to(S::Main); break;
            case R::OptionBack: title.go_to(S::Main); break;
            case R::HiscoreBack: title.go_to(S::PsjModeSelect); break;
            case R::ResultsDone: title.go_to(S::PsjModeSelect); break;
            default: break;
          }
          if (title.sound_effects != save.options.sound_effects || title.vibration != save.options.vibration) {
            save.options.sound_effects = title.sound_effects;
            save.options.vibration = title.vibration;
            write_save();
          }
          play_requests();  // sounds asked for by this frame and by the go_to() calls above
          music.update();
          voices.update();
          if (!running) break;
        }
        if (!running) break;
        if (course_chosen && title.done()) {
          music.stop();
          voices.stop();
          course_chosen = false;
          if (!play_course(chosen, 0)) break;
          show_scores();
          if (last_rank) {
            title.results_course = int(chosen);
            title.results_rank = last_rank;
            title.go_to(S::Results);
          } else {
            title.go_to(S::PsjModeSelect);
          }
          last_ns = SDL_GetTicksNS();
          behind = 0;
          continue;
        }
        // Draw the last frame the menus made.
        const auto& layers = title.layers();
        menu_lines.resize(layers.size());
        std::vector<vibr::Layer> out;
        for (std::size_t i = 0; i < layers.size(); ++i) {
          menu_lines[i].clear();
          for (const SceneLine& l : layers[i].lines)
            menu_lines[i].push_back({{l.a.x, l.a.y, l.a.z}, {l.b.x, l.b.y, l.b.z}, {l.colour.r / 255.0f, l.colour.g / 255.0f, l.colour.b / 255.0f, 1.0f}});
          const SceneView& v = layers[i].view;
          out.push_back({&menu_lines[i], vibr::look_at({v.eye.x, v.eye.y, v.eye.z}, {v.target.x, v.target.y, v.target.z}, {0, -1, 0}), v.fov_y});
        }
        renderer.draw(out, float(s.line_width * std::max(1, renderer.height()) / 288.0));
      }
    }
    write_save();
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
