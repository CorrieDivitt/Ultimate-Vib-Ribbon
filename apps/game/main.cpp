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
//   L3 (left stick click) or F3: the performance overlay, off / one line / detailed
//   F11 or Alt+Enter: a window or fullscreen (monitor; the settings' Display group has more)
//   With Visual > Camera beside Vibri: right stick zoom, left stick round her (monitor too)
//
// High scores and options (sound effects, vibration, fixed courses, line width,
// audio offset, volumes) live in save.txt in the per-user folder SDL picks, printed
// at start. Offset and width changes made while playing are saved there. Options on
// the command line apply to that run only.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <future>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#include <unistd.h>
#endif

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
#include "vib/kiosk_movie.h"
#include "vib/calibration.h"
#include "vib/colours.h"
#include "vib/course_wrap.h"
#include "vib/settings_panel.h"
#include "vib/perf_overlay.h"
#include "vib/own_cd.h"
#include "vib/file_picker.h"

namespace {

#ifdef __ANDROID__
// On the headset (Quest standalone build): the app's own folder on the shared
// storage, /sdcard/Android/data/<package>/files. adb can push into it and no
// storage permission is needed. The disc, save.txt and music live there.
std::string headset_folder() {
  const char* p = SDL_GetAndroidExternalStoragePath();
  if (!p) throw std::runtime_error(std::string("no app folder on the headset's storage: ") + SDL_GetError());
  return p;
}

// printf and stderr go nowhere on Android: send them to logcat (adb logcat -s vibplay).
void log_to_logcat() {
  static int fds[2];
  if (pipe(fds) != 0) return;
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  dup2(fds[1], 1);
  dup2(fds[1], 2);
  std::thread([] {
    char buf[1024];
    std::string line;
    for (ssize_t n; (n = read(fds[0], buf, sizeof buf)) > 0;)
      for (ssize_t i = 0; i < n; ++i)
        if (buf[i] == '\n') {
          __android_log_write(ANDROID_LOG_INFO, "vibplay", line.c_str());
          line.clear();
        } else {
          line += buf[i];
        }
  }).detach();
}

// The disc image pushed to the headset: the first .cue (by path) anywhere in the
// app's folder, a few folders deep (files/disc/ is where the tracker's command puts it).
std::string find_disc_on_headset() {
  const std::filesystem::path root = std::filesystem::path(headset_folder());
  std::vector<std::filesystem::path> cues;
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (it.depth() > 3) {
      it.disable_recursion_pending();
      continue;
    }
    std::string ext = it->path().extension().string();
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".cue") cues.push_back(it->path());
  }
  if (cues.empty()) {
    // Say what could be seen: a folder adb made before the game was installed can
    // belong to adb, and then the game may not read it.
    std::string seen;
    std::error_code ec2;
    for (std::filesystem::directory_iterator it(root / "disc", ec2), end; !ec2 && it != end; it.increment(ec2))
      seen += " " + it->path().filename().string();
    throw std::runtime_error("no disc image on the headset: push your .cue and .bin files into " + (root / "disc").string() +
                             (ec ? " (reading the app folder: " + ec.message() + ")" : std::string()) +
                             (ec2 ? " (reading disc: " + ec2.message() + ")" : " (disc holds:" + (seen.empty() ? std::string(" nothing") : seen) + ")"));
  }
  std::sort(cues.begin(), cues.end());
  std::printf("Disc: %s\n", cues.front().string().c_str());
  return cues.front().string();
}
#endif

using namespace vib;

constexpr std::int32_t kDelay = kLookAheadBlocks * kBlockSize;  // 8 s look-ahead, 22.05 kHz samples
constexpr double kOriginalRefresh = 49.759;                       // the PAL original's frame rate
// In a headset, passed parts slower than this (part lengths a second) speed up to it
// once cleared, so they keep drifting away from Vibri instead of seeming to stay with
// her. Owner's choice, fixed (not a setting): it catches the near-stationary parts of
// songs 3, 5 and 6 (down to 0.93); songs 1, 2 and 4 never go below 1.44.
constexpr double kTrailMinSpeed = 1.35;

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

// In a headset the opening (the title's intro and its menus) is seen 1 m further
// forward (owner's choice): each camera moved 1 m along its line of sight, never past
// what it looks at. The headset draws narrow cameras (the intro's first shot is a 3
// degree telephoto) with their depth squeezed by tan(fov/2) / tan(12.5 degrees) (see
// depth_for in xr_display.cpp), so the move in game units is 1 m divided by that.
SceneView opening_forward(SceneView v) {
  constexpr float kMetres = 1.0f, kMetresPerUnit = 0.001f;
  constexpr float kNarrowest = 25.0f * 3.14159265f / 180.0f;
  const float squeeze = std::clamp(std::tan(0.5f * v.fov_y) / std::tan(0.5f * kNarrowest), 0.02f, 1.0f);
  const float dx = v.target.x - v.eye.x, dy = v.target.y - v.eye.y, dz = v.target.z - v.eye.z;
  const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (dist < 1.0f) return v;
  const float move = std::min(kMetres / kMetresPerUnit / squeeze, dist * 0.9f);
  v.eye = {v.eye.x + dx / dist * move, v.eye.y + dy / dist * move, v.eye.z + dz / dist * move};
  return v;
}

// ---------------------------------------------------------------- the player's music files

// UTF-8 for a path (std::filesystem gives char8_t).
std::string utf8_of(const std::filesystem::path& p) {
  const auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

bool is_music_file(const std::filesystem::path& p) {
  std::string ext = utf8_of(p.extension());
  for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
  return ext == ".mp3" || ext == ".flac";
}

#ifdef __ANDROID__
// The headset's own Music folder (owner, S.2): with the storage permission the game reads
// it directly (files copied over USB from Windows' Explorer, no adb). Asked for the first
// time the music picker opens; -1 not asked, 0 asking, 1 granted, 2 refused.
std::atomic<int> g_music_permission{-1};
std::atomic<bool> g_music_permission_answered{false};
const char* const kSharedMusic = "/storage/emulated/0/Music";

void ask_music_permission() {
  int expected = -1;
  if (!g_music_permission.compare_exchange_strong(expected, 0)) return;
  SDL_RequestAndroidPermission(
      "android.permission.READ_EXTERNAL_STORAGE",
      [](void*, const char*, bool granted) {
        g_music_permission = granted ? 1 : 2;
        g_music_permission_answered = true;
        std::printf("Music folder permission: %s\n", granted ? "granted" : "refused (the game's own music folder only)");
      },
      nullptr);
}

// The game's own music folder beside the disc (adb push works there with no permission).
std::string headset_music_folder() {
  const std::filesystem::path music = std::filesystem::path(headset_folder()) / "music";
  std::error_code ec;
  std::filesystem::create_directories(music, ec);
  return music.string();
}
#endif

// Where the folder browser starts the first time: the system's music folder, else home.
std::string default_music_folder() {
#ifdef __ANDROID__
  {  // the headset: its own Music folder if the game may read it, else the game's own one
    std::error_code ec;
    if (g_music_permission == 1 && std::filesystem::is_directory(kSharedMusic, ec)) return kSharedMusic;
    return headset_music_folder();
  }
#endif
  if (const char* music = SDL_GetUserFolder(SDL_FOLDER_MUSIC)) return music;
  if (const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME)) return home;
  return {};
}

// The folder above `utf8`; "" (the computer's drives, on Windows) above a drive's root.
std::string parent_folder(const std::string& utf8) {
  const std::filesystem::path p = path_from_utf8(utf8);
  const std::filesystem::path up = p.has_relative_path() ? p.parent_path() : p;
  if (up == p || !p.has_relative_path()) return {};
  return utf8_of(up);
}

// What a folder holds, for the picker: its folders, then its music files, each in
// track order (hidden ones left out). "" lists the drives (Windows).
std::vector<FilePicker::Entry> list_folder(const std::string& utf8) {
  std::vector<FilePicker::Entry> folders, files;
  std::error_code ec;
  if (utf8.empty()) {
#ifdef _WIN32
    for (char d = 'A'; d <= 'Z'; ++d) {
      const std::string root = std::string(1, d) + ":/";
      if (std::filesystem::exists(root, ec)) folders.push_back({root, true});
    }
#else
    folders.push_back({"/", true});
#endif
    return folders;
  }
  for (std::filesystem::directory_iterator it(path_from_utf8(utf8), std::filesystem::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    const std::string name = utf8_of(it->path().filename());
    if (name.empty() || name[0] == '.') continue;
    std::error_code ec2;
    if (it->is_directory(ec2)) folders.push_back({name, true});
    else if (is_music_file(it->path())) files.push_back({name, false});
  }
  auto order = [](const FilePicker::Entry& a, const FilePicker::Entry& b) { return track_name_less(a.name, b.name); };
  std::sort(folders.begin(), folders.end(), order);
  std::sort(files.begin(), files.end(), order);
  folders.insert(folders.end(), files.begin(), files.end());
  return folders;
}

// A folder inside `parent`: "" (the drives) holds whole roots.
std::string child_folder(const std::string& parent, const std::string& name) {
  if (parent.empty()) return name;
  return utf8_of(path_from_utf8(parent) / path_from_utf8(name));
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
  bool vr = false;            // drawn for a headset: our staging round Vibri (off with the classic screen)
  bool headset = false;       // in a headset at all (OpenXR); `vr` is this unless the classic screen is on
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
  Args& opt;  // (opt.vr follows the Classic screen setting: see apply_settings)
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
  bool final_time_up = false;  // a speed course's music ran out before the top speed
  // The full course combo: how many songs this run of the course has, and whether every
  // song so far was cleared with no miss (play_song keeps it; its first song sets it).
  int course_songs = 1;
  bool course_clean = false;
  bool zooming = false, turning = false;                  // the sticks are moving the view (headset)
  bool stick_flicked = false;
  // The last frame's layers, kept in a headset so slow work (preparing a song) can go on
  // a worker thread while that frame is drawn again at the headset's rate (with_frames).
  std::vector<std::vector<vibr::LineInstance>> held_lines;
  std::vector<vibr::Layer> held_layers;
  bool holding = false;
  std::vector<vibr::LineInstance> classic_lines;  // the classic screen's picture (headset)  // the headset controllers' right stick pushed sideways (scroll speed), until it comes back
  bool demo = false;  // the title's demo: the game plays itself (missing now and then), any button ends it
  // The player's own music, played as a one-track audio CD: its course follows the
  // original's audio-CD rules (the medley script, placed by the disc's key).
  const vibr::Music* music = nullptr;
  std::uint32_t music_key = 0;
  std::vector<std::int32_t> music_disc;  // every track's whole seconds at 22,050 Hz, as the original counts CD tracks
  int music_track = 1;                   // which of them `music` is (from 1)
  SettingsPanel* panel = nullptr;      // our settings: the pause, and below the Options screen
  std::function<void()> write_save;    // keeps the options now
  std::vector<std::vector<vibr::LineInstance>> glow_lines;  // the glow passes' copies, a frame's worth
  // The performance overlay (ours, task 4.6): every frame measured, shown when on.
  PerfOverlay* perf = nullptr;
  FrameStats stats;
  std::vector<std::string> perf_text;  // its rows, rewritten 4 times a second so they can be read
  std::uint64_t perf_text_ns = 0, perf_print_ns = 0;
  std::vector<vibr::LineInstance> perf_lines;
  // The monitor's window as last set up (task 4.16): mode, fullscreen width, height, rate;
  // the fullscreen mode F11 goes back to; when the frame limit lets the next frame start.
  std::array<int, 4> applied_window{-1, -1, -1, -1};
  int last_fullscreen = 1;
  std::uint64_t frame_deadline_ns = 0;
  // The mouse (task 4.16 G): wheel steps and drag still to turn into camera moves; the
  // right or middle button held (a drag once it has moved); when it last moved.
  double mouse_zoom_steps = 0, mouse_turn_pixels = 0;
  bool drag_button = false, dragging = false;
  float drag_x = 0, drag_y = 0;
  std::uint64_t mouse_moved_ns = 0;
  bool cursor_hidden = false;
};

// In a headset the evolution meter (the dots around Vibri that show the run of hits)
// is moved off the road's plane (owner, session 3): in the original it is a flat ring
// in the road's own upright plane, about 1.3 m across and dipping 0.2 m below the
// road, so in stereo it ran straight through the road. The owner tried it 0.3 m in
// front of the road, then 0.3 m and 0.5 m behind it along the view's line of sight
// (which kept it lined up with everything from any angle), and chose a fixed place in
// the world: 0.5 m to the side of the road away from the default view (the default
// view stands on +x, 45 degrees round from behind her), so swinging the view round
// shows it as a layer of its own. Seen from the -x side it is in front of the road.
void evo_behind_road(std::vector<SceneLine>& lines, std::size_t first) {
  constexpr float kSide = -500.0f;  // x: away from the default view's side
  for (std::size_t i = first; i < lines.size(); ++i) lines[i].a.x += kSide, lines[i].b.x += kSide;
}

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

// Task 4.16 (the Ultimate PC version): the headset's enhancements chosen per display.
// `opt.vr` is "drawn into a headset's room" (off on the classic screen); these say
// which of our stagings are on, on whichever display is in use.
//
// Play seen from beside Vibri, with the score above her and the meter and numbers
// below: in a headset unless its Camera is the original's (the course round the
// player always stages it so); on a monitor when its Camera is beside Vibri. The
// classic screen in a headset shows the original's camera, as a monitor did.
bool beside_vibri(const Session& s) {
  const Options& o = s.save.options;
  if (s.opt.vr) return o.vr_camera == 1 || o.vr_wrap;
  return !s.opt.headset && o.camera == 1;
}
// The road drawn past the original's horizon and behind Vibri (the far generator and
// the kept trail): always in a headset; on a monitor once See ahead or See behind is
// set past the original's 8 s and 2 s.
bool far_road(const Session& s) {
  const Options& o = s.save.options;
  if (s.opt.vr) return true;
  return !s.opt.headset && (o.ahead_seconds > 8.0 + 1e-6 || o.behind_seconds > 2.0 + 1e-6);
}
double see_ahead(const Session& s) { return s.opt.vr ? s.save.options.vr_ahead_seconds : s.save.options.ahead_seconds; }
double see_behind(const Session& s) { return s.opt.vr ? s.save.options.vr_behind_seconds : s.save.options.behind_seconds; }
// The zoom and view angle of the view beside Vibri, for the display in use.
double& play_zoom(Session& s) { return s.opt.headset ? s.save.options.vr_zoom : s.save.options.zoom; }
double& view_angle(Session& s) { return s.opt.headset ? s.save.options.vr_angle : s.save.options.view_angle; }

// The view beside Vibri: the headset's (vr_play_view), or on a monitor the same place
// with the monitor's own zoom, angle and field of view.
SceneView beside_view(const Session& s, float vibri_z) {
  const Options& o = s.save.options;
  if (s.opt.vr) return vr_play_view(vibri_z, float(o.vr_zoom), o.vr_angle);
  SceneView v = vr_play_view(vibri_z, float(o.zoom), o.view_angle);
  v.fov_y = float(o.fov * 3.14159265358979 / 180.0);
  return v;
}

// In a headset: a gamepad's right stick held up zooms in (the view closer to Vibri),
// down zooms out, smoothly, twice as close or as far for each second at full tilt;
// the left stick left or right takes the view round her, 90 degrees a second.
// `zoom`: play's or the menus' own (each saved).
void zoom_with_stick(Session& s, double seconds, double& zoom, double& angle) {
  if (!s.opt.vr && !beside_vibri(s)) {  // (on a monitor only with the view beside Vibri)
    s.mouse_zoom_steps = s.mouse_turn_pixels = 0;
    return;
  }
  float y = 0, x = 0;
  if (!s.opt.headset) {  // the monitor's keyboard (task 4.16 G): Ctrl and the arrows, as the sticks
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (SDL_GetModState() & SDL_KMOD_CTRL) {
      x = float(keys[SDL_SCANCODE_RIGHT]) - float(keys[SDL_SCANCODE_LEFT]);
      y = float(keys[SDL_SCANCODE_UP]) - float(keys[SDL_SCANCODE_DOWN]);
    }
  }
  for (const auto& [id, pad] : s.pads) {
    const float v = -float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY)) / 32767.0f;  // SDL: up is negative
    if (std::abs(v) > std::abs(y)) y = v;
    const float h = float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX)) / 32767.0f;
    if (std::abs(h) > std::abs(x)) x = h;
  }
  {  // the headset's own controllers (Touch): the same sticks (the right one zooms only when pushed more up or down than sideways)
    const vibr::Display::ControllerSticks c = s.renderer.controller_sticks();
    if (std::abs(c.right_y) >= std::abs(c.right_x) && std::abs(c.right_y) > std::abs(y)) y = c.right_y;
    if (std::abs(c.left_x) > std::abs(x)) x = c.left_x;
  }
  if (std::abs(x) > 0.2f) {
    const double t = (std::abs(x) - 0.2) / 0.8 * (x > 0 ? 1.0 : -1.0);
    angle += 90.0 * t * std::min(seconds, 0.1);
    angle = std::remainder(angle, 360.0);  // kept within -180 to 180
    s.turning = true;
  } else if (s.turning) {
    s.turning = false;
    std::printf("View angle %.0f degrees\n", angle);
  }
  // The mouse: a drag turns the view (a quarter of a degree a pixel), the wheel zooms
  // (a quarter of a doubling a step), at once.
  if (s.mouse_turn_pixels != 0) {
    angle = std::remainder(angle + 0.25 * s.mouse_turn_pixels, 360.0);
    s.mouse_turn_pixels = 0;
  }
  if (s.mouse_zoom_steps != 0) {
    zoom = std::clamp(zoom * std::exp2(0.25 * s.mouse_zoom_steps), 1.0 / 36.0, 4.0);
    s.mouse_zoom_steps = 0;
  }
  if (std::abs(y) > 0.2f) {
    const double t = (std::abs(y) - 0.2) / 0.8 * (y > 0 ? 1.0 : -1.0);
    zoom = std::clamp(zoom * std::exp2(t * std::min(seconds, 0.1)), 1.0 / 36.0, 4.0);  // 4x closer to 36x further
    s.zooming = true;
  } else if (s.zooming) {
    s.zooming = false;
    std::printf("Zoom %.3f\n", zoom);
  }
}

void zoom_with_stick(Session& s, double seconds, double& zoom) { zoom_with_stick(s, seconds, zoom, view_angle(s)); }

// The monitor's menus with the camera beside Vibri (task 4.16): a menu camera turned
// round what it looks at, about the vertical, by `angle` degrees, and moved towards it
// by `zoom` (as the headset's menu zoom).
SceneView menu_view(SceneView v, double zoom, double angle) {
  const double a = angle * 3.14159265358979 / 180.0;
  const float c = float(std::cos(a)), sn = float(std::sin(a)), k = 1.0f / float(zoom);
  const float dx = v.eye.x - v.target.x, dz = v.eye.z - v.target.z;
  v.eye = {v.target.x + (dx * c - dz * sn) * k, v.target.y + (v.eye.y - v.target.y) * k, v.target.z + (dx * sn + dz * c) * k};
  return v;
}

// R3 (or Home on the keyboard): in a headset the recentre; on a monitor with the view
// beside Vibri, that view (play's, or in the menus theirs) back as it starts.
void r3_pressed(Session& s, bool menus) {
  if (s.opt.headset) {
    s.renderer.recentre();
    return;
  }
  if (!beside_vibri(s)) return;
  const Options defaults;
  if (menus) s.save.options.menu_zoom = defaults.menu_zoom, s.save.options.menu_angle = defaults.menu_angle;
  else s.save.options.zoom = defaults.zoom, s.save.options.view_angle = defaults.view_angle;
  s.fx.number(1, 90, 5);
  std::printf("View back as it starts\n");
}

bool is_mouse(const SDL_Event& e) {
  return e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_WHEEL || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         e.type == SDL_EVENT_MOUSE_BUTTON_UP;
}
// Ctrl and an arrow on the monitor with the view beside Vibri: the camera's, not a press.
bool camera_key(const Session& s, const SDL_KeyboardEvent& k) {
  const bool arrow = k.key == SDLK_UP || k.key == SDLK_DOWN || k.key == SDLK_LEFT || k.key == SDLK_RIGHT;
  return arrow && (k.mod & SDL_KMOD_CTRL) && !s.opt.headset && beside_vibri(s);
}

