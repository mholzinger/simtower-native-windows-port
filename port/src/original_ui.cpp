#include "original_ui.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace simtower {

std::optional<int> original_main_scroll_request_position(
    std::uint16_t scroll_code,
    int current_position,
    std::uint16_t message_position,
    int client_extent,
    int maximum_position) noexcept {
  int requested_position{};
  switch (scroll_code) {
    case 0U:
      requested_position = current_position - 16;
      break;
    case 1U:
      requested_position = current_position + 16;
      break;
    case 2U:
      requested_position = current_position - (client_extent - 16);
      break;
    case 3U:
      requested_position = current_position + (client_extent - 16);
      break;
    case 4U:
    case 5U:
      // 1158:01a6/023a load the Win16 lParam position directly into DX and
      // bypass the line/page clamp before calling SetScrollPos.
      return static_cast<int>(message_position);
    default:
      return std::nullopt;
  }
  return std::clamp(requested_position, 0, std::max(0, maximum_position));
}

OriginalMainScrollbarResizeState original_main_scrollbar_resize_state(
    int saved_position,
    int client_extent,
    int world_extent) noexcept {
  const int maximum = std::max(0, world_extent - client_extent);
  return {
      .minimum = 0,
      .maximum = maximum,
      .native_page_size = 0U,
      .position = std::clamp(saved_position, 0, maximum),
  };
}

OriginalMainWindowGeometry original_main_window_geometry(
    int desktop_right,
    int desktop_bottom,
    OriginalMainNonclientMetrics metrics) noexcept {
  constexpr int minimum_client_width = 100;
  constexpr int minimum_client_height = 100;
  constexpr int maximum_client_width_cap = 816;
  constexpr int maximum_client_height_cap = 576;
  constexpr int initial_x = 204;
  constexpr int initial_y = 53;

  const int maximum_client_width = std::min(
      desktop_right - metrics.vertical_scroll_width,
      maximum_client_width_cap);
  const int maximum_client_height = std::min(
      desktop_bottom - metrics.caption_height -
          metrics.horizontal_scroll_height - metrics.menu_height,
      maximum_client_height_cap);

  int initial_right = std::min(initial_x + 1000, desktop_right);
  int initial_bottom = std::min(initial_y + 1000, desktop_bottom);
  if (initial_bottom - initial_y > maximum_client_height - 1) {
    initial_bottom = initial_y + maximum_client_height - 1;
  }
  if (initial_right - initial_x > maximum_client_width - 1) {
    initial_right = initial_x + maximum_client_width - 1;
  }

  return {
      .initial_x = initial_x,
      .initial_y = initial_y,
      .initial_width = initial_right - initial_x,
      .initial_height = initial_bottom - initial_y,
      .minimum_track_width =
          2 * metrics.frame_width + minimum_client_width +
          metrics.vertical_scroll_width,
      .minimum_track_height =
          2 * metrics.frame_height + minimum_client_height +
          metrics.horizontal_scroll_height + 4 * metrics.menu_height +
          metrics.caption_height,
      .maximum_track_width =
          2 * metrics.frame_width + maximum_client_width +
          metrics.vertical_scroll_width - metrics.border_width,
      .maximum_track_height =
          2 * metrics.frame_height + maximum_client_height +
          metrics.horizontal_scroll_height + metrics.menu_height +
          metrics.caption_height - metrics.border_height,
  };
}

std::vector<OriginalAuxiliaryWindowAction>
original_auxiliary_window_actions(
    bool restore,
    bool command_visible,
    bool info_visible,
    bool map_visible) {
  std::vector<OriginalAuxiliaryWindowAction> actions(3U);
  std::size_t count{};
  const auto append = [&](OriginalAuxiliaryWindow target,
                          OriginalAuxiliaryWindowOperation operation,
                          OriginalAuxiliaryInsertAfter insert_after) {
    actions[count++] = {target, operation, insert_after};
  };
  if (restore) {
    if (command_visible) {
      append(OriginalAuxiliaryWindow::command,
             OriginalAuxiliaryWindowOperation::show,
             OriginalAuxiliaryInsertAfter::topmost);
    }
    const auto secondary_insert = command_visible
        ? OriginalAuxiliaryInsertAfter::command
        : OriginalAuxiliaryInsertAfter::top;
    if (info_visible) {
      append(OriginalAuxiliaryWindow::info,
             OriginalAuxiliaryWindowOperation::show,
             secondary_insert);
    }
    if (map_visible) {
      append(OriginalAuxiliaryWindow::map,
             OriginalAuxiliaryWindowOperation::show,
             secondary_insert);
    }
    actions.resize(count);
    return actions;
  }

  if (info_visible) {
    append(OriginalAuxiliaryWindow::info,
           OriginalAuxiliaryWindowOperation::hide,
           OriginalAuxiliaryInsertAfter::top);
  }
  if (command_visible) {
    append(OriginalAuxiliaryWindow::command,
           OriginalAuxiliaryWindowOperation::hide,
           OriginalAuxiliaryInsertAfter::top);
  }
  if (map_visible) {
    append(OriginalAuxiliaryWindow::map,
           OriginalAuxiliaryWindowOperation::hide,
           OriginalAuxiliaryInsertAfter::top);
  }
  actions.resize(count);
  return actions;
}

