#pragma once
// The game's on-screen words and its line font's character table, read from the
// player's own MAIN_G.EXE when the game starts. None of the game's text is kept in
// this project: only where it sits in the PAL program (SCES-02873), and each place
// is checked before it is used.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "vib/bytes.h"

namespace vib {

class GameText {
 public:
  // The original's languages, in its order.
  enum Language { kGerman, kEnglish, kSpanish, kFrench, kItalian, kJapanese, kLanguages };
  // The lists the results screens draw from.
  enum class List {
    CourseNames,   // 5 a language: the three courses, then the two speed courses
    Bonus,         // 3: the bonus coming, the bonus earned, the total (%d)
    Comments,      // 4: Vibri's comment when a cleared course wins no medal
    MedalSteps,    // 10: one per step of the medal sequence (%s the course, %d the score)
    FailComments,  // 5: her reactions to losing (the last follows the third)
    EndWords,      // 2: game over, time up
  };

  // Throws FormatError if `exe` is not the PAL program this layout belongs to.
  static GameText from_exe(ByteView exe);

  const std::string& get(List list, int index, int language = kEnglish) const;
  // The font object of character `c`; `second` is its second form (after '~').
  int object_for(unsigned char c, bool second) const;

 private:
  std::array<std::vector<std::string>, 6> lists_;  // [list][language * count + index]
  std::array<std::uint8_t, 148> objects_{};
};

// `format` with its first %d replaced by `number` and its first %s by `text`.
// (The words come from a file, so they are never given to printf.)
std::string fill_in(const std::string& format, int number, const std::string& text = {});

}  // namespace vib