// The mouse on the monitor (task 4.16 G). Turns a mouse event into what it means:
// dragging with the right or middle button turns the view beside Vibri, the wheel zooms
// it where `wheel_zooms` (play) or with Ctrl held; otherwise the wheel's steps (up +)
// come back for a list to scroll. A right click is one without a drag. `x`, `y`: the
// pointer in the picture, -1 to 1 (top left -1, -1), when it is over the picture.
struct MouseInput {
  int wheel = 0;
  bool moved = false, left_click = false, right_click = false, over = false;
  float x = 0, y = 0;
};
MouseInput mouse_event(Session& s, const SDL_Event& e, bool wheel_zooms) {
  MouseInput m;
  if (s.opt.headset) return m;
  auto place = [&](float wx, float wy) {  // window points to the picture (bars left out)
    int w = 0, h = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(s.window, &w, &h);
    SDL_GetWindowSizeInPixels(s.window, &pw, &ph);
    if (w <= 0 || h <= 0 || pw <= 0 || ph <= 0) return;
    const float px = wx * float(pw) / float(w), py = wy * float(ph) / float(h);
    const float a = s.renderer.picture_aspect();
    float pic_w = float(pw), pic_h = float(ph);
    if (a > 0 && float(pw) / float(ph) > a) pic_w = float(ph) * a;
    else if (a > 0) pic_h = float(pw) / a;
    m.x = (px - (float(pw) - pic_w) * 0.5f) / pic_w * 2.0f - 1.0f;
    m.y = (py - (float(ph) - pic_h) * 0.5f) / pic_h * 2.0f - 1.0f;
    m.over = std::abs(m.x) <= 1.0f && std::abs(m.y) <= 1.0f;
  };
  const bool ctrl = SDL_GetModState() & SDL_KMOD_CTRL;
  switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
      s.mouse_moved_ns = SDL_GetTicksNS();
      place(e.motion.x, e.motion.y);
      m.moved = true;
      if (s.drag_button) {
        if (!s.dragging && std::abs(e.motion.x - s.drag_x) + std::abs(e.motion.y - s.drag_y) > 5.0f) s.dragging = true;
        if (s.dragging) s.mouse_turn_pixels += double(e.motion.xrel);
      }
      break;
    case SDL_EVENT_MOUSE_WHEEL: {
      s.mouse_moved_ns = SDL_GetTicksNS();
      place(e.wheel.mouse_x, e.wheel.mouse_y);
      const float steps = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
      if ((wheel_zooms || ctrl) && beside_vibri(s)) s.mouse_zoom_steps += double(steps);
      else m.wheel = steps > 0 ? 1 : steps < 0 ? -1 : 0;
      break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      s.mouse_moved_ns = SDL_GetTicksNS();
      place(e.button.x, e.button.y);
      if (e.button.button == SDL_BUTTON_RIGHT || e.button.button == SDL_BUTTON_MIDDLE) {
        s.drag_button = true, s.dragging = false;
        s.drag_x = e.button.x, s.drag_y = e.button.y;
      }
      if (e.button.button == SDL_BUTTON_LEFT) m.left_click = true;
      if (e.button.button == SDL_BUTTON_X1) m.right_click = true;  // (the mouse's back button: back)
      break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
      place(e.button.x, e.button.y);
      if (e.button.button == SDL_BUTTON_RIGHT || e.button.button == SDL_BUTTON_MIDDLE) {
        if (!s.dragging && e.button.button == SDL_BUTTON_RIGHT) m.right_click = true;
        s.drag_button = s.dragging = false;
      }
      break;
    default: break;
  }
  return m;
}

// The mouse over our settings panel: hovering moves the cursor, clicks act on the row
// under the pointer (SettingsPanel::click), the wheel scrolls; into `in`. `aspect`: the picture's.
void panel_mouse(Session& s, SettingsPanel& panel, const MouseInput& m, SettingsPanel::Input& in) {
  const float aspect = s.renderer.picture_aspect();
  const int row = m.over ? panel.row_at(m.x, m.y, aspect) : -1;
  if (m.moved && row >= 0) panel.hover(row);
  if (m.left_click || m.right_click) {
    const SettingsPanel::Input c = panel.click(row, m.right_click);
    if (row < 0 && m.left_click) return;  // (a left click off the rows does nothing)
    in.confirm |= c.confirm, in.cancel |= c.cancel, in.left |= c.left, in.right |= c.right;
  }
  if (m.wheel > 0) in.up = true;
  if (m.wheel < 0) in.down = true;
}

// The right stick in play and in the settings' preview: the zoom, or, with the course
// wrapped round the player, the circle's size (up: smaller, nearer; down: bigger),
// live, twice or half as big for each second at full tilt, 1 to 10 m. Saved with the
// options.
void zoom_or_circle_with_stick(Session& s, double seconds, double& zoom) {
  if (!s.opt.vr || !s.save.options.vr_wrap) {
    zoom_with_stick(s, seconds, zoom);
    return;
  }
  const bool was = s.zooming;
  double nearer = 3.0 / s.save.options.vr_wrap_radius;  // (as a zoom: larger is nearer)
  zoom_with_stick(s, seconds, nearer);
  s.save.options.vr_wrap_radius = std::clamp(3.0 / nearer, 1.0, 10.0);
  if (was && !s.zooming) std::printf("Circle size %.2f m\n", s.save.options.vr_wrap_radius);
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

// L3 (or F3): the performance overlay off, one line, detailed, and round again
// (owner's choice; the wobble hold and smoothing L3 used to change are in the
// settings now). Saved, with the click to hear it in the headset.
void l3_pressed(Session& s) {
  int& level = s.save.options.perf_overlay;
  level = (level + 1) % 3;
  s.fx.number(1, 90, 5);
  static const char* names[] = {"off", "on (one line)", "detailed"};
  std::printf("Performance overlay %s (saved)\n", names[level]);
  s.perf_text_ns = 0;  // its rows at once
}

// On the monitor: the original's 50 Hz jitter, scaled (1 as it was, 0 none).
Vec3f scaled_jitter(Vec3f p, Vec3f jitter, double scale) {
  if (scale == 1.0) return p;
  const float k = float(scale) - 1.0f;
  return {p.x + jitter.x * k, p.y + jitter.y * k, p.z + jitter.z * k};
}

// The lines' wobble for the display in use (task 4.16 brings the headset's to the
// monitor): afresh every `hold` display frames, from each vertex, in a headset always
// and on a monitor with Wobble hold set (1-3); otherwise the original's 50 Hz jitter,
// scaled. `display_frame` counts the display's own frames.
Vec3f wobble(const Session& s, Vec3f p, Vec3f jitter, std::uint32_t vertex, float shake, std::uint32_t display_frame) {
  const Options& o = s.save.options;
  if (s.opt.headset)
    return headset_jitter(p, jitter, vertex, shake * float(o.vr_jitter_scale), display_frame / std::uint32_t(std::max(1, o.vr_jitter_hold)));
  if (o.jitter_hold > 0)
    return headset_jitter(p, jitter, vertex, shake * float(o.jitter_scale), display_frame / std::uint32_t(o.jitter_hold));
  return scaled_jitter(p, jitter, o.jitter_scale);
}

// D-pad left or right in play: how far the lines wobble, a tenth of the original's
// at a time, from 0 (not at all) to 2; saved for the display in use, with the click.
void change_jitter_scale(Session& s, bool more) {
  double& scale = s.opt.headset ? s.save.options.vr_jitter_scale : s.save.options.jitter_scale;
  scale = std::clamp(std::round(scale * 10.0 + (more ? 1.0 : -1.0)) / 10.0, 0.0, 2.0);
  s.fx.number(1, 90, 5);
  std::printf("%swobble %.1f (saved)\n", s.opt.headset ? "headset " : "", scale);
}

// Thicker or thinner lines, saved for the display in use. In a headset each step is
// 0.005 (owner), with the confirm click so it can be heard without a console.
void change_line_width(Session& s, bool thicker) {
  if (s.opt.headset) {
    s.line_width = std::clamp(std::round((s.line_width + (thicker ? kHeadsetLineStep : -kHeadsetLineStep)) * 1000.0) / 1000.0, kHeadsetLineStep, 1.0);
    s.save.options.vr_line_width = s.line_width;
    s.fx.number(1, 90, 5);
  } else {
    s.line_width = std::clamp(s.line_width + (thicker ? 0.125 : -0.125), 0.125, 4.0);
    s.save.options.line_width = s.line_width;
  }
  std::printf("%sline width %.3f (saved)\n", s.opt.headset ? "headset " : "", s.line_width);
}

// The monitor's window as the options say (task 4.16): a window, borderless over the
// whole screen (the desktop's own mode), or fullscreen in a display mode of its own (the
// closest the display offers to the one chosen; the desktop's if none is chosen). Does
// nothing when it is already so.
void apply_window(Session& s) {
  if (s.opt.headset) return;
  const Options& o = s.save.options;
  const std::array<int, 4> want{o.window_mode, o.fullscreen_w, o.fullscreen_h, o.fullscreen_hz};
  if (want == s.applied_window) return;
  const bool first = s.applied_window[0] < 0;
  s.applied_window = want;
  if (first && o.window_mode == 0) return;  // (it starts as a window)
  SDL_Window* w = s.window;
  if (o.window_mode == 0) {
    SDL_SetWindowFullscreen(w, false);
    std::printf("Window\n");
  } else if (o.window_mode == 1) {
    SDL_SetWindowFullscreenMode(w, nullptr);  // (borderless, the desktop's mode)
    SDL_SetWindowFullscreen(w, true);
    std::printf("Borderless fullscreen\n");
  } else {
    const SDL_DisplayID d = SDL_GetDisplayForWindow(w);
    SDL_DisplayMode mode{};
    bool found = o.fullscreen_w > 0 &&
                 SDL_GetClosestFullscreenDisplayMode(d, o.fullscreen_w, o.fullscreen_h, float(o.fullscreen_hz), true, &mode);
    if (!found)
      if (const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(d))
        found = SDL_GetClosestFullscreenDisplayMode(d, desk->w, desk->h, desk->refresh_rate, true, &mode);
    SDL_SetWindowFullscreenMode(w, found ? &mode : nullptr);
    SDL_SetWindowFullscreen(w, true);
    if (found) std::printf("Fullscreen %dx%d at %.0f Hz\n", mode.w, mode.h, double(mode.refresh_rate));
    else std::printf("Fullscreen (borderless: the display offered no mode)\n");
  }
  SDL_SyncWindow(w);
  s.renderer.request_resize();
}

// F11 or Alt+Enter on the monitor: between a window and the last fullscreen mode used
// (borderless at first), saved. True when the key was taken.
bool window_key(Session& s, const SDL_Event& e) {
  if (s.opt.headset || e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) return false;
  const bool f11 = e.key.key == SDLK_F11, alt_enter = e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT);
  if (!f11 && !alt_enter) return false;
  int& mode = s.save.options.window_mode;
  if (mode != 0) s.last_fullscreen = mode, mode = 0;
  else mode = s.last_fullscreen;
  apply_window(s);
  if (s.write_save) s.write_save();
  return true;
}

// The monitor's fullscreen modes, largest and fastest first, once each (a mode seen at
// several pixel densities counts once).
std::vector<SettingsPanel::DisplayMode> monitor_modes(SDL_Window* window) {
  std::vector<SettingsPanel::DisplayMode> out;
  int count = 0;
  SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(window), &count);
  if (!modes) return out;
  for (int i = 0; i < count; ++i) {
    const SettingsPanel::DisplayMode m{modes[i]->w, modes[i]->h, int(std::lround(modes[i]->refresh_rate))};
    if (std::none_of(out.begin(), out.end(), [&](const auto& x) { return x.w == m.w && x.h == m.h && x.hz == m.hz; })) out.push_back(m);
  }
  SDL_free(modes);
  return out;
}

// Puts the saved options into effect after the settings panel changed one.
void apply_settings(Session& s) {
  const Options& o = s.save.options;
  // The classic screen (task 4.2): the game drawn exactly as on a monitor, then shown on a
  // screen in the headset (draw_frame), so everything decided by `vr` follows the monitor.
  if (s.opt.headset) s.opt.vr = !o.vr_classic;
  // (The headset's line width and wobble on the classic screen too: owner, session 5.)
  s.line_width = s.opt.headset ? o.vr_line_width : o.line_width;
  s.audio.adjust_offset(o.audio_offset_ms - s.audio.offset_ms());
  Clip::set_smooth(s.opt.smooth || o.smooth_animation || s.opt.headset);  // always smooth in a headset (the classic screen too: owner)
  s.audio.set_volumes(s.opt.music.value_or(o.music_volume), s.opt.sfx.value_or(o.effects_volume));
  if (s.opt.headset) s.renderer.set_refresh_rate(float(o.vr_refresh_hz));  // (asks only when it changed)
  if (!s.opt.headset) {  // the monitor's window and picture (task 4.16)
    apply_window(s);
    s.renderer.set_vsync(o.vsync);
    s.renderer.set_picture_aspect(o.picture_43 ? 4.0f / 3.0f : 0.0f);
    s.renderer.set_low_latency(o.low_latency);
  }
}

// The settings in play's pause, and below the Options screen (which has the
// original's sound effects and vibration items itself).
// The settings' layout (owner's, session 4): a top list with the Visual, Modifiers and
// Custom colors groups. `pause`: play's pause (Continue first, Quit
// song last); otherwise the menus' settings. The headset has more view settings.
void setup_panel(SettingsPanel& p, bool vr, bool pause) {
  using I = SettingsPanel::Item;
  // (Task 4.16: the monitor has the headset's view settings too, its own Wobble hold
  // starting from the original's 50 Hz, and a Display group for its window.)
  if (vr)
    p.set_group(I::Visual, {I::OriginalLook, I::RestoreLook, I::WobbleSize, I::WobbleHold, I::LineWidth, I::Glow, I::Camera, I::Zoom,
                            I::ViewAngle, I::SeeAhead, I::SeeBehind, I::ClassicScreen, I::ClassicSize, I::RefreshRate, I::Performance});
  else
    p.set_group(I::Visual, {I::OriginalLook, I::RestoreLook, I::WobbleSize, I::WobbleHold, I::Smooth, I::LineWidth, I::Glow, I::Camera,
                            I::Zoom, I::ViewAngle, I::FieldOfView, I::SeeAhead, I::SeeBehind, I::Performance});
  if (!vr) p.set_group(I::Display, {I::WindowMode, I::Resolution, I::Vsync, I::FrameLimit, I::LowLatency, I::PictureShape});
  if (vr)
    p.set_group(I::Modifiers, {I::Difficulty, I::ScrollSpeed, I::TimingFeedback, I::FcCelebration, I::WrapCourse, I::WrapSize, I::ScoreNumbers, I::FixedCourses});
  else
    p.set_group(I::Modifiers, {I::Difficulty, I::ScrollSpeed, I::TimingFeedback, I::FcCelebration, I::ScoreNumbers, I::FixedCourses});
  // (owner, session 5: the manual Sound timing and Picture timing live here too, each under its tool)
  p.set_group(I::Calibration, {I::SoundCalibration, I::SoundTiming, I::PictureCalibration, I::PictureTiming});
  p.set_group(I::CustomColours, {I::ColourRoad, I::ColourBlock, I::ColourPit, I::ColourLoop, I::ColourWave, I::ColourBlockPit,
                                  I::ColourBlockLoop, I::ColourBlockWave, I::ColourPitLoop, I::ColourPitWave, I::ColourLoopWave,
                                  I::ColourAngel, I::ColourRabbit, I::ColourFrog, I::ColourWorm, I::ColourBackground, I::ColourMenu,
                                  I::ResetColours});
  std::vector<I> top;
  if (pause) top.push_back(I::Continue);
  top.insert(top.end(), {I::Visual, I::Modifiers, I::CustomColours, I::Calibration});
  if (!vr) top.push_back(I::Display);
  if (vr) top.push_back(I::Recentre);
#ifndef __ANDROID__
  if (!pause) top.push_back(I::ChangeDisc);  // (task 4.16 I: the game starts again with the other disc)
#endif
  top.insert(top.end(), {I::SoundEffects, I::EffectsVolume, I::Vibration, I::ResumeStyle});
  if (pause) top.push_back(I::QuitSong);
  p.set_items(std::move(top));
}

// Scene lines to the renderer's, jittered as play does: in a headset afresh every
// `hold` of its frames, on the monitor the original's 50 Hz jitter scaled.
void to_instances(const Session& s, const std::vector<SceneLine>& in, std::uint32_t display_frame, std::vector<vibr::LineInstance>& out) {
  out.clear();
  const Options& o = s.save.options;
  for (const SceneLine& l : in) {
    const Vec3f a = wobble(s, l.a, l.jitter_a, l.vertex_a, l.shake_a, display_frame);
    const Vec3f b = wobble(s, l.b, l.jitter_b, l.vertex_b, l.shake_b, display_frame);
    const Rgb c = shown_colour(l.colour, l.part, o.colours);  // (custom colours; the rest inverted on a light background)
    out.push_back({{a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f}});
  }
}

