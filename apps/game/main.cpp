// vibplay: Vib-Ribbon on a monitor, played from your own disc. The prototype of
// the game loop the VR builds will use: course generated eight seconds ahead of
// the music as in the original, rules from vibcore, presses judged at the moment
// they happened on the audio clock.
//
//   vibplay <disc.cue> [bronze|silver|gold | --song 1-6] [--fixed] [--smooth] [--seed N] [--last S] [--vr]
//
// --vr: in a headset through OpenXR (Meta Quest Link or SteamVR as the runtime).
// Each scene is seen from where the original's camera stands, around you;
// --vr-scale sets the metres per unit of the original's world (0.001).
//
// --last S (for testing the end of a course): every song starts S seconds before its
// end, a perfect player having played the rest, so the score and form are what a
// perfect run would have by then.
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
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "audio.h"
#include "line_renderer.h"
#include "music_file.h"
#ifdef VIB_VR
#include "xr_display.h"
#endif
#include "vib/course_gen.h"
#include "vib/course_script.h"
#include "vib/disc_image.h"
#include "vib/game_text.h"
#include "vib/gameplay.h"
#include "vib/iso9660.h"
#include "vib/known_discs.h"
#include "vib/save.h"
#include "vib/pak.h"
#include "vib/scene.h"
#include "vib/sound_bank.h"
#include "vib/title.h"
#include "vib/title_intro.h"

namespace {

using namespace vib;

constexpr std::int32_t kDelay = kLookAheadBlocks * kBlockSize;  // 8 s look-ahead, 22.05 kHz samples
constexpr double kOriginalRefresh = 49.759;                       // the PAL original's frame rate

// ---------------------------------------------------------------- sound effects

class Effects {
 public:
  // `bank`: the path of the bank's .VH and .VB without their extension.
  Effects(Iso9660& iso, vibg::Audio& audio, const std::string& bank = "GAME/AUDIO/PSJ_SE") : audio_(audio) {
    bank_ = parse_vh(iso.read(bank + ".VH"));
    const Bytes vb = iso.read(bank + ".VB");
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
  // Going: no voice may go on reading these sounds (a language change used to crash
  // here, its confirm sound still playing as the menus' sound banks were freed).
  ~Effects() { audio_.stop_using(samples_); }
  Effects(const Effects&) = delete;
  Effects& operator=(const Effects&) = delete;
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
  bool smooth = false;
  std::string music_file;     // one of the player's own MP3 or FLAC files, played as a one-track audio CD
  bool vr = false;            // in a headset (OpenXR)
  float vr_scale = 0.001f;    // metres per unit of the original's world in the headset
  double last_seconds = 0;  // > 0: start each song this long before its end (testing)
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
  vibr::Display& renderer;
  double line_width;
  std::map<SDL_JoystickID, SDL_Gamepad*> pads;
  std::map<SDL_JoystickID, std::uint8_t> triggers;  // which triggers are held, per pad
  Course course = Course::Bronze;  // being played
  int song = 0;                    // 1-6 when practising one song
  int final_score = 0, final_bonus = 0, final_form = 1;  // the last song cleared
  bool zooming = false, turning = false;                  // the sticks are moving the view (headset)
  // The player's own music, played as a one-track audio CD: its course follows the
  // original's audio-CD rules (the medley script, placed by the disc's key).
  const vibr::Music* music = nullptr;
  std::uint32_t music_key = 0;
  std::int32_t music_samples = 0;  // its whole seconds at 22,050 Hz, as the original counts a CD track
};

// In a headset, play is seen from down beside Vibri instead of from the original's
// camera (owner's decision): the road is one line in an upright plane, so seen from
// the side, as the original mostly shows it, it has almost no depth; looking along
// it from just behind her, it runs away in front of you. The view goes with her;
// `zoom` brings it closer (larger) or further away. Game units, y down.
SceneView vr_play_view(float vibri_z, float zoom, double angle_degrees) {
  // From Vibri: 1.2 m at zoom 1, at `angle` around her (0 behind, 90 to her right).
  const float distance = 1200.0f / zoom;
  const double a = angle_degrees * 3.14159265358979 / 180.0;
  const float side = distance * float(std::sin(a)), back = distance * float(std::cos(a));
  SceneView v;
  v.eye = {side, -250.0f, vibri_z - back};  // eyes a little above her head
  // Looking down the road from behind her, at her from the side or the front.
  v.target = {0.0f, -200.0f, vibri_z + 2000.0f * float(std::max(0.0, std::cos(a)))};
  v.fov_y = 1.0f;  // (the headset shows its own field of view)
  return v;
}

// In a headset: a gamepad's right stick held up zooms in (the view closer to Vibri),
// down zooms out, smoothly, twice as close or as far for each second at full tilt;
// the left stick left or right takes the view round her, 90 degrees a second.
// `zoom`: play's or the menus' own (each saved).
void zoom_with_stick(Session& s, double seconds, double& zoom) {
  if (!s.opt.vr) return;
  float y = 0, x = 0;
  for (const auto& [id, pad] : s.pads) {
    const float v = -float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY)) / 32767.0f;  // SDL: up is negative
    if (std::abs(v) > std::abs(y)) y = v;
    const float h = float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX)) / 32767.0f;
    if (std::abs(h) > std::abs(x)) x = h;
  }
  if (std::abs(x) > 0.2f) {
    const double t = (std::abs(x) - 0.2) / 0.8 * (x > 0 ? 1.0 : -1.0);
    double& a = s.save.options.vr_angle;
    a += 90.0 * t * std::min(seconds, 0.1);
    a = std::remainder(a, 360.0);  // kept within -180 to 180
    s.turning = true;
  } else if (s.turning) {
    s.turning = false;
    std::printf("View angle %.0f degrees\n", s.save.options.vr_angle);
  }
  if (std::abs(y) > 0.2f) {
    const double t = (std::abs(y) - 0.2) / 0.8 * (y > 0 ? 1.0 : -1.0);
    zoom = std::clamp(zoom * std::exp2(t * std::min(seconds, 0.1)), 1.0 / 24.0, 4.0);  // 4x closer to 24x further
    s.zooming = true;
  } else if (s.zooming) {
    s.zooming = false;
    std::printf("Zoom %.3f\n", zoom);
  }
}

