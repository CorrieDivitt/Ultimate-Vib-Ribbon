#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace vibg {

Audio::Audio(double offset_ms) : offset_ms_(offset_ms) {
  SDL_AudioSpec spec{SDL_AUDIO_S16, 2, kRate};
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Audio::callback, this);
  if (!stream_) throw std::runtime_error(std::string("SDL_OpenAudioDeviceStream: ") + SDL_GetError());
  SDL_AudioSpec device_spec;
  int frames = 0;
  if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream_), &device_spec, &frames) && frames > 0) {
    // The device buffer is counted at the device's own rate.
    device_frames_ = int(std::lround(double(frames) * kRate / std::max(1, device_spec.freq)));
  }
  std::printf("Audio: device buffer %d frames (%.1f ms)\n", device_frames_, device_frames_ * 1000.0 / kRate);
  SDL_ResumeAudioStreamDevice(stream_);
}

Audio::~Audio() {
  if (stream_) SDL_DestroyAudioStream(stream_);
}

void Audio::play_music(std::vector<std::int16_t> stereo) {
  // Drop what SDL still holds before taking our own lock: the callback runs with
  // SDL's stream lock held and then takes ours, so the other order could deadlock.
  if (stream_) SDL_ClearAudioStream(stream_);
  std::lock_guard<std::mutex> lock(mutex_);
  music_ = std::move(stereo);
  produced_ = 0;
  produced_at_ns_ = SDL_GetTicksNS();
  ++generation_;
  for (Voice& v : voices_) v = {};
}

void Audio::play_effect(const std::vector<std::int16_t>* mono, double rate, float gain, int voice) {
  if (!mono || mono->empty() || voice < 0 || voice >= 24) return;
  std::lock_guard<std::mutex> lock(mutex_);
  voices_[voice] = {mono, 0.0, rate / kRate, gain};
}

void SDLCALL Audio::callback(void* user, SDL_AudioStream* stream, int additional, int) {
  if (additional > 0) static_cast<Audio*>(user)->fill(stream, additional);
}

void Audio::fill(SDL_AudioStream* stream, int bytes) {
  const int frames = bytes / 4;
  if (frames <= 0) return;
  std::lock_guard<std::mutex> lock(mutex_);
  mix_.assign(std::size_t(frames) * 2, 0.0f);
  for (int i = 0; i < frames; ++i) {
    const std::uint64_t f = produced_ + std::uint64_t(i);
    if (f * 2 + 1 < music_.size()) {
      mix_[std::size_t(i) * 2] = music_[f * 2] * music_gain_;
      mix_[std::size_t(i) * 2 + 1] = music_[f * 2 + 1] * music_gain_;
    }
  }
  for (Voice& v : voices_) {
    if (!v.pcm) continue;
    const std::vector<std::int16_t>& s = *v.pcm;
    const float g = v.gain * effect_gain_;
    for (int i = 0; i < frames; ++i) {
      const auto at = static_cast<std::size_t>(v.position);
      if (at + 1 >= s.size()) {
        v.pcm = nullptr;
        break;
      }
      const double t = v.position - double(at);
      const float x = float(s[at] * (1.0 - t) + s[at + 1] * t) * g;  // linear interpolation
      mix_[std::size_t(i) * 2] += x;
      mix_[std::size_t(i) * 2 + 1] += x;
      v.position += v.step;
    }
  }
  out_.resize(mix_.size());
  for (std::size_t i = 0; i < mix_.size(); ++i) out_[i] = static_cast<std::int16_t>(std::clamp(mix_[i], -32768.0f, 32767.0f));
  SDL_PutAudioStreamData(stream, out_.data(), int(out_.size() * sizeof(std::int16_t)));
  produced_ += std::uint64_t(frames);
  produced_at_ns_ = SDL_GetTicksNS();
}

double Audio::latency_ms() const { return device_frames_ * 2 * 1000.0 / kRate + offset_ms_; }

double Audio::heard_frame(std::uint64_t now_ns) {
  std::uint64_t produced, at, generation;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    produced = produced_;
    at = produced_at_ns_;
    generation = generation_;
  }
  // What was handed over last is heard after the device's buffering: about two of
  // its buffers, plus the calibration offset.
  const double latency = double(device_frames_) * 2 + offset_ms_ * kRate / 1000.0;
  const double raw = double(produced) - latency + double(now_ns > at ? now_ns - at : 0) * kRate * 1e-9;
  if (generation != seen_generation_) {
    seen_generation_ = generation;
    clock_ = raw;
    clock_ns_ = now_ns;
    return clock_;
  }
  // The raw estimate jumps with every callback; follow it with a steady clock that
  // runs at the nominal rate and is pulled gently toward it.
  const double predicted = clock_ + double(now_ns > clock_ns_ ? now_ns - clock_ns_ : 0) * kRate * 1e-9;
  const double error = raw - predicted;
  double next = std::abs(error) > kRate * 0.1 ? raw : predicted + error * 0.05;
  if (next < clock_) next = clock_;  // never backwards
  clock_ = next;
  clock_ns_ = now_ns;
  return clock_;
}

double Audio::heard_frame_at(std::uint64_t now_ns, std::uint64_t event_ns) {
  const double now = heard_frame(now_ns);
  if (event_ns >= now_ns) return now;
  return now - double(now_ns - event_ns) * kRate * 1e-9;
}

}  // namespace vibg
