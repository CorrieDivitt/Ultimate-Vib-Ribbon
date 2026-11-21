#include "vib/settings_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace vib {

namespace {

constexpr double kTau = 6.28318530717958647692;
constexpr int kItemCount = 28;
constexpr int kLanguages = 5;  // German, English, Spanish, French, Italian (the PAL menus)

// Our own words. Order as SettingsPanel::Item.
const char* const kLabels[kLanguages][kItemCount] = {
    // German
    {"Weiter", "Wackelgröße", "Wackeltakt", "Weiche Bewegung", "Linienstärke", "Zoom", "Blickwinkel", "Sicht nach vorn",
     "Sicht nach hinten", "Ansicht zentrieren", "Soundeffekte", "Vibration", "Tonversatz", "Lied beenden", "Fortsetzen", "Leistung", "Punkte als Zahl", "Gleiche Strecke", "Effektlautstärke", "Scrolltempo", "Timing-Anzeige", "FC-Jubel", "Strecke um dich", "Kreisgröße", "Grafik", "Modifikatoren", "Eigene Farben", "Zurück"},
    // English
    {"Continue", "Wobble size", "Wobble hold", "Smooth motion", "Line width", "Zoom", "View angle", "See ahead", "See behind",
     "Recenter view", "Sound effects", "Vibration", "Sound timing", "Quit song", "Resume", "Performance", "Score in numbers", "Consistent courses", "Effects volume", "Scroll speed", "Timing feedback", "FC celebration", "Course around you", "Circle size", "Visual", "Modifiers", "Custom colors", "Back"},
    // Spanish
    {"Continuar", "Tamaño del temblor", "Ritmo del temblor", "Movimiento suave", "Grosor de línea", "Zoom", "Ángulo de vista",
     "Ver adelante", "Ver atrás", "Centrar vista", "Efectos de sonido", "Vibración", "Desfase de sonido", "Salir de la canción", "Reanudar", "Rendimiento", "Puntos en números", "Recorridos fijos", "Volumen de efectos", "Velocidad de avance", "Precisión en ms", "Celebrar FC", "Recorrido alrededor", "Tamaño del círculo", "Imagen", "Modificadores", "Colores propios", "Volver"},
    // French
    {"Continuer", "Taille du tremblement", "Rythme du tremblement", "Mouvement fluide", "Épaisseur des lignes", "Zoom",
     "Angle de vue", "Voir devant", "Voir derrière", "Recentrer la vue", "Effets sonores", "Vibration", "Décalage du son",
     "Quitter la chanson", "Reprise", "Performances", "Score en chiffres", "Parcours fixes", "Volume des effets", "Vitesse de défilement", "Précision en ms", "Fête du FC", "Parcours autour de toi", "Taille du cercle", "Visuel", "Modificateurs", "Couleurs perso", "Retour"},
    // Italian
    {"Continua", "Ampiezza del tremolio", "Ritmo del tremolio", "Movimento fluido", "Spessore delle linee", "Zoom",
     "Angolo di vista", "Vista avanti", "Vista indietro", "Ricentra la vista", "Effetti sonori", "Vibrazione", "Ritardo audio",
     "Esci dalla canzone", "Ripresa", "Prestazioni", "Punteggio in cifre", "Percorsi fissi", "Volume effetti", "Velocità di scorrimento", "Precisione in ms", "Festa FC", "Percorso intorno a te", "Dimensione del cerchio", "Grafica", "Modificatori", "Colori personalizzati", "Indietro"},
};
const char* const kMore[kLanguages] = {"Weitere Einstellungen (Start)", "More options (Start)", "Más ajustes (Start)", "Autres réglages (Start)",
                                       "Altre impostazioni (Start)"};
const char* const kRewind[kLanguages] = {"Zurückspulen", "Rewind", "Rebobinar", "Rembobiner", "Riavvolgi"};
const char* const kCountIn[kLanguages] = {"Countdown", "Countdown", "Cuenta atrás", "Compte à rebours", "Conto alla rovescia"};
const char* const kOn[kLanguages] = {"An", "On", "Sí", "Oui", "Sì"};
const char* const kOff[kLanguages] = {"Aus", "Off", "No", "Non", "No"};
const char* const kWholeSong[kLanguages] = {"Ganzes Lied", "Whole song", "Toda la canción", "Toute la chanson", "Tutta la canzone"};
const char* const kDetailed[kLanguages] = {"Ausführlich", "Detailed", "Detallado", "Détaillé", "Dettagliato"};

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

// Characters the disc's font has as they are: letters, digits, space and a little
// punctuation. Those we draw ourselves (own_glyph) pass too; anything else becomes '?'.
bool in_font(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '!' || c == '&' ||
         c == '\'' || c == '(' || c == ')' || c == ',' || c == '-' || c == '.' || c == '?';
}
bool drawn_by_us(char c) { return c == '/' || c == '\\' || c == ':' || c == '_' || c == '+' || c == '[' || c == ']' || c == '>'; }

// Ours: strokes for a few characters the font lacks (paths need / \ and :), the size
// of the font's own (a digit is 142 high and about 60 wide at scale 1), centred on
// (x, y), y down. Still (no jitter of their own).
void own_glyph(char c, float x, float y, float z, float scale, const Rgb& colour, std::vector<SceneLine>& out) {
  const float h = 66.0f * scale, w = 28.0f * scale;
  auto line = [&](float ax, float ay, float bx, float by) {
    SceneLine l;
    l.a = {x + ax, y + ay, z}, l.b = {x + bx, y + by, z}, l.colour = colour;
    out.push_back(l);
  };
  const float d = 6.0f * scale;  // half a dot
  switch (c) {
    case '/': line(-w, h, w, -h); break;
    case '\\': line(-w, -h, w, h); break;
    case ':':
      line(-d, -0.45f * h - d, d, -0.45f * h + d), line(-d, -0.45f * h + d, d, -0.45f * h - d);
      line(-d, 0.45f * h - d, d, 0.45f * h + d), line(-d, 0.45f * h + d, d, 0.45f * h - d);
      break;
    case '_': line(-w, h, w, h); break;
    case '+': line(-w, 0, w, 0), line(0, -w, 0, w); break;
    case '>': line(-w * 0.6f, -w, w * 0.6f, 0), line(w * 0.6f, 0, -w * 0.6f, w); break;
    case '[': line(w * 0.4f, -h, -w * 0.4f, -h), line(-w * 0.4f, -h, -w * 0.4f, h), line(-w * 0.4f, h, w * 0.4f, h); break;
    case ']': line(-w * 0.4f, -h, w * 0.4f, -h), line(w * 0.4f, -h, w * 0.4f, h), line(w * 0.4f, h, -w * 0.4f, h); break;
    default: break;
  }
}

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
      const char a = char(c);
      out += in_font(a) || drawn_by_us(a) ? a : '?';  // ('~' too: it means an accent to the font)
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
  stack_.clear();
  home();
}

