#pragma once

// Test-only copy of the direct-GDI Elevator Control painters exactly as
// native_main.cpp had them before its WM_PAINT handlers moved onto the
// shared paint_original_elevator_control / paint_original_elevator_popup
// (original_elevator_control_controller.hpp) through OriginalGdiDrawContext:
// paint_original_elevator_control_grid, paint_original_elevator_control and
// paint_original_elevator_popup with their helpers. The only edits are the
// globals made parameters (g_resources -> resources, g_tower_document ->
// document, the dialog context -> the shared controller / hover mode).
// elevator_control_painter_parity_tests compares their pixels with the
// shared painter. Do not edit except to track an intended change in the
// original painter.

#include "original_dib_win32.hpp"
#include "original_elevator_control.hpp"
#include "original_elevator_control_controller.hpp"
#include "original_font_win32.hpp"
#include "original_resources.hpp"
#include "original_tdt.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace legacy_elevator_control {
namespace {

HFONT make_original_dialog_font(int pixel_height) {
  return simtower::original_cached_font(
      static_cast<std::int16_t>(pixel_height));
}

std::wstring original_cp1252_to_wide(std::string_view text) {
  if (text.empty()) return {};
  const int length = MultiByteToWideChar(
      1252, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) return {};
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(1252, 0, text.data(), static_cast<int>(text.size()),
                      result.data(), length);
  return result;
}

RECT original_native_rectangle(const simtower::OriginalDtmpRect& source) {
  return {static_cast<LONG>(source.left), static_cast<LONG>(source.top),
          static_cast<LONG>(source.right), static_cast<LONG>(source.bottom)};
}

void draw_original_elevator_control_region(
    HDC dc,
    const simtower::OriginalResources& resources,
    int bitmap_id,
    const simtower::OriginalDtmpRect& rectangle) {
  simtower::draw_original_dib_region(
      dc, resources.find("BITMAP", bitmap_id), rectangle.left,
      rectangle.top, rectangle.left, rectangle.top,
      static_cast<int>(rectangle.right) - rectangle.left,
      static_cast<int>(rectangle.bottom) - rectangle.top);
}

void paint_original_elevator_control_grid(
    HDC dc,
    const simtower::OriginalResources& resources,
    const simtower::OriginalElevatorControlController& controller,
    const simtower::OriginalTdtDocument& document) {
  if (!controller.state().valid ||
      controller.state().elevator_index >= document.elevators.size()) {
    return;
  }
  const auto& elevator = document.elevators[controller.state().elevator_index];
  const auto* grid_source = controller.rectangle(6U);
  if (!grid_source) return;
  RECT grid = original_native_rectangle(*grid_source);
  HBRUSH black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  HBRUSH white = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  FrameRect(dc, &grid, black);

  HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
  HGDIOBJ previous_pen = pen ? SelectObject(dc, pen) : nullptr;
  int active_columns = 0;
  for (const auto& car : elevator.car_records) {
    if (car.exact_bytes[15] != std::byte{0}) ++active_columns;
  }
  for (int line_index = 1; line_index <= active_columns + 1; ++line_index) {
    const int x =
        grid.left + line_index * simtower::kOriginalElevatorControlCellSize;
    MoveToEx(dc, x, grid.top, nullptr);
    LineTo(dc, x, grid.bottom - 1);
  }
  for (int line_index = 1;
       line_index < simtower::kOriginalElevatorControlVisibleFloors;
       ++line_index) {
    const int y =
        grid.top + line_index * simtower::kOriginalElevatorControlCellSize;
    MoveToEx(dc, grid.left, y, nullptr);
    LineTo(dc, grid.right - 1, y);
  }

  // Tracked change (cleanup, 1098:19a4-19d6): the labels' text-cell
  // bottom at cell.bottom + vertical_inset (TA_BOTTOM), was TA_TOP at
  // cell.top + vertical_inset.
  const auto old_alignment = SetTextAlign(dc, TA_LEFT | TA_BOTTOM);
  const auto old_background_mode = SetBkMode(dc, TRANSPARENT);
  const auto old_text_color = GetTextColor(dc);
  const auto old_background_color = GetBkColor(dc);
  HFONT floor_font = make_original_dialog_font(13);
  HFONT small_floor_font = make_original_dialog_font(9);
  HGDIOBJ old_font = floor_font ? SelectObject(dc, floor_font) : nullptr;

  for (std::int16_t row = 0;
       row < simtower::kOriginalElevatorControlVisibleFloors; ++row) {
    const auto cell_source =
        simtower::original_elevator_control_cell_rect(-1, row);
    RECT cell = original_native_rectangle(cell_source);
    const auto floor_plan =
        simtower::original_elevator_control_floor_cell_plan(
            elevator, controller.state(), row);
    if (floor_plan.above_top) {
      HBRUSH gray = CreateSolidBrush(RGB(136, 136, 136));
      if (gray) {
        FillRect(dc, &cell, gray);
        DeleteObject(gray);
      }
      continue;
    }

    const bool serviced = floor_plan.serviced;
    FillRect(dc, &cell, serviced ? black : white);
    SetTextColor(dc, serviced ? RGB(255, 255, 255) : RGB(0, 0, 0));
    SetBkColor(dc, serviced ? RGB(0, 0, 0) : RGB(255, 255, 255));

    const auto label = original_cp1252_to_wide(floor_plan.label);
    if (floor_plan.small_font && small_floor_font) {
      SelectObject(dc, small_floor_font);
    } else if (floor_font) {
      SelectObject(dc, floor_font);
    }
    SIZE extent{};
    GetTextExtentPoint32W(dc, label.data(), static_cast<int>(label.size()),
                          &extent);
    const int x = cell.left + ((cell.right - cell.left - extent.cx) / 2) +
                  floor_plan.horizontal_inset;
    const int y = cell.bottom + floor_plan.vertical_inset;
    TextOutW(dc, x, y, label.data(), static_cast<int>(label.size()));

    if (!serviced) {
      MoveToEx(dc, cell.left, cell.top, nullptr);
      LineTo(dc, cell.right - 1, cell.bottom - 1);
      MoveToEx(dc, cell.right - 1, cell.top, nullptr);
      LineTo(dc, cell.left, cell.bottom - 1);
    }
  }

  if (old_font) SelectObject(dc, old_font);
  SetTextColor(dc, old_text_color);
  SetBkColor(dc, old_background_color);
  SetBkMode(dc, old_background_mode);
  SetTextAlign(dc, old_alignment);

  std::int16_t visual_column = 0;
  for (std::size_t car_index = 0U; car_index < elevator.car_records.size();
       ++car_index) {
    if (elevator.car_records[car_index].exact_bytes[15] == std::byte{0}) {
      continue;
    }
    for (std::int16_t row = 0;
         row < simtower::kOriginalElevatorControlVisibleFloors; ++row) {
      const auto floor = simtower::original_elevator_control_visible_floor(
          elevator, controller.state(), row);
      const auto bitmap = simtower::original_elevator_control_car_bitmap(
          document, controller.state().elevator_index, car_index, floor);
      if (bitmap == 0U) continue;
      const auto cell =
          simtower::original_elevator_control_cell_rect(visual_column, row);
      simtower::draw_original_dib_region(
          dc, resources.find("BITMAP", bitmap), cell.left, cell.top, 0, 0,
          cell.right - cell.left, cell.bottom - cell.top);
    }
    if (const auto frame =
            simtower::original_elevator_control_current_car_frame(
                document, controller.state(), car_index, visual_column)) {
      RECT cell = original_native_rectangle(*frame);
      FrameRect(dc, &cell, black);
    }
    ++visual_column;
  }

  if (pen) {
    SelectObject(dc, previous_pen);
    DeleteObject(pen);
  }
}

void paint_original_elevator_control(
    HDC dc,
    const simtower::OriginalResources& resources,
    const simtower::OriginalElevatorControlController& controller,
    const simtower::OriginalTdtDocument* document) {
  simtower::draw_original_dib(dc, resources.find("BITMAP", 400), 0, 0);
  if (!document || !controller.state().valid ||
      controller.state().elevator_index >= document->elevators.size()) {
    return;
  }
  const auto& elevator =
      document->elevators[controller.state().elevator_index];

  if (controller.state().isolation_active) {
    if (const auto* rectangle = controller.rectangle(2U)) {
      draw_original_elevator_control_region(dc, resources, 410, *rectangle);
    }
  }
  if (const auto* rectangle =
          controller.rectangle(12U + controller.state().schedule_bank)) {
    draw_original_elevator_control_region(dc, resources, 401, *rectangle);
  }
  if (controller.pressed_item() == 1) {
    if (const auto* rectangle = controller.rectangle(1U)) {
      draw_original_elevator_control_region(dc, resources, 401, *rectangle);
    }
  } else if (controller.pressed_item() == 2) {
    if (const auto* rectangle = controller.rectangle(2U)) {
      draw_original_elevator_control_region(
          dc, resources, controller.state().isolation_active ? 411 : 401,
          *rectangle);
    }
  }

  for (std::uint8_t phase = 0U; phase < 6U; ++phase) {
    if (const auto* rectangle = controller.rectangle(23U + phase)) {
      const auto mode = std::min<std::uint8_t>(
          2U, std::to_integer<std::uint8_t>(elevator.schedule[
                  simtower::original_elevator_control_schedule_index(
                      28U, controller.state().schedule_bank, phase)]));
      simtower::draw_original_dib(dc, resources.find("BITMAP", 402U + mode),
                                  rectangle->left, rectangle->top);
    }
  }
  if (const auto* selected =
          controller.rectangle(23U + controller.state().day_phase)) {
    RECT frame = original_native_rectangle(*selected);
    HBRUSH black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    FrameRect(dc, &frame, black);
    InflateRect(&frame, -1, -1);
    FrameRect(dc, &frame, black);
  }
  if (const auto* rectangle = controller.rectangle(5U)) {
    simtower::draw_original_dib(
        dc,
        resources.find("BITMAP",
                       simtower::original_elevator_control_show_bitmap(
                           elevator)),
        rectangle->left, rectangle->top);
  }

  const auto old_alignment = SetTextAlign(dc, TA_LEFT | TA_TOP);
  const auto old_background_mode = SetBkMode(dc, TRANSPARENT);
  const auto old_text = SetTextColor(dc, RGB(0, 0, 0));
  const auto old_background = SetBkColor(dc, RGB(255, 255, 255));
  HFONT font = make_original_dialog_font(16);
  HGDIOBJ old_font = font ? SelectObject(dc, font) : nullptr;
  const std::array values{
      std::pair<std::size_t, std::string>{
          41U, simtower::original_elevator_control_waiting_text(
                   elevator, controller.state())},
      std::pair<std::size_t, std::string>{
          42U, simtower::original_elevator_control_departure_text(
                   elevator, controller.state())},
  };
  for (const auto& [index, value] : values) {
    const auto* rectangle = controller.rectangle(index);
    if (!rectangle) continue;
    const auto wide = original_cp1252_to_wide(value);
    TextOutW(dc, static_cast<int>(rectangle->left) + 2,
             static_cast<int>(rectangle->top) + 3, wide.data(),
             static_cast<int>(wide.size()));
  }
  if (font) SelectObject(dc, old_font);
  SetTextColor(dc, old_text);
  SetBkColor(dc, old_background);
  SetBkMode(dc, old_background_mode);
  SetTextAlign(dc, old_alignment);

  paint_original_elevator_control_grid(dc, resources, controller, *document);
}

void paint_original_elevator_popup(HDC dc,
                                   const simtower::OriginalResources& resources,
                                   std::int16_t hover_mode) {
  simtower::draw_original_dib(dc, resources.find("BITMAP", 405), 0, 0);
  if (hover_mode < 0) return;
  const auto highlight = simtower::original_elevator_control_popup_highlight(
      static_cast<std::uint8_t>(hover_mode),
      simtower::kOriginalElevatorControlPopupWidth,
      simtower::kOriginalElevatorControlPopupHeight);
  if (!highlight) return;
  const int top = static_cast<int>(highlight->top);
  simtower::draw_original_dib_region(
      dc, resources.find("BITMAP", 406), 0, top, 0, top,
      static_cast<int>(highlight->right - highlight->left),
      static_cast<int>(highlight->bottom - highlight->top));
}

}  // namespace
}  // namespace legacy_elevator_control
