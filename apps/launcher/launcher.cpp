// Vib-Ribbon Launcher (task 4.16 I): the Windows download's front door, for players
// who have not seen the game before. One window: the disc image, monitor or headset, the
// PC display settings (window mode, resolution, refresh rate, VSync, frame limit, the
// picture's shape, low latency, the headset's refresh rate), and the controls, which
// point out the options menu in the game (Start) where everything else is. Play writes
// the choices into save.txt (the game's own file, %APPDATA%\VibRibbonVR\VibRibbonVR)
// and starts Vib-Ribbon.exe or Vib-Ribbon VR.exe from the launcher's own folder.
//
// Plain Win32 controls, no other library; English only for now.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "vib/save.h"

namespace {

// ---------------------------------------------------------------- text

std::wstring wide(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(std::size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}

std::string utf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(std::size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

// ---------------------------------------------------------------- the save file

std::filesystem::path save_path() {
  PWSTR roaming = nullptr;
  std::filesystem::path p;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) p = roaming;
  CoTaskMemFree(roaming);
  return p / L"VibRibbonVR" / L"VibRibbonVR" / L"save.txt";  // (where the game's SDL_GetPrefPath puts it)
}

vib::SaveData load_save() {
  std::ifstream in(save_path(), std::ios::binary);
  if (!in) return {};
  std::stringstream text;
  text << in.rdbuf();
  return vib::SaveData::from_text(text.str());
}

bool write_save(const vib::SaveData& d) {
  std::error_code ec;
  std::filesystem::create_directories(save_path().parent_path(), ec);
  std::ofstream out(save_path(), std::ios::binary);
  out << d.to_text();
  return bool(out);
}

// ---------------------------------------------------------------- the display's modes

struct Mode {
  int w, h, hz;
};

std::vector<Mode> display_modes() {
  std::vector<Mode> out;
  DEVMODEW dm{};
  dm.dmSize = sizeof dm;
  for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); ++i) {
    if (dm.dmBitsPerPel < 24) continue;
    const Mode m{int(dm.dmPelsWidth), int(dm.dmPelsHeight), int(dm.dmDisplayFrequency)};
    if (m.hz <= 1) continue;
    if (std::none_of(out.begin(), out.end(), [&](const Mode& x) { return x.w == m.w && x.h == m.h && x.hz == m.hz; })) out.push_back(m);
  }
  std::sort(out.begin(), out.end(), [](const Mode& a, const Mode& b) {
    return a.w != b.w ? a.w > b.w : a.h != b.h ? a.h > b.h : a.hz > b.hz;
  });
  return out;
}

// ---------------------------------------------------------------- the window

enum Id : int {
  kDisc = 100, kBrowse, kMonitor, kHeadset, kWindowMode, kResolution, kRefresh, kVsync, kFrameLimit, kShape, kLowLatency,
  kHeadsetRate, kControls, kPlay, kQuit,
};

constexpr int kFrameLimits[] = {0, 30, 60, 90, 120, 144, 165, 240, 360};
constexpr int kHeadsetRates[] = {0, 72, 80, 90, 120};

const wchar_t* const kControlsText =
    L"PLAYING (keyboard  /  gamepad)\r\n"
    L"  Block    Left arrow or A     /  left shoulder or trigger\r\n"
    L"  Pit      Up or Down arrow    /  d-pad up or down\r\n"
    L"  Loop     Right arrow or D    /  right shoulder or trigger\r\n"
    L"  Wave     Space, J or K       /  A or Y\r\n"
    L"\r\n"
    L"OPTIONS MENU: Start on the gamepad, P or Tab on the keyboard, on every screen.\r\n"
    L"  In a song it pauses. Much more is there than here: the camera, colours,\r\n"
    L"  scroll speed, timing feedback, glow, calibration, the original look...\r\n"
    L"\r\n"
    L"MENUS: d-pad, A to choose, B to go back  /  arrows, Enter, Esc  /  the mouse\r\n"
    L"  (click, right click to go back, wheel). Exit is on the title's menu (B jumps to it).\r\n"
    L"\r\n"
    L"CAMERA: set Options > Visual > Camera to Beside Vibri, then\r\n"
    L"  right stick: zoom in and out      left stick: go round Vibri\r\n"
    L"  mouse: wheel zooms, right-button drag goes round      keyboard: Ctrl + arrows\r\n"
    L"  R3 (right stick click) or Home: the view back as it starts\r\n"
    L"\r\n"
    L"ALSO: F11 or Alt+Enter fullscreen    F3 or L3 performance overlay\r\n"
    L"\r\n"
    L"IN A HEADSET (Touch controllers)\r\n"
    L"  Block LT   Loop RT   Pit left grip, X or Y   Wave right grip, A or B\r\n"
    L"  Menu button: options    Left stick: go round Vibri    Right stick: zoom\r\n"
    L"  Right stick sideways: scroll speed    R3: recenter\r\n"
    L"  Menus: left stick moves, RT or A chooses, LT or B goes back\r\n";

