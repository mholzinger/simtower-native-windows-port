#pragma once

// Win32 GDI presentation for the portable DIB views in original_dib.hpp.
#include "original_dib.hpp"

#include <windows.h>

#include <cstddef>
#include <span>

namespace simtower {

// Native SetDIBitsToDevice equivalent of original routine 1208:0529.
void draw_original_dib(HDC destination,
                       std::span<const std::byte> resource,
                       int x,
                       int y);

// Source rectangle is expressed in the original bitmap's top-left client
// coordinates. This is the native partial-blit equivalent needed by the
// Finance dialog's BITMAP/500 -> BITMAP/501 pressed-button transition.
void draw_original_dib_region(HDC destination,
                              std::span<const std::byte> resource,
                              int destination_x,
                              int destination_y,
                              int source_left,
                              int source_top,
                              int width,
                              int height);

// WinGStretchBlt/WinGBitBlt-equivalent source-region path used by the
// original Person Information portrait painter at 1100:364a through its
// direct 1100:37a9/37d1 calls. Source coordinates remain in top-left DIB
// space while destination dimensions may differ.
void draw_original_dib_region_scaled(HDC destination,
                                     std::span<const std::byte> resource,
                                     int destination_x,
                                     int destination_y,
                                     int destination_width,
                                     int destination_height,
                                     int source_left,
                                     int source_top,
                                     int source_width,
                                     int source_height);

}  // namespace simtower
