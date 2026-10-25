#pragma once

// Main's WS_HSCROLL and WS_VSCROLL bars inside the SDL window: where they
// are (the strips sdl_layout.hpp reserves right of and below the Main
// client), how USER's scroll bar tracking turns presses, moves and
// releases on them into WM_HSCROLL / WM_VSCROLL codes, and their painter
// (the dialog scroll bar painter, draw_dialog_scroll_bar). Pure logic and
// pixels, no SDL. The session sends each signal through the shared
// handle_original_main_scroll (original_main_scroll.hpp), the same path
// the Win32 MAINWNDPROC uses.
//
// TEMP (no Win3.1 reference yet, owner decision 2026-10-23): the bar look
// and metrics are the dialog scroll bar's; an arrow or trough press sends
// one code (no auto-repeat while held) and the release SB_ENDSCROLL; the
// thumb follows the pointer wherever it is (no snap-back outside the bar).
// The corner square between the bars takes no input (no tracking).

#include "sdl_layout.hpp"

#include "original_draw_context.hpp"
#include "original_main_scroll.hpp"
#include "original_world.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace simtower::sdl {

// The two bars and the corner square, in window coordinates.
struct MainScrollBarRects {
  PanelRect horizontal{};
  PanelRect vertical{};
  PanelRect corner{};
};
[[nodiscard]] MainScrollBarRects main_scroll_bar_rects(
    const WindowLayout& layout);
[[nodiscard]] OriginalRect main_scroll_bar_rect(const WindowLayout& layout,
                                                OriginalMainScrollBar bar);

// One bar's range and position (SetScrollRange / SetScrollPos state).
struct MainScrollRange {
  int min{};
  int max{};
  int position{};
};
using MainScrollRanges = std::array<MainScrollRange, 2>;  // by bar

// One WM_HSCROLL / WM_VSCROLL for handle_original_main_scroll.
struct MainScrollSignal {
  OriginalMainScrollBar bar{};
  std::uint16_t code{};
  std::uint16_t position{};

  friend bool operator==(const MainScrollSignal&,
                         const MainScrollSignal&) = default;
};

// USER's tracking of a press on one of the bars, until its release.
class MainScrollBars {
 public:
  // A left press (or the second press of a double click) at window point
  // x, y. nullopt when it is on neither bar nor the corner; otherwise the
  // signals it sends (none for a thumb press or the corner) and, on a bar,
  // the tracking starts.
  [[nodiscard]] std::optional<std::vector<MainScrollSignal>> press(
      const WindowLayout& layout, const MainScrollRanges& ranges, int x,
      int y);
  // A move while tracking: a thumb drag sends SB_THUMBTRACK when the
  // position changes.
  [[nodiscard]] std::vector<MainScrollSignal> move(
      const WindowLayout& layout, const MainScrollRanges& ranges, int x,
      int y);
  // The release: SB_THUMBPOSITION of the drag position then SB_ENDSCROLL
  // after a thumb drag, SB_ENDSCROLL after an arrow or trough press.
  [[nodiscard]] std::vector<MainScrollSignal> release();
  // Whether a press is being tracked (USER's own loop runs, so
  // SimTower's idle loop does not).
  [[nodiscard]] bool tracking() const { return drag_.has_value(); }
  // The thumb's tracking position on bar while it is dragged.
  [[nodiscard]] std::optional<int> thumb_tracking(
      OriginalMainScrollBar bar) const;
  // WM_CANCELMODE: ends the tracking with no further signal.
  bool cancel();

 private:
  struct Drag {
    OriginalMainScrollBar bar{};
    int grab{-1};  // pointer offset inside the thumb; -1: not the thumb
    int position{};
  };
  std::optional<Drag> drag_{};
};

// Paints both bars (the thumb of a dragged bar at its tracking position)
// and the corner square into raster.
void draw_main_scroll_bars(OriginalWorldRaster& raster,
                           const WindowLayout& layout,
                           const MainScrollRanges& ranges,
                           const MainScrollBars& tracking);

}  // namespace simtower::sdl
