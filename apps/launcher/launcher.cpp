// Vib-Ribbon Launcher (task 4.16 I): the Windows download's front door, for players
// who have not seen the game before. Three tabs: Setup (the disc image, checked against
// the release we know, and where to play: monitor or headset), Display (the PC's
// window, resolution, refresh rate, VSync, frame limit, picture shape, low latency, and
// the headset's refresh rate) and Controls (pointing out the options menu in the game,
// where everything else is, and that the sticks move the camera). Play writes the
// choices into the game's own save.txt and starts Vib-Ribbon.exe or Vib-Ribbon VR.exe.
//
// Drawn with Dear ImGui on SDL3's renderer (our own layout and colours, a dark theme).
// --screenshot <file.bmp> <tab 0-2> draws a tab into a file and quits (for checking the
// layout without a screen).

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "vib/bytes.h"
#include "vib/disc_image.h"
#include "vib/known_discs.h"
#include "vib/save.h"

#ifndef VIB_LAUNCHER_VERSION
#define VIB_LAUNCHER_VERSION "dev"
#endif

namespace {

// ---------------------------------------------------------------- colours

ImVec4 rgb(int hex, float a = 1.0f) {
  return ImVec4(float((hex >> 16) & 255) / 255.0f, float((hex >> 8) & 255) / 255.0f, float(hex & 255) / 255.0f, a);
}
const ImVec4 kBackground = rgb(0x0e0e14), kPanel = rgb(0x171721), kRaised = rgb(0x242432), kRaisedHover = rgb(0x2f2f41);
const ImVec4 kLine = rgb(0x2b2b3a), kText = rgb(0xececf4), kDim = rgb(0x9a9ab0), kFaint = rgb(0x6c6c80);
const ImVec4 kPink = rgb(0xf2468f), kPinkHover = rgb(0xff5ea2), kOrange = rgb(0xf47b2b), kGreen = rgb(0x4fd774), kAmber = rgb(0xf5b041);

// ---------------------------------------------------------------- the save file

std::string pref_folder() {
  std::string p;
  if (char* pref = SDL_GetPrefPath("VibRibbonVR", "VibRibbonVR")) {  // (the game's own folder for save.txt)
    p = pref;
    SDL_free(pref);
  }
  return p;
}

vib::SaveData load_save() {
  std::ifstream in(vib::path_from_utf8(pref_folder() + "save.txt"), std::ios::binary);
  if (!in) return {};
  std::stringstream text;
  text << in.rdbuf();
  return vib::SaveData::from_text(text.str());
}

bool write_save(const vib::SaveData& d) {
  std::ofstream out(vib::path_from_utf8(pref_folder() + "save.txt"), std::ios::binary);
  out << d.to_text();
  return bool(out);
}

// ---------------------------------------------------------------- the disc

struct DiscCheck {
  enum class State { None, Ok, Unknown, Unreadable } state = State::None;
  std::string text;
};

DiscCheck check_disc(const std::string& cue) {
  if (cue.empty()) return {DiscCheck::State::None, "No disc image chosen yet"};
  try {
    vib::DiscImage disc = vib::DiscImage::open(vib::path_from_utf8(cue));
    if (const vib::KnownDisc* k = vib::identify_by_data_track_sha1(disc.data_track_sha1()))
      return {DiscCheck::State::Ok, "Vib-Ribbon (" + std::string(k->region) + "), " + std::string(k->serial)};
    return {DiscCheck::State::Unknown, "Read, but not the PAL disc (SCES-02873) this was made for: it may not work"};
  } catch (const std::exception& e) {
    return {DiscCheck::State::Unreadable, std::string("Could not be read: ") + e.what()};
  }
}

std::string file_name(const std::string& path) {
  const std::size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// ---------------------------------------------------------------- the display's modes

struct Mode {
  int w, h, hz;
};

std::vector<Mode> display_modes() {
  std::vector<Mode> out;
  int count = 0;
  if (SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &count)) {
    for (int i = 0; i < count; ++i) {
      const Mode m{modes[i]->w, modes[i]->h, int(modes[i]->refresh_rate + 0.5f)};
      if (std::none_of(out.begin(), out.end(), [&](const Mode& x) { return x.w == m.w && x.h == m.h && x.hz == m.hz; })) out.push_back(m);
    }
    SDL_free(modes);
  }
  return out;
}

constexpr int kFrameLimits[] = {0, 30, 60, 90, 120, 144, 165, 240, 360};
constexpr int kHeadsetRates[] = {0, 72, 80, 90, 120};

// ---------------------------------------------------------------- widgets

struct Fonts {
  ImFont* regular = nullptr;
  ImFont* semibold = nullptr;
  ImFont* bold = nullptr;
};
Fonts g_fonts;

float dpi() { return ImGui::GetStyle().FontScaleDpi; }

void heading(const char* text) {
  ImGui::PushFont(g_fonts.bold, 19.0f);
  ImGui::TextUnformatted(text);
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, 2));
}

