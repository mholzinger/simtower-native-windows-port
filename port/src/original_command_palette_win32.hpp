#pragma once

// Win32 GDI presentation for the portable command palette rasters in
// original_command_palette.hpp.
#include "original_command_palette.hpp"

#include <windows.h>

namespace simtower {

void draw_original_command_raster(HDC destination,
                                  const OriginalCommandRaster& raster,
                                  int x = 0,
                                  int y = kOriginalCommandPaintOffsetY);

}  // namespace simtower
