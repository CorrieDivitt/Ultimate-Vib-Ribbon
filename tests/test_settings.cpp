// Our settings panel's text: UTF-8 to the line font's codes.
#include "test.h"
#include "vib/settings_panel.h"

using namespace vib;

TEST(settings_words_turn_into_the_fonts_codes) {
  CHECK(to_font_text("Zoom") == "Zoom");
  CHECK(to_font_text("Wackelgröße") == "Wackelgr~c~He");
  CHECK(to_font_text("¿Sí?") == "~nS~T?");
  CHECK(to_font_text("Épaisseur derrière") == "~Lpaisseur derri~Nre");
  CHECK(to_font_text("Ángulo, línea, canción, tamaño") == "~Bngulo, l~Tnea, canci~bn, tama~Vo");
  CHECK(to_font_text("Sì, più") == "S~S, pi~g");
  CHECK(to_font_text("cœur") == "c?ur");  // not in the font
  CHECK(to_font_text("C:\\Music/My_Album [2] +1") == "C:\\Music/My_Album [2] +1");  // drawn by us
  CHECK(to_font_text("a#b~c%") == "a?b?c?");  // neither (and a plain ~ would mean an accent)
}