void note(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, kDim);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

void divider() {
  ImGui::Dummy(ImVec2(0, 10 * dpi()));
  ImGui::Separator();
  ImGui::Dummy(ImVec2(0, 10 * dpi()));
}

// A row of buttons for one choice, the chosen one in the accent colour. True if changed.
bool segmented(const char* id, int& value, std::initializer_list<const char*> labels) {
  bool changed = false;
  ImGui::PushID(id);
  int i = 0;
  for (const char* label : labels) {
    if (i) ImGui::SameLine(0, 6 * dpi());
    const bool on = value == i;
    ImGui::PushStyleColor(ImGuiCol_Button, on ? kPink : kRaised);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? kPinkHover : kRaisedHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kPinkHover);
    if (ImGui::Button(label) && !on) value = i, changed = true;
    ImGui::PopStyleColor(3);
    ++i;
  }
  ImGui::PopID();
  return changed;
}

// A switch, its label to the right and a note under it. True if changed.
bool toggle(const char* label, bool& value, const char* description = nullptr) {
  const float h = ImGui::GetFrameHeight() * 0.78f, w = h * 1.8f;
  ImGui::PushID(label);
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const float lift = (ImGui::GetFrameHeight() - h) * 0.5f;
  const ImVec2 p(start.x, start.y + lift);
  const bool pressed = ImGui::InvisibleButton("switch", ImVec2(w, ImGui::GetFrameHeight()));
  if (pressed) value = !value;
  const bool hover = ImGui::IsItemHovered();
  ImGui::PopID();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(value ? (hover ? kPinkHover : kPink) : (hover ? kRaisedHover : kRaised)), h * 0.5f);
  const float r = h * 0.5f - 3.0f * dpi(), cx = value ? p.x + w - h * 0.5f : p.x + h * 0.5f;
  draw->AddCircleFilled(ImVec2(cx, p.y + h * 0.5f), r, ImGui::GetColorU32(kText));
  ImGui::SameLine(0, 12 * dpi());
  ImGui::AlignTextToFramePadding();
  ImGui::PushFont(g_fonts.semibold, 0.0f);
  ImGui::TextUnformatted(label);
  ImGui::PopFont();
  if (description) {
    ImGui::Indent(w + 12 * dpi());
    note(description);
    ImGui::Unindent(w + 12 * dpi());
  }
  return pressed;
}

// A label in the left column of a settings row.
void row_label(const char* label, float column) {
  ImGui::AlignTextToFramePadding();
  ImGui::PushFont(g_fonts.semibold, 0.0f);
  ImGui::TextUnformatted(label);
  ImGui::PopFont();
  ImGui::SameLine(column);
}

