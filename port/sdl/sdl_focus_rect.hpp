#pragma once

// The one portable DrawFocusRect of the SDL host: the pixels USER's
// dotted focus rectangle inverts, each visited once. The dialog widgets
// draw them in the XOR of the background they know (OriginalDrawContext
// has no raster operation); the panels XOR the raster itself.
//
// TEMP: every other perimeter pixel ((x + y) even), until the Win16
// dotted-pen pattern is verified against a Win3.1 reference.

namespace simtower::sdl {

// Visits the focus pixels of the rectangle [left, right) x [top, bottom).
template <typename Visit>
void for_each_focus_rect_pixel(int left, int top, int right, int bottom,
                               Visit&& visit) {
  if (right <= left || bottom <= top) return;
  const auto dot = [&](int px, int py) {
    if (((px + py) & 1) == 0) visit(px, py);
  };
  for (int px = left; px < right; ++px) {
    dot(px, top);
    if (bottom - top > 1) dot(px, bottom - 1);
  }
  for (int py = top + 1; py < bottom - 1; ++py) {
    dot(left, py);
    if (right - left > 1) dot(right - 1, py);
  }
}

}  // namespace simtower::sdl
