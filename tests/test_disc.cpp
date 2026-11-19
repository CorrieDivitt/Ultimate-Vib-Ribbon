// Checks against the owner's real disc image. Skipped unless VIB_TEST_CUE is set,
// so CI and anyone without the game can still run the suite.

#include <cstdlib>
#include <string>

#include "test.h"
#include "vib/disc_image.h"
#include "vib/iso9660.h"
#include "vib/known_discs.h"
#include "vib/pak.h"

using namespace vib;

static std::string test_cue() {
  const char* p = std::getenv("VIB_TEST_CUE");
  return p ? std::string(p) : std::string();
}

TEST(disc_sces02873_layout) {
  const std::string cue = test_cue();
  if (cue.empty()) SKIP("set VIB_TEST_CUE to your SCES-02873 .cue to run");
  DiscImage disc = DiscImage::open(path_from_utf8(cue));
  const KnownDisc* known = identify_by_data_track_sha1(disc.data_track_sha1());
  CHECK(known != nullptr);
  CHECK(disc.tracks().size() == 8);
  CHECK(disc.data_track().length_frames == 6667);
  CHECK(disc.track(2).pregap_frames == 833);

  Iso9660 iso(disc);
  CHECK(iso.system_id() == "PLAYSTATION");
  const IsoEntry* game = iso.find("main_g.exe");
  CHECK(game && game->size == 385024);
  const Bytes exe = iso.read("MAIN_G.EXE");
  CHECK(std::string(exe.begin(), exe.begin() + 8) == "PS-X EXE");

  const Pak pak(iso.read("GAME/02_FILES.PAK"));
  CHECK(pak.entries().size() == 242);
  CHECK(pak.view("SCRIPT/SYSTEM3.FSL").size() == 26920);
  CHECK(read_u32(pak.view("CHARA/PEELOO/MODEL.TMD"), 0) == 0x41);  // TMD id
}
