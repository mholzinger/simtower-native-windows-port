#include "sdl_audio.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>

namespace simtower::sdl {

SdlAudioDevice::SdlAudioDevice() {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    std::fprintf(stderr, "simtower_sdl: audio init failed: %s\n",
                 SDL_GetError());
    return;
  }
  subsystem_ = true;
  device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
  if (device_ == 0U) {
    std::fprintf(stderr, "simtower_sdl: no audio playback device: %s\n",
                 SDL_GetError());
  }
}

SdlAudioDevice::~SdlAudioDevice() {
  for (std::size_t voice = 0; voice < streams_.size(); ++voice) stop(voice);
  if (device_ != 0U) SDL_CloseAudioDevice(device_);
  if (subsystem_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool SdlAudioDevice::open(std::size_t voice,
                          const OriginalPcmFormat& format) noexcept {
  // waveOutOpen(WAVE_MAPPER, format) counterpart. Only PCM/8 (unsigned) and
  // PCM/16 (signed little-endian) exist in WAVE terms; every WAVE resource
  // in the pack is PCM/8 mono. SDL takes whole frames only, so block_align
  // must be the packed frame size (as the capture device requires).
  if (voice >= streams_.size() || device_ == 0U) return false;
  stop(voice);
  if (format.format_tag != 1U || format.channels == 0U ||
      format.samples_per_second == 0U ||
      format.samples_per_second > static_cast<std::uint32_t>(INT_MAX) ||
      (format.bits_per_sample != 8U && format.bits_per_sample != 16U) ||
      format.block_align !=
          format.channels * (format.bits_per_sample / 8U)) {
    return false;
  }
  const SDL_AudioSpec source{
      .format = format.bits_per_sample == 8U ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE,
      .channels = static_cast<int>(format.channels),
      .freq = static_cast<int>(format.samples_per_second),
  };
  // The output side is set by the device when the stream is bound.
  SDL_AudioStream* stream = SDL_CreateAudioStream(&source, nullptr);
  if (!stream) return false;
  if (!SDL_BindAudioStream(device_, stream)) {
    SDL_DestroyAudioStream(stream);
    return false;
  }
  streams_[voice] = stream;
  frame_bytes_[voice] = format.block_align;
  submitted_[voice] = false;
  return true;
}

bool SdlAudioDevice::submit(std::size_t voice,
                            std::span<const std::byte> samples,
                            OriginalAudioLoopPlan plan) noexcept {
  // waveOutPrepareHeader/waveOutWrite counterpart: the loop flags become
  // max(1, total_passes) copies of the buffer, then a flush so the tail is
  // converted and the stream can drain to empty. SDL refuses a partial
  // final frame, so it is dropped here, as waveOut drops it. On failure the
  // voice is closed, as the waveOut backend closes it.
  if (voice >= streams_.size() || !streams_[voice]) {
    stop(voice);
    return false;
  }
  const std::size_t frame = frame_bytes_[voice];
  const std::size_t whole = samples.size() - samples.size() % frame;
  if (whole == 0U || whole > static_cast<std::size_t>(INT_MAX)) {
    stop(voice);
    return false;
  }
  samples = samples.first(whole);
  SDL_AudioStream* stream = streams_[voice];
  const std::uint32_t passes = std::max<std::uint32_t>(1U, plan.total_passes);
  bool ok = SDL_ClearAudioStream(stream);
  for (std::uint32_t pass = 0; ok && pass < passes; ++pass) {
    ok = SDL_PutAudioStreamData(stream, samples.data(),
                                static_cast<int>(samples.size()));
  }
  ok = ok && SDL_FlushAudioStream(stream);
  if (!ok) {
    stop(voice);
    return false;
  }
  submitted_[voice] = true;
  return true;
}

void SdlAudioDevice::stop(std::size_t voice) noexcept {
  // waveOutReset/Unprepare/Close counterpart. Destroying a bound stream
  // unbinds it and drops anything still queued. Idempotent.
  if (voice >= streams_.size()) return;
  if (streams_[voice]) SDL_DestroyAudioStream(streams_[voice]);
  streams_[voice] = nullptr;
  frame_bytes_[voice] = 0U;
  submitted_[voice] = false;
}

bool SdlAudioDevice::done(std::size_t voice) noexcept {
  // WHDR_DONE counterpart: the device has pulled every converted byte. The
  // last device buffer may still be sounding, as with waveOut's done bit.
  if (voice >= streams_.size() || !streams_[voice] || !submitted_[voice]) {
    return false;
  }
  return SDL_GetAudioStreamQueued(streams_[voice]) == 0 &&
         SDL_GetAudioStreamAvailable(streams_[voice]) == 0;
}

}  // namespace simtower::sdl