struct Launcher {
  HWND window = nullptr;
  HFONT font = nullptr, bold = nullptr, mono = nullptr;
  int dpi = 96;
  vib::SaveData save;
  std::vector<Mode> modes;
  std::vector<std::pair<int, int>> sizes;  // the resolutions offered, largest first
  std::wstring disc;

  int px(int v) const { return MulDiv(v, dpi, 96); }
  HWND item(int id) const { return GetDlgItem(window, id); }

  HWND make(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT f = nullptr) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(w), px(h), window,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(f ? f : font), TRUE);
    return c;
  }
  HWND label(const wchar_t* text, int x, int y, int w, HFONT f = nullptr) { return make(L"STATIC", text, 0, x, y, w, 20, 0, f); }
  HWND combo(int id, int x, int y, int w) { return make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x, y, w, 300, id); }
  void add(int id, const std::wstring& text) { SendMessageW(item(id), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())); }
  void pick(int id, int index) { SendMessageW(item(id), CB_SETCURSEL, WPARAM(index), 0); }
  int picked(int id) const { return int(SendMessageW(item(id), CB_GETCURSEL, 0, 0)); }
  bool checked(int id) const { return SendMessageW(item(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
  void check(int id, bool on) { SendMessageW(item(id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); }

  void create_fonts() {
    NONCLIENTMETRICSW m{};
    m.cbSize = sizeof m;
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0, UINT(dpi));
    font = CreateFontIndirectW(&m.lfMessageFont);
    LOGFONTW b = m.lfMessageFont;
    b.lfWeight = FW_BOLD;
    bold = CreateFontIndirectW(&b);
    LOGFONTW c = m.lfMessageFont;
    c.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    lstrcpynW(c.lfFaceName, L"Consolas", LF_FACESIZE);
    mono = CreateFontIndirectW(&c);
  }

  void build() {
    const vib::Options& o = save.options;
    label(L"Vib-Ribbon VR: a fan project. Plays your own copy of the PAL disc (SCES-02873).", 16, 12, 640, bold);

    label(L"Disc image (.cue)", 16, 44, 200, bold);
    disc = wide(o.disc_path);
    make(L"EDIT", disc.c_str(), ES_AUTOHSCROLL | ES_READONLY | WS_BORDER, 16, 66, 520, 24, kDisc);
    make(L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, 546, 65, 110, 26, kBrowse);

    label(L"Play on", 16, 102, 200, bold);
    make(L"BUTTON", L"Monitor", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 16, 124, 120, 22, kMonitor);
    make(L"BUTTON", L"VR headset (start Virtual Desktop, Quest Link or SteamVR first)", BS_AUTORADIOBUTTON, 140, 124, 520, 22, kHeadset);
    check(kMonitor, true);

    label(L"Monitor", 16, 158, 200, bold);
    label(L"Window mode", 16, 184, 130);
    combo(kWindowMode, 150, 180, 200);
    for (const wchar_t* t : {L"Window", L"Borderless fullscreen", L"Fullscreen"}) add(kWindowMode, t);
    pick(kWindowMode, std::clamp(o.window_mode, 0, 2));

    label(L"Resolution", 16, 216, 130);
    combo(kResolution, 150, 212, 200);
    add(kResolution, L"Desktop's");
    for (const Mode& m : modes)
      if (std::find(sizes.begin(), sizes.end(), std::make_pair(m.w, m.h)) == sizes.end()) sizes.emplace_back(m.w, m.h);
    int size_at = 0;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
      add(kResolution, std::to_wstring(sizes[i].first) + L" x " + std::to_wstring(sizes[i].second));
      if (sizes[i].first == o.fullscreen_w && sizes[i].second == o.fullscreen_h) size_at = int(i) + 1;
    }
    pick(kResolution, size_at);
    label(L"Refresh rate", 370, 216, 100);
    combo(kRefresh, 476, 212, 180);
    fill_rates(o.fullscreen_hz);
    label(L"(resolution and refresh rate apply in Fullscreen)", 150, 242, 500);

    make(L"BUTTON", L"VSync (wait for the display: no tearing)", BS_AUTOCHECKBOX | WS_TABSTOP, 16, 270, 330, 22, kVsync);
    check(kVsync, o.vsync);
    label(L"Frame limit", 370, 272, 100);
    combo(kFrameLimit, 476, 268, 180);
    int limit_at = 0;
    for (int i = 0; i < int(std::size(kFrameLimits)); ++i) {
      add(kFrameLimit, kFrameLimits[i] ? std::to_wstring(kFrameLimits[i]) + L" fps" : std::wstring(L"Off"));
      if (kFrameLimits[i] == o.frame_limit) limit_at = i;
    }
    pick(kFrameLimit, limit_at);

    label(L"Picture shape", 16, 304, 130);
    combo(kShape, 150, 300, 200);
    add(kShape, L"Fill the window");
    add(kShape, L"4:3, like the original");
    pick(kShape, o.picture_43 ? 1 : 0);
    make(L"BUTTON", L"Low latency", BS_AUTOCHECKBOX | WS_TABSTOP, 370, 302, 286, 22, kLowLatency);
    check(kLowLatency, o.low_latency);

    label(L"Headset", 16, 340, 200, bold);
    label(L"Refresh rate", 16, 366, 130);
    combo(kHeadsetRate, 150, 362, 200);
    int rate_at = 0;
    for (int i = 0; i < int(std::size(kHeadsetRates)); ++i) {
      add(kHeadsetRate, kHeadsetRates[i] ? std::to_wstring(kHeadsetRates[i]) + L" Hz" : std::wstring(L"Auto (up to 120 Hz)"));
      if (kHeadsetRates[i] == o.vr_refresh_hz) rate_at = i;
    }
    pick(kHeadsetRate, rate_at);
    label(L"(Virtual Desktop: set its own frame rate instead)", 370, 366, 300);

    label(L"Controls", 16, 402, 200, bold);
    make(L"EDIT", kControlsText, ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_BORDER, 16, 424, 640, 250, kControls, mono);

    make(L"BUTTON", L"Play", BS_DEFPUSHBUTTON | WS_TABSTOP, 430, 688, 110, 32, kPlay, bold);
    make(L"BUTTON", L"Quit", BS_PUSHBUTTON | WS_TABSTOP, 546, 688, 110, 32, kQuit);
    enable_fullscreen_items();
  }

  // The refresh rates the display offers at the chosen resolution (the desktop's: none to choose).
  void fill_rates(int want_hz) {
    SendMessageW(item(kRefresh), CB_RESETCONTENT, 0, 0);
    const int s = picked(kResolution);
    if (s <= 0) {
      add(kRefresh, L"Desktop's");
      pick(kRefresh, 0);
      return;
    }
    const auto [w, h] = sizes[std::size_t(s - 1)];
    int at = 0, n = 0;
    for (const Mode& m : modes)
      if (m.w == w && m.h == h) {
        add(kRefresh, std::to_wstring(m.hz) + L" Hz");
        if (m.hz == want_hz) at = n;
        ++n;
      }
    pick(kRefresh, at);  // (the highest unless one was chosen: largest first)
  }

  void enable_fullscreen_items() {
    const bool full = picked(kWindowMode) == 2;
    EnableWindow(item(kResolution), full);
    EnableWindow(item(kRefresh), full && picked(kResolution) > 0);
  }

  void browse() {
    wchar_t file[MAX_PATH * 4] = {};
    lstrcpynW(file, disc.c_str(), int(std::size(file)));
    OPENFILENAMEW of{};
    of.lStructSize = sizeof of;
    of.hwndOwner = window;
    of.lpstrFilter = L"Disc image (*.cue)\0*.cue\0All files\0*.*\0";
    of.lpstrFile = file;
    of.nMaxFile = DWORD(std::size(file));
    of.lpstrTitle = L"Your Vib-Ribbon disc image (.cue, with its .bin files beside it)";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&of)) return;
    disc = file;
    SetWindowTextW(item(kDisc), disc.c_str());
  }

  // Writes the choices and starts the game; false if it could not.
  bool play() {
    vib::Options& o = save.options;
    if (disc.empty() || !std::filesystem::exists(disc)) {
      MessageBoxW(window, L"Choose your disc image (.cue) first.", L"Vib-Ribbon", MB_ICONINFORMATION);
      browse();
      if (disc.empty() || !std::filesystem::exists(disc)) return false;
    }
    o.disc_path = utf8(disc);
    o.window_mode = std::max(0, picked(kWindowMode));
    const int s = picked(kResolution);
    if (s > 0) {
      o.fullscreen_w = sizes[std::size_t(s - 1)].first, o.fullscreen_h = sizes[std::size_t(s - 1)].second;
      wchar_t hz[32] = {};
      GetWindowTextW(item(kRefresh), hz, 32);
      o.fullscreen_hz = _wtoi(hz);
    } else {
      o.fullscreen_w = o.fullscreen_h = o.fullscreen_hz = 0;
    }
    o.vsync = checked(kVsync);
    o.frame_limit = kFrameLimits[std::clamp(picked(kFrameLimit), 0, int(std::size(kFrameLimits)) - 1)];
    o.picture_43 = picked(kShape) == 1;
    o.low_latency = checked(kLowLatency);
    o.vr_refresh_hz = kHeadsetRates[std::clamp(picked(kHeadsetRate), 0, int(std::size(kHeadsetRates)) - 1)];
    if (!write_save(save)) {
      MessageBoxW(window, (L"Could not write the settings to\n" + save_path().wstring()).c_str(), L"Vib-Ribbon", MB_ICONWARNING);
      return false;
    }
    // The game beside the launcher.
    wchar_t self[MAX_PATH * 4] = {};
    GetModuleFileNameW(nullptr, self, DWORD(std::size(self)));
    const std::filesystem::path folder = std::filesystem::path(self).parent_path();
    const bool headset = checked(kHeadset);
    std::filesystem::path exe = folder / (headset ? L"Vib-Ribbon VR.exe" : L"Vib-Ribbon.exe");
    std::wstring extra;
    if (!std::filesystem::exists(exe)) {  // (a developer build: build\<preset>\apps\game\vibplay.exe, --vr for the headset)
      const std::filesystem::path dev = folder.parent_path() / L"game" / L"vibplay.exe";
      if (std::filesystem::exists(dev)) exe = dev, extra = headset ? L" --vr" : L"";
    }
    if (!std::filesystem::exists(exe)) {
      MessageBoxW(window, (L"Not found beside the launcher:\n" + exe.wstring()).c_str(), L"Vib-Ribbon", MB_ICONERROR);
      return false;
    }
    std::wstring command = L"\"" + exe.wstring() + L"\"" + extra;
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    const std::wstring dir = exe.parent_path().wstring();
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
      MessageBoxW(window, L"The game could not be started.", L"Vib-Ribbon", MB_ICONERROR);
      return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
  }

  LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
      case WM_COMMAND: {
        const int id = LOWORD(wp), code = HIWORD(wp);
        if (id == kBrowse) browse();
        if (id == kQuit) DestroyWindow(window);
        if (id == kPlay && play()) DestroyWindow(window);
        if (code == CBN_SELCHANGE && id == kResolution) fill_rates(0);
        if (code == CBN_SELCHANGE && (id == kWindowMode || id == kResolution)) enable_fullscreen_items();
        return 0;
      }
      case WM_CTLCOLORSTATIC: {  // (labels on the window's own colour)
        SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
        if (reinterpret_cast<HWND>(lp) == item(kControls) || reinterpret_cast<HWND>(lp) == item(kDisc)) break;
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
      }
      case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
      default: break;
    }
    return DefWindowProcW(window, msg, wp, lp);
  }
};

