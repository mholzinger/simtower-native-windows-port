#pragma once

// Single-window layout for the SDL frontend: the Win16 desktop positions of
// Map, Info, Command and Main become fixed panels inside one window whose
// logical coordinates are those desktop coordinates. Pure geometry, no SDL.
//
// - Map, Info and Command use 1128:05eb's creation rectangles
//   (original_startup_auxiliary_window_specs) with a one-pixel WS_BORDER, so
//   each client starts one pixel inside its outer origin. Their 8-pixel
//   palette frame row (1078:00c6) is part of the client, as in Win32.
// - Command's outer height is 1140:03f8's original_rating_window_height
//   (157 for rating one, 189 otherwise).
// - Main's outer origin is 1128:08d6's initial_x/initial_y
//   (original_main_window_geometry, 204,53); its client starts after the
//   frame, caption and menu of kMainNonclientMetrics, the same metrics
//   sdl_main.cpp uses for the client-size limits. [INFERRED] from those
//   metrics: the caption, menu strip and scrollbar strips stay reserved
//   (empty) until the Win16 menus and scrollbars are drawn.
// - The window is the Main client plus its outer origin and nonclient
//   extents (MainFrame::extra_width/extra_height, 228x115).
//
// TEMP: panels are fixed (no drag bar movement) until the user decides
// whether they should move inside the window.

#include "original_ui.hpp"

#include <array>

namespace simtower::sdl {

// The one Win16 desktop size and the classic metrics (as in
// resource_tests.cpp) that this frontend evaluates the Main geometry with.
// At 1024x768 or above, 1128:02aa's 816x576 client caps apply.
inline constexpr int kDesktopWidth = 1024;
inline constexpr int kDesktopHeight = 768;
inline constexpr OriginalMainNonclientMetrics kMainNonclientMetrics{
    .frame_width = 4,
    .frame_height = 4,
    .vertical_scroll_width = 16,
    .horizontal_scroll_height = 16,
    .menu_height = 19,
    .caption_height = 19,
    .border_width = 1,
    .border_height = 1,
};

// Main's outer origin, its client origin, and the window size minus the
// Main client size, all derived from original_main_window_geometry and
// kMainNonclientMetrics.
struct MainFrame {
  int outer_left{};
  int outer_top{};
  int client_left{};
  int client_top{};
  int extra_width{};
  int extra_height{};
};
[[nodiscard]] MainFrame main_frame();

struct PanelRect {
  int x{};
  int y{};
  int width{};
  int height{};

  [[nodiscard]] constexpr bool contains(int px, int py) const noexcept {
    return px >= x && py >= y && px < x + width && py < y + height;
  }
  friend constexpr bool operator==(const PanelRect&,
                                   const PanelRect&) = default;
};

enum class Panel : unsigned char {
  none,
  main,
  map,
  info,
  command,
  // Elevator Control's fixed panel (drawn above the palettes; not in
  // kPanelZOrder, the session routes it first while it is open).
  elevator_control,
};

// Stacking order, bottom to top, shared by drawing (compose_window) and
// pointer routing (Session::pointer_event), so a point always reaches the
// panel drawn on top of it. 1128:05eb creates Command, Info, then Map and
// raises each with SetWindowPos (flags 0x000b keep the z-order change), Info
// and Map at HWND_TOP, so Map is above Info; Command is TOPMOST, above both.
// TEMP: the order is fixed; the original's Info/Map menu toggles (40015,
// 40016) raise the shown palette with HWND_TOP again.
inline constexpr std::array<Panel, 4> kPanelZOrder{
    Panel::main, Panel::info, Panel::map, Panel::command};

struct WindowLayout {
  PanelRect map_outer{};
  PanelRect map_client{};
  PanelRect info_outer{};
  PanelRect info_client{};
  PanelRect command_outer{};
  PanelRect command_client{};
  PanelRect main_outer{};
  PanelRect main_client{};
  int window_width{};
  int window_height{};
};

// Main client size and Command outer height (1140:03f8) in, layout out.
[[nodiscard]] WindowLayout make_window_layout(int main_client_width,
                                              int main_client_height,
                                              int command_outer_height);

// The window size that holds a Main client of the given size, and back.
[[nodiscard]] inline int window_width_for(int main_client_width) {
  return main_client_width + main_frame().extra_width;
}
[[nodiscard]] inline int window_height_for(int main_client_height) {
  return main_client_height + main_frame().extra_height;
}
[[nodiscard]] inline int main_client_width_for(int window_width) {
  return window_width - main_frame().extra_width;
}
[[nodiscard]] inline int main_client_height_for(int window_height) {
  return window_height - main_frame().extra_height;
}

}  // namespace simtower::sdl
