#pragma once
// Audio output for the desktop game: the music and the sound effects mixed into
// one SDL3 stream, and the game clock read from what the device has played.

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace vibg {

class Audio {
 public:
  static constexpr int kRate = 44100;

  // `offset_ms`: added to the estimated output latency (positive = sound reaches
  // you later than estimated). Calibrate per device.
  explicit Audio(double offset_ms);
  ~Audio();
  Audio(const Audio&) = delete;
  Audio& operator=(const Audio&) = delete;

  // Starts playing `stereo` (interleaved, 44.1 kHz) from its first frame, and
  // restarts the clock at 0. Stops whatever played before, effects included.
  void play_music(std::vector<std::int16_t> stereo);

  // A one-shot effect on `voice` (a new sound on a busy voice replaces the old one).
  // `rate`: the sample's playback rate in Hz.
  void play_effect(const std::vector<std::int16_t>* mono, double rate, float gain, int voice);

  // The music frame being heard at `ticks_ns` (SDL_GetTicksNS time), smoothed so
  // it moves evenly; may be fractional. Call with non-decreasing times for the
  // current moment; earlier times are extrapolated back.
  double heard_frame(std::uint64_t ticks_ns);
  double heard_frame_at(std::uint64_t now_ns, std::uint64_t event_ns);

  void adjust_offset(double ms) { offset_ms_ += ms; }
  double offset_ms() const { return offset_ms_; }
  double latency_ms() const;  // estimated output latency, offset included
  void set_volumes(float music, float effects) {
    std::lock_guard<std::mutex> lock(mutex_);
    music_gain_ = music;
    effect_gain_ = effects;
  }

 private:
  static void SDLCALL callback(void* user, SDL_AudioStream* stream, int additional, int total);
  void fill(SDL_AudioStream* stream, int bytes);

  struct Voice {
    const std::vector<std::int16_t>* pcm = nullptr;
    double position = 0, step = 0;
    float gain = 0;
  };

  SDL_AudioStream* stream_ = nullptr;
  int device_frames_ = 1024;
  std::mutex mutex_;
  std::vector<std::int16_t> music_;
  std::uint64_t produced_ = 0;        // frames handed to SDL since play_music
  std::uint64_t produced_at_ns_ = 0;  // when the last of them were handed over
  std::uint64_t generation_ = 0;      // bumps on play_music, to reset the smoothing
  Voice voices_[24];
  std::vector<float> mix_;
  std::vector<std::int16_t> out_;
  float music_gain_ = 1.0f, effect_gain_ = 1.0f;
  double offset_ms_ = 0;
  // smoothing (main thread)
  std::uint64_t seen_generation_ = ~std::uint64_t(0);
  double clock_ = 0;
  std::uint64_t clock_ns_ = 0;
};

}  // namespace vibg