std::vector<OriginalAuxiliaryActivationAction>
original_auxiliary_activation_actions(
    bool active,
    bool command_visible,
    bool info_visible,
    bool map_visible) {
  std::vector<OriginalAuxiliaryActivationAction> actions;
  actions.reserve(3U);
  if (active) {
    // 1078:01fd-0251 deliberately targets the Command HWND for all three
    // visibility checks; the last enabled Command action makes it TOPMOST.
    if (map_visible) {
      actions.push_back({OriginalAuxiliaryWindow::command,
                         OriginalAuxiliaryActivationInsertAfter::top});
    }
    if (info_visible) {
      actions.push_back({OriginalAuxiliaryWindow::command,
                         OriginalAuxiliaryActivationInsertAfter::top});
    }
    if (command_visible) {
      actions.push_back({OriginalAuxiliaryWindow::command,
                         OriginalAuxiliaryActivationInsertAfter::topmost});
    }
    return actions;
  }

  if (command_visible) {
    actions.push_back({OriginalAuxiliaryWindow::command,
                       OriginalAuxiliaryActivationInsertAfter::main});
  }
  if (info_visible) {
    actions.push_back({OriginalAuxiliaryWindow::info,
                       OriginalAuxiliaryActivationInsertAfter::main});
  }
  if (map_visible) {
    actions.push_back({OriginalAuxiliaryWindow::map,
                       OriginalAuxiliaryActivationInsertAfter::main});
  }
  return actions;
}

OriginalPaletteWindowActivationPlan original_palette_window_activation_plan(
    OriginalAuxiliaryWindow window,
    bool active,
    bool modal_dialog_available,
    bool command_window_enabled,
    bool previous_shared_activation_latch) noexcept {
  OriginalPaletteWindowActivationPlan plan{};
  if (modal_dialog_available) {
    plan.insert_behind_modal = window == OriginalAuxiliaryWindow::map;
    if (active) {
      plan.activate_modal = true;
      plan.focus_modal = window == OriginalAuxiliaryWindow::command;
      return plan;
    }
    // CMDBTNWNDPROC 1050:006a jumps directly out for an inactive Command
    // while DS:31a4 exists; Info and Map continue to their local latch write.
    if (window == OriginalAuxiliaryWindow::command) return plan;
  }
  if (!modal_dialog_available &&
      window == OriginalAuxiliaryWindow::command &&
      command_window_enabled && previous_shared_activation_latch) {
    plan.promote_command_topmost = true;
    plan.command_promotion_no_activate = !active;
  }
  plan.validate_client = active && !modal_dialog_available;
  plan.write_shared_activation_latch = true;
  plan.shared_activation_latch = active;
  return plan;
}

std::vector<OriginalPaletteSurface> original_palette_repaint_order(
    bool runtime_initialized,
    bool realized_entries_changed) {
  if (!runtime_initialized || !realized_entries_changed) {
    return {};
  }
  return {
      OriginalPaletteSurface::map,
      OriginalPaletteSurface::info,
      OriginalPaletteSurface::command,
      OriginalPaletteSurface::main,
  };
}

std::vector<OriginalPaletteRepaintAction> original_palette_repaint_actions(
    bool runtime_initialized,
    bool realized_entries_changed,
    OriginalPaletteSurface source) {
  const auto order = original_palette_repaint_order(
      runtime_initialized, realized_entries_changed);
  std::vector<OriginalPaletteRepaintAction> actions;
  actions.reserve(order.size());
  for (const auto surface : order) {
    if (surface == OriginalPaletteSurface::info) {
      actions.push_back({
          .surface = surface,
          .mechanism = OriginalPaletteRepaintMechanism::update_window,
          .invalidate = true,
      });
      continue;
    }
    const bool reuse_source_dc = surface == source;
    actions.push_back({
        .surface = surface,
        .mechanism = OriginalPaletteRepaintMechanism::direct_dc,
        .invalidate = surface != OriginalPaletteSurface::command,
        .reuse_source_dc = reuse_source_dc,
        .select_and_realize = !reuse_source_dc,
        .release_dc = !reuse_source_dc,
    });
  }
  return actions;
}

}  // namespace simtower