// A choice card: a large button with a title and a line under it.
bool card(const char* title, const char* text, bool chosen, float width) {
  const float s = dpi();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float height = 96.0f * s;
  ImGui::PushID(title);
  const bool pressed = ImGui::InvisibleButton("card", ImVec2(width, height));
  const bool hover = ImGui::IsItemHovered();
  ImGui::PopID();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float round = 10.0f * s, pad = 16.0f * s;
  draw->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(hover ? kRaisedHover : kRaised), round);
  if (chosen) draw->AddRect(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(kPink), round, 0, 2.0f * s);
  const ImVec2 dot(p.x + width - pad - 8.0f * s, p.y + pad + 10.0f * s);
  draw->AddCircle(dot, 9.0f * s, ImGui::GetColorU32(chosen ? kPink : kFaint), 0, 2.0f * s);
  if (chosen) draw->AddCircleFilled(dot, 5.0f * s, ImGui::GetColorU32(kPink));
  draw->AddText(g_fonts.bold, 19.0f * s, ImVec2(p.x + pad, p.y + pad), ImGui::GetColorU32(kText), title);
  draw->AddText(g_fonts.regular, 15.0f * s, ImVec2(p.x + pad, p.y + pad + 30.0f * s), ImGui::GetColorU32(kDim), text, nullptr,
                width - 2 * pad - 24.0f * s);
  return pressed;
}

// One row of the controls tables.
void control_row(const char* what, const char* first, const char* second) {
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::PushFont(g_fonts.semibold, 0.0f);
  ImGui::TextUnformatted(what);
  ImGui::PopFont();
  ImGui::TableNextColumn();
  ImGui::TextWrapped("%s", first);
  ImGui::TableNextColumn();
  ImGui::TextWrapped("%s", second);
}

// A boxed tip with an accent bar at its left.
void callout(const char* title, const char* text) {
  const float s = dpi();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kRaised);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 9.0f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * s, 14 * s));
  ImGui::BeginChild(title, ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
  const ImVec2 p = ImGui::GetWindowPos();
  ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 5.0f * s, p.y + ImGui::GetWindowHeight()), ImGui::GetColorU32(kPink), 9.0f * s,
                                            ImDrawFlags_RoundCornersLeft);
  ImGui::PushFont(g_fonts.bold, 17.0f);
  ImGui::TextUnformatted(title);
  ImGui::PopFont();
  ImGui::PushStyleColor(ImGuiCol_Text, kDim);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

// A combo of whole numbers shown with a unit ("144 Hz"); `zero` names 0.
template <class List>
void number_combo(const char* id, int& value, const List& list, const char* unit, const char* zero, float width) {
  auto text = [&](int v) { return v > 0 ? std::to_string(v) + unit : std::string(zero); };
  ImGui::SetNextItemWidth(width);
  if (ImGui::BeginCombo(id, text(value).c_str())) {
    for (const int v : list)
      if (ImGui::Selectable(text(v).c_str(), v == value)) value = v;
    ImGui::EndCombo();
  }
}

// ---------------------------------------------------------------- the launcher

struct Launcher {
  SDL_Window* window = nullptr;
  vib::Options o;  // the launcher's copy of the settings it shows
  DiscCheck disc;
  std::vector<Mode> modes;
  std::vector<std::pair<int, int>> sizes;
  std::mutex picked_mutex;
  std::optional<std::string> picked;  // from the file dialog's thread
  std::string message;                // after Play, if something went wrong
  int force_tab = -1;

  void start() {
    o = load_save().options;
    disc = check_disc(o.disc_path);
    modes = display_modes();
    for (const Mode& m : modes)
      if (std::find(sizes.begin(), sizes.end(), std::make_pair(m.w, m.h)) == sizes.end()) sizes.emplace_back(m.w, m.h);
  }

  void browse() {
    static const SDL_DialogFileFilter filters[] = {{"Disc image (.cue)", "cue"}};
    std::string folder;
    if (const std::size_t slash = o.disc_path.find_last_of("/\\"); slash != std::string::npos) folder = o.disc_path.substr(0, slash);
    SDL_ShowOpenFileDialog(
        [](void* data, const char* const* files, int) {
          auto* self = static_cast<Launcher*>(data);
          if (files && files[0]) {
            std::lock_guard<std::mutex> lock(self->picked_mutex);
            self->picked = files[0];
          }
        },
        this, window, filters, 1, folder.empty() ? nullptr : folder.c_str(), false);
  }