Launcher* g_launcher = nullptr;

LRESULT CALLBACK window_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
  if (g_launcher && g_launcher->window == w) return g_launcher->handle(msg, wp, lp);
  return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  Launcher app;
  app.save = load_save();
  app.modes = display_modes();
  g_launcher = &app;

  WNDCLASSW wc{};
  wc.lpfnWndProc = window_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wc.lpszClassName = L"VibRibbonLauncher";
  RegisterClassW(&wc);

  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  app.dpi = int(GetDpiForSystem());
  RECT r{0, 0, app.px(672), app.px(734)};
  AdjustWindowRectExForDpi(&r, style, FALSE, 0, UINT(app.dpi));
  app.window = CreateWindowExW(0, wc.lpszClassName, L"Vib-Ribbon Launcher", style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                               r.bottom - r.top, nullptr, nullptr, instance, nullptr);
  if (!app.window) return 1;
  app.dpi = int(GetDpiForWindow(app.window));
  app.create_fonts();
  app.build();
  ShowWindow(app.window, show);
  UpdateWindow(app.window);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (IsDialogMessageW(app.window, &msg)) continue;  // (Tab between the controls, Enter plays)
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  DeleteObject(app.font);
  DeleteObject(app.bold);
  DeleteObject(app.mono);
  return 0;
}
