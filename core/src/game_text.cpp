#include "vib/game_text.h"

#include <cstring>

namespace vib {
namespace {

// Where things sit in the PAL MAIN_G.EXE (SCES-02873), as addresses once loaded.
// Found by inspecting the player's program; each is checked below before use.
constexpr std::uint32_t kLoadAddress = 0x80019000;
constexpr std::uint32_t kCharacterObjects = 0x8006D314;  // 148 bytes, from ' ' on
struct ListPlace {
  std::uint32_t address;
  int count;  // strings per language
};
constexpr ListPlace kLists[7] = {
    {0x8006CCD8, 5},   // course names
    {0x8006CD50, 3},   // bonus
    {0x8006CD98, 4},   // comments
    {0x8006CDF8, 10},  // medal steps
    {0x8006CF30, 5},   // fail comments
    {0x8006CFA8, 2},   // end words
    {0x8006CEE8, 3},   // speed time (some entries are null)
};

// A PS-X EXE: a 2 KiB header, then the program loaded at its t_addr.
struct Program {
  ByteView exe;
  std::uint32_t start = 0, size = 0;

  bool holds(std::uint32_t address, std::uint32_t bytes) const {
    return address >= start && bytes <= size && address - start <= size - bytes;
  }
  ByteView at(std::uint32_t address, std::uint32_t bytes) const {
    require(holds(address, bytes), "MAIN_G.EXE: address outside the program");
    return exe.subspan(0x800 + (address - start), bytes);
  }
  std::string string_at(std::uint32_t address) const {
    std::string s;
    for (std::uint32_t a = address;; ++a) {
      const std::uint8_t c = at(a, 1)[0];
      if (c == 0) break;
      // Text: printable characters, line breaks and the font's own codes above 0x7F.
      require(c == '\n' || c >= 0x20, "MAIN_G.EXE: not the expected text");
      require(s.size() < 256, "MAIN_G.EXE: not the expected text");
      s.push_back(char(c));
    }
    return s;
  }
};

}  // namespace

GameText GameText::from_exe(ByteView exe) {
  require(exe.size() > 0x800 && std::memcmp(exe.data(), "PS-X EXE", 8) == 0, "MAIN_G.EXE: not a PlayStation program");
  Program p{exe, read_u32(exe, 0x18), read_u32(exe, 0x1C)};
  require(p.start == kLoadAddress && p.size <= exe.size() - 0x800, "MAIN_G.EXE: not the PAL program this game reads");

  GameText t;
  const ByteView objects = p.at(kCharacterObjects, std::uint32_t(t.objects_.size()));
  for (std::size_t i = 0; i < t.objects_.size(); ++i) {
    require(objects[i] < 128, "MAIN_G.EXE: not the expected font table");
    t.objects_[i] = objects[i];
  }
  for (std::size_t l = 0; l < 7; ++l) {
    const ListPlace& place = kLists[l];
    const std::uint32_t n = std::uint32_t(place.count * kLanguages);
    const ByteView pointers = p.at(place.address, n * 4);
    for (std::uint32_t i = 0; i < n; ++i) {
      const std::uint32_t address = read_u32(pointers, i * 4);
      t.lists_[l].push_back(address == 0 ? std::string() : p.string_at(address));
    }
  }
  return t;
}

const std::string& GameText::get(List list, int index, int language) const {
  const std::size_t l = std::size_t(list);
  const int count = kLists[l].count;
  require(index >= 0 && index < count && language >= 0 && language < kLanguages, "GameText: no such text");
  return lists_[l][std::size_t(language * count + index)];
}

int GameText::object_for(unsigned char c, bool second) const {
  // As the original: a character's object is found from ' ' on; its second form
  // 32 entries further on than the character itself.
  const int at = second ? c + 32 : c - ' ';
  if (at < 0 || at >= int(objects_.size())) return 0;
  return objects_[std::size_t(at)];
}

std::string fill_in(const std::string& format, int number, const std::string& text) {
  std::string out;
  bool number_done = false, text_done = false;
  for (std::size_t i = 0; i < format.size(); ++i) {
    if (format[i] == '%' && i + 1 < format.size()) {
      const char k = format[i + 1];
      if (k == 'd' && !number_done) {
        out += std::to_string(number);
        number_done = true;
        ++i;
        continue;
      }
      if (k == 's' && !text_done) {
        out += text;
        text_done = true;
        ++i;
        continue;
      }
      if (k == '%') {
        out.push_back('%');
        ++i;
        continue;
      }
    }
    out.push_back(format[i]);
  }
  return out;
}

}  // namespace vib