  void take_picked() {
    std::lock_guard<std::mutex> lock(picked_mutex);
    if (!picked) return;
    o.disc_path = *picked;
    picked.reset();
    disc = check_disc(o.disc_path);
  }

  // Writes the launcher's settings into save.txt as it is now (the game may have
  // changed others, such as high scores, since the launcher opened) and starts the game.
  bool play() {
    vib::SaveData d = load_save();
    vib::Options& t = d.options;
    t.disc_path = o.disc_path;
    t.window_mode = o.window_mode;
    t.fullscreen_w = o.fullscreen_w, t.fullscreen_h = o.fullscreen_h, t.fullscreen_hz = o.fullscreen_hz;
    t.vsync = o.vsync, t.frame_limit = o.frame_limit, t.picture_43 = o.picture_43, t.low_latency = o.low_latency;
    t.vr_refresh_hz = o.vr_refresh_hz;
    t.launcher_vr = o.launcher_vr, t.launcher_close = o.launcher_close;
    if (!write_save(d)) {
      message = "The settings could not be written to " + pref_folder() + "save.txt";
      return false;
    }
    const char* base_path = SDL_GetBasePath();
    const std::string base = base_path ? base_path : "";
    std::vector<std::string> args;
    std::string exe = base + (o.launcher_vr ? "Vib-Ribbon VR.exe" : "Vib-Ribbon.exe");
    if (!std::filesystem::exists(vib::path_from_utf8(exe))) {  // (a developer build: ..\game\vibplay.exe, --vr for the headset)
#ifdef _WIN32
      const std::string dev = base + "..\\game\\vibplay.exe";
#else
      const std::string dev = base + "../game/vibplay";
#endif
      if (std::filesystem::exists(vib::path_from_utf8(dev))) {
        exe = dev;
        if (o.launcher_vr) args.push_back("--vr");
      }
    }
    if (!std::filesystem::exists(vib::path_from_utf8(exe))) {
      message = "Not found beside the launcher: " + file_name(exe);
      return false;
    }
    args.insert(args.begin(), exe);
    std::vector<const char*> argv;
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    SDL_Process* process = SDL_CreateProcess(argv.data(), false);
    if (!process) {
      message = std::string("The game could not be started: ") + SDL_GetError();
      return false;
    }
    SDL_DestroyProcess(process);  // (the game goes on by itself)
    message = "The game is starting...";
    return true;
  }

  // ------------------------------------------------------------ the tabs

  void setup_tab(float width) {
    heading("Game disc");
    note("Vib-Ribbon is not included. Choose your own copy: the .cue file of a disc image of the PAL disc "
         "(SCES-02873), with its .bin files beside it.");
    ImGui::Dummy(ImVec2(0, 4 * dpi()));
    if (ImGui::Button(o.disc_path.empty() ? "Browse for the disc image..." : "Choose another...")) browse();
    ImGui::SameLine(0, 14 * dpi());
    ImGui::AlignTextToFramePadding();
    if (disc.state == DiscCheck::State::Ok) ImGui::TextColored(kGreen, "OK");
    if (disc.state == DiscCheck::State::Unknown) ImGui::TextColored(kAmber, "Check");
    if (disc.state == DiscCheck::State::Unreadable) ImGui::TextColored(kAmber, "Problem");
    if (disc.state != DiscCheck::State::None) ImGui::SameLine(0, 10 * dpi());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(disc.state == DiscCheck::State::None ? kDim : kText, "%s", disc.text.c_str());
    ImGui::PopTextWrapPos();
    if (!o.disc_path.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, kFaint);
      ImGui::TextWrapped("%s", o.disc_path.c_str());
      ImGui::PopStyleColor();
    }

