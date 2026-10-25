#include "original_dib_win32.hpp"

#include <cstddef>
#include <cstdlib>
#include <stdexcept>

namespace simtower {

// The portable layout mirrors in original_dib.hpp must match the Win32
// structures exactly: original_dib_view reinterprets resource bytes as
// OriginalBitmapInfo, and the GDI calls below reinterpret that same address
// as BITMAPINFO.
static_assert(sizeof(OriginalBitmapInfoHeader) == sizeof(BITMAPINFOHEADER));
static_assert(alignof(OriginalBitmapInfoHeader) ==
              alignof(BITMAPINFOHEADER));
static_assert(offsetof(OriginalBitmapInfoHeader, biSize) ==
              offsetof(BITMAPINFOHEADER, biSize));
static_assert(offsetof(OriginalBitmapInfoHeader, biWidth) ==
              offsetof(BITMAPINFOHEADER, biWidth));
static_assert(offsetof(OriginalBitmapInfoHeader, biHeight) ==
              offsetof(BITMAPINFOHEADER, biHeight));
static_assert(offsetof(OriginalBitmapInfoHeader, biPlanes) ==
              offsetof(BITMAPINFOHEADER, biPlanes));
static_assert(offsetof(OriginalBitmapInfoHeader, biBitCount) ==
              offsetof(BITMAPINFOHEADER, biBitCount));
static_assert(offsetof(OriginalBitmapInfoHeader, biCompression) ==
              offsetof(BITMAPINFOHEADER, biCompression));
static_assert(offsetof(OriginalBitmapInfoHeader, biClrUsed) ==
              offsetof(BITMAPINFOHEADER, biClrUsed));
static_assert(sizeof(OriginalRgbQuad) == sizeof(RGBQUAD));
static_assert(offsetof(OriginalRgbQuad, rgbRed) == offsetof(RGBQUAD, rgbRed));
static_assert(sizeof(OriginalBitmapInfo) == sizeof(BITMAPINFO));
static_assert(offsetof(OriginalBitmapInfo, bmiColors) ==
              offsetof(BITMAPINFO, bmiColors));
static_assert(kOriginalDibCompressionRgb == BI_RGB);

void draw_original_dib(HDC destination,
                       std::span<const std::byte> resource,
                       int x,
                       int y) {
  const auto dib = original_dib_view(resource);
  const int height = std::abs(dib.height);
  const int result = SetDIBitsToDevice(
      destination, x, y, static_cast<DWORD>(dib.width),
      static_cast<DWORD>(height), 0, 0, 0, static_cast<UINT>(height),
      dib.pixels.data(), reinterpret_cast<const BITMAPINFO*>(dib.info),
      DIB_RGB_COLORS);
  if (result == 0) {
    throw std::runtime_error("SetDIBitsToDevice failed for original bitmap");
  }
}

void draw_original_dib_region(HDC destination,
                              std::span<const std::byte> resource,
                              int destination_x,
                              int destination_y,
                              int source_left,
                              int source_top,
                              int width,
                              int height) {
  const auto dib = original_dib_view(resource);
  const int dib_height = std::abs(dib.height);
  if (width <= 0 || height <= 0 || source_left < 0 || source_top < 0 ||
      source_left > dib.width - width || source_top > dib_height - height) {
    throw std::runtime_error("Original bitmap source rectangle is invalid");
  }
  const int source_y = dib.height > 0
      ? dib_height - source_top - height
      : source_top;
  const int result = StretchDIBits(
      destination, destination_x, destination_y, width, height,
      source_left, source_y, width, height, dib.pixels.data(),
      reinterpret_cast<const BITMAPINFO*>(dib.info), DIB_RGB_COLORS, SRCCOPY);
  if (result == 0 || result == GDI_ERROR) {
    throw std::runtime_error(
        "StretchDIBits failed for original bitmap region");
  }
}

void draw_original_dib_region_scaled(HDC destination,
                                     std::span<const std::byte> resource,
                                     int destination_x,
                                     int destination_y,
                                     int destination_width,
                                     int destination_height,
                                     int source_left,
                                     int source_top,
                                     int source_width,
                                     int source_height) {
  // Native 32-bit DIB presentation equivalent of 1100:37a9/37d1's direct
  // WinGStretchBlt/WinGBitBlt paths. The source rectangle, destination
  // rectangle, opaque SRCCOPY behavior, clipping, and COLORONCOLOR scaling
  // are retained; only the obsolete WinG surface transport is eliminated.
  const auto dib = original_dib_view(resource);
  const int dib_height = std::abs(dib.height);
  if (destination_width <= 0 || destination_height <= 0 ||
      source_width <= 0 || source_height <= 0 || source_left < 0 ||
      source_top < 0 || source_left > dib.width - source_width ||
      source_top > dib_height - source_height) {
    throw std::runtime_error("Original bitmap scaled source rectangle is invalid");
  }
  const int source_y = dib.height > 0
      ? dib_height - source_top - source_height
      : source_top;
  const int old_mode = SetStretchBltMode(destination, COLORONCOLOR);
  const int result = StretchDIBits(
      destination, destination_x, destination_y,
      destination_width, destination_height,
      source_left, source_y, source_width, source_height,
      dib.pixels.data(), reinterpret_cast<const BITMAPINFO*>(dib.info),
      DIB_RGB_COLORS, SRCCOPY);
  if (old_mode != 0) SetStretchBltMode(destination, old_mode);
  if (result == 0 || result == GDI_ERROR) {
    throw std::runtime_error(
        "StretchDIBits failed for scaled original bitmap region");
  }
}

}  // namespace simtower
