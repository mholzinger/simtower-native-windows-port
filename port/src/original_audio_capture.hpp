#pragma once

// Deterministic OriginalAudioDevice: records every voice the runtime starts,
// on the host millisecond clock, and renders them to a mixed 16-bit PCM
// timeline. A voice is done exactly when its nominal play time has elapsed
// on that clock, so reaping, the 600-tick saturation rule and channel
// arbitration follow the host clock instead of a real device. Used by
// headless capture runs and by the portable audio tests; it opens nothing.

#include "original_audio_device.hpp"
#include "original_audio_rules.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace simtower {

class OriginalAudioCaptureDevice final : public OriginalAudioDevice {
 public:
  // One started voice: a successful submit.
  struct Submission {
    std::uint32_t start_ms{};
    std::size_t voice{};
    OriginalPcmFormat format{};
    OriginalAudioLoopPlan plan{};
    std::uint32_t passes{};  // max(1, plan.total_passes)
    std::vector<std::byte> samples{};
    // Set when the runtime stopped the voice (stop() on a started voice).
    std::optional<std::uint32_t> stop_ms{};
  };

  enum class Operation : std::uint8_t { open, submit, stop };
  struct Event {
    std::uint32_t ms{};
    std::size_t voice{};
    Operation operation{};
    bool succeeded{};
  };

  explicit OriginalAudioCaptureDevice(std::function<std::uint32_t()> clock_ms,
                                      bool available = true)
      : clock_ms_(std::move(clock_ms)), available_(available) {}

  bool available() noexcept override { return available_; }
  bool open(std::size_t voice,
            const OriginalPcmFormat& format) noexcept override;
  bool submit(std::size_t voice,
              std::span<const std::byte> samples,
              OriginalAudioLoopPlan plan) noexcept override;
  void stop(std::size_t voice) noexcept override;
  bool done(std::size_t voice) noexcept override;

  [[nodiscard]] const std::vector<Submission>& submissions() const noexcept {
    return submissions_;
  }
  [[nodiscard]] const std::vector<Event>& events() const noexcept {
    return events_;
  }

  // Nominal milliseconds of one submission: ceil(bytes * 1000 / avg) per
  // pass, times the passes.
  [[nodiscard]] static std::uint64_t duration_ms(const OriginalPcmFormat& format,
                                                 std::size_t bytes,
                                                 std::uint32_t passes) noexcept;

  struct Render {
    std::uint32_t sample_rate{};
    std::uint32_t origin_ms{};  // start_ms of the first submission
    std::vector<std::int16_t> samples{};  // mono
    // Output sample range [first, end) of each submission, in order.
    std::vector<std::pair<std::size_t, std::size_t>> regions{};
  };
  // Mixes every submission onto one mono timeline at `sample_rate`. Each
  // submission starts at its clock offset from the first one and ends at its
  // stop time or after all its passes, whichever is first. Resampling is
  // zero-order hold with integer arithmetic (output sample i of a voice
  // plays source frame floor(i * source_rate / sample_rate)); 8-bit samples
  // become (u - 128) << 8, stereo is averaged, and the sum is clamped.
  [[nodiscard]] Render render(std::uint32_t sample_rate = 44100U) const;
  // A RIFF/WAVE file (PCM, mono, 16-bit) holding `render.samples`.
  [[nodiscard]] static std::vector<std::byte> wav_file(const Render& render);

 private:
  struct Voice {
    bool opened{};
    std::optional<std::size_t> submission{};  // index into submissions_
    OriginalPcmFormat format{};
  };

  std::function<std::uint32_t()> clock_ms_;
  bool available_{};
  std::array<Voice, 2> voices_{};
  std::vector<Submission> submissions_{};
  std::vector<Event> events_{};
};

// One source frame of a submission as signed 16-bit mono.
[[nodiscard]] std::int16_t original_pcm_frame_s16(
    const OriginalPcmFormat& format,
    std::span<const std::byte> samples,
    std::size_t frame) noexcept;

}  // namespace simtower
