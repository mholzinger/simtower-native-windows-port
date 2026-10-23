#include "original_information_win32.hpp"

namespace simtower {

void draw_original_facility_preview(
    HDC destination,
    const OriginalWorldRaster& raster,
    const OriginalFacilityPreview& preview,
    const RECT& container) noexcept {
  if (!destination || !preview.valid()) return;

  // 1100:446d-4493 creates a 0xcccc/0xcccc/0xcccc brush, fills the complete
  // DTMP item-2 rectangle, and destroys the brush before presenting pixels.
  HBRUSH backing = CreateSolidBrush(RGB(0xcc, 0xcc, 0xcc));
  if (backing) {
    FillRect(destination, &container, backing);
    DeleteObject(backing);
  }

  // 1100:03ac rendered the temporary WinG backing once before the modal
  // dialog. 1100:4439 always stretches that retained snapshot on repaint.
  if (raster.width <= 0 || raster.height <= 0 || raster.pixels.empty()) return;
  const auto draw = original_facility_preview_destination(
      preview, container.left, container.top, container.right,
      container.bottom);
  if (draw.width() <= 0 || draw.height() <= 0) return;

  BITMAPINFO bitmap{};
  bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap.bmiHeader.biWidth = raster.width;
  bitmap.bmiHeader.biHeight = -raster.height;
  bitmap.bmiHeader.biPlanes = 1U;
  bitmap.bmiHeader.biBitCount = 32U;
  bitmap.bmiHeader.biCompression = BI_RGB;
  const int previous_mode = SetStretchBltMode(destination, COLORONCOLOR);
  (void)StretchDIBits(
      destination, draw.left, draw.top, draw.width(), draw.height(),
      0, 0, preview.width, preview.height, raster.pixels.data(), &bitmap,
      DIB_RGB_COLORS, SRCCOPY);
  SetStretchBltMode(destination, previous_mode);
}

}  // namespace simtower
