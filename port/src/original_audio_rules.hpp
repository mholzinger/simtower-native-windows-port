#pragma once

#include "original_resources.hpp"
#include "original_time.hpp"
#include "original_wave.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace simtower {

// waveOut WAVEHDR loop flags (mmsystem.h WHDR_BEGINLOOP/WHDR_ENDLOOP); the
// Win32 host static_asserts that they match.
inline constexpr std::uint32_t kOriginalWaveBeginLoop = 0x00000004U;
inline constexpr std::uint32_t kOriginalWaveEndLoop = 0x00000008U;

inline constexpr std::uint32_t kOriginalAudioSaturationTicks = 600U;
inline constexpr std::uint32_t kOriginalAudioSaturationNominalMs =
    kOriginalAudioSaturationTicks << 4U;

[[nodiscard]] constexpr bool original_audio_saturation_elapsed(
    std::uint32_t now_tick,
    std::uint32_t last_play_tick) noexcept {
  // 11c8:01c3 calls the 1208:05e6 coarse clock and performs a signed
  // subtraction before comparing the result with 0x258.
  return std::bit_cast<std::int32_t>(now_tick - last_play_tick) >=
         static_cast<std::int32_t>(kOriginalAudioSaturationTicks);
}

// Exact externally visible gate at 11c8:02c0. The original returns before
// querying/closing a channel unless sound is enabled, the mixer is active,
// and its caller supplies a nonzero force argument. Its only callers pass
// channel zero or one; the native bound makes malformed host calls a no-op.
[[nodiscard]] constexpr bool original_audio_stop_channel_allowed(
    bool sound_enabled,
    bool mixer_active,
    bool force,
    std::size_t channel) noexcept {
  return sound_enabled && mixer_active && force && channel < 2U;
}

// Exact observable branch in 11c8:0100. Its preceding 02c0(1,0) call is
// force-gated and therefore a no-op; priority five is submitted only if the
// reserved channel's active word is zero.
[[nodiscard]] constexpr bool original_reserved_audio_should_submit(
    bool reserved_channel_active) noexcept {
  return !reserved_channel_active;
}

struct OriginalAudioChannelState {
  bool active{};
  std::uint16_t priority{};
  std::int32_t resource_id{-1};

  friend bool operator==(const OriginalAudioChannelState&,
                         const OriginalAudioChannelState&) = default;
};

struct OriginalAudioLoopPlan {
  std::uint32_t flags{};
  std::uint32_t total_passes{};

  friend bool operator==(const OriginalAudioLoopPlan&,
                         const OriginalAudioLoopPlan&) = default;
};

// Exact 11c8:0978 repeat setup. WAVMIX stores repeat_count-1 as the number of
// additional passes; waveOut expresses the same request as total passes.
[[nodiscard]] constexpr OriginalAudioLoopPlan original_audio_loop_plan(
    std::uint16_t repeat_count) noexcept {
  return repeat_count == 0U
      ? OriginalAudioLoopPlan{}
      : OriginalAudioLoopPlan{
            kOriginalWaveBeginLoop | kOriginalWaveEndLoop,
            repeat_count};
}

class OriginalAudioArbiter {
 public:
  [[nodiscard]] int select_channel(std::uint16_t requested_priority) const noexcept;
  void start(std::size_t channel,
             std::int32_t resource_id,
             std::uint16_t priority) noexcept;
  void stop(std::size_t channel) noexcept;
  void stop_all() noexcept;

  [[nodiscard]] const std::array<OriginalAudioChannelState, 2>& channels() const noexcept {
    return channels_;
  }

 private:
  std::array<OriginalAudioChannelState, 2> channels_{};
};

[[nodiscard]] bool original_audio_priority_enabled(
    std::uint16_t priority,
    const std::array<bool, 3>& category_enabled) noexcept;

struct OriginalSoundProfileValues {
  bool profile_available{};
  std::uint32_t beep_only{};
  std::uint32_t all_sounds{1U};
  std::uint32_t elevator{1U};
  std::uint32_t events{1U};
  std::uint32_t background{1U};

  friend bool operator==(const OriginalSoundProfileValues&,
                         const OriginalSoundProfileValues&) = default;
};

struct OriginalSoundProfileState {
  bool beep_only{};
  bool sound_enabled{};
  std::array<bool, 3> category_enabled{};

  friend bool operator==(const OriginalSoundProfileState&,
                         const OriginalSoundProfileState&) = default;
};

// Exact non-I/O state transition at 1128:0443-0535. DS:02a8 enters as the
// WAVMIX capability/initialization latch. A missing SIMTOWER.INI clears every
// sound word; BeepOnly disables the master and categories only when it equals
// one; otherwise the four remaining profile integers are tested as nonzero.
[[nodiscard]] OriginalSoundProfileState original_sound_profile_state(
    bool wavemix_available,
    const OriginalSoundProfileValues& values) noexcept;

struct OriginalFacilitySoundRecord {
  std::int8_t type{};          // original 18-byte facility record +0x0a
  std::int8_t phase{};         // +0x0b
  std::uint16_t linked_index{};  // +0x0c
};

struct OriginalLinkedSoundRecord {
  std::uint8_t status{};    // original linked 18-byte record +2
  std::uint8_t occupied{};  // +9
};

struct OriginalServiceSoundRecord {
  std::int8_t kind{};     // original 12-byte service record +0
  std::int8_t variant{};  // +1
};

struct OriginalAmbientProbe {
  std::int16_t row{};
  std::int16_t column{};
  std::int16_t coordinate{};

  friend bool operator==(const OriginalAmbientProbe&, const OriginalAmbientProbe&) = default;
};

// Direct translations of 11c8:0426, 05e8, and 06b6. These routines are pure
// so the simulation can preserve the original audio decisions independently
// of the native PCM backend.
[[nodiscard]] std::int32_t original_wave_resource_for_sound_event(
    std::int16_t event,
    std::int16_t random_value) noexcept;
[[nodiscard]] std::optional<std::int32_t> original_contextual_wave_resource(
    std::int16_t event,
    bool allow_contextual,
    bool background_override,
    std::int32_t game_time,
    std::int8_t day_phase) noexcept;
[[nodiscard]] bool original_should_attempt_ambient_sound(
    bool sound_enabled,
    std::uint8_t world_mode_flags,
    std::int16_t random_value) noexcept;
[[nodiscard]] std::optional<OriginalAmbientProbe> original_ambient_probe(
    std::uint16_t probe_index,
    std::int16_t tower_width,
    std::int16_t tower_height,
    std::int16_t ground_coordinate) noexcept;
[[nodiscard]] std::int32_t original_sound_event_for_facility(
    std::int16_t coordinate,
    const OriginalFacilitySoundRecord* facility,
    std::span<const OriginalLinkedSoundRecord> linked_records,
    std::span<const OriginalServiceSoundRecord> service_records) noexcept;

}  // namespace simtower