    divider();
    heading("Play on");
    const float gap = 12.0f * dpi(), w = (width - gap) * 0.5f;
    if (card("Monitor", "On this PC's screen, with a gamepad, the keyboard or the mouse.", !o.launcher_vr, w)) o.launcher_vr = false;
    ImGui::SameLine(0, gap);
    if (card("VR headset", "In a PC headset. Start Virtual Desktop, Quest Link or SteamVR first.", o.launcher_vr, w)) o.launcher_vr = true;

    divider();
    callout("Everything else is in the game",
            "Press Start on a gamepad, or P or Tab on the keyboard, on any screen for the options menu: the camera, "
            "colours, scroll speed, timing feedback, glow, calibration, the original look and more. In a song it pauses. "
            "Exit is on the title's menu: B jumps to it.");
  }

  void display_tab() {
    const float column = 160.0f * dpi(), field = 240.0f * dpi();
    heading("Monitor");
    row_label("Window mode", column);
    segmented("window", o.window_mode, {"Window", "Borderless", "Fullscreen"});

    const bool full = o.window_mode == 2;
    row_label("Resolution", column);
    ImGui::BeginDisabled(!full);
    ImGui::SetNextItemWidth(field);
    std::string now = o.fullscreen_w > 0 ? std::to_string(o.fullscreen_w) + " x " + std::to_string(o.fullscreen_h) : "Desktop's";
    if (ImGui::BeginCombo("##resolution", now.c_str())) {
      if (ImGui::Selectable("Desktop's", o.fullscreen_w <= 0)) o.fullscreen_w = o.fullscreen_h = o.fullscreen_hz = 0;
      for (const auto& [w, h] : sizes) {
        const std::string t = std::to_string(w) + " x " + std::to_string(h);
        if (ImGui::Selectable(t.c_str(), o.fullscreen_w == w && o.fullscreen_h == h)) {
          o.fullscreen_w = w, o.fullscreen_h = h, o.fullscreen_hz = 0;
          for (const Mode& m : modes)  // (the fastest the display offers there)
            if (m.w == w && m.h == h) o.fullscreen_hz = std::max(o.fullscreen_hz, m.hz);
        }
      }
      ImGui::EndCombo();
    }
    std::vector<int> rates;
    for (const Mode& m : modes)
      if (m.w == o.fullscreen_w && m.h == o.fullscreen_h) rates.push_back(m.hz);
    row_label("Refresh rate", column);
    ImGui::BeginDisabled(o.fullscreen_w <= 0);
    number_combo("##refresh", o.fullscreen_hz, rates, " Hz", "Desktop's", field);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::Indent(column);
    note(full ? "The display switches to these while the game runs."
              : "Resolution and refresh rate are for Fullscreen; a window and borderless use the desktop's.");
    ImGui::Unindent(column);
    ImGui::Dummy(ImVec2(0, 4 * dpi()));

    row_label("Frame limit", column);
    number_combo("##limit", o.frame_limit, kFrameLimits, " fps", "Off", field);
    int shape = o.picture_43 ? 1 : 0;
    row_label("Picture shape", column);
    if (segmented("shape", shape, {"Fill the screen", "4:3, like the original"})) o.picture_43 = shape == 1;
    ImGui::Dummy(ImVec2(0, 8 * dpi()));
    toggle("VSync", o.vsync, "Waits for the display before each frame: no tearing. Off, frames come as fast as they can.");
    ImGui::Dummy(ImVec2(0, 4 * dpi()));
    toggle("Low latency", o.low_latency, "Fewer frames wait in line to be shown, so what you see is closer to now.");

    divider();
    heading("VR headset");
    row_label("Refresh rate", column);
    number_combo("##headset", o.vr_refresh_hz, kHeadsetRates, " Hz", "Auto (up to 120 Hz)", field);
    ImGui::Indent(column);
    note("With Virtual Desktop, set its own frame rate instead: it offers the game only that one.");
    ImGui::Unindent(column);
  }

  void controls_tab() {
    callout("The options menu", "Start on a gamepad, P or Tab on the keyboard, on every screen. Most settings live there; in a song it pauses.");
    ImGui::Dummy(ImVec2(0, 6 * dpi()));
    callout("Move the camera",
            "Set Options > Visual > Camera to Beside Vibri. Then the right stick zooms and the left stick goes round Vibri "
            "(mouse: the wheel and a right-button drag; keyboard: Ctrl and the arrows). The right stick click puts it back.");
    ImGui::Dummy(ImVec2(0, 10 * dpi()));
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
    auto table = [&](const char* id, const char* b, const char* c) {
      if (!ImGui::BeginTable(id, 3, flags)) return false;
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.8f);
      ImGui::TableSetupColumn(b, ImGuiTableColumnFlags_WidthStretch, 1.4f);
      ImGui::TableSetupColumn(c, ImGuiTableColumnFlags_WidthStretch, 1.4f);
      ImGui::PushStyleColor(ImGuiCol_Text, kDim);
      ImGui::TableHeadersRow();
      ImGui::PopStyleColor();
      return true;
    };
    heading("Playing");
    if (table("play", "Keyboard", "Gamepad")) {
      control_row("Block", "Left arrow or A", "Left shoulder or trigger");
      control_row("Pit", "Up or Down arrow", "D-pad up or down");
      control_row("Loop", "Right arrow or D", "Right shoulder or trigger");
      control_row("Wave", "Space, J or K", "A or Y");
      control_row("Options", "P or Tab", "Start");
      control_row("Menus", "Arrows, Enter, Esc; the mouse", "D-pad, A, B");
      control_row("Fullscreen", "F11 or Alt+Enter", "");
      control_row("Performance", "F3", "Left stick click");
      ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 10 * dpi()));
    heading("Camera");
    note("Set Options > Visual > Camera to Beside Vibri, then:");
    ImGui::Dummy(ImVec2(0, 2 * dpi()));
    if (table("camera", "Keyboard and mouse", "Gamepad")) {
      control_row("Zoom", "Mouse wheel, or Ctrl + Up / Down", "Right stick up / down");
      control_row("Go round Vibri", "Right-button drag, or Ctrl + Left / Right", "Left stick");
      control_row("Back as it was", "Home", "Right stick click");
      ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 10 * dpi()));
    heading("In a headset (Touch controllers)");
    if (table("touch", "Playing", "Menus")) {
      control_row("Buttons", "Block LT, Loop RT, Pit left grip / X / Y, Wave right grip / A / B", "RT or A chooses, LT or B goes back");
      control_row("Sticks", "Left: go round Vibri. Right: zoom; sideways: scroll speed", "Left stick moves");
      control_row("Menu button", "Options", "Options");
      control_row("Right stick click", "Recenter the view", "Recenter the view");
      ImGui::EndTable();
    }
  }

  // ------------------------------------------------------------ one frame

  // True when the launcher should close (the game started and that is wanted).
  bool frame() {
    take_picked();
    const float s = dpi();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    // The title: "Vib-Ribbon" in orange, "VR" in pink, the version small and dim.
    ImGui::PushFont(g_fonts.bold, 32.0f);
    ImGui::TextColored(kOrange, "Vib-Ribbon");
    ImGui::SameLine(0, 10 * s);
    ImGui::TextColored(kPink, "VR");
    const float title_bottom = ImGui::GetItemRectMax().y;
    ImGui::PopFont();
    ImGui::SameLine(0, 14 * s);
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, title_bottom - ImGui::GetTextLineHeight() - 6 * s));
    ImGui::TextColored(kFaint, "launcher %s", VIB_LAUNCHER_VERSION);
    ImGui::Dummy(ImVec2(0, 6 * s));

    // The panel with the tabs; the footer below it keeps its room.
    const float footer = 92.0f * s;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
    ImGui::BeginChild("panel", ImVec2(0, -footer), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (ImGui::BeginTabBar("tabs")) {
      const char* names[] = {"  Setup  ", "  Display  ", "  Controls  "};
      for (int i = 0; i < 3; ++i) {
        const ImGuiTabItemFlags f = force_tab == i ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem(names[i], nullptr, f)) {
          ImGui::Dummy(ImVec2(0, 10 * s));
          ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_None);
          if (i == 0) setup_tab(ImGui::GetContentRegionAvail().x);
          if (i == 1) display_tab();
          if (i == 2) controls_tab();
          ImGui::EndChild();
          ImGui::EndTabItem();
        }
      }
      ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    force_tab = -1;

    // The footer: Play, close when it starts, the save folder.
    ImGui::Dummy(ImVec2(0, 12 * s));
    bool close = false;
    const bool ready = disc.state == DiscCheck::State::Ok || disc.state == DiscCheck::State::Unknown;
    ImGui::BeginDisabled(!ready);
    ImGui::PushStyleColor(ImGuiCol_Button, kPink);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kPinkHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kPinkHover);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10 * s);
    ImGui::PushFont(g_fonts.bold, 24.0f);
    if (ImGui::Button("Play", ImVec2(200 * s, 58 * s)) && play() && o.launcher_close) close = true;
    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    ImGui::SameLine(0, 20 * s);
    ImGui::BeginGroup();
    toggle("Close the launcher when the game starts", o.launcher_close);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * s, 3 * s));
    if (ImGui::Button("Open the save and log folder")) {
      std::string folder = pref_folder();
      std::replace(folder.begin(), folder.end(), '\\', '/');
      SDL_OpenURL(("file:///" + folder).c_str());
    }
    ImGui::PopStyleVar();
    ImGui::SameLine(0, 14 * s);
    ImGui::AlignTextToFramePadding();
    if (!ready) ImGui::TextColored(kAmber, "Choose your disc image on Setup first");
    else if (!message.empty()) ImGui::TextColored(message.rfind("The game is", 0) == 0 ? kGreen : kAmber, "%s", message.c_str());
    ImGui::EndGroup();
    ImGui::End();
    return close;
  }
};

