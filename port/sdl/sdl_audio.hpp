#pragma once

// SDL3 PCM backend for OriginalAudioRuntime: one default playback device and
// one SDL_AudioStream per voice bound to it, which SDL mixes. The stream's
// source spec is the WAVE resource's own format, so each wave plays at its
// own sample rate, as on the Win32 waveOut backend.

#include "original_audio_device.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <span>

namespace simtower::sdl {

class SdlAudioDevice final : public OriginalAudioDevice {
 public:
  // Initializes the SDL audio subsystem and opens the default playback
  // device. On failure available() is false and every voice call fails.
  SdlAudioDevice();
  ~SdlAudioDevice() override;

  SdlAudioDevice(const SdlAudioDevice&) = delete;
  SdlAudioDevice& operator=(const SdlAudioDevice&) = delete;

  bool available() noexcept override { return device_ != 0U; }
  bool open(std::size_t voice,
            const OriginalPcmFormat& format) noexcept override;
  bool submit(std::size_t voice,
              std::span<const std::byte> samples,
              OriginalAudioLoopPlan plan) noexcept override;
  void stop(std::size_t voice) noexcept override;
  bool done(std::size_t voice) noexcept override;

 private:
  bool subsystem_{};
  SDL_AudioDeviceID device_{};
  std::array<SDL_AudioStream*, 2> streams_{};
  std::array<std::size_t, 2> frame_bytes_{};  // block_align of the open voice
  std::array<bool, 2> submitted_{};
};

}  // namespace simtower::sdl
