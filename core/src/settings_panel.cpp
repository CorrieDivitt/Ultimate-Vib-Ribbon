#include "vib/settings_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace vib {

namespace {

constexpr double kTau = 6.28318530717958647692;
constexpr int kItemCount = 14;
constexpr int kLanguages = 5;  // German, English, Spanish, French, Italian (the PAL menus)

// Our own words. Order as SettingsPanel::Item.
const char* const kLabels[kLanguages][kItemCount] = {
    // German
    {"Weiter", "Wackelgröße", "Wackeltakt", "Weiche Bewegung", "Linienstärke", "Zoom", "Blickwinkel", "Sicht nach vorn",
     "Sicht nach hinten", "Ansicht zentrieren", "Soundeffekte", "Vibration", "Tonversatz", "Lied beenden"},
    // English
    {"Continue", "Wobble size", "Wobble hold", "Smooth motion", "Line width", "Zoom", "View angle", "See ahead", "See behind",
     "Recentre view", "Sound effects", "Vibration", "Sound timing", "Quit song"},
    // Spanish
    {"Continuar", "Tamaño del temblor", "Ritmo del temblor", "Movimiento suave", "Grosor de línea", "Zoom", "Ángulo de vista",
     "Ver adelante", "Ver atrás", "Centrar vista", "Efectos de sonido", "Vibración", "Desfase de sonido", "Salir de la canción"},
    // French
    {"Continuer", "Taille du tremblement", "Rythme du tremblement", "Mouvement fluide", "Épaisseur des lignes", "Zoom",
     "Angle de vue", "Voir devant", "Voir derrière", "Recentrer la vue", "Effets sonores", "Vibration", "Décalage du son",
     "Quitter la chanson"},
    // Italian
    {"Continua", "Ampiezza del tremolio", "Ritmo del tremolio", "Movimento fluido", "Spessore delle linee", "Zoom",
     "Angolo di vista", "Vista avanti", "Vista indietro", "Ricentra la vista", "Effetti sonori", "Vibrazione", "Ritardo audio",
     "Esci dalla canzone"},
};
const char* const kMore[kLanguages] = {"Weitere Einstellungen", "More settings", "Más ajustes", "Autres réglages", "Altre impostazioni"};
const char* const kOn[kLanguages] = {"An", "On", "Sí", "Oui", "Sì"};
const char* const kOff[kLanguages] = {"Aus", "Off", "No", "Non", "No"};

int table_language(int language) { return language >= 0 && language < kLanguages ? language : 1; }  // Japanese: English

// The font's accented letters and the code letter that follows "~" for each (found
// by drawing the font's second forms; the game's own text uses the same codes).
struct Accent {
  char32_t c;
  char code;
};
constexpr Accent kAccents[] = {
    {U'À', 'A'}, {U'Á', 'B'}, {U'Ä', 'C'}, {U'à', 'D'}, {U'â', 'E'}, {U'á', 'F'}, {U'ä', 'G'}, {U'ß', 'H'}, {U'Ç', 'I'},
    {U'ç', 'J'}, {U'È', 'K'}, {U'É', 'L'}, {U'é', 'M'}, {U'è', 'N'}, {U'ê', 'O'}, {U'Í', 'P'}, {U'Ì', 'Q'}, {U'î', 'R'},
    {U'ì', 'S'}, {U'í', 'T'}, {U'Ñ', 'U'}, {U'ñ', 'V'}, {U'Ò', 'W'}, {U'Ó', 'X'}, {U'Ö', 'Y'}, {U'Ô', 'Z'}, {U'ò', 'a'},
    {U'ó', 'b'}, {U'ö', 'c'}, {U'Ù', 'd'}, {U'Ú', 'e'}, {U'Ü', 'f'}, {U'ù', 'g'}, {U'û', 'h'}, {U'ú', 'i'}, {U'ü', 'j'},
    {U'ª', 'k'}, {U'¡', 'm'}, {U'¿', 'n'},
};

}  // namespace

