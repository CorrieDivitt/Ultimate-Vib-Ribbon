#include "vib/file_picker.h"

#include <algorithm>
#include <cmath>

namespace vib {

namespace {

constexpr double kTau = 6.28318530717958647692;
constexpr int kLanguages = 5;
constexpr int kRows = 12;          // rows shown at once below the heading
constexpr std::size_t kLongest = 102;  // characters of a name shown (owner: three times the first 34)

// Our own words, in the PAL menus' five languages (German, English, Spanish, French, Italian).
enum Word { kChooseFolder, kChooseMusic, kUseFolder, kDone, kUp, kNothing, kClear, kWords };
const char* const kText[kWords][kLanguages] = {
    {"Musikordner wählen", "Choose your music folder", "Elige tu carpeta de música", "Choisis ton dossier de musique",
     "Scegli la cartella della musica"},
    {"Musik wählen", "Choose music", "Elige música", "Choisis la musique", "Scegli la musica"},
    {"Diesen Ordner nehmen", "Use this folder", "Usar esta carpeta", "Prendre ce dossier", "Usa questa cartella"},
    {"Fertig", "Done", "Listo", "Terminé", "Fatto"},
    {"Ordner hoch", "Up a folder", "Subir", "Dossier parent", "Su di una cartella"},
    {"Keine Musik hier", "No music here", "No hay música aquí", "Pas de musique ici", "Nessuna musica qui"},
    {"Auswahl löschen", "Clear selection", "Borrar selección", "Effacer la sélection", "Cancella selezione"},
};

std::string word(Word w, int language) {
  return to_font_text(kText[w][language >= 0 && language < kLanguages ? language : 1]);  // (Japanese: English)
}

// A name as the font can show it, shortened from the middle if long.
std::string shown_name(const std::string& utf8) {
  std::string s = to_font_text(utf8);
  // Count shown characters ("~" and its letter are one).
  std::vector<std::size_t> starts;
  for (std::size_t i = 0; i < s.size(); ++i) {
    starts.push_back(i);
    if (s[i] == '~' && i + 1 < s.size()) ++i;
  }
  if (starts.size() <= kLongest) return s;
  const std::size_t keep_front = kLongest / 2 - 1, keep_back = kLongest - keep_front - 3;
  return s.substr(0, starts[keep_front]) + "..." + s.substr(starts[starts.size() - keep_back]);
}

}  // namespace

void FilePicker::show(Mode mode, const std::string& path, std::vector<Entry> entries, bool can_go_up) {
  if (path != path_ || mode != mode_) ticked_.clear();
  mode_ = mode;
  path_ = path;
  entries_ = std::move(entries);
  can_go_up_ = can_go_up;
  cursor_ = 0, first_ = 0;
  repeat_dir_ = 0;
}

std::vector<FilePicker::Line> FilePicker::lines() const {
  std::vector<Line> out;
  out.push_back({mode_ == Mode::Folder ? Row::UseFolder : Row::Done});
  if (can_go_up_) out.push_back({Row::Up});
  if (mode_ == Mode::Files) out.push_back({Row::Clear});  // (owner: below Up a folder)
  for (int i = 0; i < int(entries_.size()); ++i)
    if (mode_ == Mode::Files || entries_[std::size_t(i)].folder) out.push_back({Row::Entry, i});
  return out;
}

const FilePicker::Entry* FilePicker::entry() const {
  const auto l = lines();
  if (cursor_ < 0 || cursor_ >= int(l.size()) || l[std::size_t(cursor_)].row != Row::Entry) return nullptr;
  return &entries_[std::size_t(l[std::size_t(cursor_)].entry)];
}

std::vector<std::string> FilePicker::ticked() const {
  std::vector<std::string> out;
  for (const Entry& e : entries_)
    if (!e.folder && ticked_.count(e.name)) out.push_back(e.name);
  return out;
}

FilePicker::Action FilePicker::tick(const SettingsPanel::Input& in, double seconds) {
  notice_seconds_ = std::max(0.0, notice_seconds_ - seconds);
  const auto l = lines();
  const int count = int(l.size());
  if (in.cancel) return Action::Cancel;
  // Up and down move (wrapping); held, they scroll: after 0.35 s, 25 rows a second,
  // after a second of it about 60 (owner: long lists took too long). Left and right
  // page through a long list.
  int step = in.up ? -1 : in.down ? 1 : 0;
  bool scrolled = false;
  if (step) {
    scroll_dir_ = step;
    scroll_timer_ = 0.35;
    scroll_held_ = 0;
  } else if (scroll_dir_ != 0 && ((scroll_dir_ < 0 && in.held_up) || (scroll_dir_ > 0 && in.held_down))) {
    scroll_held_ += seconds;
    scroll_timer_ -= seconds;
    while (scroll_timer_ <= 0) {
      scroll_timer_ += scroll_held_ > 1.35 ? 0.016 : 0.04;
      step += scroll_dir_;
    }
    scrolled = step != 0;
  } else {
    scroll_dir_ = 0;
  }
  if (in.left || in.right) {
    step = in.left ? -kRows : kRows;
    repeat_dir_ = step;
    repeat_timer_ = 0.4;
  } else if (repeat_dir_ != 0 && ((repeat_dir_ < 0 && in.held_left) || (repeat_dir_ > 0 && in.held_right))) {
    repeat_timer_ -= seconds;
    if (repeat_timer_ <= 0) repeat_timer_ += 0.15, step = repeat_dir_;
  } else {
    repeat_dir_ = 0;
  }
  if (step) {
    // A press wraps round; a held scroll and a page stop at the ends.
    if ((step == 1 || step == -1) && !scrolled) cursor_ = (cursor_ + step + count) % count;
    else cursor_ = std::clamp(cursor_ + step, 0, count - 1);
    first_ = std::clamp(first_, cursor_ - kRows + 1, cursor_);
    return Action::Moved;
  }
  if (!in.confirm || count == 0) return Action::None;
  const Line& at = l[std::size_t(std::clamp(cursor_, 0, count - 1))];
  switch (at.row) {
    case Row::UseFolder: return Action::UseFolder;
    case Row::Done: return ticked_.empty() ? Action::None : Action::Done;
    case Row::Up: return Action::Up;
    case Row::Clear:
      if (ticked_.empty()) return Action::None;
      ticked_.clear();
      return Action::Cleared;
    case Row::Entry: {
      const Entry& e = entries_[std::size_t(at.entry)];
      if (e.folder) return Action::Enter;
      if (!ticked_.erase(e.name)) ticked_.insert(e.name);
      return Action::Toggled;
    }
  }
  return Action::None;
}

std::vector<std::string> FilePicker::rows() const {
  std::vector<std::string> out;
  for (const Line& l : lines()) {
    switch (l.row) {
      case Row::UseFolder: out.push_back(word(kUseFolder, language_)); break;
      case Row::Done: {
        std::string done = word(kDone, language_);
        if (!ticked_.empty()) done += " (" + std::to_string(ticked_.size()) + ")";
        out.push_back(done);
        break;
      }
      case Row::Up: out.push_back(word(kUp, language_)); break;
      case Row::Clear: out.push_back(word(kClear, language_)); break;
      case Row::Entry: {
        const Entry& e = entries_[std::size_t(l.entry)];
        // A folder ends in "/"; a ticked file starts with "x".
        if (e.folder) out.push_back(shown_name(e.name) + "/");
        else out.push_back(std::string(ticked_.count(e.name) ? "x " : "  ") + shown_name(e.name));
        break;
      }
    }
  }
  return out;
}

Hud::Layer FilePicker::draw(float size, std::uint32_t seed) const {
  Hud::Layer layer;
  // The settings panel's camera: at the origin looking along +z, 60 degrees high.
  layer.view.eye = {0, 0, 0};
  layer.view.target = {0, 0, 4730};
  layer.view.fov_y = float(682 * kTau / 4096.0);
  const float depth = 4000.0f * size, half_h = 4000.0f * std::tan(0.5f * layer.view.fov_y);
  const float row = 200.0f, left = -2700.0f;
  constexpr float kRowScale = 0.72f;  // (smaller, so 102 characters fit across a 4:3 picture)
  auto put = [&](const std::string& text, float x, float y, float scale, Rgb colour, std::uint32_t s) {
    std::vector<SceneLine> lines = font_.text_lines(text, scale, s, 3);
    const float shift = x + 0.5f * font_.text_width(text, scale);  // (text_lines centres on 0)
    for (SceneLine& l : lines) {
      for (Vec3f* p : {&l.a, &l.b}) *p = {(p->x + shift) * size, (p->y + y) * size, depth};
      l.colour = colour;
      l.jitter_a = {l.jitter_a.x * size, l.jitter_a.y * size, l.jitter_a.z * size};
      l.jitter_b = {l.jitter_b.x * size, l.jitter_b.y * size, l.jitter_b.z * size};
      l.shake_a *= size, l.shake_b *= size;
      layer.lines.push_back(l);
    }
  };
  const Rgb white{255, 255, 255}, dim{110, 110, 110};
  float y = -0.85f * half_h;
  put(mode_ == Mode::Folder ? word(kChooseFolder, language_) : word(kChooseMusic, language_), left, y, 1.2f, white, seed);
  y += row;
  put(shown_name(path_), left, y, kRowScale, dim, seed + 1);
  if (notice_seconds_ > 0) put(word(kNothing, language_), 900.0f, y - row, 1.0f, white, seed + 98);  // beside the heading
  y += row * 1.4f;
  const auto texts = rows();
  const auto l = lines();
  const int count = int(texts.size());
  const int first = std::clamp(first_, 0, std::max(0, count - kRows));
  for (int r = 0; r < kRows && first + r < count; ++r) {
    const int i = first + r;
    const bool at = i == cursor_;
    put(texts[std::size_t(i)], left + 300.0f, y + row * float(r), kRowScale, at ? white : dim, seed + 2 + std::uint32_t(i));
    if (at) {  // our cursor, as the settings panel's: a chevron before the row
      SceneLine a, b;
      const float cy = y + row * float(r);
      a.a = {(left + 110.0f) * size, (cy - 50.0f) * size, depth}, a.b = {(left + 180.0f) * size, cy * size, depth};
      b.a = a.b, b.b = {(left + 110.0f) * size, (cy + 50.0f) * size, depth};
      a.colour = b.colour = white;
      layer.lines.push_back(a);
      layer.lines.push_back(b);
    }
  }
  bool any_music = false;
  for (const Entry& e : entries_) any_music = any_music || !e.folder;
  if (mode_ == Mode::Files && !any_music) put(word(kNothing, language_), left + 300.0f, y + row * float(std::min(count, kRows)), 1.0f, dim, seed + 99);
  if (count > kRows) {  // more above or below: a small mark at the side
    if (first > 0) put("-", left, y, 1.0f, dim, seed + 100);
    if (first + kRows < count) put("-", left, y + row * float(kRows - 1), 1.0f, dim, seed + 101);
  }
  return layer;
}

}  // namespace vib
