#include "original_audio_capture.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>
#include <vector>

namespace simtower {
namespace {

[[nodiscard]] bool capture_format_supported(
    const OriginalPcmFormat& format) noexcept {
  return format.format_tag == 1U &&
         (format.channels == 1U || format.channels == 2U) &&
         (format.bits_per_sample == 8U || format.bits_per_sample == 16U) &&
         format.block_align == format.channels * format.bits_per_sample / 8U &&
         format.samples_per_second != 0U &&
         format.average_bytes_per_second != 0U;
}

// Makes room for one more element with geometric growth, so the push_back
// that follows cannot throw. Throws only here, before any state changes.
template <typename T>
void reserve_one(std::vector<T>& values) {
  if (values.size() < values.capacity()) return;
  values.reserve(std::max<std::size_t>(16U, values.capacity() * 2U));
}

[[nodiscard]] std::int64_t elapsed_ms(std::uint32_t now,
                                      std::uint32_t since) noexcept {
  // Wrapping host clock, as every other GetTickCount difference.
  return std::bit_cast<std::int32_t>(now - since);
}

void put_u16(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(static_cast<std::byte>(value & 0xffU));
  out.push_back(static_cast<std::byte>(value >> 8U));
}

void put_u32(std::vector<std::byte>& out, std::uint32_t value) {
  put_u16(out, static_cast<std::uint16_t>(value & 0xffffU));
  put_u16(out, static_cast<std::uint16_t>(value >> 16U));
}

void put_tag(std::vector<std::byte>& out, const char (&tag)[5]) {
  for (int index = 0; index < 4; ++index) {
    out.push_back(static_cast<std::byte>(tag[index]));
  }
}

}  // namespace

std::int16_t original_pcm_frame_s16(const OriginalPcmFormat& format,
                                    std::span<const std::byte> samples,
                                    std::size_t frame) noexcept {
  const std::size_t channels = format.channels;
  const std::size_t base = frame * format.block_align;
  std::int32_t sum = 0;
  for (std::size_t channel = 0; channel < channels; ++channel) {
    if (format.bits_per_sample == 8U) {
      const auto value = static_cast<std::int32_t>(
          std::to_integer<std::uint8_t>(samples[base + channel]));
      sum += (value - 128) * 256;
    } else {
      const std::size_t at = base + channel * 2U;
      const auto low = std::to_integer<std::uint16_t>(samples[at]);
      const auto high = std::to_integer<std::uint16_t>(samples[at + 1U]);
      sum += static_cast<std::int16_t>(
          static_cast<std::uint16_t>(low | (high << 8U)));
    }
  }
  return static_cast<std::int16_t>(sum / static_cast<std::int32_t>(channels));
}

std::uint64_t OriginalAudioCaptureDevice::duration_ms(
    const OriginalPcmFormat& format,
    std::size_t bytes,
    std::uint32_t passes) noexcept {
  if (format.average_bytes_per_second == 0U) return 0U;
  const std::uint64_t per_pass =
      (static_cast<std::uint64_t>(bytes) * 1000U +
       format.average_bytes_per_second - 1U) /
      format.average_bytes_per_second;
  return per_pass * passes;
}

bool OriginalAudioCaptureDevice::open(std::size_t voice,
                                      const OriginalPcmFormat& format) noexcept {
  if (voice >= voices_.size()) return false;
  const bool supported = capture_format_supported(format);
  try {
    reserve_one(events_);
  } catch (...) {
    // Out of memory: an unrecorded failed open.
    voices_[voice] = {};
    return false;
  }
  events_.push_back({clock_ms_(), voice, Operation::open, supported});
  if (!supported) {
    voices_[voice] = {};
    return false;
  }
  voices_[voice] = {.opened = true, .submission = {}, .format = format};
  return true;
}

bool OriginalAudioCaptureDevice::submit(std::size_t voice,
                                        std::span<const std::byte> samples,
                                        OriginalAudioLoopPlan plan) noexcept {
  if (voice >= voices_.size()) return false;
  auto& state = voices_[voice];
  const std::uint32_t now = clock_ms_();
  const bool accepted = state.opened && !state.submission &&
                        samples.size() >= state.format.block_align &&
                        samples.size() <= std::numeric_limits<std::uint32_t>::max();
  // Every allocation happens before anything is recorded, so an out-of-
  // memory failure leaves the log consistent and is an unrecorded failed
  // submit that closes the voice, as below.
  std::vector<std::byte> copy;
  try {
    reserve_one(events_);
    if (accepted) {
      reserve_one(submissions_);
      copy.assign(samples.begin(), samples.end());
    }
  } catch (...) {
    state = {};
    return false;
  }
  events_.push_back({now, voice, Operation::submit, accepted});
  if (!accepted) {
    // As a failed waveOutPrepareHeader/Write: the device closes the voice.
    state = {};
    return false;
  }
  submissions_.push_back({
      .start_ms = now,
      .voice = voice,
      .format = state.format,
      .plan = plan,
      .passes = std::max<std::uint32_t>(1U, plan.total_passes),
      .samples = std::move(copy),
      .stop_ms = {},
  });
  state.submission = submissions_.size() - 1U;
  return true;
}

void OriginalAudioCaptureDevice::stop(std::size_t voice) noexcept {
  if (voice >= voices_.size()) return;
  auto& state = voices_[voice];
  if (!state.opened) return;  // idempotent, like a reset waveOut slot
  const std::uint32_t now = clock_ms_();
  try {
    reserve_one(events_);
    events_.push_back({now, voice, Operation::stop, true});
  } catch (...) {
    // Out of memory: the stop still takes effect, only its event is lost.
  }
  if (state.submission) {
    submissions_[*state.submission].stop_ms = now;
  }
  state = {};
}

bool OriginalAudioCaptureDevice::done(std::size_t voice) noexcept {
  if (voice >= voices_.size() || !voices_[voice].submission) return false;
  const auto& submission = submissions_[*voices_[voice].submission];
  const auto elapsed = elapsed_ms(clock_ms_(), submission.start_ms);
  return elapsed >= 0 &&
         static_cast<std::uint64_t>(elapsed) >=
             duration_ms(submission.format, submission.samples.size(),
                         submission.passes);
}

OriginalAudioCaptureDevice::Render OriginalAudioCaptureDevice::render(
    std::uint32_t sample_rate) const {
  Render result{};
  result.sample_rate = sample_rate;
  if (submissions_.empty() || sample_rate == 0U) return result;
  result.origin_ms = submissions_.front().start_ms;

  const auto to_index = [&](std::int64_t ms) -> std::size_t {
    return ms <= 0 ? 0U
                   : static_cast<std::size_t>(
                         static_cast<std::uint64_t>(ms) * sample_rate / 1000U);
  };
  std::size_t total = 0U;
  for (const auto& submission : submissions_) {
    const std::uint64_t frames =
        submission.samples.size() / submission.format.block_align;
    const std::uint64_t source_frames = frames * submission.passes;
    const std::uint64_t source_rate = submission.format.samples_per_second;
    // ceil(source_frames * sample_rate / source_rate) output samples.
    const std::uint64_t natural =
        (source_frames * sample_rate + source_rate - 1U) / source_rate;
    const std::size_t first =
        to_index(elapsed_ms(submission.start_ms, result.origin_ms));
    std::size_t end = first + static_cast<std::size_t>(natural);
    if (submission.stop_ms) {
      // A stop at or after the nominal duration is the reap of a finished
      // voice; only an earlier stop truncates it.
      const auto played = elapsed_ms(*submission.stop_ms, submission.start_ms);
      if (played < 0 ||
          static_cast<std::uint64_t>(played) <
              duration_ms(submission.format, submission.samples.size(),
                          submission.passes)) {
        end = std::min(end, std::max(first, to_index(elapsed_ms(
                                                *submission.stop_ms,
                                                result.origin_ms))));
      }
    }
    result.regions.emplace_back(first, end);
    total = std::max(total, end);
  }

  std::vector<std::int32_t> mix(total, 0);
  for (std::size_t index = 0; index < submissions_.size(); ++index) {
    const auto& submission = submissions_[index];
    const auto [first, end] = result.regions[index];
    const std::uint64_t frames =
        submission.samples.size() / submission.format.block_align;
    const std::uint64_t source_rate = submission.format.samples_per_second;
    for (std::size_t at = first; at < end; ++at) {
      const std::uint64_t source =
          static_cast<std::uint64_t>(at - first) * source_rate / sample_rate;
      mix[at] += original_pcm_frame_s16(submission.format, submission.samples,
                                        static_cast<std::size_t>(source % frames));
    }
  }
  result.samples.resize(total);
  for (std::size_t at = 0; at < total; ++at) {
    result.samples[at] = static_cast<std::int16_t>(
        std::clamp<std::int32_t>(mix[at], std::numeric_limits<std::int16_t>::min(),
                                 std::numeric_limits<std::int16_t>::max()));
  }
  return result;
}

std::vector<std::byte> OriginalAudioCaptureDevice::wav_file(
    const Render& render) {
  const auto data_bytes =
      static_cast<std::uint32_t>(render.samples.size() * 2U);
  std::vector<std::byte> out;
  out.reserve(44U + data_bytes);
  put_tag(out, "RIFF");
  put_u32(out, 36U + data_bytes);
  put_tag(out, "WAVE");
  put_tag(out, "fmt ");
  put_u32(out, 16U);
  put_u16(out, 1U);  // PCM
  put_u16(out, 1U);  // mono
  put_u32(out, render.sample_rate);
  put_u32(out, render.sample_rate * 2U);
  put_u16(out, 2U);
  put_u16(out, 16U);
  put_tag(out, "data");
  put_u32(out, data_bytes);
  for (const auto sample : render.samples) {
    put_u16(out, static_cast<std::uint16_t>(sample));
  }
  return out;
}

}  // namespace simtower
