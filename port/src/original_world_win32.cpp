#include "original_world_win32.hpp"

#include <stdexcept>

namespace simtower {

HPALETTE create_original_logical_palette(
    const OriginalWorldPalette& palette) noexcept {
  struct OriginalNativeLogPalette {
    WORD version;
    WORD count;
    std::array<PALETTEENTRY, 256> entries;
  } native_palette{0x0300U, 0x0100U, {}};

  const auto original_entries = original_logical_palette_entries(palette);
  for (std::size_t index = 0U; index < original_entries.size(); ++index) {
    native_palette.entries[index] = {
        original_entries[index].red,
        original_entries[index].green,
        original_entries[index].blue,
        original_entries[index].flags,
    };
  }
  return CreatePalette(
      reinterpret_cast<const LOGPALETTE*>(&native_palette));
}

void draw_original_world_raster(HDC destination,
                                const OriginalWorldRaster& raster,
                                int x,
                                int y) {
  // 1208:09cf copied every logical palette entry into the WinG DIB color
  // table in B,G,R,0 order before presentation. Composition has already
  // resolved those same entries into 0x00RRGGBB values; little-endian memory
  // supplies the identical B,G,R,0 byte order to SetDIBitsToDevice here.
  if (!destination || raster.width <= 0 || raster.height <= 0 ||
      raster.pixels.empty()) {
    return;
  }
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = raster.width;
  info.bmiHeader.biHeight = -raster.height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  const int result = SetDIBitsToDevice(
      destination, x, y, static_cast<DWORD>(raster.width),
      static_cast<DWORD>(raster.height), 0, 0, 0,
      static_cast<UINT>(raster.height), raster.pixels.data(), &info,
      DIB_RGB_COLORS);
  if (result == 0) {
    throw std::runtime_error("SetDIBitsToDevice failed for original world");
  }
}

}  // namespace simtower
