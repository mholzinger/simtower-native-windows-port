#pragma once

// Win32 GDI font cache for the portable rules in original_font.hpp.
#include "original_font.hpp"

#include <windows.h>

#include <cstdint>

namespace simtower {

// Process-global Win32 adapter for 1208:0a8d, 1208:0ba7, and 1208:0b6a.
// Returned handles are borrowed from the original ten-entry bank and remain
// valid until destroy_original_font_cache; callers must not delete them.
void initialize_original_font_cache() noexcept;
[[nodiscard]] HFONT original_cached_font(
    std::int16_t requested_height) noexcept;
void destroy_original_font_cache() noexcept;

}  // namespace simtower