void style_it(float scale) {
  ImGuiStyle& st = ImGui::GetStyle();
  st.WindowPadding = ImVec2(24, 18);
  st.FramePadding = ImVec2(16, 9);
  st.ItemSpacing = ImVec2(10, 9);
  st.ItemInnerSpacing = ImVec2(8, 6);
  st.CellPadding = ImVec2(8, 7);
  st.WindowRounding = 0;
  st.ChildRounding = 12;
  st.FrameRounding = 7;
  st.PopupRounding = 8;
  st.GrabRounding = 7;
  st.TabRounding = 7;
  st.ScrollbarRounding = 8;
  st.ScrollbarSize = 12;
  st.WindowBorderSize = 0;
  st.ChildBorderSize = 0;
  st.PopupBorderSize = 0;
  st.FrameBorderSize = 0;
  st.TabBorderSize = 0;
  st.TabBarBorderSize = 1;
  st.SeparatorTextBorderSize = 1;
  ImVec4* c = st.Colors;
  c[ImGuiCol_Text] = kText;
  c[ImGuiCol_TextDisabled] = kFaint;
  c[ImGuiCol_WindowBg] = kBackground;
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = kRaised;
  c[ImGuiCol_Border] = kLine;
  c[ImGuiCol_FrameBg] = kRaised;
  c[ImGuiCol_FrameBgHovered] = kRaisedHover;
  c[ImGuiCol_FrameBgActive] = kRaisedHover;
  c[ImGuiCol_Button] = kRaised;
  c[ImGuiCol_ButtonHovered] = kRaisedHover;
  c[ImGuiCol_ButtonActive] = rgb(0x3a3a50);
  c[ImGuiCol_Header] = rgb(0x30304a);
  c[ImGuiCol_HeaderHovered] = rgb(0x3a3a55);
  c[ImGuiCol_HeaderActive] = rgb(0x44445f);
  c[ImGuiCol_Separator] = kLine;
  c[ImGuiCol_Tab] = kRaised;
  c[ImGuiCol_TabHovered] = kPinkHover;
  c[ImGuiCol_TabSelected] = kPink;
  c[ImGuiCol_TabSelectedOverline] = kPink;
  c[ImGuiCol_TabDimmed] = kRaised;
  c[ImGuiCol_TabDimmedSelected] = kPink;
  c[ImGuiCol_TableHeaderBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = rgb(0xffffff, 0.025f);
  c[ImGuiCol_TableBorderLight] = kLine;
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = kRaised;
  c[ImGuiCol_ScrollbarGrabHovered] = kRaisedHover;
  c[ImGuiCol_CheckMark] = kPink;
  c[ImGuiCol_NavCursor] = kPink;
  st.ScaleAllSizes(scale);
  st.FontScaleDpi = scale;
}

void load_fonts() {
  ImGuiIO& io = ImGui::GetIO();
  // Windows' own Segoe UI; elsewhere (checking the layout) whatever is at hand.
  struct Face {
    ImFont** slot;
    std::vector<const char*> files;
  };
  const Face faces[] = {
      {&g_fonts.regular, {"C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/opentype/inter/Inter-Regular.otf",
                          "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"}},
      {&g_fonts.semibold, {"C:/Windows/Fonts/seguisb.ttf", "/usr/share/fonts/opentype/inter/Inter-SemiBold.otf",
                           "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"}},
      {&g_fonts.bold, {"C:/Windows/Fonts/segoeuib.ttf", "/usr/share/fonts/opentype/inter/Inter-Bold.otf",
                       "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"}},
  };
  for (const Face& f : faces) {
    for (const char* file : f.files) {
      std::error_code ec;
      if (std::filesystem::exists(file, ec) && (*f.slot = io.Fonts->AddFontFromFileTTF(file, 16.0f))) break;
    }
    if (!*f.slot) *f.slot = io.Fonts->AddFontDefault();
  }
  io.FontDefault = g_fonts.regular;
  ImGui::GetStyle().FontSizeBase = 16.0f;
}

}  // namespace

