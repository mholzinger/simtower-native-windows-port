#pragma once

// TrueType text for OriginalCanvasDrawContext, through stb_truetype. It
// stands in for the Win32 font bank of original_font_win32.cpp (Arial, else
// MS Sans Serif) with a metric-compatible font, by default the bundled
// Liberation Sans Regular (port/third_party/liberation-fonts).
//
// Rendering rules, chosen to match GDI's text on this palette:
// - Size: a GDI font made with lfHeight = -h has an em of h pixels, so the
//   scale is h / unitsPerEm (stbtt_ScaleForMappingEmToPixels).
// - Advance: each glyph's advance is rounded to whole pixels and summed,
//   with no kerning (TextOutA applies none). Liberation Sans has Arial's
//   advance widths, so this reproduces GDI's extents wherever GDI's hinted
//   advance equals the rounded linear one.
// - Pixels: one bit per pixel. stb's 8-bit coverage is thresholded at half
//   (>= 128), so text is drawn in the exact text color with no blending,
//   like Windows 3.1 (which had no font smoothing). A dropout pass then
//   turns on a pixel of 88..127 coverage where a thin stroke would
//   otherwise vanish (the 't' and 'f' crossbars and the tops of 'm' and
//   'n' at 12 pixels, commas at 13, and bowls such as 'o' and 'a' at 11
//   whose stroke shifts a column between rows); see
//   original_canvas_font.cpp. stb has
//   no hinter, so stems can still be uneven at 12-14 pixels; this is the
//   accepted trade-off.

#include "original_font.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace simtower {

struct OriginalCanvasGlyph {
  // Pen advance in whole pixels.
  int advance{};
  // Bitmap placement relative to the pen position on the baseline.
  int left{};
  int top{};
  int width{};
  int height{};
  // width*height, row-major, 1 = text pixel.
  std::vector<std::uint8_t> mask{};
};

struct OriginalCanvasFontMetrics {
  // Rounded hhea ascent/descent at this size (descent is positive).
  int ascent{};
  int descent{};
};

class OriginalCanvasFont {
 public:
  // ttf must stay alive and unchanged for the font's lifetime. Throws
  // std::runtime_error if stb_truetype cannot parse it.
  explicit OriginalCanvasFont(std::span<const std::byte> ttf);
  ~OriginalCanvasFont();
  OriginalCanvasFont(const OriginalCanvasFont&) = delete;
  OriginalCanvasFont& operator=(const OriginalCanvasFont&) = delete;

  // The bundled Liberation Sans Regular, built into the canvas library.
  [[nodiscard]] static OriginalCanvasFont& bundled();

  // One Windows-1252 byte at an em of pixel_height pixels. Cached.
  [[nodiscard]] const OriginalCanvasGlyph& glyph(int pixel_height,
                                                 unsigned char ansi);
  [[nodiscard]] OriginalCanvasFontMetrics metrics(int pixel_height) const;
  // Sum of the rounded advances (GetTextExtentPoint32A's cx).
  [[nodiscard]] int extent(int pixel_height, std::string_view ansi);

  // The 1208:0ba7 bank rules (original_font_cache_decision) applied to this
  // font's own heights, seeded with the nine-pixel entry as 1208:0a8d does.
  // Returns the height to select, or 0 when the full bank selects nothing.
  [[nodiscard]] std::int16_t bank_select(std::int16_t requested_height);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::map<int, std::array<std::unique_ptr<OriginalCanvasGlyph>, 256>>
      glyphs_{};
  std::vector<std::int16_t> bank_{kOriginalMinimumFontPixelHeight};
};

// Windows-1252 to Unicode (the ANSI code page of an English Windows).
[[nodiscard]] std::uint32_t original_cp1252_to_unicode(
    unsigned char ansi) noexcept;

// UTF-8 text (typed text, e.g. SDL_EVENT_TEXT_INPUT) as the Windows-1252
// bytes WM_CHAR would carry: the reverse of original_cp1252_to_unicode for
// 0x20..0xFF except 0x7F. Code points the code page lacks (and controls)
// are dropped, as are invalid or truncated sequences, which never read past
// the end of the text.
[[nodiscard]] std::string original_utf8_to_cp1252(std::string_view utf8);

// The bundled TTF bytes (generated from the vendored file at build time).
[[nodiscard]] std::span<const std::byte> original_canvas_bundled_font_data();

}  // namespace simtower
