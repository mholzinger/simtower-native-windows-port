#include "original_draw_context_win32.hpp"

#include "original_dib_win32.hpp"
#include "original_font_win32.hpp"

#include <stdexcept>

namespace simtower {
namespace {

// OriginalDrawContext names TA_* and TA_UPDATECP with their Win32 values.
static_assert(kOriginalTaUpdateCp == TA_UPDATECP);
static_assert(kOriginalTaRight == TA_RIGHT);
static_assert(kOriginalTaCenter == TA_CENTER);
static_assert(kOriginalTaBottom == TA_BOTTOM);
static_assert(kOriginalTaBaseline == TA_BASELINE);
static_assert(TA_LEFT == 0 && TA_TOP == 0);

constexpr COLORREF to_colorref(OriginalRgb rgb) noexcept {
  return RGB((rgb >> 16U) & 0xffU, (rgb >> 8U) & 0xffU, rgb & 0xffU);
}

RECT to_rect(const OriginalRect& rect) noexcept {
  return RECT{rect.left, rect.top, rect.right, rect.bottom};
}

}  // namespace

int OriginalGdiDrawContext::save() { return SaveDC(dc_); }

void OriginalGdiDrawContext::restore(int saved) { RestoreDC(dc_, saved); }

void OriginalGdiDrawContext::fill_rect(const OriginalRect& rect,
                                       OriginalRgb rgb) {
  HBRUSH brush = CreateSolidBrush(to_colorref(rgb));
  if (!brush) throw std::runtime_error("CreateSolidBrush failed for info window");
  const RECT rectangle = to_rect(rect);
  FillRect(dc_, &rectangle, brush);
  DeleteObject(brush);
}

void OriginalGdiDrawContext::intersect_clip(const OriginalRect& rect) {
  IntersectClipRect(dc_, rect.left, rect.top, rect.right, rect.bottom);
}

void OriginalGdiDrawContext::draw_dib(std::span<const std::byte> resource,
                                      int x,
                                      int y) {
  draw_original_dib(dc_, resource, x, y);
}

void OriginalGdiDrawContext::push_solid_pen(OriginalRgb rgb) {
  HPEN pen = CreatePen(PS_SOLID, 1, to_colorref(rgb));
  if (!pen) throw std::runtime_error("CreatePen failed for info clock");
  HGDIOBJ previous = SelectObject(dc_, pen);
  pens_.push_back({pen, previous});
}

void OriginalGdiDrawContext::pop_pen() {
  if (pens_.empty()) return;
  const SelectedPen selected = pens_.back();
  pens_.pop_back();
  SelectObject(dc_, selected.previous);
  DeleteObject(selected.pen);
}

void OriginalGdiDrawContext::move_to(int x, int y) {
  MoveToEx(dc_, x, y, nullptr);
}

void OriginalGdiDrawContext::line_to(int x, int y) { LineTo(dc_, x, y); }

void OriginalGdiDrawContext::push_font(std::int16_t pixel_height) {
  HFONT font = original_cached_font(pixel_height);
  HGDIOBJ previous{};
  if (font) previous = SelectObject(dc_, font);
  previous_fonts_.push_back(previous);
}

void OriginalGdiDrawContext::pop_font() {
  if (previous_fonts_.empty()) return;
  HGDIOBJ previous = previous_fonts_.back();
  previous_fonts_.pop_back();
  if (previous) SelectObject(dc_, previous);
}

void OriginalGdiDrawContext::set_text_color(OriginalRgb rgb) {
  SetTextColor(dc_, to_colorref(rgb));
}

void OriginalGdiDrawContext::set_bk_transparent() {
  SetBkMode(dc_, TRANSPARENT);
}

void OriginalGdiDrawContext::set_text_align(std::uint16_t flags) {
  SetTextAlign(dc_, flags);
}

void OriginalGdiDrawContext::text_out(std::string_view text) {
  TextOutA(dc_, 0, 0, text.data(), static_cast<int>(text.size()));
}

void OriginalGdiDrawContext::draw_text_single_line(std::string_view text,
                                                   const OriginalRect& rect) {
  RECT text_rect = to_rect(rect);
  DrawTextA(dc_, text.data(), static_cast<int>(text.size()), &text_rect,
            DT_SINGLELINE);
}

int OriginalGdiDrawContext::text_extent_cx(std::string_view text) {
  SIZE extent{};
  GetTextExtentPoint32A(dc_, text.data(), static_cast<int>(text.size()),
                        &extent);
  return extent.cx;
}

}  // namespace simtower