int main(int argc, char** argv) {
  std::string screenshot;
  int screenshot_tab = 0;
  for (int i = 1; i + 2 < argc; ++i)
    if (std::string(argv[i]) == "--screenshot") screenshot = argv[i + 1], screenshot_tab = std::atoi(argv[i + 2]);
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Vib-Ribbon Launcher", SDL_GetError(), nullptr);
    return 1;
  }
  float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  if (scale <= 0) scale = 1.0f;
  int width = int(840 * scale), height = int(820 * scale);
  SDL_Rect usable{};
  if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.h > 0) {  // (a small or scaled screen: fit it)
    width = std::min(width, usable.w * 95 / 100);
    height = std::min(height, usable.h * 92 / 100);
  }
  SDL_Window* window = SDL_CreateWindow("Vib-Ribbon Launcher", width, height,
                                        SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
  SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, screenshot.empty() ? nullptr : "software") : nullptr;
  if (!renderer) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Vib-Ribbon Launcher", SDL_GetError(), nullptr);
    return 1;
  }
  SDL_SetWindowMinimumSize(window, std::min(width, int(720 * scale)), std::min(height, int(560 * scale)));
  SDL_SetRenderVSync(renderer, 1);
  SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(window);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  load_fonts();
  style_it(scale);
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  Launcher app;
  app.window = window;
  app.start();

  bool done = false;
  for (int frame = 0; !done; ++frame) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      ImGui_ImplSDL3_ProcessEvent(&e);
      if (e.type == SDL_EVENT_QUIT) done = true;
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(20);
      continue;
    }
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (!screenshot.empty() && frame < 2) app.force_tab = screenshot_tab;
    if (app.frame()) done = true;
    ImGui::Render();
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColorFloat(renderer, kBackground.x, kBackground.y, kBackground.z, 1.0f);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    if (!screenshot.empty() && frame == 4) {  // (the layout, for checking without a screen)
      if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr)) {
        SDL_SaveBMP(shot, screenshot.c_str());
        SDL_DestroySurface(shot);
      }
      done = true;
    }
    SDL_RenderPresent(renderer);
  }
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
