#pragma once

// Win32 MessageBox host for the portable ALRT plan in original_alert.hpp.
#include "original_alert.hpp"

#include <windows.h>

#include <array>
#include <string_view>

namespace simtower {

int show_original_alert(HWND owner,
                        const OriginalResources& resources,
                        int resource_id,
                        const std::array<std::string_view, 4>& substitutions);

}  // namespace simtower