std::string to_font_text(const std::string& utf8) {
  std::string out;
  for (std::size_t i = 0; i < utf8.size();) {
    const auto b = static_cast<unsigned char>(utf8[i]);
    char32_t c = b;
    int extra = 0;
    if (b >= 0xF0) c = b & 0x07, extra = 3;
    else if (b >= 0xE0) c = b & 0x0F, extra = 2;
    else if (b >= 0xC0) c = b & 0x1F, extra = 1;
    ++i;
    for (int k = 0; k < extra && i < utf8.size(); ++k, ++i) c = (c << 6) | (static_cast<unsigned char>(utf8[i]) & 0x3F);
    if (c < 0x80) {
      out += char(c);
      continue;
    }
    const auto* a = std::find_if(std::begin(kAccents), std::end(kAccents), [c](const Accent& x) { return x.c == c; });
    if (a != std::end(kAccents)) {
      out += '~';
      out += a->code;
    } else {
      out += '?';
    }
  }
  return out;
}

SettingsPanel::SettingsPanel(const Pak& game_pak, int language, const GameText* text) : text_(text), language_(language) {
  font_ = parse_tmd(game_pak.view(localized(game_pak, "FONT/FONT.TMD", language)));
}

void SettingsPanel::set_items(std::vector<Item> items) {
  items_ = std::move(items);
  home();
}

std::string SettingsPanel::label(Item item) const {
  return to_font_text(kLabels[table_language(language_)][int(item)]);
}

std::string SettingsPanel::value(Item item, const Options& o, bool vr) const {
  const int l = table_language(language_);
  auto on_off = [&](bool on) { return to_font_text(on ? kOn[l] : kOff[l]); };
  char buf[32] = {};
  switch (item) {
    case Item::WobbleSize: std::snprintf(buf, sizeof buf, "%.1f", vr ? o.vr_jitter_scale : o.jitter_scale); break;
    case Item::WobbleHold: std::snprintf(buf, sizeof buf, "%d", o.vr_jitter_hold); break;
    case Item::Smooth: return on_off(o.smooth_animation);
    case Item::LineWidth: std::snprintf(buf, sizeof buf, "%.3f", vr ? o.vr_line_width : o.line_width); break;
    case Item::Zoom: std::snprintf(buf, sizeof buf, "%.2f", o.vr_zoom); break;
    case Item::ViewAngle: std::snprintf(buf, sizeof buf, "%.0f", o.vr_angle); break;
    case Item::SeeAhead: std::snprintf(buf, sizeof buf, "%.0f s", o.vr_ahead_seconds); break;
    case Item::SeeBehind: std::snprintf(buf, sizeof buf, "%.0f s", o.vr_behind_seconds); break;
    case Item::SoundEffects: return on_off(o.sound_effects);
    case Item::Vibration: return on_off(o.vibration);
    case Item::SoundTiming: std::snprintf(buf, sizeof buf, "%.0f ms", o.audio_offset_ms); break;
    default: return {};
  }
  return buf;
}

