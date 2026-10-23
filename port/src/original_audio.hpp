#pragma once

// Win32 waveOut host for the portable rules in original_audio_rules.hpp.
#include "original_audio_rules.hpp"
#include "original_resources.hpp"
#include "original_wave.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <windows.h>
#include <mmsystem.h>

namespace simtower {

static_assert(kOriginalWaveBeginLoop == WHDR_BEGINLOOP);
static_assert(kOriginalWaveEndLoop == WHDR_ENDLOOP);

// Native replacement for the original 11c8 WAVMIX16 subsystem. Channel
// arbitration, priority gates, the 600-coarse-tick saturation rule, and repeat-count
// semantics are direct translations; waveOut is only the host PCM backend.
class OriginalAudioRuntime {
 public:
  explicit OriginalAudioRuntime(const OriginalResources& resources) : resources_(resources) {}
  ~OriginalAudioRuntime();

  OriginalAudioRuntime(const OriginalAudioRuntime&) = delete;
  OriginalAudioRuntime& operator=(const OriginalAudioRuntime&) = delete;

  [[nodiscard]] bool initialize() noexcept;
  void shutdown() noexcept;
  void activate() noexcept;
  void deactivate() noexcept;
  // Direct host-backend counterparts for WAVEMIXACTIVATE calls made outside
  // the 11c8:0aab/0add wrappers. The waveOut replacement has no session-level
  // activation switch, so these intentionally preserve the original active
  // latch while documenting the distinct lifecycle boundary.
  void activate_mixer_backend() noexcept;
  void deactivate_mixer_backend() noexcept;
  void pump(std::uint32_t tick_count_ms) noexcept;

  void set_sound_enabled(bool enabled) noexcept;
  // Hardware-validation control: retain the exact resource format, buffer
  // length, channel arbitration, repeat flags, and waveOut transaction while
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
    return play_resource(resource_id, repeat_count, priority, GetTickCount());
  }
  [[nodiscard]] bool play_reserved_if_idle(std::int32_t resource_id,
                                           std::uint16_t repeat_count,
                                           std::uint32_t tick_count_ms) noexcept;
  void stop_channel(std::size_t channel, bool force) noexcept;
  void stop_all(bool force) noexcept;
  [[nodiscard]] bool channel_active(std::size_t channel) noexcept;
  [[nodiscard]] const OriginalAudioArbiter& arbiter() const noexcept { return arbiter_; }

 private:
  struct NativeChannel {
    HWAVEOUT output{};
    WAVEHDR header{};
    bool prepared{};
    std::vector<std::byte> muted_samples{};
  };

  void reap_completed() noexcept;
  void release_channel(std::size_t channel) noexcept;
  [[nodiscard]] bool begin_native_playback(std::size_t channel,
                                           const OriginalWaveView& wave,
                                           std::uint16_t repeat_count) noexcept;

  const OriginalResources& resources_;
  OriginalAudioArbiter arbiter_{};
  std::array<NativeChannel, 2> native_channels_{};
  std::array<bool, 3> category_enabled_{true, true, true};
  std::uint32_t last_play_tick_{};
  std::uint32_t pump_deadline_{};
  bool sound_enabled_{true};
  bool initialized_{};
  bool active_{};
  bool host_output_muted_{};
};

}  // namespace simtower
