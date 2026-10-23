#pragma once

// Win32 GDI presentation for the portable Info palette model in
// original_info.hpp.
#include "original_info.hpp"

#include <windows.h>

#include <string_view>

namespace simtower {

// Native GDI presentation of the exact BITMAP/320..323/327 resource artwork
// and the original live-text/clock overlays. The shared palette-window frame
// occupies client rows 0..7; this routine paints the 1120:0215 content path.
void draw_original_info(HDC destination,
                        const OriginalResources& resources,
                        const OriginalTdtDocument* document,
                        std::string_view status = {});

}  // namespace simtower
