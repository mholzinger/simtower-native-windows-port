#pragma once

// Portable drawing interface for the original GDI painters. Each method
// stands for exactly one Win32 call (or one fixed group of calls, named in
// its comment), so a painter written against it issues the same GDI call
// sequence through OriginalGdiDrawContext (original_draw_context_win32.hpp)
// as it did when it called GDI directly. OriginalCanvasDrawContext
// (original_draw_canvas.hpp) renders the same calls into a raster for hosts
// without GDI. No windows.h here.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace simtower {

// A GDI RECT: right and bottom are exclusive.
struct OriginalRect {
  int left{};
  int top{};
  int right{};
  int bottom{};

  friend bool operator==(const OriginalRect&, const OriginalRect&) = default;
};

// SetTextAlign flags, with the Win32 values (TA_LEFT and TA_TOP are 0).
inline constexpr std::uint16_t kOriginalTaUpdateCp = 1U;   // TA_UPDATECP
inline constexpr std::uint16_t kOriginalTaRight = 2U;      // TA_RIGHT
inline constexpr std::uint16_t kOriginalTaCenter = 6U;     // TA_CENTER
inline constexpr std::uint16_t kOriginalTaBottom = 8U;     // TA_BOTTOM
inline constexpr std::uint16_t kOriginalTaBaseline = 24U;  // TA_BASELINE

// 0x00RRGGBB, as OriginalWorldRaster stores it. The GDI context converts it
// to a COLORREF.
using OriginalRgb = std::uint32_t;

class OriginalDrawContext {
 public:
  virtual ~OriginalDrawContext() = default;

  // SaveDC. Returns the saved level (0 on failure).
  virtual int save() = 0;
  // RestoreDC(saved): clip, current position, text alignment, text color,
  // background mode, and the selected font and pen.
  virtual void restore(int saved) = 0;
  // CreateSolidBrush, FillRect, DeleteObject. Throws if the brush fails.
  virtual void fill_rect(const OriginalRect& rect, OriginalRgb rgb) = 0;
  // IntersectClipRect.
  virtual void intersect_clip(const OriginalRect& rect) = 0;
  // draw_original_dib: SetDIBitsToDevice of the whole resource at (x,y)
  // with DIB_RGB_COLORS. Throws if GDI reports failure.
  virtual void draw_dib(std::span<const std::byte> resource,
                        int x,
                        int y) = 0;
  // CreatePen(PS_SOLID, 1, rgb) (throws if it fails), then SelectObject,
  // keeping the previous pen.
  virtual void push_solid_pen(OriginalRgb rgb) = 0;
  // SelectObject(previous pen), then DeleteObject(pen).
  virtual void pop_pen() = 0;
  // MoveToEx(x, y, nullptr).
  virtual void move_to(int x, int y) = 0;
  // LineTo(x, y) with the selected pen; the end pixel is not drawn.
  virtual void line_to(int x, int y) = 0;
  // original_cached_font(pixel_height) and, when the bank returns a font,
  // SelectObject keeping the previous font.
  virtual void push_font(std::int16_t pixel_height) = 0;
  // SelectObject(previous font) if push_font selected one.
  virtual void pop_font() = 0;
  // SetTextColor.
  virtual void set_text_color(OriginalRgb rgb) = 0;
  // SetBkMode(TRANSPARENT).
  virtual void set_bk_transparent() = 0;
  // SetTextAlign(flags).
  virtual void set_text_align(std::uint16_t flags) = 0;
  // TextOutA(0, 0, text). The coordinates are the ones the painters pass;
  // they only use TA_UPDATECP, where GDI ignores them.
  virtual void text_out(std::string_view text) = 0;
  // DrawTextA(text, &copy_of_rect, DT_SINGLELINE).
  virtual void draw_text_single_line(std::string_view text,
                                     const OriginalRect& rect) = 0;
  // GetTextExtentPoint32A(text).cx.
  [[nodiscard]] virtual int text_extent_cx(std::string_view text) = 0;
};

// SaveDC on construction, RestoreDC on destruction when SaveDC succeeded.
class OriginalSavedDrawState {
 public:
  explicit OriginalSavedDrawState(OriginalDrawContext& context)
      : context_(context), state_(context.save()) {}
  ~OriginalSavedDrawState() {
    if (state_ != 0) context_.restore(state_);
  }
  OriginalSavedDrawState(const OriginalSavedDrawState&) = delete;
  OriginalSavedDrawState& operator=(const OriginalSavedDrawState&) = delete;

 private:
  OriginalDrawContext& context_;
  int state_{};
};

// push_font on construction, pop_font on destruction.
class OriginalSelectedFont {
 public:
  OriginalSelectedFont(OriginalDrawContext& context, int pixel_height)
      : context_(context) {
    context_.push_font(static_cast<std::int16_t>(pixel_height));
  }
  ~OriginalSelectedFont() { context_.pop_font(); }
  OriginalSelectedFont(const OriginalSelectedFont&) = delete;
  OriginalSelectedFont& operator=(const OriginalSelectedFont&) = delete;

 private:
  OriginalDrawContext& context_;
};

// 1208:0a42 installs a CreateRectRgnIndirect region, selects it as the clip,
// and deletes it. SaveDC/IntersectClipRect/RestoreDC preserves that visible
// result without leaking the clip into later drawing.
inline void draw_original_clipped_dib(OriginalDrawContext& context,
                                      std::span<const std::byte> resource,
                                      int x,
                                      int y,
                                      const OriginalRect& clip) {
  OriginalSavedDrawState saved(context);
  context.intersect_clip(clip);
  context.draw_dib(resource, x, y);
}

}  // namespace simtower
