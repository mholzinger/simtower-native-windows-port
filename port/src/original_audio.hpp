#pragma once

// Portable host for the rules in original_audio_rules.hpp. The PCM backend
// is an OriginalAudioDevice (waveOut on Win32, SDL3, or the capture device).
#include "original_audio_device.hpp"
#include "original_audio_rules.hpp"
#include "original_resources.hpp"
#include "original_wave.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace simtower {

// Native replacement for the original 11c8 WAVMIX16 subsystem. Channel
// arbitration, priority gates, the 600-coarse-tick saturation rule, and repeat-count
// semantics are direct translations; the device is only the host PCM backend.
class OriginalAudioRuntime {
 public:
  // `clock_ms` replaces GetTickCount for the overload without a tick
  // argument. `device` must outlive the runtime (the destructor stops it).
  OriginalAudioRuntime(const OriginalResources& resources,
                       OriginalAudioDevice& device,
                       std::function<std::uint32_t()> clock_ms)
      : resources_(resources), device_(device), clock_ms_(std::move(clock_ms)) {}
  ~OriginalAudioRuntime();

  OriginalAudioRuntime(const OriginalAudioRuntime&) = delete;
  OriginalAudioRuntime& operator=(const OriginalAudioRuntime&) = delete;

  [[nodiscard]] bool initialize() noexcept;
  void shutdown() noexcept;
  void activate() noexcept;
  void deactivate() noexcept;
  // Direct host-backend counterparts for WAVEMIXACTIVATE calls made outside
  // the 11c8:0aab/0add wrappers. The device backends have no session-level
  // activation switch, so these intentionally preserve the original active
  // latch while documenting the distinct lifecycle boundary.
  void activate_mixer_backend() noexcept;
  void deactivate_mixer_backend() noexcept;
  void pump(std::uint32_t tick_count_ms) noexcept;

  void set_sound_enabled(bool enabled) noexcept;
  // Hardware-validation control: retain the exact resource format, buffer
  // length, channel arbitration, repeat flags, and device transaction while
  // replacing only submitted sample values with digital silence. Ordinary
  // game execution never enables this switch.
  void set_host_output_muted(bool muted) noexcept {
    host_output_muted_ = muted;
  }
  void set_category_enabled(std::size_t category, bool enabled) noexcept;
  [[nodiscard]] bool category_enabled(std::size_t category) const noexcept {
    return category < category_enabled_.size() && category_enabled_[category];
  }
  [[nodiscard]] bool sound_enabled() const noexcept { return sound_enabled_; }
  [[nodiscard]] bool active() const noexcept { return active_; }

  [[nodiscard]] bool play_resource(std::int32_t resource_id,
                                   std::uint16_t repeat_count,
                                   std::uint16_t priority,
                                   std::uint32_t tick_count_ms) noexcept;
  [[nodiscard]] bool play_resource(std::int32_t resource_id,
                                   std::uint16_t repeat_count,
                                   std::uint16_t priority) noexcept {
    return play_resource(resource_id, repeat_count, priority, clock_ms_());
  }
  [[nodiscard]] bool play_reserved_if_idle(std::int32_t resource_id,
                                           std::uint16_t repeat_count,
                                           std::uint32_t tick_count_ms) noexcept;
  void stop_channel(std::size_t channel, bool force) noexcept;
  void stop_all(bool force) noexcept;
  [[nodiscard]] bool channel_active(std::size_t channel) noexcept;
  [[nodiscard]] const OriginalAudioArbiter& arbiter() const noexcept { return arbiter_; }

 private:
  void reap_completed() noexcept;
  void release_channel(std::size_t channel) noexcept;
  [[nodiscard]] bool begin_native_playback(std::size_t channel,
                                           const OriginalWaveView& wave,
                                           std::uint16_t repeat_count) noexcept;

  const OriginalResources& resources_;
  OriginalAudioDevice& device_;
  std::function<std::uint32_t()> clock_ms_;
  OriginalAudioArbiter arbiter_{};
  // Per-voice digital-silence buffer for set_host_output_muted; it must stay
  // valid until the device stops the voice.
  std::array<std::vector<std::byte>, 2> muted_samples_{};
  std::array<bool, 3> category_enabled_{true, true, true};
  std::uint32_t last_play_tick_{};
  std::uint32_t pump_deadline_{};
  bool sound_enabled_{true};
  bool initialized_{};
  bool active_{};
  bool host_output_muted_{};
};

}  // namespace simtower
