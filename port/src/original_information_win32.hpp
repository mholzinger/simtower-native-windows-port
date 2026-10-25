#pragma once

// Win32 presentation for the portable Information model.
#include "original_information.hpp"
#include "original_world.hpp"

#include <windows.h>

namespace simtower {

// Exact native presentation boundary for 1100:4439. The original fills DTMP
// item 2 with RGB(0xcc,0xcc,0xcc), applies 1100:4d1d's destination geometry,
// and WinGStretchBlts the retained pre-dialog world snapshot into that area.
void draw_original_facility_preview(
    HDC destination,
    const OriginalWorldRaster& raster,
    const OriginalFacilityPreview& preview,
    const RECT& container) noexcept;

}  // namespace simtower
