#include "original_palette_frame.hpp"

#include "original_world.hpp"

#include <algorithm>

namespace simtower {

OriginalWorldRaster render_original_palette_frame(int width,
                                                  bool active,
                                                  std::uint32_t caption_rgb) {
  constexpr std::uint32_t kWhite = 0x00FFFFFFU;
  constexpr std::uint32_t kBlack = 0x00000000U;
  OriginalWorldRaster raster{};
  raster.width = std::max(0, width);
  raster.height = kOriginalPaletteFrameHeight;
  raster.pixels.assign(
      static_cast<std::size_t>(raster.width) * kOriginalPaletteFrameHeight,
      active ? caption_rgb : kWhite);
  const auto close = original_palette_frame_close_rect();
  for (int y = close.top; y < close.bottom; ++y) {
    for (int x = close.left; x < std::min(close.right, raster.width); ++x) {
      const bool edge = x == close.left || x == close.right - 1 ||
                        y == close.top || y == close.bottom - 1;
      auto& pixel =
          raster.pixels[static_cast<std::size_t>(y) * raster.width + x];
      if (active) {
        pixel = kWhite;  // FillRect(close, WHITE_BRUSH)
      } else if (edge) {
        pixel = kBlack;  // FrameRect(close, BLACK_BRUSH)
      }
    }
  }
  return raster;
}

}  // namespace simtower
