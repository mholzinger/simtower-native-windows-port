#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace simtower {

// Portable layout mirrors of the Win32 BITMAPINFOHEADER, RGBQUAD, and
// BITMAPINFO structures, keeping the Win32 field names. Resource bytes are
// reinterpreted in place exactly as before; original_dib_win32.cpp pins the
// size and field offsets against the Win32 SDK. LONG is 32-bit on Windows, so
// the signed fields use std::int32_t rather than long.
struct OriginalRgbQuad {
  std::uint8_t rgbBlue;
  std::uint8_t rgbGreen;
  std::uint8_t rgbRed;
  std::uint8_t rgbReserved;
};

struct OriginalBitmapInfoHeader {
  std::uint32_t biSize;
  std::int32_t biWidth;
  std::int32_t biHeight;
  std::uint16_t biPlanes;
  std::uint16_t biBitCount;
  std::uint32_t biCompression;
  std::uint32_t biSizeImage;
  std::int32_t biXPelsPerMeter;
  std::int32_t biYPelsPerMeter;
  std::uint32_t biClrUsed;
  std::uint32_t biClrImportant;
};

struct OriginalBitmapInfo {
  OriginalBitmapInfoHeader bmiHeader;
  OriginalRgbQuad bmiColors[1];
};

// BI_RGB.
inline constexpr std::uint32_t kOriginalDibCompressionRgb = 0U;

static_assert(sizeof(OriginalBitmapInfoHeader) == 40);
static_assert(sizeof(OriginalRgbQuad) == 4);

struct OriginalDibView {
  const OriginalBitmapInfo* info = nullptr;
  std::span<const std::byte> pixels{};
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::uint16_t bit_count = 0;
};

struct OriginalOpaqueBlitPlan {
  int source_x{};
  int source_y{};
  int destination_x{};
  int destination_y{};
  int width{};
  int height{};
  bool valid{};

  friend bool operator==(const OriginalOpaqueBlitPlan&,
                         const OriginalOpaqueBlitPlan&) = default;
};

// 1208:069a converts a source RECT and destination POINT into the equal-size
// opaque (-1 marker) 1248:0000/1250:0114 byte-copy path. 1248 clips only the
// destination rectangle and advances the source origin by the clipped-away
// left/top amount; its callers supply an already-valid source rectangle.
[[nodiscard]] constexpr OriginalOpaqueBlitPlan original_opaque_blit_plan(
    int destination_width,
    int destination_height,
    int source_left,
    int source_top,
    int width,
    int height,
    int destination_x,
    int destination_y) noexcept {
  const int clipped_left = destination_x < 0 ? 0 : destination_x;
  const int clipped_top = destination_y < 0 ? 0 : destination_y;
  const int requested_right = destination_x + width;
  const int requested_bottom = destination_y + height;
  const int clipped_right = requested_right < destination_width
                                ? requested_right
                                : destination_width;
  const int clipped_bottom = requested_bottom < destination_height
                                 ? requested_bottom
                                 : destination_height;
  if (width <= 0 || height <= 0 || clipped_right <= clipped_left ||
      clipped_bottom <= clipped_top) {
    return {};
  }
  return {
      source_left + clipped_left - destination_x,
      source_top + clipped_top - destination_y,
      clipped_left,
      clipped_top,
      clipped_right - clipped_left,
      clipped_bottom - clipped_top,
      true,
  };
}

// Exact resource shape consumed by 1030:0043 and 1208:049d/0529: a Win3
// BITMAPINFOHEADER, its palette, then packed DIB scanlines.
[[nodiscard]] OriginalDibView original_dib_view(
    std::span<const std::byte> resource);

}  // namespace simtower
