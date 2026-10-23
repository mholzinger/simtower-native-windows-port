#pragma once

// Win32 waveOut backend for OriginalAudioRuntime. The open, prepare, write,
// loop, reset, unprepare and close sequence and the WHDR_DONE reaping test
// are the Win32 host's original transaction, moved here unchanged.
#include "original_audio.hpp"
#include "original_audio_device.hpp"
#include "original_audio_rules.hpp"
#include "original_resources.hpp"

#include <array>
#include <cstddef>
#include <span>

#include <windows.h>
#include <mmsystem.h>

namespace simtower {

static_assert(kOriginalWaveBeginLoop == WHDR_BEGINLOOP);
static_assert(kOriginalWaveEndLoop == WHDR_ENDLOOP);

class OriginalWaveOutAudioDevice final : public OriginalAudioDevice {
 public:
  OriginalWaveOutAudioDevice() = default;
  ~OriginalWaveOutAudioDevice() override;

  OriginalWaveOutAudioDevice(const OriginalWaveOutAudioDevice&) = delete;
  OriginalWaveOutAudioDevice& operator=(const OriginalWaveOutAudioDevice&) =
      delete;

  bool available() noexcept override;
  bool open(std::size_t voice,
            const OriginalPcmFormat& format) noexcept override;
  bool submit(std::size_t voice,
              std::span<const std::byte> samples,
              OriginalAudioLoopPlan plan) noexcept override;
  void stop(std::size_t voice) noexcept override;
  bool done(std::size_t voice) noexcept override;

 private:
  struct NativeChannel {
    HWAVEOUT output{};
    WAVEHDR header{};
    bool prepared{};
  };

  std::array<NativeChannel, 2> native_channels_{};
};

namespace detail {
// Base-from-member holder so the device is constructed before, and
// destroyed after, the runtime that stops it.
struct OriginalWaveOutAudioDeviceHolder {
  OriginalWaveOutAudioDevice waveout_device{};
};
}  // namespace detail

// The Win32 host's runtime: the portable runtime on a waveOut device with
// GetTickCount as its clock.
class OriginalWaveOutAudioRuntime
    : private detail::OriginalWaveOutAudioDeviceHolder,
      public OriginalAudioRuntime {
 public:
  explicit OriginalWaveOutAudioRuntime(const OriginalResources& resources)
      : OriginalAudioRuntime(resources, waveout_device, [] {
          return static_cast<std::uint32_t>(GetTickCount());
        }) {}
};

}  // namespace simtower
