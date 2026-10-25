#pragma once

// Host PCM backend behind OriginalAudioRuntime (original_audio.hpp). The
// runtime keeps every 11c8 WAVMIX16 rule (gates, arbitration, saturation,
// repeat setup, reaping points); a device only opens, feeds, stops and
// polls the two voices. Implementations: waveOut (Win32,
// original_audio_waveout_win32.hpp), SDL3 (sdl/sdl_audio.hpp) and the
// deterministic capture device (original_audio_capture.hpp).

#include "original_audio_rules.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace simtower {

// The WAVE resource's fmt chunk, as the runtime hands it to waveOutOpen
// (WAVEFORMATEX without cbSize, which the runtime always sends as zero).
struct OriginalPcmFormat {
  std::uint16_t format_tag{};
  std::uint16_t channels{};
  std::uint32_t samples_per_second{};
  std::uint32_t average_bytes_per_second{};
  std::uint16_t block_align{};
  std::uint16_t bits_per_sample{};

  friend bool operator==(const OriginalPcmFormat&,
                         const OriginalPcmFormat&) = default;
};

// Every call is noexcept, as are the OriginalAudioRuntime methods that make
// them: an implementation that allocates must catch the failure itself and
// report it as a failed open or submit (stop still closes the voice).
class OriginalAudioDevice {
 public:
  virtual ~OriginalAudioDevice() = default;

  // A PCM endpoint exists (waveOutGetNumDevs() != 0).
  virtual bool available() noexcept = 0;
  // Opens voice `voice` (0 or 1) for one buffer of this format. On failure
  // the voice is left closed.
  virtual bool open(std::size_t voice,
                    const OriginalPcmFormat& format) noexcept = 0;
  // Starts the opened voice: one pass when plan.flags is zero, otherwise
  // plan.total_passes passes. `samples` stays valid until stop(voice). On
  // failure the device has already closed the voice itself.
  virtual bool submit(std::size_t voice,
                      std::span<const std::byte> samples,
                      OriginalAudioLoopPlan plan) noexcept = 0;
  // Reset, release and close. Idempotent: a closed voice is a no-op.
  virtual void stop(std::size_t voice) noexcept = 0;
  // The submitted buffer finished playing (WHDR_DONE). Pure query.
  virtual bool done(std::size_t voice) noexcept = 0;
};

}  // namespace simtower
