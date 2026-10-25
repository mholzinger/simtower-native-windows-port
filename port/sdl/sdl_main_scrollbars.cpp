#include "sdl_main_scrollbars.hpp"

#include "sdl_dialog.hpp"
#include "sdl_style.hpp"

#include "original_canvas_font.hpp"
#include "original_dialog_model.hpp"
#include "original_draw_canvas.hpp"

#include <cstddef>

namespace simtower::sdl {
namespace {

OriginalRect to_rect(const PanelRect& rect) {
  return {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
}

DialogScrollAxis axis_of(OriginalMainScrollBar bar) {
  return bar == OriginalMainScrollBar::horizontal
             ? DialogScrollAxis::horizontal
             : DialogScrollAxis::vertical;
}

const MainScrollRange& range_of(const MainScrollRanges& ranges,
                                OriginalMainScrollBar bar) {
  return ranges[static_cast<std::size_t>(bar)];
}

MainScrollSignal signal(OriginalMainScrollBar bar, std::uint16_t code,
                        int position = 0) {
  // The Win16 LOWORD(lParam) of the thumb codes.
  return {bar, code, static_cast<std::uint16_t>(position)};
}

}  // namespace

MainScrollBarRects main_scroll_bar_rects(const WindowLayout& layout) {
  // WS_VSCROLL right of the client, WS_HSCROLL below it, each one metric
  // thick (kMainNonclientMetrics, the strips main_frame reserves), and
  // the square where they meet.
  constexpr auto& metrics = kMainNonclientMetrics;
  const auto& client = layout.main_client;
  const int right = client.x + client.width;
  const int bottom = client.y + client.height;
  return {
      .horizontal = {client.x, bottom, client.width,
                     metrics.horizontal_scroll_height},
      .vertical = {right, client.y, metrics.vertical_scroll_width,
                   client.height},
      .corner = {right, bottom, metrics.vertical_scroll_width,
                 metrics.horizontal_scroll_height},
  };
}

OriginalRect main_scroll_bar_rect(const WindowLayout& layout,
                                  OriginalMainScrollBar bar) {
  const auto rects = main_scroll_bar_rects(layout);
  return to_rect(bar == OriginalMainScrollBar::horizontal ? rects.horizontal
                                                          : rects.vertical);
}

std::optional<std::vector<MainScrollSignal>> MainScrollBars::press(
    const WindowLayout& layout, const MainScrollRanges& ranges, int x,
    int y) {
  const auto rects = main_scroll_bar_rects(layout);
  if (rects.corner.contains(x, y)) {
    // TEMP [INFERRED]: the square between the bars does nothing (no
    // tracking, no code).
    return std::vector<MainScrollSignal>{};
  }
  OriginalMainScrollBar bar{};
  if (rects.horizontal.contains(x, y)) {
    bar = OriginalMainScrollBar::horizontal;
  } else if (rects.vertical.contains(x, y)) {
    bar = OriginalMainScrollBar::vertical;
  } else {
    return std::nullopt;
  }
  const auto axis = axis_of(bar);
  const auto& range = range_of(ranges, bar);
  const auto parts = dialog_scroll_parts(main_scroll_bar_rect(layout, bar),
                                         range.min, range.max,
                                         range.position, axis);
  const bool horizontal = axis == DialogScrollAxis::horizontal;
  const int along = horizontal ? x : y;
  const bool has_thumb = parts.thumb.bottom > parts.thumb.top;
  const int thumb_start = horizontal ? parts.thumb.left : parts.thumb.top;
  std::uint16_t code{};
  if (original_rect_contains(parts.up, x, y)) {
    code = kOriginalScrollLineUp;
  } else if (original_rect_contains(parts.down, x, y)) {
    code = kOriginalScrollLineDown;
  } else if (has_thumb && original_rect_contains(parts.thumb, x, y)) {
    drag_ = Drag{bar, along - thumb_start, range.position};
    return std::vector<MainScrollSignal>{};
  } else if (has_thumb) {
    code = along < thumb_start ? kOriginalScrollPageUp
                               : kOriginalScrollPageDown;
  } else {
    // No thumb (empty range or no room): the trough takes the press
    // without a code.
    drag_ = Drag{bar, -1, range.position};
    return std::vector<MainScrollSignal>{};
  }
  drag_ = Drag{bar, -1, range.position};
  return std::vector<MainScrollSignal>{signal(bar, code)};
}

std::vector<MainScrollSignal> MainScrollBars::move(
    const WindowLayout& layout, const MainScrollRanges& ranges, int x,
    int y) {
  if (!drag_ || drag_->grab < 0) return {};
  const auto bar = drag_->bar;
  const auto axis = axis_of(bar);
  const auto& range = range_of(ranges, bar);
  const int along = axis == DialogScrollAxis::horizontal ? x : y;
  const int position = dialog_scroll_position_at(
      main_scroll_bar_rect(layout, bar), range.min, range.max,
      along - drag_->grab, axis);
  if (position == drag_->position) return {};
  drag_->position = position;
  return {signal(bar, kOriginalScrollThumbTrack, position)};
}

std::vector<MainScrollSignal> MainScrollBars::release() {
  if (!drag_) return {};
  const auto drag = *drag_;
  drag_.reset();
  if (drag.grab >= 0) {
    return {signal(drag.bar, kOriginalScrollThumbPosition, drag.position),
            signal(drag.bar, kOriginalScrollEnd)};
  }
  return {signal(drag.bar, kOriginalScrollEnd)};
}

std::optional<int> MainScrollBars::thumb_tracking(
    OriginalMainScrollBar bar) const {
  if (drag_ && drag_->grab >= 0 && drag_->bar == bar) {
    return drag_->position;
  }
  return std::nullopt;
}

bool MainScrollBars::cancel() {
  if (!drag_) return false;
  drag_.reset();
  return true;
}

void draw_main_scroll_bars(OriginalWorldRaster& raster,
                           const WindowLayout& layout,
                           const MainScrollRanges& ranges,
                           const MainScrollBars& tracking) {
  OriginalCanvasDrawContext context(raster, OriginalCanvasFont::bundled());
  const auto rects = main_scroll_bar_rects(layout);
  // TEMP: COLOR_SCROLLBAR as the button face gray, and the corner square
  // as plain face.
  context.fill_rect(to_rect(rects.corner), style::kButtonFace);
  for (const auto bar : {OriginalMainScrollBar::horizontal,
                         OriginalMainScrollBar::vertical}) {
    const auto& range = range_of(ranges, bar);
    draw_dialog_scroll_bar(
        context, main_scroll_bar_rect(layout, bar), range.min, range.max,
        tracking.thumb_tracking(bar).value_or(range.position), true,
        style::kButtonFace, axis_of(bar));
  }
}

}  // namespace simtower::sdl
