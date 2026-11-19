#include "test.h"
#include "vib/cue.h"

using namespace vib;

TEST(cue_msf) {
  CHECK(msf_to_frames("00:00:00") == 0);
  CHECK(msf_to_frames("00:02:00") == 150);
  CHECK(msf_to_frames("01:00:74") == 75 * 60 + 74);
  CHECK_THROWS(msf_to_frames("00:60:00"));
  CHECK_THROWS(msf_to_frames("garbage"));
}

TEST(cue_redump_layout) {
  const char* text =
      "FILE \"Game (Track 1).bin\" BINARY\r\n"
      "  TRACK 01 MODE2/2352\r\n"
      "    INDEX 01 00:00:00\r\n"
      "FILE \"Game (Track 2).bin\" BINARY\r\n"
      "  TRACK 02 AUDIO\r\n"
      "    INDEX 00 00:00:00\r\n"
      "    INDEX 01 00:11:08\r\n";
  const CueSheet s = parse_cue(text, "base");
  CHECK(s.tracks.size() == 2);
  CHECK(s.tracks[0].mode == TrackMode::Mode2Raw);
  CHECK(s.tracks[0].file.filename() == "Game (Track 1).bin");
  CHECK(s.tracks[1].mode == TrackMode::Audio);
  CHECK(s.tracks[1].index00.has_value() && *s.tracks[1].index00 == 0);
  CHECK(s.tracks[1].index01 == 11 * 75 + 8);
}

TEST(cue_rejects_cooked_images) {
  CHECK_THROWS(parse_cue("FILE \"x.iso\" BINARY\n TRACK 01 MODE1/2048\n INDEX 01 00:00:00\n", "."));
  CHECK_THROWS(parse_cue("", "."));
}