// The classic screen (headset, task 4.2): each layer's lines through the camera and the
// 4:3 picture a monitor would use, cut to the picture's edges, laid on a flat screen
// `vr_classic_size` m wide, 3 m ahead at eye height, with a dim frame round it. Replaces
// `layers` with that one room-fixed layer; returns the line width's factor (1: the
// headset's own Line width, as everywhere else in the headset).
float classic_screen(Session& s, std::vector<vibr::Layer>& layers) {
  constexpr float kDistance = 3.0f, kAspect = 4.0f / 3.0f;
  const float w = float(s.save.options.vr_classic_size), h = w / kAspect;
  std::vector<vibr::LineInstance>& out = s.classic_lines;
  out.clear();
  struct P4 {
    float x, y, z, w;
  };
  auto apply = [](const vibr::Mat4& m, const float* p) {
    P4 r{};
    float* o = &r.x;
    for (int row = 0; row < 4; ++row) o[row] = m.m[row] * p[0] + m.m[4 + row] * p[1] + m.m[8 + row] * p[2] + m.m[12 + row];
    return r;
  };
  for (const vibr::Layer& layer : layers) {
    if (!layer.lines || layer.glow > 0) continue;
    const vibr::Mat4 m = vibr::mul(vibr::perspective(layer.fov_y, kAspect, 10.0f, 100000.0f), layer.view);
    for (const vibr::LineInstance& l : *layer.lines) {
      P4 a = apply(m, l.a), b = apply(m, l.b);
      // Cut to the picture (Vulkan's clip space: -w <= x, y <= w, 0 <= z <= w).
      float t0 = 0, t1 = 1;
      bool keep = true;
      auto plane = [&](float fa, float fb) {
        if (fa < 0 && fb < 0) keep = false;
        else if (fa < 0) t0 = std::max(t0, fa / (fa - fb));
        else if (fb < 0) t1 = std::min(t1, fa / (fa - fb));
      };
      plane(a.w + a.x, b.w + b.x), plane(a.w - a.x, b.w - b.x), plane(a.w + a.y, b.w + b.y), plane(a.w - a.y, b.w - b.y);
      plane(a.z, b.z), plane(a.w - a.z, b.w - b.z);
      if (!keep || t0 >= t1) continue;
      auto at = [&](float t) {
        const P4 p{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
        return P4{p.x / p.w * w * 0.5f, -p.y / p.w * h * 0.5f, -kDistance, 1};  // (clip y is down; the room's is up)
      };
      const P4 pa = at(t0), pb = at(t1);
      out.push_back({{pa.x, pa.y, pa.z}, {pb.x, pb.y, pb.z}, {l.colour[0], l.colour[1], l.colour[2], l.colour[3]}});
    }
  }
  // The screen's edge, dim, a little outside the picture.
  const float fw = w * 0.5f + 0.02f, fh = h * 0.5f + 0.02f, g = 0.25f;
  const float corners[5][2] = {{-fw, -fh}, {fw, -fh}, {fw, fh}, {-fw, fh}, {-fw, -fh}};
  for (int i = 0; i < 4; ++i)
    out.push_back({{corners[i][0], corners[i][1], -kDistance}, {corners[i + 1][0], corners[i + 1][1], -kDistance}, {g, g, g, 1.0f}});
  layers.assign(1, vibr::Layer{&out, vibr::Mat4{}, 1.0f, 1.0f});  // (anchored: metres)
  // Lines as thick as anywhere else in the headset: the headset's own Line width (owner,
  // session 5: 0.05 there, not the monitor's width scaled to the screen).
  return 1.0f;
}

// One frame through the display, measured for the performance overlay: when it is
// on, its layer goes over the others (in a headset fixed in the room up and to the
// right of the recentred view, on the monitor at the top left). `update_ms`, `build_ms`: the game's own work for
// this frame before drawing (rules, sound and input; making the lines).
void draw_frame(Session& s, std::vector<vibr::Layer>& layers, double update_ms, double build_ms) {
  if (s.opt.headset && !s.holding) {  // (kept for with_frames: a copy of this frame's own layers)
    s.held_lines.resize(layers.size());
    s.held_layers = layers;
    for (std::size_t i = 0; i < layers.size(); ++i)
      if (layers[i].lines) {
        s.held_lines[i] = *layers[i].lines;
        s.held_layers[i].lines = &s.held_lines[i];
      }
  }
  const std::uint64_t now = SDL_GetTicksNS();
  if (!s.opt.headset) {  // the mouse pointer hides after 2 s still, and shows when it moves (task 4.16 G)
    const bool idle = now - s.mouse_moved_ns > 2'000'000'000ull && !s.drag_button;
    if (idle != s.cursor_hidden) {
      if (idle) SDL_HideCursor();
      else SDL_ShowCursor();
      s.cursor_hidden = idle;
    }
  }
  const int level = s.save.options.perf_overlay;
  if (level > 0 && s.perf) {
    if (now - s.perf_text_ns > 250'000'000) {
      s.perf_text_ns = now;
      s.perf_text = perf_rows(s.stats.summary(now), level, s.audio.latency_ms());
      if (now - s.perf_print_ns > 1'000'000'000) {  // and in the console once a second
        s.perf_print_ns = now;
        std::string line = "Perf";
        for (const std::string& r : s.perf_text) line += "  |  " + r;
        line += "  |  display lead " + std::to_string(int(std::lround(s.renderer.display_lead_ms()))) + " ms" +
                (s.renderer.lead_measured() ? "" : " (estimated)");
        std::printf("%s\n", line.c_str());
      }
    }
    const FrameStats::Summary sum = s.stats.summary(now);
    float aspect = 4.0f / 3.0f;  // (the classic screen is 4:3)
    if (!s.opt.headset && s.renderer.picture_aspect() > 0) aspect = s.renderer.picture_aspect();  // (the monitor's picture, bars left out)
    const Hud::Layer pl = s.perf->draw(s.perf_text, level >= 2 ? s.stats.intervals(now) : std::vector<double>{}, sum.target_ms, s.opt.vr, aspect);
    s.perf_lines.clear();
    for (const SceneLine& l : pl.lines) {
      const Rgb c = shown_colour(l.colour, l.part, s.save.options.colours);
      s.perf_lines.push_back({{l.a.x, l.a.y, l.a.z}, {l.b.x, l.b.y, l.b.z}, {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f}});
    }
    const SceneView& v = pl.view;
    layers.push_back({&s.perf_lines, vibr::look_at({v.eye.x, v.eye.y, v.eye.z}, {v.target.x, v.target.y, v.target.z}, {0, -1, 0}), v.fov_y,
                      s.opt.vr ? PerfOverlay::kAnchorMetresPerUnit : 0.0f});
  }
  // The classic screen (task 4.2): every layer as the monitor shows it, through a 4:3
  // picture, put on a flat screen in the room (fixed where the view was last recentred).
  float width_scale = 1.0f;
  if (s.opt.headset && !s.opt.vr) width_scale = classic_screen(s, layers);
  int lines = 0;
  for (const vibr::Layer& l : layers) lines += l.lines ? int(l.lines->size()) : 0;
  const double build_more = double(SDL_GetTicksNS() - now) * 1e-6;  // (the overlay's own lines count as building)
  // Glow (ours): under every layer a copy of its lines, wide and faint (Visual > Glow).
  if (const int g = std::clamp(s.save.options.glow, 0, 4); g > 0) {
    // (Owner: the first Soft, 7 wide at 0.16, was already about the most anyone would
    // want; it is Strong now, the others below it.)
    static const float kWidth[5] = {0, 3, 4, 5.5f, 7}, kBright[5] = {0, 0.04f, 0.07f, 0.11f, 0.16f};
    s.glow_lines.resize(layers.size());
    std::vector<vibr::Layer> with_glow;
    with_glow.reserve(layers.size() * 2);
    for (std::size_t i = 0; i < layers.size(); ++i) {
      if (layers[i].lines && !layers[i].lines->empty()) {
        std::vector<vibr::LineInstance>& copy = s.glow_lines[i];
        copy = *layers[i].lines;
        for (vibr::LineInstance& l : copy) l.colour[3] *= kBright[g];
        vibr::Layer gl = layers[i];
        gl.lines = &copy;
        gl.glow = kWidth[g];
        with_glow.push_back(gl);
      }
      with_glow.push_back(layers[i]);
    }
    layers = std::move(with_glow);
  }
  // The original's lines are one pixel on a 288-line picture.
  const Rgb bg = background_colour(s.save.options.colours);  // (custom colours: the background, on every screen)
  s.renderer.set_background(bg.r / 255.0f, bg.g / 255.0f, bg.b / 255.0f);
  s.renderer.set_passthrough(s.save.options.colours[std::size_t(kColourBackground)] == kBackgroundPassthrough);
  s.renderer.draw(layers, float(s.line_width * std::max(1, s.renderer.height()) / 288.0) * width_scale);
  const vibr::FrameTiming t = s.renderer.timing();
  FrameSample f;
  f.ns = SDL_GetTicksNS();
  f.interval_ms = t.interval_ms, f.period_ms = t.period_ms, f.wait_ms = t.wait_ms, f.submit_ms = t.submit_ms;
  // The monitor's frame limit (task 4.16): after a frame is handed over, wait until the
  // next may start, so the frame after is built just before it is shown. Its period is
  // then the overlay's target.
  if (const int limit = s.save.options.frame_limit; !s.opt.headset && limit > 0) {
    const auto period = std::uint64_t(1e9 / limit);
    const std::uint64_t now_ns = SDL_GetTicksNS();
    if (s.frame_deadline_ns > now_ns) SDL_DelayPrecise(std::min(s.frame_deadline_ns - now_ns, period));
    s.frame_deadline_ns = std::max(s.frame_deadline_ns, SDL_GetTicksNS()) + period;
    const double limit_ms = 1000.0 / limit;
    f.period_ms = s.save.options.vsync ? std::max(f.period_ms, limit_ms) : limit_ms;
  } else if (!s.opt.headset && !s.save.options.vsync) {
    f.period_ms = 0;  // (no target: as fast as it goes)
  }
  f.update_ms = update_ms, f.build_ms = build_ms + build_more, f.lines = lines;
  s.stats.add(f);
}

// The scroll speed shown, moved towards the setting: most of the way in about a
// fifth of a second, landing exactly on it.
double ease_scroll(double shown, double target, double dt) {
  if (shown == target) return shown;
  const double next = target + (shown - target) * std::exp(-std::max(0.0, dt) / 0.07);
  return std::abs(next - target) < 0.002 ? target : next;
}

// The menus' settings screen shows play beside the settings: Vibri walking a road of
// obstacles that never ends, seen as in play (in a headset from beside her, with the
// zoom and angle set, the road as far ahead and behind as set), clearing each piece as
// it reaches her, so every setting can be seen at work. Ours: the pieces come round in
// a fixed made-up order.
struct PlayPreview {
  double t = 0;
  double scroll = 1.0;  // the scroll speed shown, eased as play's is
  CourseWrap wrap;      // the course round the player, if set (headset)
  void start(Session& s) {
    t = 0;
    scroll = s.save.options.scroll_speed;
    s.scene.reset(true, false);
  }
  void draw(Session& s, double dt, std::uint32_t jitter_seed, std::vector<SceneLine>& lines, SceneView& view) {
    constexpr double kSpeed = 2.0;    // part lengths a second
    constexpr double kSpacing = 2.5;  // part lengths between pieces
    const double before = t * kSpeed;
    t += std::min(dt, 0.1);
    const double now = t * kSpeed;
    const Options& o = s.save.options;
    scroll = ease_scroll(scroll, o.scroll_speed, dt);
    // (Its endless road shows at most 2 minutes each way, enough to reach the horizon;
    // on the monitor as far as the original's horizon as drawn, whatever the scroll.)
    const bool far = far_road(s);
    const double ahead = far ? std::min(see_ahead(s), 120.0) * kSpeed : 14.0 / scroll;
    const double behind = far ? std::min(see_behind(s), 120.0) * kSpeed : 3.0 / scroll;
    Scene::Frame f;
    f.seconds = dt;
    f.jitter_seed = jitter_seed;
    f.speed = Fixed::from(kSpeed * scroll);
    const auto first = std::max<long long>(1, (long long)std::ceil((now - behind) / kSpacing));
    const auto last = (long long)std::floor((now + ahead) / kSpacing);
    for (long long k = first; k <= last; ++k) {
      const double x = double(k) * kSpacing;
      const int road = int((std::uint32_t(k) * 2654435761u >> 13) % 4);
      if (x > before && x <= now) {  // it reaches her: she clears it
        s.scene.set_move(road, false);
        if (o.sound_effects) s.fx.number(16 + road, 100, 3);
      }
      f.road.push_back({road, Fixed::from((x - now) * scroll + 1.25), false, false});  // (Vibri stands 1.25 along)
    }
    s.scene.draw(f, lines, view);
    if (beside_vibri(s)) {
      view = beside_view(s, s.scene.vibri_z());
      evo_behind_road(lines, s.scene.evo_first());
    }
    if (s.opt.vr && o.vr_wrap) {
      wrap.radius = float(o.vr_wrap_radius / double(s.opt.vr_scale));
      wrap.advance(kSpeed * 600.0 * scroll * std::min(dt, 0.1));
      view = wrap.view();
      wrap.bend_lines(lines, s.scene.vibri_z());
    }
  }
};

// Left or right held now, on the keyboard or any gamepad's d-pad (for the panel's repeat).
void held_directions(Session& s, SettingsPanel::Input& in) {
  const bool* keys = SDL_GetKeyboardState(nullptr);
  in.held_left = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A];
  in.held_right = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D];
  in.held_up = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
  in.held_down = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];
  for (const auto& [id, pad] : s.pads) {
    in.held_left = in.held_left || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    in.held_right = in.held_right || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    in.held_up = in.held_up || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP);
    in.held_down = in.held_down || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
  }
  const vibr::Display::ControllerSticks c = s.renderer.controller_sticks();  // (Touch: the left stick in the menus)
  in.held_up = in.held_up || c.held[0];
  in.held_down = in.held_down || c.held[1];
  in.held_left = in.held_left || c.held[2];
  in.held_right = in.held_right || c.held[3];
}

// The panel's clicks: a quiet one for a move, the full one for a change.
void panel_sound(Session& s, SettingsPanel::Action a) {
  if (a == SettingsPanel::Action::Moved) s.fx.number(1, 50, 5);
  else if (a != SettingsPanel::Action::None) s.fx.number(1, 90, 5);
}

// A trigger pulled past halfway (+1 right, -1 left) since it was last let go; 0 otherwise.
int trigger_edge(Session& s, const SDL_GamepadAxisEvent& a) {
  if (a.axis != SDL_GAMEPAD_AXIS_LEFT_TRIGGER && a.axis != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) return 0;
  const std::uint8_t bit = a.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 1 : 2;
  std::uint8_t& held = s.triggers[a.which];
  if (a.value > 16000 && !(held & bit)) {
    held = std::uint8_t(held | bit);
    return bit == 2 ? 1 : -1;
  }
  if (a.value < 8000) held = std::uint8_t(held & ~bit);
  return 0;
}

std::uint8_t trigger_pulled(Session& s, const SDL_GamepadAxisEvent& a) {
  const int d = trigger_edge(s, a);
  if (d == 0) return 0;
  if (s.opt.vr) {
    change_line_width(s, d > 0);
    return 0;
  }
  return d < 0 ? std::uint8_t(kPressBlock) : std::uint8_t(kPressLoop);
}

// The scroll speed a step faster (d 1) or slower (-1), with the click; false at an end.
bool change_scroll_speed(Session& s, int d) {
  double& v = s.save.options.scroll_speed;
  const double before = v;
  v = clamp_scroll_speed(v + kScrollSpeedStep * d);
  if (v == before) return false;
  s.fx.number(1, 90, 5);
  std::printf("Scroll speed %s (saved)\n", SettingsPanel::scroll_text(v).c_str());
  return true;
}

// After a pause, the tape-rewind sound: the music from `to` back to `from` (heard
// samples; `out` starts at `start`, 2 stereo frames at 44.1 kHz per heard sample),
// backwards and fast, quieter, faded in and out.
std::vector<std::int16_t> rewind_sound(const std::vector<std::int16_t>& out, std::int32_t start, std::int32_t from, std::int32_t to,
                                       double seconds) {
  const std::int64_t first = std::int64_t(from - start) * 2, last = std::int64_t(to - start) * 2;
  const std::int64_t frames = std::int64_t(seconds * 44100.0), total = std::int64_t(out.size() / 2);
  const std::int64_t fade = 441;  // 10 ms
  std::vector<std::int16_t> r;
  r.reserve(std::size_t(frames) * 2);
  for (std::int64_t i = 0; i < frames; ++i) {
    const std::int64_t f = last - (last - first) * i / std::max<std::int64_t>(1, frames);
    const double gain = 0.4 * double(std::min({i, frames - 1 - i, fade})) / double(fade);
    for (int c = 0; c < 2; ++c)
      r.push_back(f >= 0 && f < total ? std::int16_t(double(out[std::size_t(f * 2 + c)]) * gain) : std::int16_t(0));
  }
  return r;
}

// In a headset the score marks float above Vibri at a fixed height, in the world
// with her (owner's choice, task 4.8): seen through the HUD's own camera they stood a
// fixed distance in front of the player, so zoomed out they hung halfway between the
// player and her. The marks' own space (the HUD camera's: x right, y down, z away) is
// set upright above her and turned about the vertical to face `eye`, at their own
// size, so it never moves when she jumps. Their lowest point is 1,388 above the road
// (owner: 25% lower than the first 1,850, which was too high): just over the tallest
// obstacle (1,378, pit and loop). Only the angel's block move (1,721) reaches higher;
// every other move and form stays under 1,360. Game units, y down.
// Level, from the eye towards Vibri: what the score and the progress bar turn to face.
void facing(float vibri_z, Vec3f eye, float& fx, float& fz) {
  fx = 0.0f - eye.x, fz = vibri_z - eye.z;
  const float len = std::sqrt(fx * fx + fz * fz);
  if (len < 1.0f) fx = 0.0f, fz = 1.0f;
  else fx /= len, fz /= len;
}

void score_above_vibri(const std::vector<SceneLine>& marks, float vibri_z, Vec3f eye, std::vector<SceneLine>& out) {
  constexpr float kBottom = -1388.0f;              // its lowest point (1,850 less 25%)
  constexpr float kMarksBottom = -439.0f;          // the marks' lowest point, in their own space (measured)
  constexpr Vec3f kMarksCentre{0.0f, 0.0f, 636.0f};  // and the middle of their depth
  float fx, fz;
  facing(vibri_z, eye, fx, fz);
  auto place = [&](Vec3f p) {  // marks' space to the world
    const float x = p.x - kMarksCentre.x, z = p.z - kMarksCentre.z;
    return Vec3f{fz * x + fx * z, kBottom + (p.y - kMarksBottom), vibri_z - fx * x + fz * z};
  };
  auto turn = [&](Vec3f v) { return Vec3f{fz * v.x + fx * v.z, v.y, -fx * v.x + fz * v.z}; };  // (a jitter: no move)
  out.clear();
  for (const SceneLine& l : marks) {
    SceneLine w = l;
    w.a = place(l.a), w.b = place(l.b);
    w.jitter_a = turn(l.jitter_a), w.jitter_b = turn(l.jitter_b);
    if (w.vertex_a) w.vertex_a += 0x40000000u;  // (its own vertices, apart from the scene's)
    if (w.vertex_b) w.vertex_b += 0x40000000u;
    out.push_back(w);
  }
}

