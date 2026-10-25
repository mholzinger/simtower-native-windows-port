#pragma once

// Win32 presentation for the portable palette frame in
// original_palette_frame.hpp.
#include "original_palette_frame.hpp"

#include <windows.h>

namespace simtower {

// Exact active/inactive eight-pixel frame painter at 1078:00c6. Active uses
// COLOR_ACTIVECAPTION plus a white close square; inactive uses a white bar and
// a one-pixel black outline around the close square.
void draw_original_palette_frame(HDC destination, int width, bool active);

}  // namespace simtower
