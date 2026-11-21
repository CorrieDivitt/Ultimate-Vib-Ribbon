// The player's own music as a CD (own_cd.h): layout, key and track order. No game data.
#include "test.h"
#include "vib/course_gen.h"
#include "vib/own_cd.h"

using namespace vib;

TEST(one_file_makes_the_same_disc_as_before) {
  // vibplay --music always laid one file out from sector 150, whole sectors.
  const std::uint32_t frames = 3087000;  // 70 s
  const OwnCd cd = make_own_cd({{"song.mp3", frames}});
  const std::uint32_t sectors = (frames + 587) / 588;
  CHECK(cd.tracks.size() == 1 && cd.tracks[0].start == 150 && cd.tracks[0].sectors == sectors);
  CHECK(cd.lead_out == 150 + sectors && cd.key == disc_key(150 + sectors, 1));
  CHECK(cd.track_samples() == std::vector<std::int32_t>{std::int32_t(sectors / 75) * kSampleRate});
}

TEST(several_files_lie_one_after_another_and_change_the_key) {
  const OwnCd two = make_own_cd({{"a.flac", 588 * 750}, {"b.flac", 588 * 1500 + 1}});
  CHECK(two.tracks[0].start == 150 && two.tracks[0].sectors == 750);
  CHECK(two.tracks[1].start == 900 && two.tracks[1].sectors == 1501);  // a part sector counts whole
  CHECK(two.lead_out == 2401 && two.key == disc_key(2401, 2));
  CHECK(two.track_samples() == (std::vector<std::int32_t>{10 * kSampleRate, 20 * kSampleRate}));
  // Picking only one of them is another disc.
  CHECK(make_own_cd({{"a.flac", 588 * 750}}).key != two.key);
}

TEST(track_names_sort_as_a_listener_expects) {
  CHECK(track_name_less("2 Intro.mp3", "10 Outro.mp3"));
  CHECK(track_name_less("02 b.mp3", "10 a.mp3"));
  CHECK(track_name_less("Apple.mp3", "banana.mp3") && track_name_less("apple.mp3", "Banana.mp3"));
  CHECK(!track_name_less("Same.mp3", "Same.mp3"));
  CHECK(track_name_less("Track 9.flac", "Track 10.flac"));
}