// A gamepad trigger pulled past halfway (once until it is let go): the press it
// stands for in play, or in a headset, thicker lines (R2) or thinner (L2).
// Returns the press for play, 0 for none.
std::uint8_t trigger_pulled(Session& s, const SDL_GamepadAxisEvent& a);

// In a headset the lines jitter afresh every frame of its own (owner's choice),
// not at the original's 50 Hz: each end loses its frame's jitter and gets a new
// one, as wide, from its vertex and the display frame, so lines through the same
// vertex still meet.
Vec3f headset_jitter(Vec3f p, Vec3f old_jitter, std::uint32_t vertex, float shake, std::uint32_t frame) {
  const Vec3f base{p.x - old_jitter.x, p.y - old_jitter.y, p.z - old_jitter.z};
  if (shake <= 0 || vertex == 0) return base;
  std::uint32_t h = vertex * 0x9E3779B1u ^ (frame + 1) * 0x85EBCA77u;
  auto next = [&h] {
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return float(double(h) / 4294967296.0 * 2.0 - 1.0);  // [-1, 1)
  };
  const float dx = next(), dy = next(), dz = next();
  return {base.x + dx * shake, base.y + dy * shake, base.z + dz * shake};
}

// Thicker or thinner lines, saved for the display in use. In a headset each step is
// a quarter more or less, with the confirm click so it can be heard without a console.
void change_line_width(Session& s, bool thicker) {
  if (s.opt.vr) {
    s.line_width = std::clamp(thicker ? s.line_width * 1.25 : s.line_width / 1.25, 0.03, 4.0);
    s.save.options.vr_line_width = s.line_width;
    s.fx.number(1, 90, 5);
  } else {
    s.line_width = std::clamp(s.line_width + (thicker ? 0.125 : -0.125), 0.125, 4.0);
    s.save.options.line_width = s.line_width;
  }
  std::printf("%sline width %.3f (saved)\n", s.opt.vr ? "headset " : "", s.line_width);
}

std::uint8_t trigger_pulled(Session& s, const SDL_GamepadAxisEvent& a) {
  if (a.axis != SDL_GAMEPAD_AXIS_LEFT_TRIGGER && a.axis != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) return 0;
  const std::uint8_t bit = a.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 1 : 2;
  std::uint8_t& held = s.triggers[a.which];
  if (a.value > 16000 && !(held & bit)) {  // pulled past halfway
    held = std::uint8_t(held | bit);
    if (s.opt.vr) {
      change_line_width(s, bit == 2);
      return 0;
    }
    return bit == 1 ? std::uint8_t(kPressBlock) : std::uint8_t(kPressLoop);
  }
  if (a.value < 8000) held = std::uint8_t(held & ~bit);
  return 0;
}

