#pragma once

// Win32 DLGTEMPLATE emission for the portable dialog model in
// original_dialog.hpp.
#include "original_dialog.hpp"

#include <windows.h>

#include <cstddef>
#include <vector>

namespace simtower {

// Converts the Win16 ANSI template to a Win32 Unicode DLGTEMPLATE while
// preserving styles, coordinates, identifiers, classes, text, and font.
[[nodiscard]] std::vector<std::byte> build_native_dialog_template(
    const OriginalDialog& dialog);

}  // namespace simtower