void SettingsPanel::set_group(Item group, std::vector<Item> items) {
  items.push_back(Item::Back);
  for (auto& g : groups_)
    if (g.first == group) {
      g.second = std::move(items);
      return;
    }
  groups_.emplace_back(group, std::move(items));
}

bool SettingsPanel::is_group(Item item) const {
  return std::any_of(groups_.begin(), groups_.end(), [item](const auto& g) { return g.first == item; });
}

void SettingsPanel::open(Item group) {
  for (const auto& g : groups_)
    if (g.first == group) {
      stack_.push_back({items_, cursor_, first_, group});
      items_ = g.second;
      cursor_ = 0, first_ = 0;
      return;
    }
}

void SettingsPanel::close_group() {
  if (stack_.empty()) return;
  items_ = std::move(stack_.back().items);
  cursor_ = stack_.back().cursor, first_ = stack_.back().first;
  stack_.pop_back();
}

std::string SettingsPanel::label(Item item, bool vr) const {
  std::string text = to_font_text(kLabels[table_language(language_)][int(item)]);
  switch (item) {
    case Item::WobbleSize: case Item::WobbleHold: case Item::Smooth: case Item::LineWidth: case Item::Zoom:
    case Item::ViewAngle: case Item::SeeAhead: case Item::SeeBehind: case Item::SoundTiming: case Item::FixedCourses:
    case Item::EffectsVolume: case Item::ScrollSpeed: case Item::WrapCourse: case Item::WrapSize:
      text += " (" + value(item, original_options(), vr) + ")";  // the original game's value
      break;
    default: break;
  }
  return text;
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
    case Item::SeeAhead: case Item::SeeBehind: {
      const double v = item == Item::SeeAhead ? o.vr_ahead_seconds : o.vr_behind_seconds;
      if (v >= kWholeSongSeconds) return to_font_text(kWholeSong[l]);
      std::snprintf(buf, sizeof buf, "%.0f s", v);
      break;
    }
    case Item::SoundEffects: return on_off(o.sound_effects);
    case Item::Vibration: return on_off(o.vibration);
    case Item::SoundTiming: std::snprintf(buf, sizeof buf, "%.0f ms", o.audio_offset_ms); break;
    case Item::ResumeStyle: return to_font_text(o.resume_countdown ? kCountIn[l] : kRewind[l]);
    case Item::Performance: return to_font_text(o.perf_overlay == 0 ? kOff[l] : o.perf_overlay == 1 ? kOn[l] : kDetailed[l]);
    case Item::ScoreNumbers: return on_off(o.score_numbers);
    case Item::FixedCourses: return on_off(o.fixed_courses);
    case Item::EffectsVolume: std::snprintf(buf, sizeof buf, "%d", int(std::lround(o.effects_volume * 10.0f))); break;
    case Item::ScrollSpeed: return scroll_text(o.scroll_speed);
    case Item::TimingFeedback: return on_off(o.timing_feedback);
    case Item::FcCelebration: return on_off(o.fc_celebration);
    case Item::WrapCourse: return on_off(o.vr_wrap);
    case Item::WrapSize: std::snprintf(buf, sizeof buf, "%.1f m", o.vr_wrap_radius); break;
    case Item::Visual: case Item::Modifiers: case Item::CustomColours: return ">";
    default: return {};
  }
  return buf;
}

