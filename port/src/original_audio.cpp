#include "original_audio.hpp"

#include <cstdint>
#include <limits>

namespace simtower {

OriginalAudioRuntime::~OriginalAudioRuntime() {
  shutdown();
}

bool OriginalAudioRuntime::initialize() noexcept {
  // 11c8:08eb's WAVMIXOPENCHANNEL wrapper is represented by native channel
  // creation on demand; initialization verifies that the PCM backend exists.
  shutdown();
  arbiter_.stop_all();
  last_play_tick_ = 0;
  if (!sound_enabled_) {
    return true;
  }
  initialized_ = device_.available();
  active_ = initialized_;
  return initialized_;
}

void OriginalAudioRuntime::shutdown() noexcept {
  // Exact lifecycle equivalent of 11c8:0a31: close every channel, release
  // retained wave state, close the session, and clear its live latch.
  for (std::size_t channel = 0; channel < arbiter_.channels().size(); ++channel) {
    release_channel(channel);
  }
  arbiter_.stop_all();
  initialized_ = false;
  active_ = false;
}

void OriginalAudioRuntime::activate() noexcept {
  // Exact 11c8:0aab activates the live WAVMIX device once and latches the
  // active flag. The device remains only the native PCM backend.
  if (!active_ && initialized_ && sound_enabled_) {
    active_ = true;
  }
}

void OriginalAudioRuntime::deactivate() noexcept {
  // Exact 11c8:0add stops active channels, deactivates WAVMIX, and clears the
  // latch. The native backend keeps the same observable lifecycle.
  if (initialized_ && sound_enabled_) {
    stop_all(true);
  }
  active_ = false;
}

void OriginalAudioRuntime::activate_mixer_backend() noexcept {
  // A direct WAVEMIXACTIVATE(handle, 1) does not update DS:0252. The device
  // has no corresponding session activation state, so preserving active_ is
  // the exact native replacement.
}

void OriginalAudioRuntime::deactivate_mixer_backend() noexcept {
  // A direct WAVEMIXACTIVATE(handle, 0) does not run 11c8:0135 and does not
  // clear DS:0252. Callers that require the wrapper semantics use
  // deactivate(); About already stops both channels before reaching here.
}

void OriginalAudioRuntime::pump(std::uint32_t tick_count_ms) noexcept {
  const auto plan = original_wavemix_pump_plan(
      pump_deadline_, tick_count_ms, category_enabled_[0],
      category_enabled_[1], category_enabled_[2]);
  if (!plan.due) return;
  pump_deadline_ = plan.next_deadline;
  if (plan.pump_backend) {
    // 11e0:0eae-0ee7 drains only WAVMIX callback message 0x03BD before
    // WaveMixPump. No device posts such a window message; observing the
    // device's done state (waveOut: WHDR_DONE) and retiring the matching
    // channel state is its native equivalent.
    reap_completed();
  }
}

void OriginalAudioRuntime::set_sound_enabled(bool enabled) noexcept {
  if (sound_enabled_ == enabled) {
    return;
  }
  if (!enabled) {
    shutdown();
  }
  sound_enabled_ = enabled;
}

void OriginalAudioRuntime::set_category_enabled(std::size_t category, bool enabled) noexcept {
  if (category < category_enabled_.size()) {
    category_enabled_[category] = enabled;
  }
}

bool OriginalAudioRuntime::play_resource(std::int32_t resource_id,
                                         std::uint16_t repeat_count,
                                         std::uint16_t priority,
                                         std::uint32_t tick_count_ms) noexcept {
  // Direct native translation of 11c8:0167: master/active/category gates,
  // priority arbitration, 600-coarse-tick (~9.6-second) saturated-channel
  // flush, resource open,
  // replacement, and repeat/non-repeat submission.
  // 11c8:01c3 reaches its clock through 1208:05e6, so the shared 0e84 pump
  // precedes even a play request that will later fail an enable/priority gate.
  pump(tick_count_ms);
  const std::uint32_t coarse_tick = original_coarse_tick(tick_count_ms);
  reap_completed();
  if (!sound_enabled_ || !active_ ||
      !original_audio_priority_enabled(priority, category_enabled_)) {
    return false;
  }

  int channel = arbiter_.select_channel(priority);
  if (channel < 0) {
    if (!original_audio_saturation_elapsed(coarse_tick, last_play_tick_)) {
      return false;
    }
    stop_all(true);
    channel = arbiter_.select_channel(priority);
  }
  if (channel < 0) {
    return false;
  }

  last_play_tick_ = coarse_tick;
  const std::size_t selected = static_cast<std::size_t>(channel);
  if (arbiter_.channels()[selected].active) {
    release_channel(selected);
    arbiter_.stop(selected);
  }

  const auto wave = parse_original_wave(resources_.find("WAVE", resource_id));
  if (wave.logical_size == 0U) {
    return false;
  }
  if (!begin_native_playback(selected, wave, repeat_count)) {
    release_channel(selected);
    arbiter_.stop(selected);
    return false;
  }
  arbiter_.start(selected, resource_id, priority);
  return true;
}

bool OriginalAudioRuntime::play_reserved_if_idle(std::int32_t resource_id,
                                                  std::uint16_t repeat_count,
                                                  std::uint32_t tick_count_ms) noexcept {
  reap_completed();
  // 11c8:0100 calls the priority-5 path only while channel 1's active word is
  // zero; it does not interrupt an already-playing reserved sound.
  if (!original_reserved_audio_should_submit(arbiter_.channels()[1].active)) {
    return false;
  }
  return play_resource(resource_id, repeat_count, 5U, tick_count_ms);
}

void OriginalAudioRuntime::stop_channel(std::size_t channel, bool force) noexcept {
  if (!original_audio_stop_channel_allowed(
          sound_enabled_, active_, force, channel)) {
    return;
  }
  release_channel(channel);
  arbiter_.stop(channel);
}

void OriginalAudioRuntime::stop_all(bool force) noexcept {
  // Exact 11c8:0135 two-channel stop wrapper around 11c8:02c0.
  for (std::size_t channel = 0; channel < arbiter_.channels().size(); ++channel) {
    stop_channel(channel, force);
  }
}

bool OriginalAudioRuntime::channel_active(std::size_t channel) noexcept {
  // Exact observable value of 11c8:0390's per-channel active-word query.
  reap_completed();
  return channel < arbiter_.channels().size() && arbiter_.channels()[channel].active;
}

void OriginalAudioRuntime::reap_completed() noexcept {
  for (std::size_t channel = 0; channel < arbiter_.channels().size(); ++channel) {
    if (arbiter_.channels()[channel].active && device_.done(channel)) {
      release_channel(channel);
      arbiter_.stop(channel);
    }
  }
}

void OriginalAudioRuntime::release_channel(std::size_t channel) noexcept {
  if (channel >= arbiter_.channels().size()) {
    return;
  }
  device_.stop(channel);
  muted_samples_[channel] = {};
}

bool OriginalAudioRuntime::begin_native_playback(std::size_t channel,
                                                  const OriginalWaveView& wave,
                                                  std::uint16_t repeat_count) noexcept {
  // Native PCM transaction for 11c8:0920: bind a channel/wave/repeat count,
  // mark that channel active, and submit it to the platform audio backend.
  // The 32-bit bound is waveOut's DWORD dwBufferLength, checked before any
  // device call as the Win32 host always did.
  if (channel >= arbiter_.channels().size() ||
      wave.samples.size() > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }

  const OriginalPcmFormat format{
      .format_tag = wave.format_tag,
      .channels = wave.channels,
      .samples_per_second = wave.samples_per_second,
      .average_bytes_per_second = wave.average_bytes_per_second,
      .block_align = wave.block_align,
      .bits_per_sample = wave.bits_per_sample,
  };
  if (!device_.open(channel, format)) {
    return false;
  }

  std::span<const std::byte> submitted_samples = wave.samples;
  if (host_output_muted_) {
    // PCM/8 is unsigned and rests at 0x80; wider PCM formats rest at zero.
    // Only the validation smoke enables this branch. The same format, byte
    // count, loop flags, open, submit, completion, and teardown paths remain
    // production code.
    const std::byte silence =
        wave.bits_per_sample == 8U ? std::byte{0x80} : std::byte{0x00};
    muted_samples_[channel].assign(wave.samples.size(), silence);
    submitted_samples = muted_samples_[channel];
  }
  if (!device_.submit(channel, submitted_samples,
                      original_audio_loop_plan(repeat_count))) {
    muted_samples_[channel] = {};
    return false;
  }
  return true;
}

}  // namespace simtower
