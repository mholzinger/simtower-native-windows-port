#include "sdl_layout.hpp"

#include "original_ui.hpp"

namespace simtower::sdl {

MainFrame main_frame() {
  constexpr auto& metrics = kMainNonclientMetrics;
  const auto geometry =
      original_main_window_geometry(kDesktopWidth, kDesktopHeight, metrics);
  MainFrame frame{};
  frame.outer_left = geometry.initial_x;
  frame.outer_top = geometry.initial_y;
  frame.client_left = frame.outer_left + metrics.frame_width;
  frame.client_top = frame.outer_top + metrics.frame_height +
                     metrics.caption_height + metrics.menu_height;
  frame.extra_width =
      frame.client_left + metrics.vertical_scroll_width + metrics.frame_width;
  frame.extra_height = frame.client_top + metrics.horizontal_scroll_height +
                       metrics.frame_height;
  return frame;
}

WindowLayout make_window_layout(int main_client_width,
                                int main_client_height,
                                int command_outer_height) {
  // 1128:05eb with SM_CXBORDER = SM_CYBORDER = 1.
  constexpr auto specs = original_startup_auxiliary_window_specs(1, 1);
  const auto& command = specs[0];
  const auto& info = specs[1];
  const auto& map = specs[2];

  WindowLayout layout{};
  layout.map_outer = {map.x, map.y, map.create_outer_width,
                      map.create_outer_height};
  layout.map_client = {map.x + 1, map.y + 1, map.rectangle_width,
                       map.rectangle_height};
  layout.info_outer = {info.x, info.y, info.create_outer_width,
                       info.create_outer_height};
  layout.info_client = {info.x + 1, info.y + 1, info.rectangle_width,
                        info.rectangle_height};
  // Command's SETWINDOWPOS keeps the raw 63-pixel outer width (flags 0x000a)
  // and 1140:010d's MoveWindow then sets the rating height; WS_BORDER
  // leaves a 61-pixel client. [INFERRED 70%] one-pixel border.
  layout.command_outer = {command.x, command.y, command.set_position_width,
                          command_outer_height};
  layout.command_client = {command.x + 1, command.y + 1,
                           command.set_position_width - 2,
                           command_outer_height - 2};
  const auto frame = main_frame();
  layout.main_outer = {
      frame.outer_left, frame.outer_top,
      frame.extra_width - frame.outer_left + main_client_width,
      frame.extra_height - frame.outer_top + main_client_height};
  layout.main_client = {frame.client_left, frame.client_top, main_client_width,
                        main_client_height};
  layout.window_width = window_width_for(main_client_width);
  layout.window_height = window_height_for(main_client_height);
  return layout;
}

}  // namespace simtower::sdl