Result play_song(Session& s, int song, int track_index, int& points, std::uint32_t seed) {
  // Timing: like the original, a little different on every play, unless --fixed.
  const CourseTiming timing = s.fixed ? CourseTiming::consistent() : CourseTiming::like_original(seed);
  const std::vector<std::int16_t> disc_pcm = s.music ? std::vector<std::int16_t>{} : s.disc.read_audio_track(song + 1);
  const std::vector<std::int16_t>& pcm = s.music ? s.music->pcm : disc_pcm;
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

  // Testing: start the song this far in (--last), the music from there and no 8 s wait.
  std::int32_t skip = 0;
  if (s.opt.last_seconds > 0) {
    const auto last = static_cast<std::int32_t>(s.opt.last_seconds * kSampleRate);
    skip = std::max<std::int32_t>(0, total - last) / kBlockSize * kBlockSize;
  }
  // Where the music buffer starts, in heard samples from the song's start.
  const std::int32_t buffer_start = skip > 0 ? skip : -kDelay;
  if (skip > 0) {
    const std::size_t cut = std::min(out.size(), std::size_t(kDelay + skip) * 4);  // 2 frames of stereo per heard sample
    out.erase(out.begin(), out.begin() + std::ptrdiff_t(cut));
  }

  CourseGenerator gen(s.script);
  gen.set_timing(timing);
  // Starts a generator on this song: the game's own round, or the player's track on its "disc".
  auto start = [&](CourseGenerator& g) {
    if (s.music) {
      g.prepare_disc({s.music_samples}, s.music_key);
      g.start_track(1, 0);
    } else {
      g.start_round(song, 0);
    }
  };
  start(gen);
  Gameplay game;
  RoundSetup setup;
  setup.course = s.music ? Course::OneTrack : s.song ? Course::Bronze : s.course;
  setup.course_seconds = total / kSampleRate;
  setup.points = points;
  setup.track_index = track_index;
  game.start(setup);
  // In a headset the whole line is in view: passed parts stay on it for 8 s, and
  // every part built so far (8 s ahead) shows ahead.
  if (s.opt.vr) game.keep_passed(static_cast<std::int32_t>(s.save.options.vr_behind_seconds * kSampleRate));
  // Further ahead than play builds: a second generator on the same music and timing,
  // running ahead, drawn only. Its course is the same course (it is checked below),
  // so its parts become play's own as play reaches them.
  CourseGenerator far(s.script);
  far.set_timing(timing);
  start(far);
  std::size_t far_block = 0;
  const auto far_extra = static_cast<std::int32_t>(std::max(0.0, s.save.options.vr_ahead_seconds - 8.0) * kSampleRate);
  bool far_warned = false;
  s.scene.reset(true, false);
  s.hud.start_round(track_index, !s.song && !s.music);  // the "Round 1/2" sign belongs to the two-song courses

  std::printf("\nSong %d: %d s, %s course (lead %d, frame phase %.0f)\n", song, total / kSampleRate,
              s.fixed ? "fixed" : "original-style", timing.lead, double(timing.frame_phase_q16) / 65536.0);

  std::size_t next_block = 0, fed = 0;
  bool was_selecting = gen.selecting(), ended = false;
  // The course is analysed eight seconds ahead of what is heard.
  auto analyse_up_to = [&](std::int32_t heard) {
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
  };
  if (skip > 0) {
    // A perfect player plays up to the starting point: each part pressed as it arrives.
    for (std::int32_t at = -kDelay; at < skip; at = std::min(at + kBlockSize, skip)) {
      analyse_up_to(at);
      const Part* first = nullptr;
      for (const Part& p : game.parts())
        if (!p.done && (!first || p.time < first->time)) first = &p;
      game.update(at, first && first->time <= at ? road_presses(first->road) : 0, true);
    }
    analyse_up_to(skip);
    game.update(skip, 0, true);
    std::printf("Starting %d s from the end, score %d so far (--last).\n", (total - skip) / kSampleRate, game.score());
  }
  s.audio.play_music(std::move(out));
  s.fx.play(0, 11, 63, 4);  // the round's opening sound (the original plays it whatever the option)

  std::int32_t last_position = skip > 0 ? skip : -kDelay;
  std::uint64_t last_ns = SDL_GetTicksNS();
  const std::uint64_t start_ns = last_ns;
  double title_timer = 0;
  std::vector<PressEvent> presses;
  std::vector<SceneLine> scene_lines;
  std::uint32_t display_frame = 0;
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
        // Audio CDs: no obstacles in seconds 52 to 59 of every minute.
        case GameEvent::Type::SelectOff: gen.set_select(false); break;
        case GameEvent::Type::SelectOn: gen.set_select(true); break;
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
            change_line_width(s, e.key.key == SDLK_EQUALS);
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
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) s.renderer.recentre();  // R3
          if (const std::uint8_t b = pad_bits(e.gbutton.button)) presses.push_back({b, e.gbutton.timestamp});
          break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
          // (In a headset the triggers change the line width for testing; LB and RB block and loop.)
          if (const std::uint8_t b = trigger_pulled(s, e.gaxis)) presses.push_back({b, e.gaxis.timestamp});
          break;
        default: break;
      }
    }

    const std::uint64_t now_ns = SDL_GetTicksNS();
    const double dt = double(now_ns - last_ns) * 1e-9;
    zoom_with_stick(s, dt, s.save.options.vr_zoom);
    last_ns = now_ns;
    // Heard position, 22.05 kHz samples from the song's start (negative during the 8 s ahead).
    auto heard_at = [&](std::uint64_t ns) {
      return static_cast<std::int32_t>(std::floor(s.audio.heard_frame_at(now_ns, ns) / 2.0)) + buffer_start;
    };
    const std::int32_t heard = std::max(heard_at(now_ns), last_position);

    analyse_up_to(heard);

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
    f.road = game.road(s.opt.vr);
    if (s.opt.vr && far_extra > 0) {
      while (far_block < blocks.size() && std::int32_t(far_block * kBlockSize) <= heard + kDelay + far_extra) {
        const auto t = static_cast<std::int32_t>(far_block * kBlockSize);
        if (s.music) {
          // The break as play will have it when this block is heard (play switches it as the music is heard).
          const int seconds = (t - kDelay) / kSampleRate;
          far.set_select((seconds + setup.stream_delay_seconds) % 60 < 52 || seconds + setup.stream_delay_seconds < 0);
        }
        far.process(t, blocks[far_block]);
        ++far_block;
      }
      const auto& ahead = far.events();
      // (With the player's music the breaks can differ by a part at their edges: play's decides.)
      if (!far_warned && !s.music && fed > 0 && fed <= ahead.size()) {
        const CourseEvent &a = ahead[fed - 1], &b = gen.events()[fed - 1];
        if (a.type != b.type || a.time != b.time || a.value != b.value || a.length != b.length) {
          std::printf("Warning: the course drawn ahead differs from play's own (from event %zu); report this.\n", fed - 1);
          far_warned = true;
        }
      }
      RoadPiece piece;
      for (std::size_t i = fed; i < ahead.size(); ++i)
        if (game.preview(ahead[i], heard, piece)) f.road.push_back(piece);
      std::stable_sort(f.road.begin(), f.road.end(), [](const RoadPiece& a, const RoadPiece& b) { return a.position < b.position; });
    }
    f.still = game.near_part();
    f.shake = game.shake();
    f.form = game.form();
    f.stage = game.stage();
    SceneView view;
    s.scene.draw(f, scene_lines, view);
    if (s.opt.vr) view = vr_play_view(s.scene.vibri_z(), float(s.save.options.vr_zoom), s.save.options.vr_angle);
    ++display_frame;
    auto convert = [&](const std::vector<SceneLine>& in, std::vector<vibr::LineInstance>& dst) {
      dst.clear();
      for (const SceneLine& l : in) {
        Vec3f a = l.a, b = l.b;
        if (s.opt.vr) {  // jittered afresh every headset frame
          a = headset_jitter(l.a, l.jitter_a, l.vertex_a, l.shake_a, display_frame);
          b = headset_jitter(l.b, l.jitter_b, l.vertex_b, l.shake_b, display_frame);
        }
        dst.push_back({{a.x, a.y, a.z}, {b.x, b.y, b.z}, {l.colour.r / 255.0f, l.colour.g / 255.0f, l.colour.b / 255.0f, 1.0f}});
      }
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
      s.final_score = game.score();
      s.final_bonus = game.bonus();
      s.final_form = game.form();
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

// After the language changes the game starts again in it, at the title's mode menu.
constexpr int kRestart = -100;
bool g_restart_at_title = false;

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
    else if (a == "--smooth") opt.smooth = true;
    else if (a == "--vr") opt.vr = true;
    else if (a == "--music") opt.music_file = value();
    else if (a == "--vr-scale") opt.vr_scale = std::clamp(float(std::atof(value())), 0.0001f, 0.1f);
    else if (a == "--last") opt.last_seconds = std::max(1.0, std::atof(value()));
    else if (a == "--seed") opt.seed = std::uint32_t(std::strtoul(value(), nullptr, 10)), opt.seed_given = true;
    else if (a == "--offset") opt.offset_ms = std::atof(value());
    else if (a == "--sfx") opt.sfx = float(std::atof(value()));
    else if (a == "--music") opt.music = float(std::atof(value()));
    else if (a == "--width") opt.line_width = std::atof(value());
    else if (opt.cue.empty() && a.size() > 0 && a[0] != '-') opt.cue = a;
    else throw std::runtime_error("unknown argument: " + a);
  }
  if (opt.cue.empty()) {
    std::fputs("usage: vibplay <disc.cue> [bronze|silver|gold | --song 1-6] [--fixed] [--smooth] [--seed N]\n"
               "               [--music file.mp3|file.flac] [--vr] [--vr-scale metres] [--last seconds] [--offset ms] [--sfx 0-1] [--music 0-1] [--width x]\n",
               stderr);
    return 2;
  }
  if (!opt.seed_given) opt.seed = static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());

  DiscImage disc = DiscImage::open(path_from_utf8(opt.cue));
  Iso9660 iso(disc);
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
  // In a headset the window only takes the keyboard (keep it in front) and shows nothing.
  SDL_Window* window = opt.vr ? SDL_CreateWindow("Vib-Ribbon VR: look in the headset (keep this window in front for the keyboard)", 640, 120, 0)
                              : SDL_CreateWindow("Vib-Ribbon", 1200, 900, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
  // The language's own archive: GAME/<its bit>_FILES.PAK (04 German, 02 English,
  // 08 Spanish, 10 French, 20 Italian), as the original names them.
  const int language = save.options.language < 0 ? GameText::kEnglish : save.options.language;
  const Pak pak(iso.read("GAME/" + language_prefix(language) + "FILES.PAK"));
  const CourseScript script = parse_course_script(pak.view("SCRIPT/SYSTEM3.FSL"));
  Scene scene(pak);
  Hud hud(pak, language);
  auto write_save = [&]() {
    if (save_path.empty()) return;
    std::ofstream out(path_from_utf8(save_path), std::ios::binary);
    out << save.to_text();
  };

  int code = 0;
  bool restart = false;
  {
    std::unique_ptr<vibr::Display> display;
    if (opt.vr) {
#ifdef VIB_VR
      auto xr = std::make_unique<vibr::XrDisplay>(opt.vr_scale);
      std::printf("Headset runtime: %s (one unit = %.4f m)\n", xr->runtime_name(), double(opt.vr_scale));
      display = std::move(xr);
#else
      throw std::runtime_error("this build has no headset support (VIB_VR is off)");
#endif
    } else {
      display = std::make_unique<vibr::Renderer>(window);
    }
    vibr::Display& renderer = *display;
    vibg::Audio audio(opt.offset_ms.value_or(save.options.audio_offset_ms));
    audio.set_volumes(opt.music.value_or(save.options.music_volume), opt.sfx.value_or(save.options.effects_volume));
    Effects fx(iso, audio);
    const bool fixed = opt.fixed || save.options.fixed_courses;
    std::printf("GPU: %s\nCourses: %s\n", renderer.gpu_name(), fixed ? "fixed (the same every play)" : "vary from play to play, like the original");
    // Smooth animation is always on in a headset (owner's decision): stepped poses look far worse up close.
    Clip::set_smooth(opt.smooth || save.options.smooth_animation || opt.vr);
    std::printf("Animation: %s\n", Clip::smooth() ? "smooth (poses blended)" : "stepped, like the original");
    Session s{opt, save, fixed, disc, script, scene, hud, audio, fx, window, renderer,
              opt.line_width.value_or(opt.vr ? save.options.vr_line_width : save.options.line_width), {}, {}};
    std::uint32_t seed = opt.seed;
    int last_rank = 0, last_score = 0, lost_song = 0, points_before_lost = 0;
    bool last_lost = false, last_left = false, last_cleared = false;  // game over; left with Start or Esc; cleared
    int last_song_score = 0, last_song_bonus = 0, last_form = 1;

    // A course from song `first` (0 or 1) to its end, or one song as practice.
    // Returns false if the window was closed.
    auto play_course = [&](Course course, int song, int first = 0, int carried = 0) -> bool {
      s.course = course;
      s.song = song;
      std::vector<int> songs;
      if (song) songs = {song};
      else songs = {int(course) * 2 + 1, int(course) * 2 + 2};
      int points = carried;
      bool finished = false;  // the course ended, cleared or lost, rather than being left
      last_lost = last_left = last_cleared = false;
      for (std::size_t i = std::size_t(first); i < songs.size(); ++i) {
        const int before = points;
        const Result r = play_song(s, songs[i], int(i), points, seed);
        seed = next_random(seed);
        if (r == Result::Quit) return false;
        if (r == Result::Abort) {
          // As the original: no pause; Start ends the round and asks "again?".
          last_left = true;
          lost_song = int(i);
          points_before_lost = before;
          break;
        }
        if (r == Result::GameOver) {
          std::printf("Game over.\n");
          finished = true;
          last_lost = true;
          lost_song = int(i);
          points_before_lost = before;
          break;
        }
        if (i + 1 == songs.size()) {
          std::printf("Course clear! Total %d\n", points);
          finished = true;
          last_cleared = true;
          last_song_score = s.final_score;
          last_song_bonus = s.final_bonus;
          last_form = s.final_form;
        }
      }
      // Whatever happened, the song stops here: leaving a course early must not leave
      // its music playing under the menus.
      s.audio.play_music({});
      last_rank = 0;
      last_score = points;
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

    std::optional<vibr::Music> own_music;
    if (!opt.music_file.empty()) {
      // One of the player's own files, as a one-track audio CD. Its table of contents
      // (the track from sector 150, its length rounded up to whole sectors) gives the
      // disc's key, so the same file always gives the same course.
      own_music = vibr::load_music(opt.music_file);
      const auto frames = std::uint32_t(own_music->pcm.size() / 2);
      const std::uint32_t sectors = (frames + 587) / 588;  // 588 frames a sector at 75 a second
      s.music = &*own_music;
      s.music_key = disc_key(150 + sectors, 1);
      s.music_samples = std::int32_t(sectors / 75) * kSampleRate;
      std::printf("Music: %s (%u Hz, %u channels, %u:%02u)\n", opt.music_file.c_str(), own_music->source_rate, own_music->source_channels,
                  sectors / 75 / 60, sectors / 75 % 60);
      int points = 0;
      const Result r = play_song(s, 0, 0, points, seed ? seed : std::uint32_t(SDL_GetTicksNS()));
      std::printf("%s, score %d\n", r == Result::Clear ? "Cleared" : r == Result::GameOver ? "Game over" : "Left", s.final_score);
    } else if (opt.song || opt.course_given) {
      play_course(opt.course, opt.song);  // straight into play, as before
    } else {
      // The menus, run at the original's 50 frames a second whatever the display does.
      TitleScreens title(pak, language);
      // The screens' words come from the game's own program on the disc.
      std::optional<GameText> words;
      try {
        words = GameText::from_exe(iso.read("MAIN_G.EXE"));
        title.set_text(&*words, language);
      } catch (const std::exception& e) {
        std::printf("The results screens' words could not be read (%s); they are left out.\n", e.what());
      }
      using S = TitleScreens::Screen;
      using R = TitleScreens::Result;
      title.sound_effects = save.options.sound_effects;
      title.vibration = save.options.vibration;
      auto show_scores = [&]() {
        for (std::size_t c = 0; c < 3; ++c) title.psj_high_scores[c] = save.high_scores[c];
      };
      show_scores();
      // First the title (the original's MAIN_T): its intro, then the title waiting for a
      // button, which leads to the main menu.
      const Pak title_pak(iso.read("TITLE/FILES.PAK"));
      TitleIntro intro(title_pak, language);
      Effects title_fx(iso, audio, "TITLE/AUDIO/TITLE");
      constexpr int kTitleVoice = 2;  // the title's voices 0, 1 and 5 play on 2, 3 and 7
      bool at_title = true, menus_shown = false;
      if (g_restart_at_title) {  // back after a change of language: straight to the title's mode menu
        g_restart_at_title = false;
        intro.start(true);
      }
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
      std::vector<Hud::Layer> prev_layers;
      std::uint32_t menu_display_frame = 0;
      std::uint64_t last_ns = SDL_GetTicksNS();
      double behind = 0;
      bool course_chosen = false, running = true, replay = false;
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
                case SDLK_MINUS: case SDLK_EQUALS:
                  if (down) {
                    change_line_width(s, e.key.key == SDLK_EQUALS);
                    write_save();
                  }
                  break;
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
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
              if (opt.vr) {  // the line width, in the menus too
                const double before = s.line_width;
                trigger_pulled(s, e.gaxis);
                if (s.line_width != before) write_save();
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
                case SDL_GAMEPAD_BUTTON_RIGHT_STICK: if (down) renderer.recentre(); break;  // R3
                default: break;
              }
              break;
            }
            default: break;
          }
        }
        if (!running) break;
        const std::uint64_t now_ns = SDL_GetTicksNS();
        zoom_with_stick(s, double(now_ns - last_ns) * 1e-9, s.save.options.vr_menu_zoom);
        behind = std::min(behind + double(now_ns - last_ns) * 1e-9, 0.1);
        last_ns = now_ns;
        while (behind >= 1.0 / TitleScreens::kFrameRate) {
          behind -= 1.0 / TitleScreens::kFrameRate;
          prev_layers = at_title ? intro.layers() : title.layers();  // for blending between this frame and the next
          TitleScreens::Input in = pressed;
          in.held_up = held.up, in.held_down = held.down, in.held_left = held.left, in.held_right = held.right;
          pressed = {};
          in.voices_playing = voices.playing();
          in.music_playing = music.playing();
          if (at_title) {
            TitleIntro::Input ti;
            ti.up = in.up, ti.down = in.down, ti.left = in.left, ti.right = in.right;
            ti.confirm = in.confirm, ti.cancel = in.cancel;
            ti.confirm_sound_playing = title_fx.is_playing(kTitleVoice + 1);
            intro.tick(ti);
            for (const TitleIntro::Sound& snd : intro.take_sounds()) title_fx.number(snd.number, 64, kTitleVoice + snd.voice);
            using C = TitleIntro::Choice;
            using M = TitleIntro::Menu;
            if (!menus_shown && intro.intro_over()) {
              // The language menu the first time (nothing chosen yet), else play / demo / back.
              menus_shown = true;
              intro.set_language_cursor(language);
              intro.show(save.options.language < 0 ? M::Language : M::Mode);
            }
            switch (intro.choice()) {
              case C::German: case C::English: case C::Spanish: case C::French: case C::Italian: {
                const int chosen_language = int(intro.choice()) - int(C::German);
                save.options.language = chosen_language;
                write_save();
                if (chosen_language != language) {
                  // Everything in the new language: start again at the title's mode menu.
                  g_restart_at_title = true;
                  restart = true;
                  running = false;
                } else {
                  intro.show(M::Mode);
                }
                break;
              }
              case C::Back: intro.show(M::Language); break;
              case C::Demo:
                std::printf("The demo comes later.\n");
                intro.show(M::Mode);
                break;
              case C::Play:
                intro.show(M::Start);
                intro.leave();
                break;
              case C::Started:
                at_title = false;
                for (int v : {0, 1, 5}) title_fx.stop(kTitleVoice + v);
                title.go_to(S::Main);  // its lines scatter in, as the original's first screen
                break;
              default: break;
            }
            if (!running) break;
            continue;
          }
          const R r = title.tick(in);
          switch (r) {
            case R::PsjModes: title.go_to(S::PsjModeSelect); break;
            case R::UnknownModes: std::printf("Courses from your own music come later.\n"); break;
            case R::Options: title.go_to(S::Option); break;
            // The original's "leave" goes back to its title screen (close the window to quit).
            case R::Leave:
              music.stop();
              voices.stop();
              title.go_to(S::None);
              intro.start(true);
              intro.close_menus();  // the mode menu again, gathering in
              at_title = true;
              break;
            case R::Bronze: case R::Silver: case R::Gold:
              chosen = r == R::Bronze ? Course::Bronze : r == R::Silver ? Course::Silver : Course::Gold;
              course_chosen = true;
              title.go_to(S::Done);
              break;
            case R::PsjHiscores: title.go_to(S::PsjHiscore); break;
            case R::PsjBack: title.go_to(S::Main); break;
            case R::OptionBack: title.go_to(S::Main); break;
            case R::HiscoreBack: title.go_to(S::PsjModeSelect); break;
            case R::ResultsDone: title.go_to(title.screen() == S::Again ? S::AgainSelect : S::PsjModeSelect); break;
            case R::PlayAgain:  // the original's own courses start again from the song that was lost
              replay = true;
              course_chosen = true;
              title.go_to(S::Done);
              break;
            case R::AgainBack: title.go_to(S::PsjModeSelect); break;
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
          const bool ok = replay ? play_course(chosen, 0, lost_song, points_before_lost) : play_course(chosen, 0);
          replay = false;
          if (!ok) break;
          show_scores();
          title.results_course = int(chosen);
          title.results_rank = last_rank;
          title.results_score = std::min(last_cleared ? last_song_score : last_score, Hud::kMaxScore);
          title.results_bonus = last_cleared ? last_song_bonus : 0;
          title.results_form = last_form;
          if (last_lost) title.go_to(S::Again);                // Vibri's reaction, then "again?"
          else if (last_left) title.go_to(S::AgainSelect);     // Start during play: straight to "again?"
          else if (last_cleared) title.go_to(S::Clear);       // the score counted, the comment or the medal, the high scores
          else title.go_to(S::PsjModeSelect);
          last_ns = SDL_GetTicksNS();
          behind = 0;
          continue;
        }
        // Draw the last frame the title or the menus made.
        const auto& layers = at_title ? intro.layers() : title.layers();
        menu_lines.resize(layers.size());
        std::vector<vibr::Layer> out;
        // With smooth animation the menus move at the display's rate, not only the
        // original's 50 Hz: each line's place is blended from the previous frame's to
        // this one's (shown a frame late), its jitter still this frame's, as play's is.
        // A layer whose lines changed (another model, another screen) is shown as it is.
        const bool blend = Clip::smooth() && prev_layers.size() == layers.size();
        ++menu_display_frame;
        const float alpha = float(std::clamp(behind * TitleScreens::kFrameRate, 0.0, 1.0));
        auto mix = [&](Vec3f now, Vec3f now_jitter, Vec3f before, Vec3f before_jitter) {
          const Vec3f n{now.x - now_jitter.x, now.y - now_jitter.y, now.z - now_jitter.z};
          const Vec3f b{before.x - before_jitter.x, before.y - before_jitter.y, before.z - before_jitter.z};
          if (std::abs(n.x - b.x) + std::abs(n.y - b.y) + std::abs(n.z - b.z) > 600.0f) return now;  // a jump, not a move
          return Vec3f{b.x + (n.x - b.x) * alpha + now_jitter.x, b.y + (n.y - b.y) * alpha + now_jitter.y,
                       b.z + (n.z - b.z) * alpha + now_jitter.z};
        };
        for (std::size_t i = 0; i < layers.size(); ++i) {
          menu_lines[i].clear();
          const Hud::Layer* before = blend && prev_layers[i].lines.size() == layers[i].lines.size() ? &prev_layers[i] : nullptr;
          for (std::size_t k = 0; k < layers[i].lines.size(); ++k) {
            const SceneLine& l = layers[i].lines[k];
            Vec3f a = l.a, b = l.b;
            if (before) {
              const SceneLine& p = before->lines[k];
              a = mix(l.a, l.jitter_a, p.a, p.jitter_a);
              b = mix(l.b, l.jitter_b, p.b, p.jitter_b);
            }
            if (opt.vr) {  // jittered afresh every headset frame (the blend kept this frame's jitter in a and b)
              a = headset_jitter(a, l.jitter_a, l.vertex_a, l.shake_a, menu_display_frame);
              b = headset_jitter(b, l.jitter_b, l.vertex_b, l.shake_b, menu_display_frame);
            }
            menu_lines[i].push_back({{a.x, a.y, a.z}, {b.x, b.y, b.z}, {l.colour.r / 255.0f, l.colour.g / 255.0f, l.colour.b / 255.0f, 1.0f}});
          }
          SceneView v = layers[i].view;
          if (blend) {  // the camera too
            const SceneView& p = prev_layers[i].view;
            v.eye = mix(v.eye, {}, p.eye, {});
            v.target = mix(v.target, {}, p.target, {});
            v.fov_y = p.fov_y + (v.fov_y - p.fov_y) * alpha;
          }
          if (opt.vr) {  // zoom: each camera moved along its line of sight towards what it looks at
            const float k = 1.0f / float(save.options.vr_menu_zoom);
            v.eye = {v.target.x + (v.eye.x - v.target.x) * k, v.target.y + (v.eye.y - v.target.y) * k,
                     v.target.z + (v.eye.z - v.target.z) * k};
          }
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
  return restart ? kRestart : code;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    int code;
    do code = run(argc, argv);
    while (code == kRestart);
    return code;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "vibplay: %s\n", e.what());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "vibplay", e.what(), nullptr);  // so a failure is never just a closed window
    return 1;
  }
}