// In a headset the song's progress is the original's own time meter, moved below
// Vibri (task 4.9; owner: "more in the style of the original", running right to left
// as it does). The meter's model is drawn far larger than the original's screen and
// only a strip of it shows along the screen's bottom edge, sliding as the song goes;
// so the meter's lines are cut to that strip, the original picture's 4:3 window
// through its camera, then that strip is placed below her as the score is above:
// centred on her, upright, turned to face `eye`, at a fixed height (1,100 below the
// road: under the deepest pit, 559, and the timing feedback just below it), never
// moving when she jumps, 1,200 across.
void meter_below_vibri(const Hud::Layer& meter, float vibri_z, Vec3f eye, std::vector<SceneLine>& out) {
  constexpr float kWidth = 1200.0f, kY = 1100.0f;
  out.clear();
  // The meter's camera: right, down and forward, as the original's screen shows them.
  const Vec3f e = meter.view.eye, t = meter.view.target;
  auto norm = [](Vec3f v) {
    const float n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return n > 0 ? Vec3f{v.x / n, v.y / n, v.z / n} : Vec3f{0, 0, 1};
  };
  const Vec3f f = norm({t.x - e.x, t.y - e.y, t.z - e.z});
  const Vec3f r = norm({f.z, 0.0f, -f.x});  // f x (0, -1, 0): right on screen
  const Vec3f d{f.y * r.z - f.z * r.y, f.z * r.x - f.x * r.z, f.x * r.y - f.y * r.x};  // f x r: down on screen
  auto dot = [](Vec3f a, Vec3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
  auto to_camera = [&](Vec3f p) { const Vec3f q{p.x - e.x, p.y - e.y, p.z - e.z}; return Vec3f{dot(q, r), dot(q, d), dot(q, f)}; };
  // The original's window at the meter's depth (its lines all lie at one depth).
  float depth = 0;
  for (const SceneLine& l : meter.lines) depth = std::max(depth, to_camera(l.a).z);
  if (meter.lines.empty() || depth <= 0) return;
  const float half_h = depth * std::tan(0.5f * meter.view.fov_y), half_w = half_h * 4.0f / 3.0f;
  const float k = kWidth / (2.0f * half_w);
  // Its visible strip: from the lowest point any line reaches inside the window, to the window's bottom edge.
  float facing_x, facing_z;
  facing(vibri_z, eye, facing_x, facing_z);
  auto place = [&](Vec3f c, float centre_y) {  // camera space to the world below Vibri
    const float across = c.x * k, down = (c.y - centre_y) * k;
    return Vec3f{facing_z * across, kY + down, vibri_z - facing_x * across};
  };
  auto turn = [&](Vec3f v) {  // a jitter, from the meter's world into ours
    const Vec3f c{dot(v, r) * k, dot(v, d) * k, dot(v, f) * k};
    return Vec3f{facing_z * c.x + facing_x * c.z, c.y, -facing_x * c.x + facing_z * c.z};
  };
  struct Cut {
    Vec3f a, b, ja, jb;
    std::uint32_t va, vb;
    const SceneLine* from;
  };
  std::vector<Cut> cuts;
  float top = half_h;
  for (const SceneLine& l : meter.lines) {
    Vec3f a = to_camera(l.a), b = to_camera(l.b);
    // Cut to the window (Liang-Barsky on x and y); the depth hardly varies.
    double t0 = 0, t1 = 1;
    const double dx = b.x - a.x, dy = b.y - a.y;
    bool inside = true;
    auto clip = [&](double p, double q) {
      if (p == 0) return q >= 0;
      const double u = q / p;
      if (p < 0) { if (u > t1) return false; if (u > t0) t0 = u; }
      else { if (u < t0) return false; if (u < t1) t1 = u; }
      return true;
    };
    inside = clip(-dx, a.x + half_w) && clip(dx, half_w - a.x) && clip(-dy, a.y + half_h) && clip(dy, half_h - a.y);
    if (!inside) continue;
    const Vec3f ca{float(a.x + t0 * dx), float(a.y + t0 * dy), a.z}, cb{float(a.x + t1 * dx), float(a.y + t1 * dy), b.z};
    // Ends that were cut get no fresh jitter of their own (no shared vertex there).
    cuts.push_back({ca, cb, t0 > 0 ? Vec3f{} : l.jitter_a, t1 < 1 ? Vec3f{} : l.jitter_b, t0 > 0 ? 0u : l.vertex_a, t1 < 1 ? 0u : l.vertex_b, &l});
    top = std::min({top, ca.y, cb.y});
  }
  const float centre_y = 0.5f * (top + half_h);
  for (const Cut& c : cuts) {
    SceneLine w = *c.from;
    w.a = place(c.a, centre_y), w.b = place(c.b, centre_y);
    w.jitter_a = turn(c.ja), w.jitter_b = turn(c.jb);
    w.vertex_a = c.va ? c.va + 0x50000000u : 0u;  // (its own vertices, apart from the scene's)
    w.vertex_b = c.vb ? c.vb + 0x50000000u : 0u;
    w.shake_a = c.from->shake_a * k, w.shake_b = c.from->shake_b * k;
    out.push_back(w);
  }
}

// In a headset, with the Score in numbers setting (task 4.10), the score's number sits
// below the time meter under Vibri, placed as the meter is: centred on her, upright,
// facing `eye`, at a fixed height (its middle 1,290 below the road, just under the
// meter's strip), in the game's own line font with its text jitter. `text`: from
// SettingsPanel::text_lines, centred on the origin as if seen from -z.
// The timing feedback's middle in a headset: just below the road, under the deepest
// pit (559) and above the time meter's strip (its middle 1,100 below, about 36 each
// way); y is down. The scroll speed's note goes under the score's number.
// "Full Combo!" in a headset: its middle 1,000 above the road, over Vibri's head (she
// runs under 805) and under the score marks once settled.
constexpr float kFullComboAboveRoad = -1000.0f;
// "Full Course Combo!": two lines, "Combo!" larger and lower; its middle a little higher.
constexpr float kFullCourseComboAboveRoad = -1150.0f;
constexpr float kTimingBelowRoad = 720.0f, kScrollNoteBelowRoad = 1530.0f, kTimingScale = 0.8f;

void number_below_vibri(const std::vector<SceneLine>& text, float vibri_z, Vec3f eye, std::vector<SceneLine>& out, float kY = 1290.0f) {
  float fx, fz;
  facing(vibri_z, eye, fx, fz);
  auto place = [&](Vec3f p) { return Vec3f{fz * p.x + fx * p.z, kY + p.y, vibri_z - fx * p.x + fz * p.z}; };
  auto turn = [&](Vec3f v) { return Vec3f{fz * v.x + fx * v.z, v.y, -fx * v.x + fz * v.z}; };
  out.clear();
  for (const SceneLine& l : text) {
    SceneLine w = l;
    w.a = place(l.a), w.b = place(l.b);
    w.jitter_a = turn(l.jitter_a), w.jitter_b = turn(l.jitter_b);
    if (w.vertex_a) w.vertex_a += 0x60000000u;  // (its own vertices, apart from the scene's)
    if (w.vertex_b) w.vertex_b += 0x60000000u;
    out.push_back(w);
  }
}

// Ours: sound calibration (task 3.6, in the settings' Calibration group). A click track
// at 120 a minute plays as the music; the player presses any action button on each
// click; each press is placed by the clock play judges with (TapCalibration), and after
// 16 the screen offers the Sound timing that would centre them. B leaves at any time,
// changing nothing. Returns false if the window was closed (the quit is passed on).
// `picture`: the picture calibration instead: silent, a ring flashing on every beat
// when the picture is drawn for it (with Picture timing), to set Picture timing by. Do
// the sound one first: both use the same clock, so the picture is then lined up with
// the sound as this player hears and sees them.
bool run_calibration(Session& s, SettingsPanel& panel, bool picture) {
  const int l = std::clamp(panel.language(), 0, 4);
  static const char* const kSoundTitle[5] = {"Ton kalibrieren", "Sound calibration", "Calibrar sonido", "Calibrer le son", "Calibra audio"};
  static const char* const kPictureTitle[5] = {"Bild kalibrieren", "Picture calibration", "Calibrar imagen", "Calibrer l'image", "Calibra immagine"};
  static const char* const kSoundHow[5] = {"Bei jedem Klick eine Taste drücken", "Press a button on every click", "Pulsa un botón en cada clic",
                                           "Appuie sur un bouton à chaque clic", "Premi un tasto a ogni clic"};
  static const char* const kPictureHow[5] = {"Taste drücken, wenn eine Raute das Ziel erreicht", "Press a button as each diamond reaches the ring",
                                             "Pulsa cuando cada rombo llegue al círculo", "Appuie quand chaque losange touche l'anneau",
                                             "Premi quando ogni rombo raggiunge il cerchio"};
  const char* const* kTitle = picture ? kPictureTitle : kSoundTitle;
  const char* const* kHow = picture ? kPictureHow : kSoundHow;
  double& setting = picture ? s.save.options.picture_offset_ms : s.save.options.audio_offset_ms;
  const double lowest = picture ? -300.0 : -500.0, highest = picture ? 300.0 : 500.0;
  std::printf("%s calibration: started at %+.0f ms\n", picture ? "Picture" : "Sound", setting);
  auto suggestion = [&](const TapCalibration& c) { return std::clamp(c.suggested_ms(setting), lowest, highest); };
  static const char* const kPresses[5] = {"Treffer", "Presses", "Pulsaciones", "Appuis", "Pressioni"};
  static const char* const kLast[5] = {"Zuletzt", "Last", "Última", "Dernier", "Ultima"};
  // (Owner, session 5: "average -9 ms, suggested -75" was confusing; the average is how far
  // off the presses were at the current setting, and the suggestion is the setting plus it.)
  static const char* const kAverage[5] = {"Abweichung", "Off by", "Desvío", "Décalage", "Scostamento"};
  static const char* const kEarly[5] = {"früh", "early", "pronto", "en avance", "presto"};
  static const char* const kLate[5] = {"spät", "late", "tarde", "en retard", "tardi"};
  static const char* const kSpread[5] = {"Streuung", "Spread", "Dispersión", "Écart", "Scarto"};
  static const char* const kNow[5] = {"Jetzt", "Now", "Ahora", "Actuel", "Ora"};
  static const char* const kSuggested[5] = {"Vorschlag", "Suggested", "Sugerido", "Suggéré", "Consigliato"};
  static const char* const kUse[5] = {"A: übernehmen   B: behalten", "A: use it   B: keep", "A: usar   B: mantener", "A: appliquer   B: garder",
                                      "A: usa   B: mantieni"};
  static const char* const kTooFew[5] = {"Zu wenige Treffer. A: nochmal   B: zurück", "Too few presses. A: again   B: back",
                                         "Pocas pulsaciones. A: otra vez   B: volver", "Trop peu d'appuis. A: encore   B: retour",
                                         "Troppo poche. A: ancora   B: indietro"};
  // The click track: a short falling tone on every beat, higher on every fourth.
  auto make_track = [picture]() {
    std::vector<std::int16_t> pcm(std::size_t(TapCalibration::length_frames()) * 2, 0);
    for (int k = 0; k < TapCalibration::kClicks && !picture; ++k) {  // (the picture's is silent: the clock only)
      const double hz = k % 4 == 0 ? 1760.0 : 1320.0;
      const std::int64_t at = TapCalibration::click_frame(k);
      for (int i = 0; i < 1323; ++i) {  // 30 ms
        const double t = double(i) / TapCalibration::kRate;
        const auto v = std::int16_t(14000.0 * std::exp(-t * 140.0) * std::sin(2.0 * 3.14159265358979 * hz * t));
        pcm[std::size_t(at + i) * 2] = pcm[std::size_t(at + i) * 2 + 1] = v;
      }
    }
    return pcm;
  };
  // Only the clicks: the effects channel (menu music, voices) is silent meanwhile.
  s.audio.set_volumes(s.opt.music.value_or(s.save.options.music_volume), 0.0f);
  auto restore = [&]() {
    s.audio.play_music({});
    apply_settings(s);
  };
  TapCalibration cal;
  s.audio.play_music(make_track());
  bool finished = false, have_last = false;
  double last_ms = 0;
  std::uint32_t frame = 0;
  std::vector<vibr::LineInstance> lines;
  for (;;) {
    const std::uint64_t now_ns = SDL_GetTicksNS();
    bool confirm = false, cancel = false;
    std::vector<std::uint64_t> presses;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (window_key(s, ev)) continue;  // (F11 or Alt+Enter: window or fullscreen)
      switch (ev.type) {
        case SDL_EVENT_QUIT:
          std::printf("Calibration: the game is closing\n");
          restore();
          SDL_PushEvent(&ev);  // (for the caller to see)
          return false;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: case SDL_EVENT_WINDOW_RESIZED: s.renderer.request_resize(); break;
        case SDL_EVENT_GAMEPAD_ADDED:
          if (SDL_Gamepad* g = SDL_OpenGamepad(ev.gdevice.which)) s.pads[ev.gdevice.which] = g;
          break;
        case SDL_EVENT_GAMEPAD_REMOVED:
          if (auto it = s.pads.find(ev.gdevice.which); it != s.pads.end()) SDL_CloseGamepad(it->second), s.pads.erase(it);
          break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
          const auto b = ev.gbutton.button;
          if (b == SDL_GAMEPAD_BUTTON_EAST || b == SDL_GAMEPAD_BUTTON_BACK || b == SDL_GAMEPAD_BUTTON_START) cancel = true;
          else if (b == SDL_GAMEPAD_BUTTON_SOUTH && finished) confirm = true;
          else presses.push_back(ev.gbutton.timestamp);  // (A counts as a press too, while clicking)
          break;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
          if (trigger_edge(s, ev.gaxis) != 0) presses.push_back(ev.gaxis.timestamp);
          break;
        case SDL_EVENT_KEY_DOWN:
          if (ev.key.repeat) break;
          if (ev.key.key == SDLK_ESCAPE || ev.key.key == SDLK_BACKSPACE) cancel = true;
          else if ((ev.key.key == SDLK_RETURN || ev.key.key == SDLK_SPACE) && finished) confirm = true;
          else presses.push_back(ev.key.timestamp);
          break;
        default: break;
      }
    }
    if (!finished)
      for (const std::uint64_t ns : presses)
        if (const auto ms = cal.press(s.audio.heard_frame_at(now_ns, ns))) last_ms = *ms, have_last = true;
    if (!finished && (cal.done() || TapCalibration::over(s.audio.heard_frame(now_ns)))) {
      finished = true;
      s.audio.play_music({});
    }
    const bool enough = cal.count() >= 6;
    if (cancel) break;
    if (confirm && finished && enough) {
      const double v = suggestion(cal);
      std::printf("%s timing %+.0f ms (calibrated at %+.0f ms: off by %+.1f ms, spread %.1f ms over %d presses; display lead %.0f ms)\n",
                  picture ? "Picture" : "Sound", v, setting, cal.average_ms(), cal.spread_ms(), cal.count(), s.renderer.display_lead_ms());
      setting = v;
      apply_settings(s);
      if (s.write_save) s.write_save();
      s.fx.number(1, 90, 5);
      break;
    }
    if (confirm && finished && !enough) {  // again
      cal = TapCalibration{};
      finished = have_last = false;
      s.audio.play_music(make_track());
    }

    // Draw: the rows of text through the text camera (in a headset 1 m away).
    auto ms_text = [](double v) { return SettingsPanel::offset_text(v); };
    std::vector<std::string> rows = {kTitle[l], kHow[l], std::string(kPresses[l]) + ": " + std::to_string(cal.count()) + " / " + std::to_string(TapCalibration::kWanted)};
    if (have_last) rows.push_back(std::string(kLast[l]) + ": " + ms_text(last_ms));
    if (cal.count() >= 4) {
      char spread[16];
      std::snprintf(spread, sizeof spread, "%.0f ms", cal.spread_ms());
      const double avg = cal.average_ms();
      rows.push_back(std::string(kAverage[l]) + ": " + ms_text(avg) + (std::lround(avg) == 0 ? "" : std::string(" ") + (avg < 0 ? kEarly[l] : kLate[l])) +
                     "   " + kSpread[l] + ": " + spread);
    }
    if (finished && enough) {
      char now_t[16], sug[16];
      std::snprintf(now_t, sizeof now_t, "%.0f ms", setting);
      std::snprintf(sug, sizeof sug, "%.0f ms", suggestion(cal));
      // The sum, so the suggestion reads as what it is: now plus how far off.
      rows.push_back(std::string(kSuggested[l]) + ": " + sug + "   (" + kNow[l] + " " + now_t + ", " + kAverage[l] + " " + ms_text(cal.average_ms()) + ")");
      rows.push_back(kUse[l]);
    } else if (finished) {
      rows.push_back(kTooFew[l]);
    }
    const float size = s.opt.vr ? 0.25f : 1.0f;
    const auto seed = next_random(static_cast<std::uint32_t>(double(now_ns) * 1e-9 * kOriginalRefresh) * 2654435761u);
    std::vector<SceneLine> text;
    for (std::size_t k = 0; k < rows.size(); ++k) {
      const float y = -560.0f + 190.0f * float(k) + (k > 0 ? 80.0f : 0.0f);
      for (SceneLine ln : panel.text_lines(to_font_text(rows[k]), k == 0 ? 1.5f : 0.95f, seed + std::uint32_t(k), 3)) {
        for (Vec3f* q : {&ln.a, &ln.b}) *q = {q->x * size, (q->y + y) * size, 4000.0f * size};
        ln.jitter_a = {ln.jitter_a.x * size, ln.jitter_a.y * size, 0}, ln.jitter_b = {ln.jitter_b.x * size, ln.jitter_b.y * size, 0};
        ln.shake_a *= size, ln.shake_b *= size;
        text.push_back(ln);
      }
    }
    if (picture && !finished) {
      // A track with a target in the middle; a marker for every beat slides in from the
      // right and reaches the target as the picture is drawn for that beat (Picture
      // timing ahead of the clock), so the player can see it coming. The target swells
      // for a moment as each marker reaches it.
      const double drawn = s.audio.heard_frame(now_ns) + (setting + s.renderer.display_lead_ms()) * TapCalibration::kRate / 1000.0;
      auto add = [&](float ax, float ay, float bx, float by, std::uint8_t g) {
        SceneLine ln;
        ln.a = {ax * size, ay * size, 4000.0f * size}, ln.b = {bx * size, by * size, 4000.0f * size};
        ln.colour = {g, g, g};
        text.push_back(ln);
      };
      constexpr float kY = 760.0f, kSpacing = 650.0f, kReach = 2000.0f;  // below the text; a beat apart
      add(-kReach, kY, kReach, kY, 110);  // the track
      const double beat = (drawn - double(TapCalibration::kLeadFrames)) / double(TapCalibration::kPeriod);  // beats since the first
      const double since = beat - std::floor(beat);  // of the last beat
      const bool hit = beat >= 0 && since < 0.12 && !TapCalibration::over(drawn);
      constexpr int kSides = 40;
      for (const float r : hit ? std::initializer_list<float>{110.0f, 135.0f, 160.0f} : std::initializer_list<float>{110.0f})
        for (int k = 0; k < kSides; ++k) {
          const double a0 = 6.28318530717958647692 * k / kSides, a1 = 6.28318530717958647692 * (k + 1) / kSides;
          add(r * float(std::cos(a0)), kY + r * float(std::sin(a0)), r * float(std::cos(a1)), kY + r * float(std::sin(a1)), 255);
        }
      // Only as many markers still to come as presses are still wanted (owner: the 16th is
      // the last one drawn; a missed one brings one more at the far end). The warm-up's
      // markers come first, and the ones already passed stay dimmed for a moment.
      const int wanted_left = TapCalibration::kWanted - cal.count();
      int coming = 0;
      for (int k = 0; k < TapCalibration::kClicks; ++k) {  // the markers: diamonds on the track
        const float x = float(double(k) - beat) * kSpacing;
        const bool passed = double(k) < beat - 0.5;  // (out of a press's reach, near enough)
        if (!passed && k >= TapCalibration::kWarmUp && k > cal.last_click() && coming++ >= wanted_left) break;
        if (x < -kSpacing * 0.6f || x > kReach) continue;
        const float d = 70.0f;
        const std::uint8_t g = x < 0 ? std::uint8_t(120) : std::uint8_t(255);  // (dimmed once past)
        add(x - d, kY, x, kY - d, g), add(x, kY - d, x + d, kY, g), add(x + d, kY, x, kY + d, g), add(x, kY + d, x - d, kY, g);
      }
    }
    tag_menu(text);  // (custom colours: Menu)
    to_instances(s, text, ++frame, lines);
    std::vector<vibr::Layer> out{{&lines, vibr::look_at({0, 0, 0}, {0, 0, 4730}, {0, -1, 0}), float(682 * 6.28318530717958647692 / 4096.0)}};
    draw_frame(s, out, 0.0, double(SDL_GetTicksNS() - now_ns) * 1e-6);
  }
  restore();
  return true;
}

// Slow work without a stall in the headset (Quest, session 5: a song's preparation took
// one frame of 117-175 ms): `work` runs on a worker thread while the last frame is drawn
// again at the headset's rate. On the monitor it simply runs. Events wait in SDL's queue.
template <typename F>
auto with_frames(Session& s, F work) -> decltype(work()) {
  if (!s.opt.headset || s.held_layers.empty()) return work();
  auto job = std::async(std::launch::async, std::move(work));
  s.holding = true;
  while (job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    SDL_PumpEvents();
    std::vector<vibr::Layer> layers = s.held_layers;
    draw_frame(s, layers, 0.0, 0.0);
  }
  s.holding = false;
  return job.get();
}

