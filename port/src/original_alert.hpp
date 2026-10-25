#pragma once

#include "original_resources.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace simtower {

// MessageBox style bits (Win32 UINT) used by the ALRT presentation plan.
using OriginalAlertStyle = std::uint32_t;

// Win32 MessageBox style and result values, kept portable here and pinned
// against the MB_* and ID* macros in original_alert_win32.cpp.
inline constexpr OriginalAlertStyle kOriginalAlertIconExclamation = 0x30U;
inline constexpr OriginalAlertStyle kOriginalAlertSystemModal = 0x1000U;
inline constexpr OriginalAlertStyle kOriginalAlertButtonsOk = 0U;
inline constexpr OriginalAlertStyle kOriginalAlertButtonsOkCancel = 1U;
inline constexpr OriginalAlertStyle kOriginalAlertButtonsAbortRetryIgnore =
    2U;
inline constexpr OriginalAlertStyle kOriginalAlertButtonsYesNoCancel = 3U;
inline constexpr OriginalAlertStyle kOriginalAlertButtonsYesNo = 4U;
inline constexpr int kOriginalAlertResultOk = 1;
inline constexpr int kOriginalAlertResultCancel = 2;
inline constexpr int kOriginalAlertResultAbort = 3;
inline constexpr int kOriginalAlertResultYes = 6;
inline constexpr int kOriginalAlertResultNo = 7;

struct OriginalAlert {
  std::uint16_t button_mode = 0;
  std::uint16_t preserved_word = 0;
  std::string message_template;
};

struct OriginalAlertPresentation {
  std::uint16_t button_mode{};
  std::string message{};
  OriginalAlertStyle style{};

  friend bool operator==(const OriginalAlertPresentation&,
                         const OriginalAlertPresentation&) = default;
};

// ALRT layout and consumers recovered at 1208:0133/017a/0274/0369.
[[nodiscard]] OriginalAlert parse_original_alert(
    std::span<const std::byte> resource);

[[nodiscard]] std::string format_original_alert(
    std::string_view message_template,
    const std::array<std::string_view, 4>& substitutions);

[[nodiscard]] OriginalAlertStyle original_alert_message_box_style(
    std::uint16_t button_mode);

[[nodiscard]] int original_alert_result(std::uint16_t button_mode,
                                        int message_box_result);

// Pure resource/format/style half of 1208:0133. The native host passes this
// exact plan to MessageBox; tests can verify it without opening a window.
[[nodiscard]] OriginalAlertPresentation prepare_original_alert(
    const OriginalResources& resources,
    int resource_id,
    const std::array<std::string_view, 4>& substitutions);

}  // namespace simtower
