#include "original_audio_rules.hpp"

#include <limits>

namespace simtower {

int OriginalAudioArbiter::select_channel(std::uint16_t requested_priority) const noexcept {
  // 11c8:0597 reserves channel 1 for priority 5 without examining its state.
  if (requested_priority == 5U) {
    return 1;
  }
  for (std::size_t channel = 0; channel < channels_.size(); ++channel) {
    if (!channels_[channel].active) {
      return static_cast<int>(channel);
    }
  }
  for (std::size_t channel = 0; channel < channels_.size(); ++channel) {
    if (channels_[channel].priority < requested_priority) {
      return static_cast<int>(channel);
    }
  }
  return -1;
}

void OriginalAudioArbiter::start(std::size_t channel,
                                 std::int32_t resource_id,
                                 std::uint16_t priority) noexcept {
  if (channel >= channels_.size()) {
    return;
  }
  channels_[channel] = {true, priority, resource_id};
}

void OriginalAudioArbiter::stop(std::size_t channel) noexcept {
  if (channel >= channels_.size()) {
    return;
  }
  channels_[channel] = {};
  channels_[channel].resource_id = -1;
}

void OriginalAudioArbiter::stop_all() noexcept {
  for (std::size_t channel = 0; channel < channels_.size(); ++channel) {
    stop(channel);
  }
}

bool original_audio_priority_enabled(
    std::uint16_t priority,
    const std::array<bool, 3>& category_enabled) noexcept {
  // 11c8:0188-01b1: priority 0, priority 1, and every other priority use
  // three independent configuration words.
  if (priority == 0U) {
    return category_enabled[0];
  }
  if (priority == 1U) {
    return category_enabled[1];
  }
  return category_enabled[2];
}

OriginalSoundProfileState original_sound_profile_state(
    bool wavemix_available,
    const OriginalSoundProfileValues& values) noexcept {
  OriginalSoundProfileState state{};
  if (!values.profile_available) {
    // 1128:0517-0535: failure to locate either SIMTOWER.INI candidate zeros
    // BeepOnly, AllSounds, and all three category words.
    return state;
  }

  state.beep_only = values.beep_only == 1U;
  if (state.beep_only || !wavemix_available) {
    // 1128:046f-0495: BeepOnly uses an equality test, while a failed WAVMIX
    // startup leaves the initially-zero category words untouched.
    return state;
  }

  state.sound_enabled = values.all_sounds != 0U;
  state.category_enabled = {
      values.elevator != 0U,
      values.events != 0U,
      values.background != 0U,
  };
  return state;
}

namespace {

std::uint16_t original_magnitude(std::int16_t value) noexcept {
  // Mirrors cwd/xor/sub in the original. The -32768 edge remains 0x8000.
  const std::uint16_t bits = static_cast<std::uint16_t>(value);
  return value < 0 ? static_cast<std::uint16_t>(0U - bits) : bits;
}

bool phase_has_active_substep(std::int8_t phase, std::int8_t limit) noexcept {
  return phase < limit && (static_cast<std::uint8_t>(phase) & 7U) != 0U;
}

}  // namespace

std::int32_t original_wave_resource_for_sound_event(
    std::int16_t event,
    std::int16_t random_value) noexcept {
  const std::uint16_t magnitude = original_magnitude(random_value);
  switch (event) {
    case 3:
    case 4:
    case 5:
      return 1577;
    case 6:
      return 1384 + static_cast<std::int32_t>(magnitude % 2U);
    case 7:
      return 1448;
    case 9:
      return magnitude % 10U == 0U ? 1576 : 1577;
    case 10:
    case 12:
      return magnitude % 2U == 0U ? 1385 : 1640;
    case 11:
      return 1704 + static_cast<std::int32_t>(magnitude % 2U);
    case 29:
    case 30:
      return 2856;
    default:
      // Events 8 and 13..28, as well as values outside the jump-table range,
      // deliberately pass through unchanged at 11c8:04c4.
      return event;
  }
}

std::optional<std::int32_t> original_contextual_wave_resource(
    std::int16_t event,
    bool allow_contextual,
    bool background_override,
    std::int32_t game_time,
    std::int8_t day_phase) noexcept {
  if (event >= 0) {
    return original_wave_resource_for_sound_event(event, 0);
  }
  if (!allow_contextual || event != -1) {
    return std::nullopt;
  }
  if (background_override) {
    return 10002;
  }
  const std::int32_t phase = (game_time / 3) % 4;
  if (phase == 2 && day_phase < 4) {
    return 10012;
  }
  if (phase == 3 && day_phase >= 4) {
    return 10011;
  }
  return std::nullopt;
}

bool original_should_attempt_ambient_sound(bool sound_enabled,
                                           std::uint8_t world_mode_flags,
                                           std::int16_t random_value) noexcept {
  // 11c8:03ab suppresses ambient probes for mode bits 0 and 3 and then takes
  // exactly one in every sixteen values from the game's PRNG.
  return sound_enabled && (world_mode_flags & 0x09U) == 0U &&
         original_magnitude(random_value) % 16U == 0U;
}

std::optional<OriginalAmbientProbe> original_ambient_probe(
    std::uint16_t probe_index,
    std::int16_t tower_width,
    std::int16_t tower_height,
    std::int16_t ground_coordinate) noexcept {
  if (probe_index > 5U) {
    return std::nullopt;
  }
  const std::int16_t highest_row = static_cast<std::int16_t>(tower_height - 1);
  const std::int16_t row = probe_index < 3U
      ? static_cast<std::int16_t>(highest_row >> 1)
      : static_cast<std::int16_t>(highest_row - (highest_row >> 2));
  std::int16_t column{};
  switch (probe_index % 3U) {
    case 0:
      column = static_cast<std::int16_t>(tower_width >> 2);
      break;
    case 1:
      column = static_cast<std::int16_t>(tower_width >> 1);
      break;
    default:
      column = static_cast<std::int16_t>(tower_width - (tower_width >> 2));
      break;
  }
  return OriginalAmbientProbe{
      row, column, static_cast<std::int16_t>(ground_coordinate - row)};
}

std::int32_t original_sound_event_for_facility(
    std::int16_t coordinate,
    const OriginalFacilitySoundRecord* facility,
    std::span<const OriginalLinkedSoundRecord> linked_records,
    std::span<const OriginalServiceSoundRecord> service_records) noexcept {
  if (!facility) {
    return coordinate < 10 ? -2 : -1;
  }

  const auto type = static_cast<std::int32_t>(facility->type);
  switch (type) {
    case 3:
    case 4:
    case 5:
      return phase_has_active_substep(facility->phase, 16) ? type : -2;
    case 7:
      return phase_has_active_substep(facility->phase, 8) ? type : -2;
    case 9:
      return phase_has_active_substep(facility->phase, 16) ? type : -2;
    case 11:
      return facility->phase >= 2 ? type : -2;
    case 6:
    case 10:
    case 12: {
      if (facility->linked_index >= linked_records.size()) {
        return -2;
      }
      const auto& linked = linked_records[facility->linked_index];
      return linked.status != 0xffU && linked.status != 3U && linked.occupied != 0U
          ? type
          : -2;
    }
    case 29:
    case 30: {
      if (facility->linked_index >= service_records.size()) {
        return -2;
      }
      return service_records[facility->linked_index].kind >= 2 ? type : -2;
    }
    case 18:
    case 19:
    case 34:
    case 35: {
      if (facility->linked_index >= service_records.size()) {
        return -2;
      }
      const auto& service = service_records[facility->linked_index];
      return service.kind == 3
          ? 9001 + static_cast<std::int32_t>(service.variant)
          : -2;
    }
    default:
      return -2;
  }
}

}  // namespace simtower