bool SettingsPanel::change(Item item, int d, Options& o, bool vr) {
  auto step = [](double& v, double by, double lo, double hi) {
    const double before = v;
    v = std::clamp(std::round((v + by) * 1000.0) / 1000.0, lo, hi);
    return v != before;
  };
  switch (item) {
    case Item::WobbleSize: return step(vr ? o.vr_jitter_scale : o.jitter_scale, 0.1 * d, 0.0, 2.0);
    case Item::WobbleHold: {
      const int before = o.vr_jitter_hold;
      o.vr_jitter_hold = std::clamp(o.vr_jitter_hold + d, 1, 3);
      return o.vr_jitter_hold != before;
    }
    case Item::Smooth: o.smooth_animation = !o.smooth_animation; return true;
    case Item::LineWidth: {
      if (!vr) return step(o.line_width, 0.125 * d, 0.125, 4.0);
      const double before = o.vr_line_width;
      o.vr_line_width = std::clamp(d > 0 ? o.vr_line_width * 1.25 : o.vr_line_width / 1.25, 0.03, 4.0);
      return o.vr_line_width != before;
    }
    case Item::Zoom: {
      const double before = o.vr_zoom;
      o.vr_zoom = std::clamp(o.vr_zoom * std::exp2(0.25 * d), 1.0 / 24.0, 4.0);
      return o.vr_zoom != before;
    }
    case Item::ViewAngle: o.vr_angle = std::remainder(std::round(o.vr_angle / 15.0) * 15.0 + 15.0 * d, 360.0); return true;
    case Item::SeeAhead: return step(o.vr_ahead_seconds, 2.0 * d, 8.0, 60.0);
    case Item::SeeBehind: return step(o.vr_behind_seconds, 2.0 * d, 0.0, 60.0);
    case Item::SoundEffects: o.sound_effects = !o.sound_effects; return true;
    case Item::Vibration: o.vibration = !o.vibration; return true;
    case Item::SoundTiming: return step(o.audio_offset_ms, 5.0 * d, -500.0, 500.0);
    default: return false;
  }
}

SettingsPanel::Action SettingsPanel::tick(const Input& in, double seconds, Options& o, bool vr) {
  if (items_.empty()) return in.cancel ? Action::Close : Action::None;
  const int count = int(items_.size());
  if (in.cancel) return Action::Close;
  if (in.up || in.down) {
    repeat_dir_ = 0;
    if (in.up && cursor_ == 0 && !wraps_) return Action::LeaveTop;
    cursor_ = (cursor_ + (in.down ? 1 : count - 1)) % count;
    first_ = std::clamp(first_, cursor_ - rows_ + 1, cursor_);
    return Action::Moved;
  }
  const Item item = items_[std::size_t(cursor_)];
  if (in.confirm) {
    switch (item) {
      case Item::Continue: return Action::Close;
      case Item::Recentre: return Action::Recentre;
      case Item::QuitSong: return Action::QuitSong;
      case Item::Smooth: case Item::SoundEffects: case Item::Vibration:
        change(item, 1, o, vr);
        return Action::Changed;
      default: return Action::None;
    }
  }
  int d = in.right ? 1 : in.left ? -1 : 0;
  if (d) {
    repeat_dir_ = d;
    repeat_timer_ = 0.4;  // held this long, it repeats
  } else if (repeat_dir_ != 0 && ((repeat_dir_ > 0 && in.held_right) || (repeat_dir_ < 0 && in.held_left))) {
    repeat_timer_ -= seconds;
    if (repeat_timer_ <= 0) {
      repeat_timer_ += 0.08;
      d = repeat_dir_;
    }
  } else {
    repeat_dir_ = 0;
  }
  if (d && change(item, d, o, vr)) return Action::Changed;
  return Action::None;
}

float SettingsPanel::text_width(const std::string& s, float scale) const {
  int columns = 0;
  for (const char c : s)
    if (c != '~') ++columns;
  return float(columns) * 72.0f * scale;
}

void SettingsPanel::write(const std::string& s, float x, float y, float scale, const Rgb* colour, int range, std::vector<SceneLine>& out) {
  if (!text_) return;
  LineBuilder::Xform xf;
  const float k = 0.8f * scale * size_;
  xf.m.m = {{{k, 0, 0}, {0, k, 0}, {0, 0, k}}};
  bool second = false;
  int column = 0;
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    const bool this_second = second;
    second = false;
    if (c == '~') {
      second = true;
      continue;
    }
    if (c != ' ') {
      const int object = text_->object_for(c, this_second);
      if (object >= 0 && object < int(font_.objects.size())) {
        xf.t = {(x + 72.0f * scale * float(column)) * size_, y * size_, 4000.0f * size_};
        lines_.object(font_.objects[std::size_t(object)], xf, range, 0, nullptr, out, colour);
      }
    }
    ++column;
  }
}

