#pragma once

// Portable MAINWNDPROC WM_HSCROLL / WM_VSCROLL (1158:015b / 1158:01e7) and
// the 1058:05f8 tail they call. The Win32 host (native_main.cpp
// main_window_proc) forwards LOWORD(wParam) and HIWORD(wParam) here; the SDL
// session forwards the codes its drawn Main scroll bars (and the SDL-only
// arrow key and wheel stand-ins) send. Every effect goes through
// OriginalMainScrollHost, in the order the Win32 host performed it inline.

#include <cstdint>
#include <optional>

namespace simtower {

// SB_HORZ (0) and SB_VERT (1), the bar argument of GetScrollPos /
// SetScrollPos at 1158:01b7 / 025c.
enum class OriginalMainScrollBar : std::uint8_t {
  horizontal = 0,
  vertical = 1,
};

class OriginalMainScrollHost {
 public:
  virtual ~OriginalMainScrollHost() = default;

  // DS:24b8: while set, both branches return before anything else
  // (1158:015b / 01e7).
  virtual bool modal_input_locked() = 0;
  // InvalidateRect(Main, NULL, FALSE) at 1158:016d / 01f9, before the
  // code is decoded (an unsupported code keeps it).
  virtual void invalidate_main() = 0;
  // The bar's position (GetScrollPos, 1158:01b9 / 025e). nullopt: the
  // host cannot read it (Win32 GetScrollInfo failed) and nothing follows.
  virtual std::optional<int> scroll_position(OriginalMainScrollBar bar) = 0;
  // The Main client width (DS:778a) or height (DS:778c - DS:7788).
  virtual int client_extent(OriginalMainScrollBar bar) = 0;
  // SetScrollPos(Main, bar, position, TRUE) (1158:0245): the host clamps
  // to the range 1080:00d7 installed and redraws the bar.
  virtual void set_scroll_position(OriginalMainScrollBar bar,
                                   int position) = 0;
  // 1058:05f8's tail (original_scrollbar_refresh_order): the synchronous
  // 1080:0a1e(1) rebuild, then 1080:055d's Map focus exchange.
  virtual void rebuild_main_with_sky() = 0;
  virtual void adjust_map_focus() = 0;
};

// One WM_HSCROLL / WM_VSCROLL: code is the Win16 wParam (SB_LINEUP 0 ..
// SB_THUMBTRACK 5; 6.. are ignored after the invalidation), position the
// Win16 LOWORD(lParam) that SB_THUMBPOSITION / SB_THUMBTRACK carry. Both
// thumb codes set the raw position and rebuild (1158:01a6 / 023a), so a
// drag rebuilds on every SB_THUMBTRACK and again on SB_THUMBPOSITION.
void handle_original_main_scroll(OriginalMainScrollHost& host,
                                 OriginalMainScrollBar bar,
                                 std::uint16_t code,
                                 std::uint16_t position);

}  // namespace simtower