Result play_song(Session& s, int song, int track_index, int& points, std::uint32_t seed) {
  struct MenusAfter {  // whichever way the song ends, the Touch buttons go back to the menus'
    vibr::Display& d;
    ~MenusAfter() { d.set_input_mode(vibr::Display::InputMode::Menus); }
  } menus_after{s.renderer};
  // Timing: like the original, a little different on every play, unless consistent
  // mode is on (--fixed, or the Consistent courses setting, read at each song's start).
  const bool fixed = s.opt.fixed || s.save.options.fixed_courses;
  const CourseTiming timing = fixed ? CourseTiming::consistent() : CourseTiming::like_original(seed);
  // The song's sound, read and decoded, its blocks for the analysis, its loudness, and
  // what you hear: prepared off the frame loop (with_frames), as it is slow on a headset.
  struct Prepared {
    std::vector<std::int16_t> disc_pcm;
    std::vector<Block> blocks;
    std::vector<std::uint8_t> loudness;
    std::vector<std::int16_t> out;
  };
  Prepared prepared = with_frames(s, [&s, song, timing]() {
    Prepared p;
    if (!s.music) p.disc_pcm = s.disc.read_audio_track(song + 1);
    const std::vector<std::int16_t>& pcm = s.music ? s.music->pcm : p.disc_pcm;
    p.blocks = make_blocks(pcm, timing.lead);
    // How loud the music is around each block: gameplay's sounds follow it, as the
    // original's do (10% in silence to full in loud music).
    p.loudness = loudness_levels(p.blocks);
    // What you hear: the 8 s the original spends analysing ahead in silence, then the
    // song, lined up with what the analysis sees (the lead shifts both together).
    const std::int64_t frames = std::int64_t(pcm.size() / 2);
    p.out.reserve(std::size_t(kDelay) * 4 + pcm.size() + std::size_t(std::max<std::int64_t>(0, -timing.lead)) * 2);
    p.out.assign(std::size_t(kDelay) * 2 * 2, 0);
    const std::int64_t from = std::max<std::int64_t>(0, timing.lead);
    if (from < frames) p.out.insert(p.out.end(), pcm.begin() + std::ptrdiff_t(from) * 2, pcm.begin() + std::ptrdiff_t(frames) * 2);
    if (timing.lead < 0) p.out.insert(p.out.begin() + std::ptrdiff_t(kDelay) * 4, std::size_t(-timing.lead) * 2, 0);
    return p;
  });
  const std::vector<Block>& blocks = prepared.blocks;
  const std::vector<std::uint8_t>& loudness = prepared.loudness;
  std::vector<std::int16_t>& out = prepared.out;
  const auto total = static_cast<std::int32_t>(blocks.size() * kBlockSize);

  // Testing: start the song this far in (--last), the music from there and no 8 s wait.
  std::int32_t skip = 0;
  if (s.opt.last_seconds > 0) {
    const auto last = static_cast<std::int32_t>(s.opt.last_seconds * kSampleRate);
    skip = std::max<std::int32_t>(0, total - last) / kBlockSize * kBlockSize;
  }
  // Where the music buffer starts, in heard samples from the song's start.
  std::int32_t buffer_start = skip > 0 ? skip : -kDelay;
  const std::int32_t music_start = buffer_start;  // where `out` starts (a pause's run-up plays from later in it)
  if (skip > 0) {
    const std::size_t cut = std::min(out.size(), std::size_t(kDelay + skip) * 4);  // 2 frames of stereo per heard sample
    out.erase(out.begin(), out.begin() + std::ptrdiff_t(cut));
  }

  CourseGenerator gen(s.script);
  gen.set_timing(timing);
  gen.set_difficulty(s.save.options.difficulty);  // (task 4.17; read at the song's start)
  // Starts a generator on this song: the game's own round, or the player's track on its "disc".
  auto start = [&](CourseGenerator& g) {
    if (s.music) {
      g.prepare_disc(s.music_disc, s.music_key);
      g.start_track(s.music_track, 0);
    } else {
      g.start_round(song, 0);
    }
  };
  start(gen);
  Gameplay game;
  RoundSetup setup;
  setup.course = s.music ? (s.course >= Course::OneTrack ? s.course : Course::OneTrack) : s.song ? Course::Bronze : s.course;
  setup.course_seconds = total / kSampleRate;
  setup.points = points;
  setup.track_index = track_index;
  setup.demo = s.demo;
  game.start(setup);
  if (s.demo) game.seed_demo(seed);
  // In a headset the whole line is in view: passed parts stay on it for 8 s, and
  // every part built so far (8 s ahead) shows ahead.
  if (far_road(s)) game.keep_passed(static_cast<std::int32_t>(std::min(see_behind(s), kWholeSongSeconds) * kSampleRate));
  game.set_trail_min_speed(kTrailMinSpeed);  // (only the far road's trail behind her uses it)
  // The scroll speed (display only), eased towards the setting so a change in the
  // pause or between songs stretches or squeezes the road smoothly.
  // (The idle demo shows the original's look: 1x.)
  auto scroll_wanted = [&]() { return s.demo ? 1.0 : s.save.options.scroll_speed; };
  double scroll_shown = scroll_wanted();
  game.set_scroll(scroll_shown);
  // Changed mid-song (L2/R2 in a headset, PageUp/PageDown): saved, and "Scroll speed:
  // 2x" shows for a moment (below Vibri in a headset, at the bottom on the monitor).
  double scroll_note = 0;
  // The course wrapped round the player (task 4.11; headset, if set): Vibri's place on
  // the ring moves with the heard music (still while paused, back while rewinding).
  CourseWrap wrap;
  std::int32_t wrap_heard = 0;
  bool wrap_started = false;
  // The timing feedback (task 4.12): the last hit's offset, shown for a moment.
  constexpr double kTimingNoteSeconds = 0.6;
  double timing_ms = 0, timing_note = 0;
  auto scroll_changed = [&](int d) {
    if (!change_scroll_speed(s, d)) return;
    scroll_note = 1.5;
    if (s.write_save) s.write_save();
  };
  // Further ahead than play builds: a second generator on the same music and timing,
  // running ahead, drawn only. Its course is the same course (it is checked below),
  // so its parts become play's own as play reaches them.
  CourseGenerator far(s.script);
  far.set_timing(timing);
  far.set_difficulty(gen.difficulty());  // (the same course as play's)
  start(far);
  std::size_t far_block = 0;
  // (Read every frame: the pause's settings can change it mid-song.)
  auto far_extra = [&s] { return far_road(s) ? static_cast<std::int32_t>(std::clamp(see_ahead(s) - 8.0, 0.0, kWholeSongSeconds) * kSampleRate) : 0; };
  bool far_warned = false;
  s.scene.reset(true, false);
  s.hud.start_round(track_index, !s.song && !s.music && !s.demo);  // the "Round 1/2" sign belongs to the two-song courses
  // A speed course: its level (1-3) for the dial's label; the generator follows the
  // course's beat (Gameplay::fixed_period) instead of the script's speed list.
  const int speed_level = game.speed_course() ? int(setup.course) - int(Course::Speed1) + 1 : 0;
  if (speed_level) s.hud.start_speed(speed_level);

  std::printf("\nSong %d: %d s, %s course (lead %d, frame phase %.0f)\n", song, total / kSampleRate,
              fixed ? "fixed" : "original-style", timing.lead, double(timing.frame_phase_q16) / 65536.0);

  std::size_t next_block = 0, fed = 0;
  bool was_selecting = gen.selecting(), ended = false;
  // The course is analysed eight seconds ahead of what is heard.
  auto analyse_up_to = [&](std::int32_t heard) {
    while (next_block < blocks.size() && std::int32_t(next_block * kBlockSize) <= heard + kDelay) {
      const auto t = static_cast<std::int32_t>(next_block * kBlockSize);
      gen.process(t, blocks[next_block++]);
      for (; fed < gen.events().size(); ++fed) game.add_event(gen.events()[fed]);
      if (was_selecting && !gen.selecting() && !speed_level) game.quiet(t);  // (not in speed courses, as the original)
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
  s.audio.play_music(out);  // (`out` kept: after a pause the run-up plays it again from a little earlier)
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
  std::deque<std::vector<vibr::LineInstance>> score_number_lines;  // (a deque: layers point into it)
  Result result = Result::Clear;

  // The sound effects option silences gameplay's sounds only, as in the original
  // (the menus' music and voice are not effects). Read as each sound comes: the
  // pause can change it.
  const bool& effects_on = s.save.options.sound_effects;
  // The full combo (task 4.18): a song cleared with no miss, celebrated if set; a
  // whole course with none, more so, at the end of its last song.
  int song_hits = 0, song_misses = 0;
  bool celebrating = false, whole_course = false;
  double fc_time = 0;
  const bool clean_before = track_index == 0 || s.course_clean;
  s.course_clean = false;  // (left so unless this song is a full combo)
  // A gameplay sound's volume at the music's loudness where it is heard now (none
  // before the song plays: the original's level is 0 then).
  auto fx_volume = [&](int volume) {
    const std::int32_t at = last_position;
    const int level = at >= 0 && std::size_t(at / kBlockSize) < loudness.size() ? loudness[std::size_t(at / kBlockSize)] : 0;
    return loud_volume(volume, level);
  };
  auto handle = [&](const std::vector<GameEvent>& events) {
    bool form_up = false, hit = false, miss = false;
    for (const GameEvent& e : events) {
      switch (e.type) {
        case GameEvent::Type::Hit:
          if (effects_on) s.fx.number(16 + e.road, fx_volume(127), 3);
          s.scene.set_move(e.road, false);
          hit = true;
          ++song_hits;
          break;
        case GameEvent::Type::Stray: {
          const int n = e.value == 1 ? 31 : e.value == 2 ? 28 : e.value == 4 ? 29 : 27;
          if (effects_on) s.fx.number(n, fx_volume(100), 3);
          s.scene.set_move(e.road, true);
          break;
        }
        case GameEvent::Type::Miss:
          if (effects_on) s.fx.play(1, 10, fx_volume(127), 2);
          if (s.save.options.vibration) {
            for (auto& [id, pad] : s.pads) SDL_RumbleGamepad(pad, 0x6000, 0xC000, 200);
            s.renderer.rumble(0.2f, 0.6f);  // (the headset's own controllers, Touch)
          }
          miss = true;
          ++song_misses;
          break;
        case GameEvent::Type::FormUp:
          if (effects_on) s.fx.play(1, 14, fx_volume(127), 4);
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

  // Ours, not the original's (it has no pause): Start (or P / Tab) freezes the song and
  // shows the settings over the frozen course. Going on, as the Resume setting says:
  // Rewind runs the road back about 2 s, to where no part is at Vibri, with a
  // tape-rewind sound, then plays from there with Vibri clearing again what she had
  // cleared, the rules standing still and presses ignored until the music is back
  // where it stopped; Countdown counts 3, 2, 1 over the frozen course and goes on
  // from the same place.
  enum class Going { No, Rewinding, Counting, RunUp };
  bool paused = false, pause = false, resume = false;
  Going going = Going::No;
  double going_t = 0, rewind_seconds = 0;
  std::int32_t pause_at = 0, run_from = 0;
  std::vector<Part> replay;
  std::size_t replayed = 0;
  constexpr double kCountStep = 0.5;  // seconds per number of the count-in
  SettingsPanel::Input pin;
  Hud::Layer panel_layer;
  std::vector<vibr::LineInstance> panel_lines, scroll_note_lines, timing_lines, fc_lines;

  s.stats.reset_audio();
  for (;;) {
    const std::uint64_t frame_start_ns = SDL_GetTicksNS();
    presses.clear();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (window_key(s, e)) continue;  // (F11 or Alt+Enter: window or fullscreen)
      if (is_mouse(e)) {  // (task 4.16 G: in play the camera; in the pause our settings)
        const MouseInput m = mouse_event(s, e, !paused);
        if (paused && s.panel) panel_mouse(s, *s.panel, m, pin);
        continue;
      }
      if (e.type == SDL_EVENT_KEY_DOWN && camera_key(s, e.key)) continue;  // (Ctrl and the arrows: the camera)
      if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_HOME && !e.key.repeat) {
        r3_pressed(s, false);
        continue;
      }
      switch (e.type) {
        case SDL_EVENT_QUIT: return Result::Quit;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED: s.renderer.request_resize(); break;
        case SDL_EVENT_KEY_DOWN:
          if (e.key.repeat) break;
          if (s.demo) return Result::Abort;  // any key ends the demo
          if (paused) {
            switch (e.key.key) {
              case SDLK_UP: case SDLK_W: pin.up = true; break;
              case SDLK_DOWN: case SDLK_S: pin.down = true; break;
              case SDLK_LEFT: case SDLK_A: pin.left = true; break;
              case SDLK_RIGHT: case SDLK_D: pin.right = true; break;
              case SDLK_RETURN: case SDLK_SPACE: case SDLK_Z: pin.confirm = true; break;
              case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_X: pin.cancel = true; break;
              case SDLK_P: case SDLK_TAB: resume = true; break;
              case SDLK_F3: l3_pressed(s); break;
              default: break;
            }
            break;
          }
          if (e.key.key == SDLK_P || e.key.key == SDLK_TAB) {
            pause = true;
            break;
          }
          if (e.key.key == SDLK_F3) {
            l3_pressed(s);
            break;
          }
          if (e.key.key == SDLK_ESCAPE) return Result::Abort;
          if (e.key.key == SDLK_LEFTBRACKET || e.key.key == SDLK_RIGHTBRACKET) {
            s.audio.adjust_offset(e.key.key == SDLK_LEFTBRACKET ? -5 : 5);
            s.save.options.audio_offset_ms = s.audio.offset_ms();
            std::printf("audio offset %+.0f ms (latency now %.0f ms)\n", s.audio.offset_ms(), s.audio.latency_ms());
          } else if (e.key.key == SDLK_MINUS || e.key.key == SDLK_EQUALS) {
            change_line_width(s, e.key.key == SDLK_EQUALS);
          } else if (e.key.key == SDLK_PAGEUP || e.key.key == SDLK_PAGEDOWN) {  // the scroll speed, mid-song
            scroll_changed(e.key.key == SDLK_PAGEUP ? 1 : -1);
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
          if (s.demo) return Result::Abort;  // any button ends the demo
          if (paused) {
            switch (e.gbutton.button) {
              case SDL_GAMEPAD_BUTTON_DPAD_UP: pin.up = true; break;
              case SDL_GAMEPAD_BUTTON_DPAD_DOWN: pin.down = true; break;
              case SDL_GAMEPAD_BUTTON_DPAD_LEFT: pin.left = true; break;
              case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: pin.right = true; break;
              case SDL_GAMEPAD_BUTTON_SOUTH: pin.confirm = true; break;
              case SDL_GAMEPAD_BUTTON_EAST: pin.cancel = true; break;
              case SDL_GAMEPAD_BUTTON_BACK: case SDL_GAMEPAD_BUTTON_START: resume = true; break;
              case SDL_GAMEPAD_BUTTON_RIGHT_STICK: r3_pressed(s, false); break;
              case SDL_GAMEPAD_BUTTON_LEFT_STICK: l3_pressed(s); break;
              default: break;
            }
            break;
          }
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START) {  // Start: our pause and settings (owner's choice)
            pause = true;
            break;
          }
          // Select (View): the original's own Start, which ends the song and asks "again?".
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) return Result::Abort;
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) r3_pressed(s, false);  // R3
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK) l3_pressed(s);           // L3
          if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT || e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
            change_jitter_scale(s, e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT);  // (unused by play)
          if (const std::uint8_t b = pad_bits(e.gbutton.button)) presses.push_back({b, e.gbutton.timestamp});
          break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
          // In a headset L2 and R2 change the scroll speed mid-song (owner); LB and RB block
          // and loop. On the monitor the triggers block and loop, as the original's L2 and R2.
          if (s.opt.vr) {
            if (const int d = trigger_edge(s, e.gaxis); d && !s.demo) scroll_changed(d);
          } else if (const std::uint8_t b = trigger_pulled(s, e.gaxis); b && !paused) {
            presses.push_back({b, e.gaxis.timestamp});
          }
          break;
        default: break;
      }
    }

    const std::uint64_t now_ns = SDL_GetTicksNS();
    const double dt = double(now_ns - last_ns) * 1e-9;
    zoom_or_circle_with_stick(s, dt, play_zoom(s));
    if (s.opt.headset && !s.demo) {  // Touch: the right stick flicked right or left, the scroll speed (as a gamepad's R2 and L2)
      const vibr::Display::ControllerSticks c = s.renderer.controller_sticks();
      if (!s.stick_flicked && std::abs(c.right_x) > 0.7f && std::abs(c.right_x) > 2.0f * std::abs(c.right_y)) {
        s.stick_flicked = true;
        scroll_changed(c.right_x > 0 ? 1 : -1);
      } else if (std::abs(c.right_x) < 0.4f) {
        s.stick_flicked = false;
      }
    }
    // Touch buttons: play's in play, the menus' in the pause.
    s.renderer.set_input_mode(paused || celebrating ? vibr::Display::InputMode::Menus : vibr::Display::InputMode::Play);
    last_ns = now_ns;
    scroll_shown = ease_scroll(scroll_shown, scroll_wanted(), dt);
    scroll_note = std::max(0.0, scroll_note - dt);
    if (!paused) timing_note = std::max(0.0, timing_note - dt);
    game.set_scroll(scroll_shown);

    if (pause && !paused && going == Going::No && s.panel && !celebrating) {
      paused = true;
      pause_at = last_position;
      s.audio.play_music({});
      for (auto& [id, pad] : s.pads) SDL_RumbleGamepad(pad, 0, 0, 0);
      s.renderer.rumble(0.0f, 0.0f);
      setup_panel(*s.panel, s.opt.headset, true);
      s.panel->set_wraps(true);
      s.panel->set_active(true);
      s.panel->set_place(0.6f, 14, s.opt.vr ? 0.25f : 1.0f);  // in a headset 1 m away, in front of Vibri
      s.panel->set_columns(-1600.0f, 1600.0f);
      s.fx.number(1, 90, 5);
      std::printf("Paused\n");
    }
    pause = false;
    if (paused) {
      held_directions(s, pin);
      const SettingsPanel::Action act = s.panel->tick(pin, dt, s.save.options, s.opt.headset);
      pin = {};
      panel_sound(s, act);
      switch (act) {
        case SettingsPanel::Action::Changed: apply_settings(s); break;
        case SettingsPanel::Action::Recentre: s.renderer.recentre(); break;
        case SettingsPanel::Action::Calibrate:
          if (!run_calibration(s, *s.panel, false)) return Result::Quit;
          last_ns = SDL_GetTicksNS();
          break;
        case SettingsPanel::Action::CalibratePicture:
          if (!run_calibration(s, *s.panel, true)) return Result::Quit;
          last_ns = SDL_GetTicksNS();
          break;
        case SettingsPanel::Action::QuitSong:
          if (s.write_save) s.write_save();
          return Result::Abort;
        case SettingsPanel::Action::Close: case SettingsPanel::Action::LeaveTop: resume = true; break;
        default: break;
      }
      if (resume) {
        paused = false;
        going_t = 0;
        if (s.save.options.resume_countdown) {
          going = Going::Counting;
          s.fx.number(1, 90, 5);
        } else {
          going = Going::Rewinding;
          const std::int32_t earliest = std::max(music_start, pause_at - 6 * kSampleRate);
          run_from = game.clear_before(std::max(earliest, pause_at - 2 * kSampleRate), kSampleRate * 2 / 5, earliest);
          rewind_seconds = std::clamp(double(pause_at - run_from) / kSampleRate * 0.35, 0.5, 1.5);
          s.audio.play_music(rewind_sound(out, music_start, run_from, pause_at, rewind_seconds));
          replay = game.judged(run_from, pause_at);
          replayed = 0;
          std::printf("Going on from %.1f s earlier\n", double(pause_at - run_from) / kSampleRate);
        }
        if (s.write_save) s.write_save();
      }
    }
    resume = false;
    // The rewind or the count-in over, the music goes on: from where the road was run
    // back to, or from where it stopped.
    if (going == Going::Rewinding || going == Going::Counting) {
      const double before = going_t;
      going_t += dt;
      if (going == Going::Counting && going_t < 3 * kCountStep && int(going_t / kCountStep) != int(before / kCountStep))
        s.fx.number(1, 90, 5);  // a click for each number
      if (going_t >= (going == Going::Rewinding ? rewind_seconds : 3 * kCountStep)) {
        const std::int32_t from = going == Going::Rewinding ? run_from : pause_at;
        const std::size_t first = std::min(out.size(), std::size_t(from - music_start) * 4);  // 2 stereo frames per heard sample
        s.audio.play_music(std::vector<std::int16_t>(out.begin() + std::ptrdiff_t(first), out.end()));
        buffer_start = from;
        going = going == Going::Rewinding ? Going::RunUp : Going::No;
      }
    }

    // Heard position, 22.05 kHz samples from the song's start (negative during the 8 s ahead).
    auto heard_at = [&](std::uint64_t ns) {
      return static_cast<std::int32_t>(std::floor(s.audio.heard_frame_at(now_ns, ns) / 2.0)) + buffer_start;
    };
    std::int32_t raw = pause_at;
    if (going == Going::Rewinding) {  // eased back from where it stopped
      const double u = std::clamp(going_t / rewind_seconds, 0.0, 1.0);
      raw = pause_at - static_cast<std::int32_t>(double(pause_at - run_from) * u * u * (3.0 - 2.0 * u));
    } else if (!paused && going != Going::Counting) {
      raw = heard_at(now_ns);
    }
    if (going == Going::RunUp) {
      // Vibri clears again what she had cleared, as the music reaches it.
      for (; replayed < replay.size() && replay[replayed].time <= raw; ++replayed) {
        const Part& p = replay[replayed];
        if (p.missed) continue;
        s.scene.set_move(p.road, false);
        if (effects_on) s.fx.number(16 + p.road, 127, 3);
      }
      if (raw >= pause_at) going = Going::No;
    }
    const bool frozen = paused || going != Going::No;  // the rules stand still
    const std::int32_t heard = frozen ? last_position : std::max(raw, last_position);
    // Where the road is drawn: what is heard, plus Picture timing (ours) to make up for
    // the display's own delay; judging stays on `heard`.
    // In a headset also the display's own lead (task 4.5): the frame being made now reaches
    // the eyes this much later (the runtime's predicted display time), so it shows that moment.
    const auto picture_ahead = static_cast<std::int32_t>((s.save.options.picture_offset_ms + s.renderer.display_lead_ms()) * kSampleRate / 1000.0);
    const std::int32_t view_at = frozen ? std::min(raw, pause_at) : heard + picture_ahead;
    if (frozen || celebrating) presses.clear();
    // (In a headset always; on a monitor with See behind past the original's 2 s. A
    // trail kept is not dropped when the setting goes back: it simply ages out.)
    if (far_road(s)) game.keep_passed(static_cast<std::int32_t>(std::min(see_behind(s), kWholeSongSeconds) * kSampleRate));

    if (!frozen) analyse_up_to(heard);

    // Presses first, each judged where the music was when it happened.
    Outcome outcome = Outcome::Playing;
    std::stable_sort(presses.begin(), presses.end(), [](const PressEvent& a, const PressEvent& b) { return a.ns < b.ns; });
    for (const PressEvent& p : presses) {
      const std::int32_t at = std::clamp(heard_at(p.ns), last_position, heard);
      outcome = game.update(at, p.bits, s.scene.move_done(0.5));
      last_position = at;
      for (const GameEvent& ev : game.events())  // how early or late, for the performance overlay
        if (ev.type == GameEvent::Type::Hit) {
          const double ms = double(at - ev.time) * 1000.0 / kSampleRate;  // press minus perfect: - early, + late
          s.stats.add_press(ms);
          timing_ms = ms;
          timing_note = kTimingNoteSeconds;
        }
      handle(game.events());
      if (outcome != Outcome::Playing) break;
    }
    if (outcome == Outcome::Playing && !frozen && !celebrating) {  // (the song is over while celebrating)
      outcome = game.update(heard, 0, s.scene.move_done(0.5));
      if (speed_level) gen.set_fixed_cue(game.fixed_period());
      last_position = heard;
      handle(game.events());
    }

    // The sound clock for the overlay, while the song plays (not frozen, not rewinding).
    if (frozen) s.stats.reset_audio();
    else s.stats.add_audio(now_ns, double(heard) / kSampleRate);  // (play's own reading: the clock is read once a frame)
    const std::uint64_t build_start_ns = SDL_GetTicksNS();

    // Draw. The jitter changes at the original's 50 Hz, whatever the display does.
    Scene::Frame f;
    const bool still_frame = paused || going == Going::Rewinding || going == Going::Counting;  // Vibri waits
    f.seconds = still_frame ? 0.0 : dt;
    f.jitter_seed = next_random(static_cast<std::uint32_t>(double(now_ns - start_ns) * 1e-9 * kOriginalRefresh) * 2654435761u);
    f.speed = scroll_shown == 1.0 ? game.speed() : Fixed::from(game.speed().to_double() * scroll_shown);  // Vibri runs as fast as the road comes
    const bool far_on = far_road(s);
    f.road = frozen || picture_ahead != 0 ? game.road_at(view_at, far_on) : game.road(far_on);
    if (far_on && far_extra() > 0 && !speed_level) {  // (a speed course's beat changes as the player scores)
      // At most about 2 ms of analysis a frame: with the whole song asked for, the far
      // course fills in over the first second or so instead of in one long frame.
      const std::uint64_t far_budget_end = SDL_GetTicksNS() + 2'000'000;
      int far_count = 0;
      while (far_block < blocks.size() && std::int32_t(far_block * kBlockSize) <= heard + kDelay + far_extra()) {
        if ((++far_count & 31) == 0 && SDL_GetTicksNS() > far_budget_end) break;
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
        if (ahead[i].time <= view_at + kDelay + far_extra() && game.preview(ahead[i], view_at, piece)) f.road.push_back(piece);
      std::stable_sort(f.road.begin(), f.road.end(), [](const RoadPiece& a, const RoadPiece& b) { return a.position < b.position; });
    }
    f.still = game.near_part();
    f.shake = game.shake();
    f.form = game.form();
    f.stage = game.stage();
    SceneView view;
    s.scene.draw(f, scene_lines, view);
    const bool wrapped = s.opt.vr && s.save.options.vr_wrap;
    const float vz = s.scene.vibri_z();
    const bool staged = beside_vibri(s);  // (the view beside Vibri, and the HUD with her)
    if (staged) {
      view = beside_view(s, vz);
      evo_behind_road(scene_lines, s.scene.evo_first());
    }
    if (wrapped) {
      wrap.radius = float(s.save.options.vr_wrap_radius / double(s.opt.vr_scale));
      // Her walking speed: the speed of the parts nearest her, weighted to the nearest,
      // so the pieces by her stand still in the room (no easing to lag behind a change).
      const double near_rate = game.ground_rate_near(1.0);
      if (near_rate > 0) wrap.rate = near_rate * 600.0 * scroll_shown * kSampleRate;  // units a second
      if (wrap_started) wrap.advance(wrap.rate * double(view_at - wrap_heard) / kSampleRate);
      wrap_started = true;
      wrap_heard = view_at;
      view = wrap.view();
      wrap.bend_lines(scene_lines, vz);
    }
    // The HUD's viewer and its place: as seen from the view, or (wrapped) from the
    // ring's middle and then carried to Vibri's place on it.
    const Vec3f hud_eye = wrapped ? wrap.eye_play(vz) : view.eye;
    auto to_ring = [&](std::vector<SceneLine>& v) {
      if (wrapped) wrap.place(v, vz);
    };
    ++display_frame;
    auto convert = [&](const std::vector<SceneLine>& in, std::vector<vibr::LineInstance>& dst) {
      dst.clear();
      for (const SceneLine& l : in) {
        // (Jittered afresh every headset frame, the classic screen too; on a monitor as its Wobble hold says.)
        const Vec3f a = wobble(s, l.a, l.jitter_a, l.vertex_a, l.shake_a, display_frame);
        const Vec3f b = wobble(s, l.b, l.jitter_b, l.vertex_b, l.shake_b, display_frame);
        const Rgb c = shown_colour(l.colour, l.part, s.save.options.colours);  // (custom colours; the rest inverted on a light background)
        dst.push_back({{a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f}});
      }
    };
    auto matrix = [](const SceneView& v) {
      return vibr::look_at({v.eye.x, v.eye.y, v.eye.z}, {v.target.x, v.target.y, v.target.z}, {0, -1, 0});
    };
    convert(scene_lines, lines);
    std::vector<vibr::Layer> layers{{&lines, matrix(view), view.fov_y}};
    score_number_lines.clear();

    // The meters and the sign, each through its own camera, over the scene.
    Hud::Frame h;
    h.seconds = still_frame ? 0.0 : dt;
    h.jitter_seed = next_random(f.jitter_seed);
    h.score = game.score();
    h.combo = game.combo();
    h.demo = s.demo;
    const int seconds = view_at >= 0 ? view_at / kSampleRate : 0;  // whole seconds, as the original counts
    h.time_left = 1.0 - double(seconds) / std::max(1, setup.course_seconds);
    h.speed_course = speed_level;
    h.target_speed = game.target_speed().to_double();
    s.hud.draw(h, hud_layers);
    hud_lines.resize(hud_layers.size());
    for (std::size_t i = 0; i < hud_layers.size(); ++i) {
      if (staged && hud_layers[i].time) {
        // In a headset the original's time meter sits below Vibri (task 4.9).
        std::vector<SceneLine> bar;
        meter_below_vibri(hud_layers[i], s.scene.vibri_z(), hud_eye, bar);
        to_ring(bar);
        convert(bar, hud_lines[i]);
        layers.push_back({&hud_lines[i], matrix(view), view.fov_y});
        continue;
      }
      if (staged && hud_layers[i].score) {
        // In a headset the score floats above Vibri in the world (task 4.8), not in
        // front of the player: at a fixed height, turned to face the view.
        std::vector<SceneLine> above;
        score_above_vibri(hud_layers[i].lines, s.scene.vibri_z(), hud_eye, above);
        to_ring(above);
        convert(above, hud_lines[i]);
        layers.push_back({&hud_lines[i], matrix(view), view.fov_y});
        continue;
      }
      if (staged && s.opt.vr && hud_layers[i].speed) {
        // In a headset the speed dial sits higher above Vibri (owner's request): raised
        // in its own camera's space by 800, about 80% of its height (its lines span
        // y -1,333 to -307 there; y is down).
        constexpr float kSpeedDialRaise = 800.0f;
        for (SceneLine& l : hud_layers[i].lines) l.a.y -= kSpeedDialRaise, l.b.y -= kSpeedDialRaise;
      }
      convert(hud_layers[i].lines, hud_lines[i]);
      layers.push_back({&hud_lines[i], matrix(hud_layers[i].view), hud_layers[i].view.fov_y});
    }
    // Ours (task 4.10): the score in numbers as well, if set. In a headset below the
    // meter under Vibri; on the monitor near the bottom, just above the meter.
    if (s.save.options.score_numbers && s.panel && !s.demo) {
      const int shown = std::min(game.score(), Hud::kMaxScore);
      if (staged) {
        std::vector<SceneLine> placed;
        number_below_vibri(s.panel->text_lines(std::to_string(shown), 1.2f, h.jitter_seed, 4), s.scene.vibri_z(), hud_eye, placed);
        to_ring(placed);
        score_number_lines.emplace_back();
        convert(placed, score_number_lines.back());
        layers.push_back({&score_number_lines.back(), matrix(view), view.fov_y});
      } else {
        const Hud::Layer number = s.panel->draw_score(shown, h.jitter_seed);
        score_number_lines.emplace_back();
        convert(number.lines, score_number_lines.back());
        layers.push_back({&score_number_lines.back(), matrix(number.view), number.view.fov_y});
      }
    }
    if (s.save.options.timing_feedback && timing_note > 0 && s.panel && !s.demo && !paused) {
      // The last hit's offset in ms, full brightness, dimming over its last 0.2 s. In a
      // headset just below the road (above the time meter), facing the eye; on the
      // monitor above the score's number.
      const float bright = float(std::min(1.0, timing_note / 0.2));
      const std::string text = SettingsPanel::offset_text(timing_ms);
      timing_lines.clear();
      if (staged) {
        std::vector<SceneLine> placed;
        number_below_vibri(s.panel->text_lines(text, kTimingScale, h.jitter_seed, 4), s.scene.vibri_z(), hud_eye, placed, kTimingBelowRoad);
        to_ring(placed);
        const auto grey = static_cast<std::uint8_t>(bright * 255.0f);
        for (SceneLine& l : placed) l.colour = {grey, grey, grey};
        convert(placed, timing_lines);
        layers.push_back({&timing_lines, matrix(view), view.fov_y});
      } else {
        const Hud::Layer note = s.panel->draw_timing(text, bright, h.jitter_seed);
        convert(note.lines, timing_lines);
        layers.push_back({&timing_lines, matrix(note.view), note.view.fov_y});
      }
    }
    if (celebrating && s.panel) {  // "Full Combo!", over everything but the pause
      fc_lines.clear();
      if (staged) {
        std::vector<SceneLine> placed;
        number_below_vibri(whole_course ? s.panel->full_course_combo(fc_time, h.jitter_seed) : s.panel->full_combo(fc_time, h.jitter_seed),
                           s.scene.vibri_z(), hud_eye, placed, whole_course ? kFullCourseComboAboveRoad : kFullComboAboveRoad);
        to_ring(placed);
        convert(placed, fc_lines);
        layers.push_back({&fc_lines, matrix(view), view.fov_y});
      } else {
        const Hud::Layer word = s.panel->draw_full_combo(fc_time, h.jitter_seed, whole_course);
        convert(word.lines, fc_lines);
        layers.push_back({&fc_lines, matrix(word.view), word.view.fov_y});
      }
    }
    if (scroll_note > 0 && s.panel && !paused) {  // the scroll speed, just changed
      scroll_note_lines.clear();
      if (staged) {
        std::vector<SceneLine> placed;
        number_below_vibri(s.panel->text_lines(s.panel->scroll_line(s.save.options), 1.0f, h.jitter_seed, 4), s.scene.vibri_z(), hud_eye, placed, kScrollNoteBelowRoad);
        to_ring(placed);
        convert(placed, scroll_note_lines);
        layers.push_back({&scroll_note_lines, matrix(view), view.fov_y});
      } else {
        const Hud::Layer note = s.panel->draw_scroll_speed(s.save.options, h.jitter_seed);
        convert(note.lines, scroll_note_lines);
        layers.push_back({&scroll_note_lines, matrix(note.view), note.view.fov_y});
      }
    }
    if (paused || going == Going::Counting) {  // the settings, or the count-in's number, over everything
      panel_layer = paused ? s.panel->draw(s.save.options, s.opt.headset, f.jitter_seed)
                           : s.panel->draw_big(std::to_string(3 - std::min(2, int(going_t / kCountStep))), f.jitter_seed);
      if (paused && !s.opt.headset) {  // the keys and the mouse, at the bottom (task 4.16 G)
        const Hud::Layer keys = s.panel->draw_keys(next_random(f.jitter_seed));
        panel_layer.lines.insert(panel_layer.lines.end(), keys.lines.begin(), keys.lines.end());  // (the same camera)
      }
      convert(panel_layer.lines, panel_lines);
      layers.push_back({&panel_lines, matrix(panel_layer.view), panel_layer.view.fov_y});
    }
    const std::uint64_t built_ns = SDL_GetTicksNS();
    draw_frame(s, layers, double(build_start_ns - frame_start_ns) * 1e-6, double(built_ns - build_start_ns) * 1e-6);

    title_timer -= dt;
    if (title_timer <= 0) {
      static const char* forms[] = {"angel", "rabbit", "frog", "worm"};
      char title[160];
      std::snprintf(title, sizeof title, "Vib-Ribbon  song %d  score %d  %s  %s", song, game.score(), forms[game.form()],
                    heard < 0 ? "(analysing ahead)" : "");
      SDL_SetWindowTitle(s.window, title);
      title_timer = 0.25;
    }

    // A full combo: the song is over, the rules stand still, and "Full Combo!" plays for
    // its 2.6 s over the running scene before the clear goes on as usual.
    if (celebrating) {
      fc_time += dt;
      if (fc_time >= (whole_course ? SettingsPanel::kFullCourseComboSeconds : SettingsPanel::kFullComboSeconds)) outcome = Outcome::Clear;
    } else if (outcome == Outcome::Clear && s.save.options.fc_celebration && !s.demo && !course_info(s.course).auto_play &&
               song_misses == 0 && song_hits > 0 && game.score() > 0) {
      s.course_clean = clean_before;
      whole_course = clean_before && s.course_songs > 1 && track_index == s.course_songs - 1;
      std::printf("Full %scombo: %d hits, no misses\n", whole_course ? "course " : "", song_hits);
      celebrating = true;
      fc_time = 0;
      if (effects_on) s.fx.play(1, 14, 127, 4);  // the form-up sound
      s.scene.play_evo();
      continue;
    }
    if (outcome == Outcome::Clear) {
      std::printf("Song %d clear: score %d + end bonus %d\n", song, game.score(), game.bonus());
      if (game.speed_course()) {
        // A speed course is won on time: the seconds it took to reach the top speed.
        std::printf("Speed course clear in %d s\n", game.seconds());
        points = game.seconds();
        s.final_score = game.seconds();
        s.final_bonus = 0;
        s.final_form = game.form();
        result = Result::Clear;
        break;
      }
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
      s.final_time_up = game.time_up();
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
int g_demo_count = 0;  // which demo comes next, kept across a change of language

// The packaged headset program ("Vib-Ribbon VR.exe", task 4.16 I) is this program built
// with VIB_DEFAULT_VR: it starts in the headset without --vr.
#ifdef VIB_DEFAULT_VR
constexpr bool kDefaultVr = true;
#else
constexpr bool kDefaultVr = false;
#endif
bool g_disc_changed = false;  // Change disc in the settings: the next run() takes the saved disc

// The player picks their disc image (.cue) in the system's own file dialog (task 4.16
// I). Waits for the answer, keeping the window's events moving; empty if cancelled.
std::string pick_disc(SDL_Window* window, const std::string& start_at) {
  struct Answer {
    std::atomic<bool> done{false};
    std::string path;
  } answer;
  static const SDL_DialogFileFilter filters[] = {{"Disc image (.cue)", "cue"}};
  SDL_ShowOpenFileDialog(
      [](void* data, const char* const* files, int) {
        auto* a = static_cast<Answer*>(data);
        if (files && files[0]) a->path = files[0];
        a->done = true;
      },
      &answer, window, filters, 1, start_at.empty() ? nullptr : start_at.c_str(), false);
  while (!answer.done) {
    SDL_PumpEvents();
    SDL_Delay(10);
  }
  return answer.path;
}

int run(int argc, char** argv) {
  Args opt;
  opt.vr = kDefaultVr;
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
    else if (a == "--flat") opt.vr = false;  // (the monitor, from the headset program)
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
#ifdef __ANDROID__
  opt.vr = true;  // the headset build is always in the headset
  if (opt.cue.empty()) opt.cue = find_disc_on_headset();
  // Keep running while the activity is paused: OpenXR's session states say when to
  // draw (SDL would otherwise stop the loop inside its event polling).
  SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
#endif
  opt.headset = opt.vr;
  if (!opt.seed_given) opt.seed = static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());

  // Saved high scores and options, in the per-user folder SDL picks for the platform.
  // Read first: they say where the disc is when none is named (task 4.16 I).
  std::string save_path;
#ifdef __ANDROID__
  save_path = headset_folder() + "/save.txt";  // (beside the disc: adb pull can fetch it)
#else
  if (char* pref = SDL_GetPrefPath("VibRibbonVR", "VibRibbonVR")) {
    save_path = std::string(pref) + "save.txt";
    SDL_free(pref);
  }
#endif
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

  SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");  // (Windows: our icon, resource 1, on the window and the taskbar)
  SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
  // The disc: named on the command line, else the one picked before (save.txt's
  // disc_path), else the player picks it now in a file dialog; remembered.
  if (g_disc_changed) opt.cue = save.options.disc_path, g_disc_changed = false;
  const bool cue_named = !opt.cue.empty();
  if (opt.cue.empty()) opt.cue = save.options.disc_path;
  std::optional<DiscImage> opened_disc;
  for (bool asked = false;;) {
    if (!opt.cue.empty()) {
      try {
        opened_disc.emplace(DiscImage::open(path_from_utf8(opt.cue)));
        break;
      } catch (const std::exception& e) {
        if (cue_named) throw;  // (named on the command line: say why, as before)
        std::printf("The disc image %s could not be read: %s\n", opt.cue.c_str(), e.what());
#ifndef __ANDROID__
        if (asked) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Vib-Ribbon", (std::string("That disc image could not be read:\n") + e.what()).c_str(), nullptr);
#endif
      }
    }
#ifdef __ANDROID__
    (void)asked;
    throw std::runtime_error("no disc image on the headset");
#else
    if (!asked)
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Vib-Ribbon",
                               "Choose your Vib-Ribbon disc image: the .cue file of your own copy of the PAL disc (SCES-02873), with its .bin files beside it.\n\n"
                               "It is remembered; Change disc in the settings picks another.",
                               nullptr);
    asked = true;
    std::string start_at;
    if (!save.options.disc_path.empty()) {  // (the folder of the disc picked before)
      const std::u8string folder = path_from_utf8(save.options.disc_path).parent_path().u8string();
      start_at.assign(reinterpret_cast<const char*>(folder.data()), folder.size());
    }
    opt.cue = pick_disc(nullptr, start_at);
    if (opt.cue.empty()) {
      std::printf("No disc image chosen: closing.\n");
      SDL_Quit();
      return 2;
    }
#endif
  }
  if (save.options.disc_path != opt.cue && !cue_named) {
    save.options.disc_path = opt.cue;
    write_save();
  }
  std::printf("Disc: %s\n", opt.cue.c_str());
  DiscImage& disc = *opened_disc;
  Iso9660 iso(disc);
  // In a headset the window only takes the keyboard (keep it in front) and shows nothing.
  SDL_Window* window = opt.vr ? SDL_CreateWindow("Vib-Ribbon VR: look in the headset (keep this window in front for the keyboard)", 640, 120, 0)
                              : SDL_CreateWindow("Vib-Ribbon", 1200, 900, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
  // The language's own archive: GAME/<its bit>_FILES.PAK (04 German, 02 English,
  // 08 Spanish, 10 French, 20 Italian), as the original names them.
  const int language = save.options.language < 0 ? GameText::kEnglish : save.options.language;
  const Pak pak(iso.read("GAME/" + language_prefix(language) + "FILES.PAK"));
  const CourseScript script = parse_course_script(pak.view("SCRIPT/SYSTEM3.FSL"));
  Scene scene(pak);
  Hud hud(pak, language);

  int code = 0;
  bool restart = false;
  {
    std::unique_ptr<vibr::Display> display;
    if (opt.vr) {
#ifdef VIB_VR
      auto xr = std::make_unique<vibr::XrDisplay>(opt.vr_scale, float(save.options.vr_refresh_hz));
      std::printf("Headset runtime: %s (one unit = %.4f m)\n", xr->runtime_name(), double(opt.vr_scale));
      display = std::move(xr);
#else
      throw std::runtime_error("this build has no headset support (VIB_VR is off)");
#endif
    } else {
      display = std::make_unique<vibr::Renderer>(window);
    }
    vibr::Display& renderer = *display;
    if (opt.headset) opt.vr = !save.options.vr_classic;  // (the classic screen: drawn as on a monitor)
    vibg::Audio audio(opt.offset_ms.value_or(save.options.audio_offset_ms));
    audio.set_volumes(opt.music.value_or(save.options.music_volume), opt.sfx.value_or(save.options.effects_volume));
    Effects fx(iso, audio);
    const bool fixed = opt.fixed || save.options.fixed_courses;
    std::printf("GPU: %s\nCourses: %s\n", renderer.gpu_name(), fixed ? "fixed (the same every play)" : "vary from play to play, like the original");
    // Smooth animation is always on in a headset (owner's decision): stepped poses look far worse up close.
    Clip::set_smooth(opt.smooth || save.options.smooth_animation || opt.headset);
    std::printf("Animation: %s\n", Clip::smooth() ? "smooth (poses blended)" : "stepped, like the original");
    Session s{opt, save, fixed, disc, script, scene, hud, audio, fx, window, renderer,
              opt.line_width.value_or(opt.vr ? save.options.vr_line_width : save.options.line_width), {}, {}};
    // The game's words and its font's character table, from the program on the disc.
    std::optional<GameText> words;
    try {
      words = GameText::from_exe(iso.read("MAIN_G.EXE"));
    } catch (const std::exception& e) {
      std::printf("The game's words could not be read (%s); they and the settings' words are left out.\n", e.what());
    }
    SettingsPanel panel(pak, language, words ? &*words : nullptr);
    {
      std::vector<int> rates;
      for (const float r : renderer.refresh_rates()) rates.push_back(int(std::lround(r)));
      panel.set_refresh_rates(std::move(rates));
      panel.set_passthrough_available(renderer.passthrough_available());
      if (!opt.headset) panel.set_display_modes(monitor_modes(window));
    }
    s.panel = &panel;
    PerfOverlay perf(pak, language, words ? &*words : nullptr);
    s.perf = &perf;
    if (save.options.perf_overlay > 0) std::printf("Performance overlay on (L3 or F3 steps it: off, one line, detailed)\n");
    s.write_save = write_save;
    if (!opt.headset) {  // the monitor's window and picture as saved (task 4.16)
      if (save.options.window_mode != 0) s.last_fullscreen = save.options.window_mode;
      apply_window(s);
      renderer.set_vsync(save.options.vsync);
      renderer.set_picture_aspect(save.options.picture_43 ? 4.0f / 3.0f : 0.0f);
      renderer.set_low_latency(save.options.low_latency);
    }
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
      s.course_songs = int(songs.size());  // (a single song as practice: no course combo)
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
      if (finished && !song && s.save.options.difficulty != 1) {
        std::printf("High scores are kept at Normal difficulty only\n");
      } else if (finished && !song) {
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
      const OwnCd cd = make_own_cd({{opt.music_file, std::uint32_t(own_music->pcm.size() / 2)}});
      s.music_key = cd.key;
      s.music_disc = cd.track_samples();
      s.music_track = 1;
      std::printf("Music: %s (%u Hz, %u channels, %u:%02u)\n", opt.music_file.c_str(), own_music->source_rate, own_music->source_channels,
                  sectors / 75 / 60, sectors / 75 % 60);
      int points = 0;
      s.course_songs = 1;
      const Result r = play_song(s, 0, 0, points, seed ? seed : std::uint32_t(SDL_GetTicksNS()));
      std::printf("%s, score %d\n", r == Result::Clear ? "Cleared" : r == Result::GameOver ? "Game over" : "Left", s.final_score);
    } else if (opt.song || opt.course_given) {
      play_course(opt.course, opt.song);  // straight into play, as before
    } else {
      // The menus, run at the original's 50 frames a second whatever the display does.
      TitleScreens title(pak, language);
      // The screens' words come from the game's own program on the disc.
      if (words) title.set_text(&*words, language);
      using S = TitleScreens::Screen;
      using R = TitleScreens::Result;
      title.sound_effects = save.options.sound_effects;
      title.vibration = save.options.vibration;
      auto show_scores = [&]() {
        for (std::size_t c = 0; c < title.high_scores.size(); ++c) title.high_scores[c] = save.high_scores[c];
      };
      show_scores();
      // First the title (the original's MAIN_T): its intro, then the title waiting for a
      // button, which leads to the main menu.
      const Pak title_pak(iso.read("TITLE/FILES.PAK"));
      TitleIntro intro(title_pak, language);
      // Ours (owner, session 6): "exit" below "back" on the title's mode menu, a clean way
      // out of the game on the monitor and in the headset.
      bool exiting = false;
      if (words) {
        const TmdModel font = parse_tmd(pak.view(localized(pak, "FONT/FONT.TMD", language)));
        intro.add_exit_item(font, *words, language);
      }
      Effects title_fx(iso, audio, "TITLE/AUDIO/TITLE");
      constexpr int kTitleVoice = 2;  // the title's voices 0, 1 and 5 play on 2, 3 and 7
      bool at_title = true, menus_shown = false;
      // Left alone at the title for 96 s, the original plays a demo: songs 1 to 3, the
      // kiosk movie, songs 4 to 6, the kiosk movie again, one each time, in turn.
      int idle_ticks = 0;
      bool demo_start = false;
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
      // The kiosk movie (the menu's "demo", and every fourth demo), loaded when first wanted.
      std::optional<Pak> kiosk_pak;
      std::optional<KioskMovie> kiosk;
      std::optional<Effects> kiosk_fx;
      constexpr int kKioskVoice = 8;  // its voices 0 and 1 play on 8 and 9
      bool in_kiosk = false;
      // The screens where a course is chosen: L2 and R2 change the scroll speed there.
      auto choosing_course = [&]() {
        const S screen = title.screen();
        return !in_kiosk && !at_title && (screen == S::PsjModeSelect || screen == S::UnknownModeSelect || screen == S::SpeedSelect);
      };
      // Its music is a CD track of the game's disc, as the original's kiosk program
      // plays it: audio track 7 (disc track 8, after the data track and the six songs).
      std::vector<std::int16_t> kiosk_track;
      auto start_kiosk = [&]() {
        if (!kiosk) {
          kiosk_pak.emplace(iso.read("KIOSK/" + language_prefix(language) + "FILES.PAK"));
          kiosk.emplace(*kiosk_pak, language);
          kiosk_fx.emplace(iso, audio, "KIOSK/AUDIO/KIOSK");
          kiosk_track = disc.read_audio_track(8);
        }
        audio.play_music(kiosk_track);  // the film starts with its music
        for (int v : {0, 1, 5}) title_fx.stop(kTitleVoice + v);
        music.stop();
        voices.stop();
        kiosk->start();
        in_kiosk = true;
        prev_layers.clear();
        std::printf("Demo: the kiosk movie\n");
      };
      // Ours: a loading screen while the player's music is read (owner: a big folder gave
      // no sign of life). The game's font, three rows, the settings panel's camera; it
      // runs until `ready()`, and Back, Select or Esc cancels (false; also when the
      // window closes, which ends the menus).
      bool quit_requested = false;  // (the window closed during a loading screen)
      auto show_loading = [&](const std::function<std::string()>& detail, const std::function<std::string()>& count,
                              const std::function<bool()>& ready) {
        static const char* const kLoading[5] = {"Laden", "Loading", "Cargando", "Chargement", "Caricamento"};
        const std::string heading = to_font_text(kLoading[language >= 0 && language < 5 ? language : 1]);
        std::vector<vibr::LineInstance> loading_lines;
        while (!ready()) {
          SDL_Event ev;
          while (SDL_PollEvent(&ev)) {
            if (window_key(s, ev)) continue;
            if (ev.type == SDL_EVENT_QUIT) {
              quit_requested = true;
              return false;
            }
            if (ev.type == SDL_EVENT_GAMEPAD_ADDED) {
              if (SDL_Gamepad* g = SDL_OpenGamepad(ev.gdevice.which)) s.pads[ev.gdevice.which] = g;
            } else if (ev.type == SDL_EVENT_GAMEPAD_REMOVED) {
              if (auto it = s.pads.find(ev.gdevice.which); it != s.pads.end()) SDL_CloseGamepad(it->second), s.pads.erase(it);
            } else if ((ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                        (ev.gbutton.button == SDL_GAMEPAD_BUTTON_EAST || ev.gbutton.button == SDL_GAMEPAD_BUTTON_BACK)) ||
                       (ev.type == SDL_EVENT_KEY_DOWN && (ev.key.key == SDLK_ESCAPE || ev.key.key == SDLK_BACKSPACE))) {
              return false;
            } else if (ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || ev.type == SDL_EVENT_WINDOW_RESIZED) {
              renderer.request_resize();
            }
          }
          const std::uint64_t t0 = SDL_GetTicksNS();
          const float size = opt.vr ? 0.25f : 1.0f;
          Hud::Layer layer;
          layer.view.eye = {0, 0, 0};
          layer.view.target = {0, 0, 4730};
          layer.view.fov_y = float(682 * 6.28318530717958647692 / 4096.0);
          const auto seed_now = next_random(static_cast<std::uint32_t>(double(t0) * 1e-9 * kOriginalRefresh) * 2654435761u);
          const std::string rows[3] = {heading, to_font_text(detail()), to_font_text(count())};
          const float scales[3] = {1.6f, 0.9f, 1.0f}, ys[3] = {-260.0f, 20.0f, 260.0f};
          for (int k = 0; k < 3; ++k)
            for (SceneLine l : panel.text_lines(rows[k], scales[k], seed_now + std::uint32_t(k), 3)) {
              for (Vec3f* q : {&l.a, &l.b}) *q = {q->x * size, (q->y + ys[k]) * size, 4000.0f * size};
              l.jitter_a = {l.jitter_a.x * size, l.jitter_a.y * size, 0}, l.jitter_b = {l.jitter_b.x * size, l.jitter_b.y * size, 0};
              l.shake_a *= size, l.shake_b *= size;
              layer.lines.push_back(l);
            }
          tag_menu(layer.lines);  // (custom colours: Menu)
          to_instances(s, layer.lines, ++menu_display_frame, loading_lines);
          std::vector<vibr::Layer> out{{&loading_lines, vibr::look_at({0, 0, 0}, {0, 0, 4730}, {0, -1, 0}), layer.view.fov_y}};
          draw_frame(s, out, 0.0, double(SDL_GetTicksNS() - t0) * 1e-6);
        }
        return true;
      };
      // Ours: Start (or P / Tab) opens the settings anywhere in the menus, beside a preview of play.
      bool in_settings = false, toggle_settings = false, settings_after_film = false, picker_start = false;
      PlayPreview preview;
      std::vector<SceneLine> preview_lines;
      std::vector<vibr::LineInstance> settings_lines[3];
      std::uint64_t last_ns = SDL_GetTicksNS();
      double behind = 0;
      bool course_chosen = false, running = true, replay = false;
      // Ours: the player's own music, picked in the game (owner's design). The first
      // time, the folder browser; after that, the files of the remembered folder.
      FilePicker picker(panel, language);
      bool in_picker = false, own_play = false;
      std::vector<std::string> own_files;
      std::string own_folder;
      Course own_course = Course::OneTrack;
      std::vector<int> own_order;  // the tracks to play next (from 1)
      // "Again?" after losing on the player's music: the track to start from and, for
      // all tracks, the score carried into it.
      std::size_t own_start = 0;
      int own_start_points = 0;
      // What the picker is for (owner's flow): picking tracks to play, or a folder whose
      // tracks all play in order.
      bool picking_folder = false;
      auto play_own = [&]() {  // the CD just loaded, every track in its (name) order, straight away
        own_order.clear();
        for (int k = 1; k <= int(own_files.size()); ++k) own_order.push_back(k);  // (the files picked: they decode as they play)
        if (own_order.empty()) return;
        own_start = 0;
        own_start_points = 0;
        own_play = true;
        title.go_to(S::Done);
      };
      auto load_own_cd = [&]() {  // (the tracks are read and decoded as they play: see own_play)
        title.num_tracks = std::max(1, int(own_files.size()));
      };
      auto show_folder = [&](FilePicker::Mode mode, const std::string& folder) {
        picker.show(mode, folder, list_folder(folder), !folder.empty());
      };
      // Opens the picker: for tracks, in the remembered folder's files (the first time,
      // the folder browser first); for a whole folder, the folder browser there.
      auto open_picker = [&](bool for_folder) {
        picking_folder = for_folder;
#ifdef __ANDROID__
        ask_music_permission();  // (the first time: the headset's Music folder, S.2)
#endif
        std::error_code ec;
        const std::string& saved = save.options.music_folder;
        const bool known = !saved.empty() && std::filesystem::is_directory(path_from_utf8(saved), ec);
        if (known && !for_folder) show_folder(FilePicker::Mode::Files, saved);
        else show_folder(FilePicker::Mode::Folder, known ? saved : default_music_folder());
        in_picker = true;
        prev_layers.clear();
      };
      Course chosen = Course::Bronze;
      while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
          if (window_key(s, e)) continue;  // (F11 or Alt+Enter: window or fullscreen)
          auto press = [&](int which, bool down) {
            bool* p[6] = {&pressed.up, &pressed.down, &pressed.left, &pressed.right, &pressed.confirm, &pressed.cancel};
            bool* h[4] = {&held.up, &held.down, &held.left, &held.right};
            if (down) *p[which] = true;
            if (which < 4) *h[which] = down;
          };
          if (is_mouse(e)) {  // (task 4.16 G)
            const MouseInput m = mouse_event(s, e, false);
            if (in_settings) {  // our settings: hover, click, wheel
              SettingsPanel::Input pi;
              panel_mouse(s, panel, m, pi);
              const bool what[6] = {pi.up, pi.down, pi.left, pi.right, pi.confirm, pi.cancel};
              for (int k = 0; k < 6; ++k)
                if (what[k]) press(k, true), press(k, false);
            } else {  // the original's menus and the picker: the wheel moves, left confirms, right goes back
              if (m.wheel > 0) press(0, true), press(0, false);
              if (m.wheel < 0) press(1, true), press(1, false);
              if (m.left_click && m.over) press(4, true), press(4, false);
              if (m.right_click) press(5, true), press(5, false);
            }
            continue;
          }
          if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) && camera_key(s, e.key)) continue;
          if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_HOME && !e.key.repeat) {
            r3_pressed(s, !in_settings);
            continue;
          }
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
                case SDLK_P: case SDLK_TAB:
                  if (down) {
                    if (in_picker) picker_start = true;  // (as Start)
                    else toggle_settings = true;
                  }
                  break;
                case SDLK_F3:
                  if (down) {
                    l3_pressed(s);
                    write_save();
                  }
                  break;
                case SDLK_MINUS: case SDLK_EQUALS:
                  if (down) {
                    change_line_width(s, e.key.key == SDLK_EQUALS);
                    write_save();
                  }
                  break;
                case SDLK_PAGEUP: case SDLK_PAGEDOWN:  // the scroll speed, on the course selects (as L2 and R2)
                  if (down && choosing_course() && change_scroll_speed(s, e.key.key == SDLK_PAGEUP ? 1 : -1)) write_save();
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
              if (choosing_course()) {  // L2 and R2: the scroll speed, on the course selects
                if (const int d = trigger_edge(s, e.gaxis); d && change_scroll_speed(s, d)) write_save();
              } else if (opt.vr) {  // the line width, in the other menus
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
                case SDL_GAMEPAD_BUTTON_SOUTH: press(4, down); break;
                // Start: our settings, on every screen (owner's choice; the original's
                // menus took it as confirm). During the kiosk film it ends the film first.
                case SDL_GAMEPAD_BUTTON_START:
                  if (down) {
                    if (in_kiosk) press(4, true), settings_after_film = true;
                    else if (in_picker) picker_start = true;  // the picker's Done / Use this folder (owner)
                    else toggle_settings = true;
                  }
                  break;
                case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_NORTH: press(5, down); break;
                case SDL_GAMEPAD_BUTTON_BACK: break;  // (Select: nothing in the menus; in play the original's Start)
                case SDL_GAMEPAD_BUTTON_RIGHT_STICK: if (down) r3_pressed(s, !in_settings); break;  // R3
                case SDL_GAMEPAD_BUTTON_LEFT_STICK:  // L3
                  if (down) {
                    l3_pressed(s);
                    write_save();
                  }
                  break;
                default: break;
              }
              break;
            }
            default: break;
          }
        }
        if (!running) break;
        const std::uint64_t now_ns = SDL_GetTicksNS();
        const double frame_dt = double(now_ns - last_ns) * 1e-9;
        if (settings_after_film && !in_kiosk) settings_after_film = false, toggle_settings = true;
        if (in_picker) toggle_settings = false;  // (never behind the picker)
