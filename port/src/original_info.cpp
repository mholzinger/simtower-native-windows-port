#include "original_info.hpp"

#include "original_tables.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>

namespace simtower {
namespace {

std::int32_t scaled_balance(std::int32_t balance) noexcept {
  // 1118:01c3 uses a 32-bit IMUL by 100 and keeps the low dword.
  const std::uint32_t wrapped =
      static_cast<std::uint32_t>(balance) * std::uint32_t{100};
  return std::bit_cast<std::int32_t>(wrapped);
}

std::string strl(const OriginalResources& resources, std::uint16_t index) {
  return original_strl_entry(resources.find("STRL", 713), index);
}

bool set_status(const OriginalResources& resources,
                OriginalInfoStatusState& state,
                std::int32_t resource_id,
                std::uint16_t string_index,
                std::int16_t priority,
                std::int16_t maximum_replaced_priority,
                std::uint32_t now_tick) {
  if (state.priority > maximum_replaced_priority) return false;
  if (string_index == 0U) {
    state.text.clear();
    state.started_tick = 0U;
    state.priority = 0;
    return true;
  }
  state.text = original_strl_entry(resources.find("STRL", resource_id),
                                   string_index);
  state.started_tick = now_tick;
  state.priority = priority;
  return true;
}

}  // namespace

bool set_original_info_construction_status(
    const OriginalResources& resources,
    OriginalInfoStatusState& state,
    std::uint16_t string_index,
    std::uint32_t now_tick) {
  // 1118:0933 is unconditional and loads STRL/1003 at priority two.
  return set_status(resources, state, 1003, string_index, 2, 0x7fff,
                    now_tick);
}

bool set_original_info_notification_status(
    const OriginalResources& resources,
    OriginalInfoStatusState& state,
    std::uint16_t string_index,
    std::uint32_t now_tick) {
  // 1118:09be is likewise unconditional, but uses STRL/1010.
  return set_status(resources, state, 1010, string_index, 2, 0x7fff,
                    now_tick);
}

bool set_original_info_income_status(const OriginalResources& resources,
                                     OriginalInfoStatusState& state,
                                     std::uint16_t string_index,
                                     std::uint32_t now_tick) {
  // 1118:0a49 returns without drawing while a positive-priority message is
  // live. Its STRL/1007 messages retain priority zero.
  return set_status(resources, state, 1007, string_index, 0, 0, now_tick);
}

bool set_original_info_command_status(const OriginalResources& resources,
                                      OriginalInfoStatusState& state,
                                      std::uint16_t string_index,
                                      std::uint32_t now_tick) {
  // 1118:0ad5 can replace income/command text but not priority-two text.
  return set_status(resources, state, 1009, string_index, 1, 1, now_tick);
}

bool expire_original_info_status(OriginalInfoStatusState& state,
                                 std::uint32_t now_tick) noexcept {
  if (state.started_tick == 0U) return false;
  // 1118:090c performs wrapping subtraction, then 1000:39ea returns the
  // signed dword's magnitude before the signed comparison with 300.
  const std::uint32_t magnitude =
      original_tick_magnitude_delta(now_tick, state.started_tick);
  if (std::bit_cast<std::int32_t>(magnitude) <= 300) return false;
  state.text.clear();
  state.started_tick = 0U;
  state.priority = 0;
  return true;
}

std::string original_info_ordinal(const OriginalResources& resources,
                                  std::int32_t value) {
  std::uint16_t suffix = 14U;
  if (value < 10 || value >= 20) {
    const std::int32_t remainder = value % 10;
    if (remainder >= 1 && remainder <= 3) {
      suffix = static_cast<std::uint16_t>(10 + remainder);
    }
  }
  return std::to_string(value) + strl(resources, suffix);
}

OriginalInfoClockTime original_info_clock_time(
    std::uint16_t frame_time) noexcept {
  const int phase = frame_time / 400U;
  const int delta = static_cast<int>(frame_time) - phase * 400;
  OriginalInfoClockTime result{};

  switch (phase) {
    case 0: {
      const int scaled = delta * 5;
      result.hour = scaled / 400 + 7;
      result.minute = (scaled % 400) * 60 / 400;
      break;
    }
    case 1:
      result.minute = delta * 60 / 800;
      break;
    case 2:
      result.minute = delta * 60 / 800 + 30;
      break;
    case 3:
    case 4:
    case 5: {
      const int scaled = delta * 4;
      result.hour = scaled / 400 + (phase == 3 ? 1 : phase == 4 ? 5 : 9);
      result.minute = (scaled % 400) * 60 / 400;
      if (result.hour >= 12) result.hour -= 12;
      break;
    }
    case 6: {
      const int scaled = delta * 12;
      result.hour = scaled / 400 + 1;
      result.minute = (scaled % 400) * 60 / 400;
      if (result.hour >= 12) result.hour -= 12;
      break;
    }
    default:
      break;
  }
  result.minute = std::min(result.minute, 59);
  return result;
}

OriginalInfoPoint original_info_clock_point(int index, int radius) noexcept {
  if (index < 0 || index >= 60 || radius < 0) return {};
  // 1200:0050 stores (index * 314) as a signed word, divides it by the
  // single-precision 3000.0 constant, and calls the runtime sin/cos helpers.
  // 1000:0f1c then FRNDINTs with the x87 round-down mode before 1000:1049
  // converts to an integer.
  const double angle = static_cast<double>(index * 314) / 3000.0;
  return {static_cast<int>(std::floor(std::sin(angle) * radius)),
          -static_cast<int>(std::floor(std::cos(angle) * radius))};
}

OriginalInfoContent build_original_info_content(
    const OriginalResources& resources,
    const OriginalTdtDocument* document,
    std::string_view status) {
  const std::uint16_t rating = document ? document->header.rating : 1U;
  const std::int32_t balance = document ? document->header.balance : 20000;
  const std::uint16_t frame_time =
      document ? document->header.frame_time : 0x09e5U;
  const std::int32_t current_day =
      document ? document->header.current_day : 0;
  const std::int32_t population =
      document ? document->post_elevator.finance.total_population : 0;

  OriginalInfoContent content{};
  content.rating = rating;
  // 1208:0004 is the executable's thin _wsprintf wrapper. Native translated
  // callers use bounded std::to_string/std::snprintf equivalents.
  content.balance = strl(resources, 15U) +
                    std::to_string(scaled_balance(balance));
  content.population = std::to_string(population);
  content.status.assign(status);

  const std::int32_t day_in_cycle = current_day % 3;
  if (day_in_cycle < 2) {
    content.date_dark = original_info_ordinal(resources, day_in_cycle + 1) +
                        strl(resources, 9U) + strl(resources, 8U);
  } else {
    content.date_red = strl(resources, 10U);
    content.date_dark = strl(resources, 8U);
  }
  content.date_dark += std::to_string((current_day / 3) % 4 + 1);
  content.date_dark += strl(resources, 4U);
  content.date_dark += strl(resources, 8U);
  content.date_dark += original_info_ordinal(resources, current_day / 12 + 1);
  content.date_dark += strl(resources, 1U);

  content.clock = original_info_clock_time(frame_time);
  content.minute_hand = original_info_clock_point(content.clock.minute, 15);
  const int hour_index = content.clock.hour * 5 + content.clock.minute / 12;
  content.hour_hand = original_info_clock_point(hour_index, 9);
  return content;
}

}  // namespace simtower
