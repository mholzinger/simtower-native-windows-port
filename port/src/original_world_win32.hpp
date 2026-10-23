#pragma once

// Win32 presentation for the portable world model in original_world.hpp.
#include "original_world.hpp"

#include <windows.h>

namespace simtower {

// Exact 1020:0f4f allocation boundary after 1020:0e29. The temporary Win16
// movable block is represented by fixed native storage, but LOGPALETTE
// version 0x0300, count 0x0100, entry bytes, and CreatePalette call are
// preserved. The returned object is owned by the caller and must be deleted.
[[nodiscard]] HPALETTE create_original_logical_palette(
    const OriginalWorldPalette& palette) noexcept;

void draw_original_world_raster(HDC destination,
                                const OriginalWorldRaster& raster,
                                int x = 0,
                                int y = 0);

}  // namespace simtower
