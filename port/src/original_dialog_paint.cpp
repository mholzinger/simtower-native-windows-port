#include "original_dialog_paint.hpp"

namespace simtower {
namespace {

void line(OriginalDrawContext& context, int x0, int y0, int x1, int y1,
          int x2, int y2) {
  context.move_to(x0, y0);
  context.line_to(x1, y1);
  context.line_to(x2, y2);
}

// The four bevel strokes of 11e0:06d9 (select_gray_and_draw_*), each with its
// own pen created, selected, restored and deleted.
void outer_top_left(OriginalDrawContext& context, const OriginalRect& rect,
                    std::uint8_t level) {
  context.push_solid_pen(original_gray(level));
  for (int inset = 0; inset < 3; ++inset) {
    line(context, rect.right - 2 - inset, rect.top + inset, rect.left + inset,
         rect.top + inset, rect.left + inset, rect.bottom - 2 - inset);
  }
  context.pop_pen();
}

void outer_bottom_right(OriginalDrawContext& context, const OriginalRect& rect,
                        std::uint8_t level) {
  context.push_solid_pen(original_gray(level));
  for (int inset = 0; inset < 3; ++inset) {
    line(context, rect.left + inset, rect.bottom - 1 - inset,
         rect.right - 1 - inset, rect.bottom - 1 - inset,
         rect.right - 1 - inset, rect.top + inset);
  }
  context.pop_pen();
}

void inner_top_left(OriginalDrawContext& context, const OriginalRect& rect,
                    std::uint8_t level) {
  context.push_solid_pen(original_gray(level));
  for (int inset = 0; inset < 3; ++inset) {
    line(context, rect.right - 1 - inset, rect.top + inset, rect.left + inset,
         rect.top + inset, rect.left + inset, rect.bottom - 1 - inset);
  }
  context.pop_pen();
}

void inner_bottom_right(OriginalDrawContext& context, const OriginalRect& rect,
                        std::uint8_t level) {
  context.push_solid_pen(original_gray(level));
  for (int inset = 0; inset < 3; ++inset) {
    line(context, rect.left + inset + 1, rect.bottom - 1 - inset,
         rect.right - 1 - inset, rect.bottom - 1 - inset,
         rect.right - 1 - inset, rect.top + inset + 1);
  }
  context.pop_pen();
}

// IsRectEmpty.
bool empty(const OriginalRect& rect) {
  return rect.right <= rect.left || rect.bottom <= rect.top;
}

OriginalRect to_rect(const OriginalDtmpRect& source) {
  return {source.left, source.top, source.right, source.bottom};
}

}  // namespace

void paint_original_dtmp_frame(OriginalDrawContext& context,
                               const OriginalRect& rect) {
  // The color arguments passed through 11e0:0e00 resolve to 179, 140, 102,
  // 230, and 204 in that order.
  outer_top_left(context, rect, 179);
  outer_bottom_right(context, rect, 140);
  const auto inner = original_deflate_rect(rect, 3);
  inner_top_left(context, inner, 102);
  inner_bottom_right(context, inner, 230);
  context.fill_rect(original_deflate_rect(inner, 3), original_gray(204));
}

void paint_original_dtmp_placeholder(OriginalDrawContext& context,
                                     const OriginalRect& rect) {
  context.push_solid_pen(original_gray(89));
  for (int inset = 0; inset < 2; ++inset) {
    line(context, rect.right - 1 - inset, rect.top + inset, rect.left + inset,
         rect.top + inset, rect.left + inset, rect.bottom - 2 - inset);
  }
  context.pop_pen();
  context.push_solid_pen(original_gray(255));
  for (int inset = 0; inset < 2; ++inset) {
    line(context, rect.left + inset, rect.bottom - 1 - inset,
         rect.right - 1 - inset, rect.bottom - 1 - inset,
         rect.right - 1 - inset, rect.top + inset + 1);
  }
  context.pop_pen();
  context.fill_rect(original_deflate_rect(rect, 2), original_gray(230));
}

void paint_original_dtmp_chrome(OriginalDrawContext& context,
                                const OriginalRect& client,
                                const OriginalDtmp& dtmp,
                                const OriginalDialogHasChild& has_child) {
  paint_original_dtmp_frame(context, client);
  for (std::size_t index = 0; index < dtmp.rectangles.size(); ++index) {
    const auto rect = to_rect(dtmp.rectangles[index]);
    if (empty(rect)) continue;
    if (!has_child(static_cast<std::uint16_t>(index + 1U))) {
      paint_original_dtmp_placeholder(context, rect);
    }
  }
}

std::vector<OriginalDtmpRenderOp> original_dtmp_render_ops(
    const OriginalDtmp& dtmp) {
  using Kind = OriginalDtmpRenderOp::Kind;
  std::vector<OriginalDtmpRenderOp> ops;
  if (dtmp.bitmap_resource_id >= 0) {
    ops.push_back({Kind::bitmap, dtmp.bitmap_resource_id, 0, 0, 0U, {}});
  }
  for (std::size_t index = 0; index < dtmp.rectangles.size(); ++index) {
    const auto& rect = dtmp.rectangles[index];
    if (rect.left == 0) continue;
    if (rect.bottom == 0) {
      ops.push_back({Kind::bitmap, rect.right, rect.left, rect.top, 0U, {}});
      continue;
    }
    ops.push_back({Kind::place_child, 0, 0, 0,
                   static_cast<std::uint16_t>(index + 1U), to_rect(rect)});
  }
  return ops;
}

}  // namespace simtower