Hud::Layer SettingsPanel::start_layer(std::uint32_t seed) {
  Hud::Layer layer;
  // As the original's text: a camera at the origin looking along +z, 60 degrees high.
  layer.view.eye = {0, 0, 0};
  layer.view.target = {0, 0, 4730};
  layer.view.fov_y = float(682 * kTau / 4096.0);
  lines_.set_view(layer.view);
  lines_.seed(seed);
  return layer;
}

Hud::Layer SettingsPanel::draw_hint(std::uint32_t seed) {
  Hud::Layer layer = start_layer(seed);
  const float half_height = 4000.0f * std::tan(0.5f * layer.view.fov_y);
  const float y = -top_ * half_height + 100.0f;
  const Rgb dim{110, 110, 110};
  write(to_font_text(kMore[table_language(language_)]), label_x_, y, 1.0f, &dim, 4, layer.lines);
  const float x = label_x_ - 130.0f;  // a mark pointing down, before it
  SceneLine a, b;
  a.a = {(x - 50.0f) * size_, (y - 30.0f) * size_, 4000.0f * size_}, a.b = {x * size_, (y + 30.0f) * size_, 4000.0f * size_};
  b.a = a.b, b.b = {(x + 50.0f) * size_, (y - 30.0f) * size_, 4000.0f * size_};
  a.colour = b.colour = dim;
  layer.lines.push_back(a);
  layer.lines.push_back(b);
  return layer;
}

Hud::Layer SettingsPanel::draw(const Options& o, bool vr, std::uint32_t seed) {
  Hud::Layer layer = start_layer(seed);
  const float half_height = 4000.0f * std::tan(0.5f * layer.view.fov_y);
  const float row_height = 200.0f;
  const float top = -top_ * half_height + row_height * 0.5f;  // y down
  const Rgb dim{110, 110, 110};
  auto at_size = [this](float x, float y) { return Vec3f{x * size_, y * size_, 4000.0f * size_}; };
  const int shown = std::min(rows_, int(items_.size()));
  first_ = std::clamp(first_, 0, std::max(0, int(items_.size()) - shown));
  for (int r = 0; r < shown; ++r) {
    const int i = first_ + r;
    const Item item = items_[std::size_t(i)];
    const bool at = active_ && i == cursor_;
    const float y = top + row_height * float(r);
    const Rgb* colour = at ? nullptr : &dim;
    write(label(item), label_x_, y, 1.0f, colour, 4, layer.lines);
    const std::string v = value(item, o, vr);
    if (!v.empty()) write(v, value_right_ - text_width(v, 1.0f), y, 1.0f, colour, 4, layer.lines);
    if (at) {  // our cursor: a chevron before the label
      SceneLine a, b;
      a.a = at_size(label_x_ - 190.0f, y - 50.0f), a.b = at_size(label_x_ - 120.0f, y);
      b.a = a.b, b.b = at_size(label_x_ - 190.0f, y + 50.0f);
      a.colour = b.colour = {255, 255, 255};
      layer.lines.push_back(a);
      layer.lines.push_back(b);
    }
  }
  // More above or below: a small mark at that end.
  auto mark = [&](float y, float dir) {
    SceneLine a, b;
    const float mid = (label_x_ + value_right_) * 0.5f;
    a.a = at_size(mid - 60.0f, y), a.b = at_size(mid, y + 40.0f * dir);
    b.a = a.b, b.b = at_size(mid + 60.0f, y);
    a.colour = b.colour = dim;
    layer.lines.push_back(a);
    layer.lines.push_back(b);
  };
  if (first_ > 0) mark(top - row_height * 0.6f, -1.0f);
  if (first_ + shown < int(items_.size())) mark(top + row_height * (float(shown) - 0.4f), 1.0f);
  return layer;
}

}  // namespace vib
