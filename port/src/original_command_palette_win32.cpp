#include "original_command_palette_win32.hpp"

#include <stdexcept>

namespace simtower {

// The portable message routing tables in original_command_palette.hpp use
// these literal Win32 message and virtual-key values.
static_assert(WM_CREATE == 0x0001);
static_assert(WM_DESTROY == 0x0002);
static_assert(WM_ACTIVATE == 0x0006);
static_assert(WM_PAINT == 0x000f);
static_assert(WM_ACTIVATEAPP == 0x001c);
static_assert(WM_NCHITTEST == 0x0084);
static_assert(WM_NCACTIVATE == 0x0086);
static_assert(WM_INITDIALOG == 0x0110);
static_assert(WM_MOUSEMOVE == 0x0200);
static_assert(WM_LBUTTONDOWN == 0x0201);
static_assert(WM_LBUTTONUP == 0x0202);
static_assert(WM_RBUTTONDOWN == 0x0204);
static_assert(WM_QUERYNEWPALETTE == 0x030f);
static_assert(WM_PALETTECHANGED == 0x0311);
static_assert(VK_LBUTTON == 0x01);
static_assert(VK_RBUTTON == 0x02);

void draw_original_command_raster(HDC destination,
                                  const OriginalCommandRaster& raster,
                                  int x,
                                  int y) {
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
    throw std::runtime_error("SetDIBitsToDevice failed for original command palette");
  }
}

}  // namespace simtower