#ifdef __ANDROID__
        // The storage permission just granted while the folder browser shows the game's own
        // music folder for the first time: on to the headset's Music folder instead.
        if (g_music_permission_answered.exchange(false) && in_picker && g_music_permission == 1 &&
            picker.mode() == FilePicker::Mode::Folder && picker.path() == headset_music_folder() && save.options.music_folder.empty()) {
          std::error_code ec;
          if (std::filesystem::is_directory(kSharedMusic, ec)) show_folder(FilePicker::Mode::Folder, kSharedMusic);
        }
#endif
        if (toggle_settings && !in_kiosk) {
          toggle_settings = false;
          in_settings = !in_settings;
          if (in_settings) {
            setup_panel(panel, opt.headset, false);
            panel.set_wraps(true);
            panel.set_active(true);
            preview.start(s);
          } else {
            title.sound_effects = save.options.sound_effects;
            title.vibration = save.options.vibration;
            write_save();
          }
          s.fx.number(1, 90, 5);
          prev_layers.clear();
          pressed = {};
        }
        toggle_settings = false;
        // (In the settings the right stick zooms play's view, which the preview shows.)
        if (in_settings) zoom_or_circle_with_stick(s, frame_dt, play_zoom(s));
        else if (opt.vr) zoom_with_stick(s, frame_dt, s.save.options.vr_menu_zoom);
        else if (beside_vibri(s)) zoom_with_stick(s, frame_dt, s.save.options.menu_zoom, s.save.options.menu_angle);  // (the monitor's menus)
        behind = std::min(behind + double(now_ns - last_ns) * 1e-9, 0.1);
        last_ns = now_ns;
        while (behind >= 1.0 / TitleScreens::kFrameRate) {
          behind -= 1.0 / TitleScreens::kFrameRate;
          prev_layers = in_kiosk ? kiosk->layers() : at_title ? intro.layers() : title.layers();  // for blending
          TitleScreens::Input in = pressed;
          in.held_up = held.up, in.held_down = held.down, in.held_left = held.left, in.held_right = held.right;
          pressed = {};
          in.voices_playing = voices.playing();
          in.music_playing = music.playing();
          if (in_picker) {
            // The menus stand still meanwhile (their music goes on).
            SettingsPanel::Input pi;
            pi.up = in.up, pi.down = in.down, pi.left = in.left, pi.right = in.right;
            pi.confirm = in.confirm, pi.cancel = in.cancel;
            held_directions(s, pi);
            using PA = FilePicker::Action;
            PA act = picker.tick(pi, 1.0 / TitleScreens::kFrameRate);
            if (picker_start) {
              picker_start = false;
              if (act == PA::None || act == PA::Moved) act = picker.start();
            }
            if (act == PA::Moved) s.fx.number(1, 50, 5);
            else if (act != PA::None) s.fx.number(1, 90, 5);
            const FilePicker::Mode mode = picker.mode();
            switch (act) {
              case PA::Enter:
                if (const FilePicker::Entry* opened = picker.entry()) show_folder(mode, child_folder(picker.path(), opened->name));
                break;
              case PA::Up: show_folder(mode, parent_folder(picker.path())); break;
              case PA::UseFolder:
                save.options.music_folder = picker.path();
                write_save();
                std::printf("Music folder: %s (saved)\n", picker.path().c_str());
                if (picking_folder) {
                  // All tracks: every music file in the folder, in name order, straight away.
                  own_files.clear();
                  for (const FilePicker::Entry& entry : list_folder(picker.path()))
                    if (!entry.folder) own_files.push_back(entry.name);
                  if (own_files.empty()) {
                    picker.notice_no_music();  // (shown in the picker, which stays open)
                    std::printf("No music files in that folder.\n");
                    break;
                  }
                  own_folder = picker.path();
                  in_picker = false;
                  load_own_cd();
                  play_own();
                } else {
                  show_folder(FilePicker::Mode::Files, picker.path());  // the first time: now its files
                }
                break;
              case PA::Done:  // Pick track(s): the ticked files, in name order, straight away
                own_files = picker.ticked();
                own_folder = picker.path();
                save.options.music_folder = picker.path();  // (remembered for next time)
                write_save();
                in_picker = false;
                load_own_cd();
                play_own();
                break;
              case PA::Cancel: in_picker = false; break;
              default: break;
            }
            idle_ticks = 0;
            music.update();
            voices.update();
            continue;
          }
          if (in_settings) {
            // The menus stand still meanwhile (their music goes on).
            SettingsPanel::Input pi;
            pi.up = in.up, pi.down = in.down, pi.left = in.left, pi.right = in.right;
            pi.confirm = in.confirm, pi.cancel = in.cancel;
            held_directions(s, pi);
            const SettingsPanel::Action act = panel.tick(pi, 1.0 / TitleScreens::kFrameRate, save.options, opt.headset);
            panel_sound(s, act);
            if (act == SettingsPanel::Action::Changed) apply_settings(s);
            if (act == SettingsPanel::Action::Recentre) renderer.recentre();
            if (act == SettingsPanel::Action::Calibrate || act == SettingsPanel::Action::CalibratePicture) {
              if (!run_calibration(s, panel, act == SettingsPanel::Action::CalibratePicture)) {
                running = false;
                break;
              }
              last_ns = SDL_GetTicksNS();
              behind = 0;
            }
            if (act == SettingsPanel::Action::ChangeDisc) {  // (task 4.16 I) another disc image: start again with it
              std::string folder;
              if (!opt.cue.empty()) {
                const std::u8string f = path_from_utf8(opt.cue).parent_path().u8string();
                folder.assign(reinterpret_cast<const char*>(f.data()), f.size());
              }
              const std::string picked = pick_disc(window, folder);
              last_ns = SDL_GetTicksNS();
              behind = 0;
              if (!picked.empty() && picked != opt.cue) {
                save.options.disc_path = picked;
                write_save();
                g_disc_changed = true;
                restart = true;
                running = false;
                break;
              }
            }
            if (act == SettingsPanel::Action::Close) toggle_settings = true;
            idle_ticks = 0;
            music.update();
            voices.update();
            continue;
          }
          if (in_kiosk) {
            KioskMovie::Input ki;
            ki.confirm = in.confirm || in.cancel;
            ki.music_playing = kiosk_fx->is_playing(kKioskVoice);
            ki.confirm_sound_playing = kiosk_fx->is_playing(kKioskVoice + 1);
            kiosk->tick(ki);
            for (const KioskMovie::Sound& snd : kiosk->take_sounds()) kiosk_fx->number(snd.number, 64, kKioskVoice + snd.voice);
            if (kiosk->finished()) {
              // Back to the title's menu, its lines gathering in.
              kiosk_fx->stop(kKioskVoice);
              kiosk_fx->stop(kKioskVoice + 1);
              audio.play_music({});
              in_kiosk = false;
              intro.start(true);
              intro.close_menus();
              idle_ticks = 0;
              prev_layers.clear();
            }
            continue;
          }
          if (at_title) {
            TitleIntro::Input ti;
            ti.up = in.up, ti.down = in.down, ti.left = in.left, ti.right = in.right;
            ti.confirm = in.confirm, ti.cancel = in.cancel;
            ti.confirm_sound_playing = title_fx.is_playing(kTitleVoice + 1);
            intro.tick(ti);
            const bool touched = ti.up || ti.down || ti.left || ti.right || ti.confirm || ti.cancel || in.held_up ||
                                 in.held_down || in.held_left || in.held_right;
            idle_ticks = touched ? 0 : idle_ticks + 1;
            if (menus_shown && save.options.language >= 0 && !intro.leaving() &&
                idle_ticks >= int(96 * TitleScreens::kFrameRate)) {
              idle_ticks = 0;
              demo_start = true;
              break;
            }
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
              case C::Demo:  // the kiosk movie, as the original's kiosk program
                intro.show(M::Mode);
                start_kiosk();
                break;
              case C::Play:
                intro.show(M::Start);
                intro.leave();
                break;
              case C::Exit:  // the menu flies apart as for play, then the game closes
                exiting = true;
                intro.show(M::Start);
                intro.leave();
                break;
              case C::Started:
                if (exiting) {
                  std::printf("Exit chosen on the title's menu\n");
                  running = false;
                  break;
                }
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
            case R::UnknownModes: title.go_to(S::UnknownModeSelect); break;
            // The own-CD courses: our picker stands in for the original's CD step (the CD
            // change screen, the track select and the alerts come next).
            // The own-CD courses go through the original's CD screen; "change the CD"
            // opens our picker (the player's files stand for the CD).
            // Owner's flow: "Pick track(s)" opens the picker for one or more files; all
            // tracks (and in order, which the game plays itself) for a whole folder. Done
            // plays straight away; afterwards back to this menu.
            case R::OneTrack: own_course = Course::OneTrack, open_picker(false); break;
            case R::AllTracks: own_course = Course::AllTracks, open_picker(true); break;
            case R::InOrder: own_course = Course::InOrder, open_picker(true); break;
            case R::ChangeCd: open_picker(false); break;
            case R::CdReady:
              if (own_files.empty()) {
                open_picker(false);  // no CD in yet: pick one first
              } else if (own_course == Course::OneTrack) {
                title.go_to(S::TrackSelect);
              } else {
                // All tracks: shuffled from the disc's key, so a CD always comes in the same
                // order; in order: the disc's own.
                own_order.clear();
                for (int k = 1; k <= int(own_files.size()); ++k) own_order.push_back(k);
                own_play = true;
                title.go_to(S::Done);
              }
              break;
            case R::TrackChosen:
              own_order = {title.track};
              own_play = true;
              title.go_to(S::Done);
              break;
            case R::TrackBack: title.go_to(S::CdChange); break;
            case R::CdChangeBack: title.go_to(S::UnknownModeSelect); break;
            // The speed courses: the original's level select, then our picker for the
            // track(s) to race on.
            case R::SpeedModes: title.go_to(S::SpeedSelect); break;
            case R::Speed1: own_course = Course::Speed1, open_picker(false); break;
            case R::Speed2: own_course = Course::Speed2, open_picker(false); break;
            case R::Speed3: own_course = Course::Speed3, open_picker(false); break;
            case R::SpeedBack: title.go_to(S::UnknownModeSelect); break;
            case R::UnknownHiscores: title.go_to(S::UnknownHiscore); break;
            case R::UnknownBack: title.go_to(S::Main); break;
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
            case R::HiscoreBack: title.go_to(title.screen() == S::UnknownHiscore ? S::UnknownModeSelect : S::PsjModeSelect); break;
            case R::ResultsDone:
              if (title.screen() == S::Again) title.go_to(S::AgainSelect);
              else if (title.results_course >= int(Course::OneTrack)) title.go_to(is_speed_course(Course(title.results_course)) ? S::SpeedSelect : S::UnknownModeSelect);
              else title.go_to(S::PsjModeSelect);
              break;
            case R::PlayAgain:
              if (title.results_course >= int(Course::OneTrack)) {
                // The player's music: the same files again, from the track that was lost
                // (a speed course from its first track).
                if (is_speed_course(own_course)) own_start = 0, own_start_points = 0;
                own_play = true;
              } else {
                replay = true;  // the original's own courses start again from the song that was lost
                course_chosen = true;
              }
              title.go_to(S::Done);
              break;
            case R::AgainBack:
              if (title.results_course >= int(Course::OneTrack)) title.go_to(is_speed_course(Course(title.results_course)) ? S::SpeedSelect : S::UnknownModeSelect);
              else title.go_to(S::PsjModeSelect);
              break;
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
        const std::uint64_t ticked_ns = SDL_GetTicksNS();  // (the menus' work this frame, for the overlay)
        if (own_play && title.done()) {
          // The player's music: first every file's length (read without decoding, so it
          // is quick) for the CD's table of contents and key; then each track decoded
          // just before it plays, the next one in the background while one plays, and
          // let go once played, so even a big folder starts at once and fits in memory.
          own_play = false;
          music.stop();
          voices.stop();
          const std::size_t count = own_files.size();
          std::atomic<std::size_t> probed{0};
          std::atomic<bool> stop_probing{false};
          auto probing = std::async(std::launch::async, [&]() {
            std::vector<std::pair<std::string, std::uint32_t>> names;
            std::vector<std::string> paths;
            for (std::size_t k = 0; k < count && !stop_probing; ++k) {
              const std::string file = child_folder(own_folder, own_files[k]);
              try {
                names.push_back({own_files[k], std::uint32_t(vibr::probe_music_frames(file))});
                paths.push_back(file);
              } catch (const std::exception& error) {
                std::printf("Left out %s: %s\n", own_files[k].c_str(), error.what());
              }
              ++probed;
            }
            return std::make_pair(names, paths);
          });
          const bool read_all = show_loading([&]() { return own_folder; },
                                             [&]() { return std::to_string(probed.load()) + " / " + std::to_string(count); },
                                             [&]() { return probing.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
          stop_probing = true;
          const auto probed_files = probing.get();
          const auto& names = probed_files.first;
          const auto& paths = probed_files.second;
          if (quit_requested) {
            running = false;
            break;
          }
          Result r = Result::Clear;
          int speed_seconds = -1;  // a speed course cleared: its time
          int own_rank = 0;        // the place of the last result recorded (0: none)
          bool own_cleared = false;
          std::size_t own_lost_track = 0;
          int own_lost_points = 0;
          if (read_all && names.empty()) {
            // Nothing playable after all (every file unreadable or under 10 s): back to
            // the picker with the message, as for a folder with no music files.
            open_picker(picking_folder);
            if (!own_folder.empty())
              show_folder(picking_folder ? FilePicker::Mode::Folder : FilePicker::Mode::Files, own_folder);
            picker.notice_no_music();
            title.go_to(S::UnknownModeSelect);
            prev_layers.clear();
            last_ns = SDL_GetTicksNS();
            behind = 0;
            continue;
          }
          if (read_all && !names.empty()) {
            const OwnCd cd = make_own_cd(names);
            s.music_key = cd.key;
            s.music_disc = cd.track_samples();
            s.course = own_course;
            auto decode = [&paths](std::size_t k) { return std::async(std::launch::async, [path = paths[k]]() { return vibr::load_music(path); }); };
            const std::size_t first = own_start < names.size() ? own_start : 0;
            own_start = 0;
            std::future<vibr::Music> next = decode(first);
            int points = own_course == Course::AllTracks ? own_start_points : 0;
            own_start_points = 0;
            const bool speed = is_speed_course(own_course);
            const bool scored = own_course == Course::OneTrack || own_course == Course::AllTracks;
            s.course_songs = own_course == Course::AllTracks ? int(names.size()) : 1;  // (all tracks: a full course combo is possible)
            // (A speed course: each picked track is a try, the next one starting when the
            // music runs out first; reaching the top speed ends the run.)
            for (std::size_t k = first; k < names.size() && (r == Result::Clear || (speed && r == Result::GameOver && s.final_time_up)); ++k) {
              std::future<vibr::Music> current = std::move(next);
              const std::string& name = names[k].first;
              const bool loaded = show_loading([&]() { return name; },
                                               [&]() { return std::to_string(k + 1) + " / " + std::to_string(names.size()); },
                                               [&]() { return current.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
              if (!loaded) {
                r = quit_requested ? Result::Quit : Result::Abort;
                current.wait();
                break;
              }
              vibr::Music track;
              try {
                track = current.get();
              } catch (const std::exception& error) {
                std::printf("Left out %s: %s\n", name.c_str(), error.what());
                if (k + 1 < names.size()) next = decode(k + 1);
                continue;
              }
              if (track.pcm.size() / 2 != names[k].second)
                std::printf("Note: %s decoded to %zu frames, not the %u its header gave\n", name.c_str(), track.pcm.size() / 2, names[k].second);
              if (k + 1 < names.size()) next = decode(k + 1);  // the next one, while this plays
              s.music = &track;
              s.music_track = int(k) + 1;
              std::printf("Track %zu of %zu: %s\n", k + 1, names.size(), name.c_str());
              const int before = points;
              r = play_song(s, 0, int(k), points, seed);
              seed = next_random(seed);
              if (scored && r == Result::GameOver) {
                own_lost_track = k;
                own_lost_points = before;
              }
              if (speed && r == Result::Clear) {
                own_rank = save.record(own_course, points);
                write_save();
                std::printf("Speed record: %d s%s\n", points, own_rank ? (" (place " + std::to_string(own_rank) + ")").c_str() : "");
                speed_seconds = points;
                points = 0;
                break;
              }
              // One track: each track cleared is a result of its own (its score and bonus);
              // all tracks: the whole run once its last track is cleared.
              if (r == Result::Clear && (own_course == Course::OneTrack || (own_course == Course::AllTracks && k + 1 == names.size()))) {
                // (High scores at Normal difficulty only, task 4.17; speed courses keep their own rule.)
                own_rank = save.options.difficulty == 1 ? save.record(own_course, points) : 0;
                write_save();
                own_cleared = true;
                std::printf("%s: %d%s\n", own_course == Course::OneTrack ? "Track score" : "All tracks total", points,
                            own_rank ? (" (place " + std::to_string(own_rank) + ")").c_str() : "");
              }
              if (own_course != Course::AllTracks) points = 0;
              s.music = nullptr;
            }
            if (next.valid()) next.wait();
          }
          audio.play_music({});
          if (r == Result::Quit || quit_requested) {
            running = false;
            break;
          }
          // A speed course ends on the original's speed clear or time-up screen, then the
          // speed select; the other own-CD courses go back to the own-CD mode select.
          // One track and all tracks end on the clear or game-over screens as the game
          // disc's courses do, then "again?" (from the lost track) after a loss.
          title.results_speed = 0;
          title.results_time_up = false;
          title.results_course = int(own_course);
          show_scores();
          if (is_speed_course(own_course) && (speed_seconds >= 0 || (r == Result::GameOver && s.final_time_up))) {
            title.results_speed = int(own_course) - int(Course::Speed1) + 1;
            title.results_time_up = speed_seconds < 0;
            title.results_score = std::max(0, speed_seconds);
            title.results_rank = speed_seconds >= 0 ? own_rank : 0;
            title.go_to(speed_seconds >= 0 ? S::Clear : S::Again);
          } else if (is_speed_course(own_course) && r == Result::GameOver) {
            title.results_score = 0;  // lost on the way: Vibri's reaction, then "again?"
            title.results_rank = 0;
            title.go_to(S::Again);
          } else if ((own_course == Course::OneTrack || own_course == Course::AllTracks) && r == Result::GameOver) {
            own_start = own_lost_track;
            own_start_points = own_lost_points;
            title.results_score = std::min(s.final_score, Hud::kMaxScore);
            title.results_rank = 0;
            title.go_to(S::Again);
          } else if (own_cleared && r == Result::Clear) {
            title.results_score = std::min(s.final_score, Hud::kMaxScore);
            title.results_bonus = s.final_bonus;
            title.results_form = s.final_form;
            title.results_rank = own_rank;
            title.go_to(S::Clear);
          } else {
            title.go_to(is_speed_course(own_course) ? S::SpeedSelect : S::UnknownModeSelect);
          }
          prev_layers.clear();
          last_ns = SDL_GetTicksNS();
          behind = 0;
          continue;
        }
        if (demo_start) {
          demo_start = false;
          static const int kDemoLevels[8] = {1, 2, 3, 0, 4, 5, 6, 0};  // 0: the kiosk movie
          const int level = kDemoLevels[g_demo_count++ % 8];
          if (level == 0) {
            start_kiosk();
            continue;
          }
          for (int v : {0, 1, 5}) title_fx.stop(kTitleVoice + v);
          music.stop();
          voices.stop();
          s.demo = true;
          s.course = Course(int(Course::Bronze) + (level - 1) / 2);
          s.song = 0;
          int demo_points = 0;
          std::printf("Demo: song %d\n", level);
          const Result r = play_song(s, level, (level - 1) % 2, demo_points, seed + std::uint32_t(g_demo_count));
          s.demo = false;
          s.audio.play_music({});
          if (r == Result::Quit) break;
          // Back to the title's menu, its lines gathering in.
          intro.start(true);
          intro.close_menus();
          prev_layers.clear();
          last_ns = SDL_GetTicksNS();
          behind = 0;
          continue;
        }
        if (course_chosen && title.done()) {
          music.stop();
          voices.stop();
          course_chosen = false;
          const bool ok = replay ? play_course(chosen, 0, lost_song, points_before_lost) : play_course(chosen, 0);
          replay = false;
          if (!ok) break;
          show_scores();
          title.sound_effects = save.options.sound_effects;  // the pause may have changed them
          title.vibration = save.options.vibration;
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
        if (in_picker) {  // the picker alone, as a screen of its own
          ++menu_display_frame;
          const auto jitter_seed = next_random(static_cast<std::uint32_t>(double(now_ns) * 1e-9 * kOriginalRefresh) * 2654435761u);
          const Hud::Layer pl = picker.draw(opt.vr ? 0.25f : 1.0f, jitter_seed);
          to_instances(s, pl.lines, menu_display_frame, settings_lines[1]);
          std::vector<vibr::Layer> out{{&settings_lines[1], vibr::look_at({pl.view.eye.x, pl.view.eye.y, pl.view.eye.z},
                                                                           {pl.view.target.x, pl.view.target.y, pl.view.target.z}, {0, -1, 0}),
                                        pl.view.fov_y}};
          draw_frame(s, out, double(ticked_ns - now_ns) * 1e-6, double(SDL_GetTicksNS() - ticked_ns) * 1e-6);
          continue;
        }
        if (in_settings) {
          // Play's preview and the settings beside it (left of the road).
          ++menu_display_frame;
          SceneView view;
          const auto jitter_seed = next_random(static_cast<std::uint32_t>(double(now_ns) * 1e-9 * kOriginalRefresh) * 2654435761u);
          preview.draw(s, frame_dt, jitter_seed, preview_lines, view);
          // Beside Vibri, not over her: on the monitor the original's camera has her at the
          // right, so the settings go left; in a headset she is on the side the view is
          // not (seen from her right she is to the left), so they go on the other.
          panel.set_place(0.6f, 14, opt.vr ? 0.25f : 1.0f);
          // (Wide enough for the longest label with its default and the longest value.)
          if (opt.vr && save.options.vr_angle >= 0) panel.set_columns(150.0f, 3050.0f);
          else if (opt.vr) panel.set_columns(-3050.0f, -150.0f);
          else panel.set_columns(-3000.0f, -100.0f);
          const Hud::Layer pl = panel.draw(save.options, opt.headset, next_random(jitter_seed));
          to_instances(s, preview_lines, menu_display_frame, settings_lines[0]);
          to_instances(s, pl.lines, menu_display_frame, settings_lines[1]);
          auto matrix = [](const SceneView& v) {
            return vibr::look_at({v.eye.x, v.eye.y, v.eye.z}, {v.target.x, v.target.y, v.target.z}, {0, -1, 0});
          };
          std::vector<vibr::Layer> out{{&settings_lines[0], matrix(view), view.fov_y}, {&settings_lines[1], matrix(pl.view), pl.view.fov_y}};
          if (!opt.headset) {  // the keys and the mouse, at the bottom (task 4.16 G)
            const Hud::Layer keys = panel.draw_keys(next_random(jitter_seed + 1));
            to_instances(s, keys.lines, menu_display_frame, settings_lines[2]);
            out.push_back({&settings_lines[2], matrix(keys.view), keys.view.fov_y});
          }
          draw_frame(s, out, double(ticked_ns - now_ns) * 1e-6, double(SDL_GetTicksNS() - ticked_ns) * 1e-6);
          continue;
        }
        // Draw the last frame the title or the menus made.
        const auto& layers = in_kiosk ? kiosk->layers() : at_title ? intro.layers() : title.layers();
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
        // On the Options screen, the hint that View opens our settings, drawn as its other layers are.
        // On the course selects, the scroll speed at the bottom (L2 and R2 change it).
        const bool show_panel = !in_kiosk && !at_title && (title.screen() == S::Option || choosing_course());
        Hud::Layer panel_layer;
        if (show_panel) {  // a dimmed "More options (Select)" at the bottom left, away from Vibri and the menu
          panel.set_place(-0.8f, 1, 0.65f);
          panel.set_columns(-1850.0f, 450.0f);
          const std::uint32_t panel_seed = next_random(menu_display_frame * 2654435761u);
          panel_layer = title.screen() == S::Option ? panel.draw_hint(panel_seed) : panel.draw_scroll_speed(save.options, panel_seed);
        }
        menu_lines.resize(layers.size() + 1);
        for (std::size_t i = 0; i < layers.size() + (show_panel ? 1 : 0); ++i) {
          const Hud::Layer& layer = i < layers.size() ? layers[i] : panel_layer;
          menu_lines[i].clear();
          const Hud::Layer* before = i < layers.size() && blend && prev_layers[i].lines.size() == layers[i].lines.size() ? &prev_layers[i] : nullptr;
          for (std::size_t k = 0; k < layer.lines.size(); ++k) {
            const SceneLine& l = layer.lines[k];
            Vec3f a = l.a, b = l.b;
            if (before) {
              const SceneLine& p = before->lines[k];
              a = mix(l.a, l.jitter_a, p.a, p.jitter_a);
              b = mix(l.b, l.jitter_b, p.b, p.jitter_b);
            }
            // (Afresh every headset frame, or as the monitor's Wobble hold says; the blend kept this frame's jitter in a and b.)
            a = wobble(s, a, l.jitter_a, l.vertex_a, l.shake_a, menu_display_frame);
            b = wobble(s, b, l.jitter_b, l.vertex_b, l.shake_b, menu_display_frame);
            // (The original's menu screens' lines are menu items for custom colours, Vibri
            // apart; the kiosk film keeps its own.)
            const std::uint8_t part = l.part == line_part::kNone && !in_kiosk ? line_part::kMenu : l.part;
            const Rgb c = shown_colour(l.colour, part, save.options.colours);
            menu_lines[i].push_back({{a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f}});
          }
          SceneView v = layer.view;
          if (i < layers.size() && blend) {  // the camera too
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
          if (opt.vr && at_title && !in_kiosk) v = opening_forward(v);
          if (!opt.headset && beside_vibri(s) && i < layers.size()) v = menu_view(v, save.options.menu_zoom, save.options.menu_angle);
          out.push_back({&menu_lines[i], vibr::look_at({v.eye.x, v.eye.y, v.eye.z}, {v.target.x, v.target.y, v.target.z}, {0, -1, 0}), v.fov_y});
        }
        draw_frame(s, out, double(ticked_ns - now_ns) * 1e-6, double(SDL_GetTicksNS() - ticked_ns) * 1e-6);
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

#ifdef _WIN32
void log_to_file_without_console(const char* path_utf8);  // (windows_log.cpp)
#endif

int main(int argc, char** argv) {
#ifdef __ANDROID__
  log_to_logcat();
#endif
#ifdef _WIN32
  // Started without a console (the packaged programs): the log goes beside save.txt.
  if (char* pref = SDL_GetPrefPath("VibRibbonVR", "VibRibbonVR")) {
    log_to_file_without_console((std::string(pref) + (kDefaultVr ? "log-vr.txt" : "log.txt")).c_str());
    SDL_free(pref);
  }
#endif
  try {
    int code;
    do code = run(argc, argv);
    while (code == kRestart);
#ifdef __ANDROID__
    std::printf("The game closed normally\n");
    SDL_Delay(200);  // (lets the last lines reach logcat)
#endif
    return code;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "vibplay: %s\n", e.what());
#ifdef __ANDROID__
    // Straight to logcat as well: the process ends at once, before the pipe's thread
    // has passed the line on (session 5: a close with no trace in the log).
    __android_log_print(ANDROID_LOG_ERROR, "vibplay", "vibplay stopped: %s", e.what());
#endif
#ifndef __ANDROID__  // (a 2D dialog would never be seen in the headset; logcat has it)
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "vibplay", e.what(), nullptr);  // so a failure is never just a closed window
#endif
    return 1;
  }
}
