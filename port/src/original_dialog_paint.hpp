#pragma once

// Portable DTMP dialog chrome (the 1068:0567 frame path and its bevel
// routines at 11e0:06d9/0950) over OriginalDrawContext, shared by the Win32
// host (paint_original_dialog_chrome wraps it with OriginalGdiDrawContext)
// and the SDL dialog runtime (OriginalCanvasDrawContext). No windows.h here.

#include "original_draw_context.hpp"
#include "original_dtmp.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace simtower {

// RGB(level, level, level).
[[nodiscard]] constexpr OriginalRgb original_gray(std::uint8_t level) noexcept {
  return (static_cast<OriginalRgb>(level) << 16U) |
         (static_cast<OriginalRgb>(level) << 8U) | level;
}

// A DTMP rectangle as a draw-context rectangle.
[[nodiscard]] constexpr OriginalRect original_dtmp_rect(
    const OriginalDtmpRect& source) noexcept {
  return {source.left, source.top, source.right, source.bottom};
}

// FrameRect with a solid brush: the one-pixel border inside the
// rectangle; an empty rectangle draws nothing.
void original_frame_rect(OriginalDrawContext& context,
                         const OriginalRect& rect, OriginalRgb rgb);

// InflateRect(&rect, -by, -by).
[[nodiscard]] constexpr OriginalRect original_deflate_rect(OriginalRect rect,
                                                           int by) noexcept {
  return {rect.left + by, rect.top + by, rect.right - by, rect.bottom - by};
}

// 11e0:06d9: two three-pixel bevels (outer 179 top-left / 140 bottom-right,
// inner 102 / 230) and a 204 fill of what remains.
void paint_original_dtmp_frame(OriginalDrawContext& context,
                               const OriginalRect& rect);

// 11e0:0950: a two-pixel sunken bevel (89 / 255) and a 230 fill, drawn for a
// DTMP rectangle that has no dialog child.
void paint_original_dtmp_placeholder(OriginalDrawContext& context,
                                     const OriginalRect& rect);

// Whether the dialog has a child with this control id (GetDlgItem +
// IsWindow in the Win32 host).
using OriginalDialogHasChild = std::function<bool(std::uint16_t id)>;

// The frame over the whole client, then a placeholder for every nonempty
// DTMP rectangle i whose child id i + 1 does not exist.
void paint_original_dtmp_chrome(OriginalDrawContext& context,
                                const OriginalRect& client,
                                const OriginalDtmp& dtmp,
                                const OriginalDialogHasChild& has_child);

// One step of the 1070:0231 DTMP render pass (render_original_dtmp).
struct OriginalDtmpRenderOp {
  enum class Kind : std::uint8_t {
    // BITMAP/resource drawn at (x, y): the header bitmap, or a rectangle
    // whose bottom is zero (its right word is the resource id).
    bitmap,
    // MoveWindow(child id, rect) + ShowWindow(SW_SHOW), when the child
    // exists.
    place_child,
  };
  Kind kind{Kind::bitmap};
  int resource_id{};
  int x{};
  int y{};
  std::uint16_t child_id{};
  OriginalRect rect{};

  friend bool operator==(const OriginalDtmpRenderOp&,
                         const OriginalDtmpRenderOp&) = default;
};

// The render pass as a pure op list: the header bitmap (when the id is not
// negative), then per rectangle in order: left == 0 skips, bottom == 0 is a
// bitmap, anything else places child i + 1 (an op is emitted whether or not
// the child exists; the host skips a missing one).
[[nodiscard]] std::vector<OriginalDtmpRenderOp> original_dtmp_render_ops(
    const OriginalDtmp& dtmp);

}  // namespace simtower