namespace {

// How far the headset's line reaches: every 2 s up to a minute, then longer steps,
// then the whole song. Moves `v` to the next stop in direction `d`.
bool step_seconds(double& v, int d, double lowest) {
  std::vector<double> stops;
  for (double s = lowest; s <= 60.0; s += 2.0) stops.push_back(s);
  for (const double s : {75.0, 90.0, 120.0, 150.0, 180.0, 240.0, 300.0, kWholeSongSeconds}) stops.push_back(s);
  const double before = v;
  if (d > 0) {
    const auto it = std::upper_bound(stops.begin(), stops.end(), v + 1e-6);
    if (it != stops.end()) v = *it;
  } else {
    const auto it = std::lower_bound(stops.begin(), stops.end(), v - 1e-6);
    if (it != stops.begin()) v = *(it - 1);
  }
  return v != before;
}

}  // namespace

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
      o.vr_zoom = std::clamp(o.vr_zoom * std::exp2(0.25 * d), 1.0 / 36.0, 4.0);
      return o.vr_zoom != before;
    }
    case Item::ViewAngle: o.vr_angle = std::remainder(std::round(o.vr_angle / 15.0) * 15.0 + 15.0 * d, 360.0); return true;
    case Item::SeeAhead: return step_seconds(o.vr_ahead_seconds, d, 8.0);
    case Item::SeeBehind: return step_seconds(o.vr_behind_seconds, d, 2.0);  // (at least 2 s: about what the original keeps)
    case Item::SoundEffects: o.sound_effects = !o.sound_effects; return true;
    case Item::Vibration: o.vibration = !o.vibration; return true;
    case Item::SoundTiming: return step(o.audio_offset_ms, 5.0 * d, -500.0, 500.0);
    case Item::ResumeStyle: o.resume_countdown = !o.resume_countdown; return true;
    case Item::Performance: o.perf_overlay = (o.perf_overlay + (d > 0 ? 1 : 2)) % 3; return true;  // round: off, on, detailed
    case Item::ScoreNumbers: o.score_numbers = !o.score_numbers; return true;
    case Item::FixedCourses: o.fixed_courses = !o.fixed_courses; return true;
    case Item::EffectsVolume: {  // tenths, 0 (silent) to 10 (as loud as the original)
      const float before = o.effects_volume;
      o.effects_volume = std::clamp(std::round(o.effects_volume * 10.0f + float(d)) / 10.0f, 0.0f, 1.0f);
      return o.effects_volume != before;
    }
    case Item::TimingFeedback: o.timing_feedback = !o.timing_feedback; return true;
    case Item::FcCelebration: o.fc_celebration = !o.fc_celebration; return true;
    case Item::WrapCourse: o.vr_wrap = !o.vr_wrap; return true;
    case Item::WrapSize: return step(o.vr_wrap_radius, 0.5 * d, 1.0, 10.0);
    case Item::ScrollSpeed: {
      const double before = o.scroll_speed;
      o.scroll_speed = clamp_scroll_speed(o.scroll_speed + kScrollSpeedStep * d);
      return o.scroll_speed != before;
    }
    default: return false;
  }
}

