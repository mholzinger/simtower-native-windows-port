#pragma once

// Win32 MessageBox host for the portable ALRT plan in original_alert.hpp.
#include "original_alert.hpp"
#include "original_dialog_model.hpp"

#include <windows.h>

#include <array>
#include <string_view>

namespace simtower {

int show_original_alert(HWND owner,
                        const OriginalResources& resources,
                        int resource_id,
                        const std::array<std::string_view, 4>& substitutions);

// One shared message-box request: MessageBeep(-1) first when beep_first is
// set, then MessageBoxA(owner, text, caption, style). Returns the raw
// MessageBox result (IDOK, IDYES, ..., or 0 on failure); callers that need
// the ALRT normalization pass it to original_alert_result(button_mode, ...).
int show_original_message_box(HWND owner,
                              const OriginalMessageBoxRequest& request);

}  // namespace simtower