std::string SettingsPanel::offset_text(double ms) {
  const long v = std::lround(ms);
  char buf[24];
  std::snprintf(buf, sizeof buf, v > 0 ? "+%ld ms" : "%ld ms", v);
  return buf;
}

std::string SettingsPanel::scroll_text(double speed) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%.2f", speed);
  std::string t = buf;
  while (t.back() == '0') t.pop_back();
  if (t.back() == '.') t.pop_back();
  return t + "x";
}

SettingsPanel::Action SettingsPanel::tick(const Input& in, double seconds, Options& o, bool vr) {
  if (items_.empty()) return in.cancel ? Action::Close : Action::None;
  if (in.cancel) {
    if (stack_.empty()) return Action::Close;
    close_group();  // B in a group: back to the list above
    return Action::Moved;
  }
  const int count = int(items_.size());
  if (in.up || in.down) {
    repeat_dir_ = 0;
    scroll_dir_ = in.down ? 1 : -1;
    scroll_timer_ = 0.35;
    if (in.up && cursor_ == 0 && !wraps_) return Action::LeaveTop;
    cursor_ = (cursor_ + (in.down ? 1 : count - 1)) % count;
    first_ = std::clamp(first_, cursor_ - rows_ + 1, cursor_);
    return Action::Moved;
  }
  // Held (owner): after 0.35 s, about 14 rows a second, stopping at the ends.
  if (scroll_dir_ != 0 && ((scroll_dir_ < 0 && in.held_up) || (scroll_dir_ > 0 && in.held_down))) {
    scroll_timer_ -= seconds;
    if (scroll_timer_ <= 0) {
      scroll_timer_ += 0.07;
      const int to = std::clamp(cursor_ + scroll_dir_, 0, count - 1);
      if (to != cursor_) {
        cursor_ = to;
        first_ = std::clamp(first_, cursor_ - rows_ + 1, cursor_);
        return Action::Moved;
      }
    }
  } else {
    scroll_dir_ = 0;
  }
  const Item item = items_[std::size_t(cursor_)];
  if (in.confirm && item == Item::Back) {
    close_group();
    return Action::Moved;
  }
  if (in.confirm && is_group(item)) {
    open(item);
    return Action::Moved;
  }
  if (in.confirm) {
    switch (item) {
      case Item::Continue: return Action::Close;
      case Item::Recentre: return Action::Recentre;
      case Item::QuitSong: return Action::QuitSong;
      case Item::Smooth: case Item::SoundEffects: case Item::Vibration: case Item::ResumeStyle: case Item::Performance: case Item::ScoreNumbers: case Item::FixedCourses: case Item::TimingFeedback: case Item::FcCelebration: case Item::WrapCourse:
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
    if (c != ' ' && !this_second && drawn_by_us(char(c))) {
      own_glyph(char(c), (x + 72.0f * scale * float(column)) * size_, y * size_, 4000.0f * size_, scale * size_,
                colour ? *colour : Rgb{255, 255, 255}, out);
    } else if (c != ' ') {
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
  return layer;
}

std::string SettingsPanel::scroll_line(const Options& o) const {
  return to_font_text(kLabels[table_language(language_)][int(Item::ScrollSpeed)]) + ": " + scroll_text(o.scroll_speed);
}

Hud::Layer SettingsPanel::draw_scroll_speed(const Options& o, std::uint32_t seed) {
  Hud::Layer layer = start_layer(seed);
  const float half_height = 4000.0f * std::tan(0.5f * layer.view.fov_y);
  const Rgb dim{150, 150, 150};
  const std::string text = scroll_line(o);
  write(text, -0.5f * text_width(text) + 36.0f, 0.86f * half_height, 1.0f, &dim, 4, layer.lines);
  return layer;
}

Hud::Layer SettingsPanel::draw_big(const std::string& text, std::uint32_t seed) {
  Hud::Layer layer = start_layer(seed);
  constexpr float kScale = 4.0f;
  write(text, -0.5f * text_width(text, kScale) + 36.0f * kScale, 0.0f, kScale, nullptr, 6, layer.lines);
  return layer;
}

std::vector<SceneLine> SettingsPanel::text_lines(const std::string& text, float scale, std::uint32_t seed, int range) {
  std::vector<SceneLine> out;
  if (!text_) return out;
  SceneView v;  // seen from -z, so the font's facing marks face the viewer
  v.eye = {0, 0, -4000};
  v.target = {0, 0, 0};
  v.fov_y = 1.0f;
  lines_.set_view(v);
  lines_.seed(seed);
  LineBuilder::Xform xf;
  const float k = 0.8f * scale;
  xf.m.m = {{{k, 0, 0}, {0, k, 0}, {0, 0, k}}};
  const float left = -0.5f * text_width(text, scale) + 36.0f * scale;
  bool second = false;
  int column = 0;
  for (const char ch : text) {
    const auto c = static_cast<unsigned char>(ch);
    const bool this_second = second;
    second = false;
    if (c == '~') {
      second = true;
      continue;
    }
    if (c != ' ' && !this_second && drawn_by_us(char(c))) {
      own_glyph(char(c), left + 72.0f * scale * float(column), 0.0f, 0.0f, scale, Rgb{255, 255, 255}, out);
    } else if (c != ' ') {
      const int object = text_->object_for(c, this_second);
      if (object >= 0 && object < int(font_.objects.size())) {
        xf.t = {left + 72.0f * scale * float(column), 0.0f, 0.0f};
        lines_.object(font_.objects[std::size_t(object)], xf, range, 0, nullptr, out);
      }
    }
    ++column;
  }
  return out;
}

Hud::Layer SettingsPanel::draw_score(int score, std::uint32_t seed) {
  Hud::Layer layer;
  layer.view.eye = {0, 0, 0};
  layer.view.target = {0, 0, 4730};
  layer.view.fov_y = float(682 * kTau / 4096.0);
  const float half_height = 4000.0f * std::tan(0.5f * layer.view.fov_y);
  layer.lines = text_lines(std::to_string(score), 1.4f, seed, 4);
  for (SceneLine& l : layer.lines) {  // to just above the time meter's strip (its top is about 84% down)
    l.a.y += 0.72f * half_height, l.b.y += 0.72f * half_height;
    l.a.z += 4000.0f, l.b.z += 4000.0f;
  }
  return layer;
}

namespace {

constexpr double kPi = 3.14159265358979;

double ease_back(double u) {  // 0 to 1, overshooting to about 1.35 on the way
  const double c = 2.4;
  const double v = u - 1.0;
  return 1.0 + (c + 1.0) * v * v * v + c * v * v;
}

std::uint8_t grey(double v) { return static_cast<std::uint8_t>(255.0 * std::clamp(v, 0.0, 1.0)); }

// A burst of short rays flying out from the middle on an ellipse, shrinking and
// fading as they go; `u` 0 to 1 through it.
void rays(double u, int count, double turn, double from, double to, double length, double fade, std::vector<SceneLine>& out) {
  if (u <= 0 || u >= 1) return;
  const double far = from + (to - from) * (1.0 - (1.0 - u) * (1.0 - u));  // fast, then slowing
  const double len = length * (1.0 - u);
  const std::uint8_t shade = grey((1.0 - u) * fade);
  for (int k = 0; k < count; ++k) {
    const double a = 2.0 * kPi * (double(k) + turn) / double(count);
    const double cx = std::cos(a) * 1.25, cy = std::sin(a) * 0.55;  // wider than tall, as the words are
    SceneLine l;
    l.a = {float(cx * far), float(cy * far), 0.0f};
    l.b = {float(cx * (far + len)), float(cy * (far + len)), 0.0f};
    l.colour = {shade, shade, shade};
    out.push_back(l);
  }
}

}  // namespace

void SettingsPanel::pop_word(const std::string& word, float scale, float y, double t, double start, double stagger,
                             float shade, std::uint32_t seed, std::vector<SceneLine>& out) {
  constexpr double kPop = 0.32;  // each letter's swell and settle
  const float width = text_width(word, scale);
  const int n = int(word.size());
  const std::uint8_t g = grey(shade);
  for (int i = 0; i < n; ++i) {
    if (word[std::size_t(i)] == ' ') continue;
    const double u = (t - start - stagger * i) / kPop;
    if (u <= 0) continue;
    const double grow = u >= 1 ? 1.0 : ease_back(u);
    const double hop = u < 1 ? -90.0 * std::sin(kPi * u) : 0.0;
    // Once all are in, a wave runs along the word.
    const double settled = t - (start + stagger * (n - 1) + kPop);
    const double wave = settled > 0 ? -30.0 * std::sin(2.0 * kPi * (1.4 * settled) - 0.55 * i) * std::min(1.0, settled * 3.0) : 0.0;
    std::string only(word.size(), ' ');
    only[std::size_t(i)] = word[std::size_t(i)];
    std::vector<SceneLine> letter = text_lines(to_font_text(only), scale, seed + std::uint32_t(i) * 977u, u < 1 ? 14 : 5);
    const float cx = -0.5f * width + 36.0f * scale + 72.0f * scale * float(i);
    for (SceneLine& l : letter) {
      for (Vec3f* p : {&l.a, &l.b}) {
        p->x = cx + (p->x - cx) * float(grow);
        p->y = y + p->y * float(grow) + float(hop + wave);
      }
      l.jitter_a = {l.jitter_a.x * float(grow), l.jitter_a.y * float(grow), l.jitter_a.z};
      l.jitter_b = {l.jitter_b.x * float(grow), l.jitter_b.y * float(grow), l.jitter_b.z};
      l.colour = {g, g, g};
      out.push_back(l);
    }
  }
}

std::vector<SceneLine> SettingsPanel::full_combo(double t, std::uint32_t seed) {
  // (The words are the same in every language: gamers say "full combo" everywhere.)
  const double fade = std::clamp((kFullComboSeconds - t) / 0.45, 0.0, 1.0);
  std::vector<SceneLine> out;
  pop_word("Full Combo!", 2.2f, 0.0f, t, 0.0, 0.07, float(fade), seed, out);  // about 1,630 across
  // Two bursts of rays from behind the word.
  rays((t - 0.55) / 0.85, 18, 0.0, 300.0, 1700.0, 260.0, fade, out);
  rays((t - 1.15) / 0.85, 24, 0.5, 300.0, 1700.0, 260.0, fade, out);
  return out;
}

std::vector<SceneLine> SettingsPanel::full_course_combo(double t, std::uint32_t seed) {
  const double fade = std::clamp((kFullCourseComboSeconds - t) / 0.6, 0.0, 1.0);
  std::vector<SceneLine> out;
  // "Full Course" pops in above.
  pop_word("Full Course", 2.2f, -300.0f, t, 0.0, 0.07, float(fade), seed, out);
  // Then "Combo!" slams down: from 2.6 times its size to its size in 0.18 s, a shake
  // that dies away, then the same wave as the line above.
  constexpr double kSlam = 1.0, kSlamFor = 0.18;
  if (t > kSlam) {
    const double u = std::min(1.0, (t - kSlam) / kSlamFor);
    const double big = 1.0 + 1.6 * (1.0 - u) * (1.0 - u);
    const double since = t - kSlam - kSlamFor;
    const double shake = since > 0 ? 40.0 * std::exp(-since * 6.0) * std::sin(since * 60.0) : 0.0;
    std::vector<SceneLine> word;
    pop_word("Combo!", 3.6f, 0.0f, kSlam + 1.0, kSlam, 0.0, float(fade), seed + 7u, word);  // (all in, settled)
    const double wave = since > 0 ? -25.0 * std::sin(2.0 * kPi * 1.1 * since) * std::min(1.0, since * 2.0) : 0.0;
    for (SceneLine& l : word) {
      for (Vec3f* p : {&l.a, &l.b}) {
        p->x = float(p->x * big + shake);
        p->y = float(260.0 + p->y * big + wave);
      }
      out.push_back(l);
    }
    // The shockwave: a ring racing out from "Combo!" as it lands, thinning to nothing.
    const double r = (t - kSlam - kSlamFor) / 0.7;
    if (r > 0 && r < 1) {
      const double rad = 250.0 + 2200.0 * (1.0 - (1.0 - r) * (1.0 - r));
      const std::uint8_t g = grey((1.0 - r) * fade);
      constexpr int kSides = 64;
      for (int k = 0; k < kSides; ++k) {
        const double a0 = 2.0 * kPi * k / kSides, a1 = 2.0 * kPi * (k + 1) / kSides;
        SceneLine l;
        l.a = {float(std::cos(a0) * rad * 1.25), float(260.0 + std::sin(a0) * rad * 0.6), 0.0f};
        l.b = {float(std::cos(a1) * rad * 1.25), float(260.0 + std::sin(a1) * rad * 0.6), 0.0f};
        l.colour = {g, g, g};
        out.push_back(l);
      }
    }
  }
  // Three bursts of rays, more and wider than a song's.
  rays((t - 0.6) / 0.9, 24, 0.0, 400.0, 2300.0, 320.0, fade, out);
  rays((t - 1.2) / 1.0, 36, 0.5, 450.0, 2800.0, 420.0, fade, out);
  rays((t - 1.9) / 1.0, 30, 0.25, 400.0, 2500.0, 360.0, fade, out);
  // Sparkles: small four-pointed stars appearing and vanishing all round the words.
  if (t > 1.2) {
    for (int k = 0; k < 28; ++k) {
      std::uint32_t h = (std::uint32_t(k) + 1u) * 2654435761u;
      h ^= h >> 13;
      h *= 0x5bd1e995u;
      h ^= h >> 15;
      const double phase = double(h & 1023u) / 1024.0;              // when in its cycle it starts
      const double life = std::fmod((t - 1.2) * 1.6 + phase, 1.0);  // 0 to 1, again and again
      const double size = 70.0 * std::sin(kPi * life);
      const double a = 2.0 * kPi * double((h >> 10) & 1023u) / 1024.0;
      const double d = 1100.0 + 900.0 * double((h >> 20) & 1023u) / 1024.0;
      const double x = std::cos(a) * d * 1.2, y = 0.0 + std::sin(a) * d * 0.55;
      const std::uint8_t g = grey(std::sin(kPi * life) * fade);
      for (int arm = 0; arm < 2; ++arm) {
        SceneLine l;
        l.a = arm ? Vec3f{float(x - size), float(y), 0.0f} : Vec3f{float(x), float(y - size), 0.0f};
        l.b = arm ? Vec3f{float(x + size), float(y), 0.0f} : Vec3f{float(x), float(y + size), 0.0f};
        l.colour = {g, g, g};
        out.push_back(l);
      }
    }
  }
  return out;
}

Hud::Layer SettingsPanel::draw_full_combo(double t, std::uint32_t seed, bool course) {
  Hud::Layer layer;
  layer.view.eye = {0, 0, 0};
  layer.view.target = {0, 0, 4730};
  layer.view.fov_y = float(682 * kTau / 4096.0);
  const float half_height = 4000.0f * std::tan(0.5f * layer.view.fov_y);
  layer.lines = course ? full_course_combo(t, seed) : full_combo(t, seed);
  for (SceneLine& l : layer.lines) {  // the upper middle of the screen
    l.a.y -= 0.35f * half_height, l.b.y -= 0.35f * half_height;
    l.a.z += 4000.0f, l.b.z += 4000.0f;
  }
  return layer;
}

Hud::Layer SettingsPanel::draw_timing(const std::string& text, float brightness, std::uint32_t seed) {
  Hud::Layer layer;
  layer.view.eye = {0, 0, 0};
  layer.view.target = {0, 0, 4730};
  layer.view.fov_y = float(682 * kTau / 4096.0);
  const float half_height = 4000.0f * std::tan(0.5f * layer.view.fov_y);
  layer.lines = text_lines(text, 1.2f, seed, 4);
  const auto grey = static_cast<std::uint8_t>(std::clamp(brightness, 0.0f, 1.0f) * 255.0f);
  for (SceneLine& l : layer.lines) {  // above the score's number (0.72 down), still below the middle
    l.a.y += 0.5f * half_height, l.b.y += 0.5f * half_height;
    l.a.z += 4000.0f, l.b.z += 4000.0f;
    l.colour = {grey, grey, grey};
  }
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
  if (!stack_.empty()) {  // in a group: its name above the list, dimmed
    const Rgb title{170, 170, 170};
    write(label(stack_.back().group, vr), label_x_, top - row_height * 1.15f, 1.0f, &title, 4, layer.lines);
  }
  for (int r = 0; r < shown; ++r) {
    const int i = first_ + r;
    const Item item = items_[std::size_t(i)];
    const bool at = active_ && i == cursor_;
    const float y = top + row_height * float(r);
    const Rgb* colour = at ? nullptr : &dim;
    write(label(item, vr), label_x_, y, 1.0f, colour, 4, layer.lines);
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
