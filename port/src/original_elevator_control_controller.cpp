#include "original_elevator_control_controller.hpp"

#include "original_construction.hpp"
#include "original_dialog_paint.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace simtower {
namespace {

using Kind = OriginalDialogSignal::Kind;

// draw_original_dib_region(dc, dib, dst, src, w, h): the source pixels
// at (src_x, src_y) land at (dst_x, dst_y), clipped to the w x h area.
void draw_dib_region(OriginalDrawContext& context,
                     std::span<const std::byte> dib, int dst_x, int dst_y,
                     int src_x, int src_y, int width, int height) {
  if (dib.empty() || width <= 0 || height <= 0) return;
  OriginalSavedDrawState saved(context);
  context.intersect_clip({dst_x, dst_y, dst_x + width, dst_y + height});
  context.draw_dib(dib, dst_x - src_x, dst_y - src_y);
}

// draw_original_elevator_control_region: one DTMP rectangle of a bitmap
// at its own coordinates.
void draw_region(OriginalDrawContext& context,
                 const OriginalResources& resources, int bitmap_id,
                 const OriginalDtmpRect& rectangle) {
  draw_dib_region(context, resources.find("BITMAP", bitmap_id),
                  rectangle.left, rectangle.top, rectangle.left,
                  rectangle.top,
                  static_cast<int>(rectangle.right) - rectangle.left,
                  static_cast<int>(rectangle.bottom) - rectangle.top);
}

// TextOutW(x, y) with TA_LEFT | TA_TOP.
void text_at(OriginalDrawContext& context, int x, int y,
             std::string_view text) {
  context.move_to(x, y);
  context.text_out(text);
}

constexpr OriginalRgb kBlack = 0x000000U;
constexpr OriginalRgb kWhite = 0xFFFFFFU;
constexpr OriginalRgb kAboveTopGray = 0x888888U;

void paint_grid(OriginalDrawContext& context,
                const OriginalResources& resources,
                const OriginalElevatorControlController& controller,
                const OriginalTdtDocument& document) {
  // nm paint_original_elevator_control_grid (1098:16a4).
  const auto& state = controller.state();
  if (!state.valid || state.elevator_index >= document.elevators.size()) {
    return;
  }
  const auto& elevator = document.elevators[state.elevator_index];
  const auto* grid_source = controller.rectangle(6U);
  if (!grid_source) return;
  const auto grid = original_dtmp_rect(*grid_source);
  original_frame_rect(context, grid, kBlack);

  context.push_solid_pen(kBlack);
  int active_columns = 0;
  for (const auto& car : elevator.car_records) {
    if (car.exact_bytes[15] != std::byte{0}) ++active_columns;
  }
  for (int line = 1; line <= active_columns + 1; ++line) {
    const int x = grid.left + line * kOriginalElevatorControlCellSize;
    context.move_to(x, grid.top);
    context.line_to(x, grid.bottom - 1);
  }
  for (int line = 1; line < kOriginalElevatorControlVisibleFloors; ++line) {
    const int y = grid.top + line * kOriginalElevatorControlCellSize;
    context.move_to(grid.left, y);
    context.line_to(grid.right - 1, y);
  }

  {
    OriginalSavedDrawState saved(context);
    context.set_text_align(kOriginalTaUpdateCp);
    context.set_bk_transparent();
    {
      // The Win32 port's paint asked the 1208:0a8d font bank for 13, then
      // 9, here on every paint, whether or not a row needs 9; asking in
      // that order keeps the process-global bank's slot order unchanged
      // (pixel-neutral: both are deselected again before any text).
      const OriginalSelectedFont floor_font(context, 13);
      const OriginalSelectedFont small_floor_font(context, 9);
    }
    for (std::int16_t row = 0; row < kOriginalElevatorControlVisibleFloors;
         ++row) {
      const auto cell =
          original_dtmp_rect(original_elevator_control_cell_rect(-1, row));
      const auto plan =
          original_elevator_control_floor_cell_plan(elevator, state, row);
      if (plan.above_top) {
        context.fill_rect(cell, kAboveTopGray);
        continue;
      }
      const bool serviced = plan.serviced;
      context.fill_rect(cell, serviced ? kBlack : kWhite);
      context.set_text_color(serviced ? kWhite : kBlack);
      {
        OriginalSelectedFont font(context, plan.small_font ? 9 : 13);
        const int extent = context.text_extent_cx(plan.label);
        const int x = cell.left + ((cell.right - cell.left - extent) / 2) +
                      plan.horizontal_inset;
        const int y = cell.top + plan.vertical_inset;
        text_at(context, x, y, plan.label);
      }
      if (!serviced) {
        // 1098:226e BLACK_PEN diagonals, ending at right-1 / bottom-1.
        context.move_to(cell.left, cell.top);
        context.line_to(cell.right - 1, cell.bottom - 1);
        context.move_to(cell.right - 1, cell.top);
        context.line_to(cell.left, cell.bottom - 1);
      }
    }
  }

  std::int16_t visual_column = 0;
  for (std::size_t car_index = 0U; car_index < elevator.car_records.size();
       ++car_index) {
    if (elevator.car_records[car_index].exact_bytes[15] == std::byte{0}) {
      continue;
    }
    for (std::int16_t row = 0; row < kOriginalElevatorControlVisibleFloors;
         ++row) {
      const auto floor =
          original_elevator_control_visible_floor(elevator, state, row);
      const auto bitmap = original_elevator_control_car_bitmap(
          document, state.elevator_index, car_index, floor);
      if (bitmap == 0U) continue;
      const auto cell = original_elevator_control_cell_rect(visual_column,
                                                            row);
      // 11e0:0430 clips the 16x16 DIB to the 12x12 grid cell.
      draw_dib_region(context, resources.find("BITMAP", bitmap), cell.left,
                      cell.top, 0, 0, cell.right - cell.left,
                      cell.bottom - cell.top);
    }
    if (const auto frame = original_elevator_control_current_car_frame(
            document, state, car_index, visual_column)) {
      original_frame_rect(context, original_dtmp_rect(*frame), kBlack);
    }
    ++visual_column;
  }
  context.pop_pen();
}

}  // namespace

// --- ELVDLOGMAIN ----------------------------------------------------------

OriginalElevatorControlController::OriginalElevatorControlController(
    OriginalDtmp dtmp, OriginalElevatorControlState state)
    : dtmp_(std::move(dtmp)), state_(std::move(state)) {}

const OriginalDtmpRect* OriginalElevatorControlController::rectangle(
    std::size_t one_based) const {
  if (one_based == 0U || one_based > dtmp_.rectangles.size()) return nullptr;
  return &dtmp_.rectangles[one_based - 1U];
}

int OriginalElevatorControlController::hit_item(int x, int y) const {
  // nm original_elevator_control_hit_item: DTMP rectangles 1..28.
  for (std::size_t item = 1U; item <= kOriginalElevatorControlItemCount;
       ++item) {
    const auto* entry = rectangle(item);
    if (entry && original_dtmp_rect_contains(*entry, x, y)) {
      return static_cast<int>(item);
    }
  }
  return -1;
}

void OriginalElevatorControlController::disable_windows(
    OriginalElevatorControlHost& host) const {
  using Window = OriginalElevatorControlCloseWindow;
  for (const auto window :
       {Window::main, Window::map, Window::info, Window::command}) {
    host.enable_window(window, false);
  }
}

void OriginalElevatorControlController::initialize_scroll_bar(
    OriginalDialogOps& ops) const {
  // nm refresh_original_elevator_control_scrollbar. The Win32 port's call
  // order, kept for parity (paint-only difference): 1098:07c5-083d runs
  // SetScrollRange(FALSE), SetScrollPos(TRUE), EnableWindow(TRUE), then
  // UpdateWindow(scroll bar).
  const bool enabled = original_elevator_control_has_scrollbar(state_);
  ops.enable_item(kOriginalElevatorControlScrollBarId, enabled);
  if (enabled) {
    ops.set_scroll_range(kOriginalElevatorControlScrollBarId,
                         state_.scroll_min, state_.scroll_max);
    ops.set_scroll_pos(kOriginalElevatorControlScrollBarId,
                       state_.scroll_position);
  }
}

void OriginalElevatorControlController::signal(
    const OriginalDialogSignal& signal, OriginalDialogOps& ops,
    OriginalElevatorControlHost& host) {
  if (closed_) return;
  switch (signal.kind) {
    case Kind::scroll:
      scroll(signal.notify_code, static_cast<std::int16_t>(signal.value),
             ops);
      break;
    case Kind::mouse_move:
      // The Win32 port's repaint while Close / Simulate is pressed (kept
      // for parity). It draws from an unchanged pressed item, so it is
      // paint-only: 1098:09e0 only calls 11e0:0d80(0), SetCursor(IDC_ARROW),
      // which the hosts do through the class cursor.
      if (pressed_item_ >= 1 && pressed_item_ <= 2) {
        ops.invalidate();
        ops.update_window();
      }
      break;
    case Kind::button_down:
      button_down(signal.x, signal.y, ops, host);
      break;
    case Kind::button_up:
      button_up(signal.x, signal.y, ops, host);
      break;
    default:
      break;
  }
}

void OriginalElevatorControlController::scroll(std::uint16_t code,
                                               std::int16_t position,
                                               OriginalDialogOps& ops) {
  // nm 3564-3599 (1098:10c3 table at 126b).
  using Command = OriginalElevatorControlScrollCommand;
  std::optional<Command> command;
  switch (code) {
    case kOriginalScrollLineUp: command = Command::line_up; break;
    case kOriginalScrollLineDown: command = Command::line_down; break;
    case kOriginalScrollPageUp: command = Command::page_up; break;
    case kOriginalScrollPageDown: command = Command::page_down; break;
    case kOriginalScrollThumbPosition: command = Command::thumb_position;
      break;
    case kOriginalScrollThumbTrack: command = Command::thumb_track; break;
    default: break;
  }
  if (command &&
      original_elevator_control_scroll(state_, *command, position)) {
    ops.set_scroll_pos(kOriginalElevatorControlScrollBarId,
                       state_.scroll_position);
    ops.invalidate();
    ops.update_window();
  }
}

void OriginalElevatorControlController::mark_changed(
    bool world_changed, OriginalDialogOps& ops,
    OriginalElevatorControlHost& host) const {
  // nm mark_original_elevator_control_changed.
  host.mark_document_dirty();
  ops.invalidate();
  if (world_changed) {
    host.invalidate_main();
    host.invalidate_info();
    host.invalidate_map();
  }
}

std::uint8_t OriginalElevatorControlController::run_popup(
    std::size_t phase_item, OriginalElevatorControlHost& host) const {
  // nm run_original_elevator_popup.
  const auto* document = host.document();
  if (!document || state_.elevator_index >= document->elevators.size()) {
    return 0U;
  }
  const auto* phase = rectangle(phase_item);
  if (!phase) return 0U;
  const auto& elevator = document->elevators[state_.elevator_index];
  auto popup_state = state_;
  popup_state.day_phase = static_cast<std::uint8_t>(phase_item - 23U);
  const auto mode = std::min<std::uint8_t>(
      2U, original_elevator_control_floor_mode(elevator, popup_state));
  const int result = host.popup(*phase, phase_item, mode);
  return result >= 0 && result <= 2 ? static_cast<std::uint8_t>(result)
                                    : mode;
}

void OriginalElevatorControlController::button_down(
    int x, int y, OriginalDialogOps& ops, OriginalElevatorControlHost& host) {
  // nm 3607-3642 (1098:09ea, table at 1291). Kept as the Win32 port had it:
  // a press on no item still writes -1 and repaints (1098:0a48 returns
  // FALSE there and keeps DS:31b4), and neither host models the 11e0:0e84
  // audio pumps or the 1208:05e6 tick sample into DS:31b0/31b2 (0a57; no
  // reader in the port).
  auto* document = host.document();
  if (!document) return;
  const int item = hit_item(x, y);
  pressed_item_ = item;
  bool changed = false;
  if (item == 8) {
    changed = original_elevator_control_adjust_waiting(*document, state_, 1);
  } else if (item == 9) {
    changed = original_elevator_control_adjust_waiting(*document, state_, -1);
  } else if (item == 10) {
    changed =
        original_elevator_control_adjust_departure(*document, state_, 1);
  } else if (item == 11) {
    changed =
        original_elevator_control_adjust_departure(*document, state_, -1);
  } else if (item >= 23 && item <= 28 && !host.primary_button_down()) {
    // 1098:0cf2: the button is already up, so no popup and no schedule
    // write: the phase is selected, the document marked and the phase
    // buttons and DTMP 41/42 repainted.
    (void)original_elevator_control_select_phase(
        state_, static_cast<std::uint8_t>(item - 23));
    changed = true;
  } else if (item >= 23 && item <= 28) {
    const auto mode = run_popup(static_cast<std::size_t>(item), host);
    changed = original_elevator_control_select_phase(
                  state_, static_cast<std::uint8_t>(item - 23)) ||
              changed;
    changed = original_elevator_control_set_floor_mode(*document, state_,
                                                       mode) ||
              changed;
  }
  if (changed) {
    mark_changed(false, ops, host);
  } else {
    ops.invalidate();
  }
  ops.update_window();
}

void OriginalElevatorControlController::button_up(
    int x, int y, OriginalDialogOps& ops, OriginalElevatorControlHost& host) {
  // nm 3643-3683 (1098:0d65, table at 1277). Kept as the Win32 port had
  // it: a release on no item still resets DS:31b4 (1098:0ea6 returns
  // before the 10ba reset).
  auto* document = host.document();
  if (!document) return;
  const int item = hit_item(x, y);
  const int pressed = pressed_item_;
  pressed_item_ = -1;
  ops.invalidate();
  ops.update_window();
  if (item != pressed) return;
  if (item == 1) {
    close(ops, host);
    return;
  }
  if (item == 2) {
    toggle_simulation(ops, host);
    return;
  }
  if (item == 5 &&
      original_elevator_control_toggle_show(*document,
                                            state_.elevator_index)) {
    mark_changed(true, ops, host);
    return;
  }
  if (item == 6) {
    grid_action(x, y, ops, host);
    return;
  }
  if ((item == 12 || item == 13) &&
      original_elevator_control_select_bank(
          state_, static_cast<std::uint8_t>(item - 12))) {
    // 1098:2215 invalidates the fifteen schedule rows, then redraws the
    // grid and phase bands.
    ops.invalidate();
    ops.update_window();
  }
}

void OriginalElevatorControlController::grid_action(
    int x, int y, OriginalDialogOps& ops, OriginalElevatorControlHost& host) {
  // nm run_original_elevator_control_grid_action (1098:1ff5).
  auto* document = host.document();
  if (!document) return;
  const auto hit = original_elevator_control_grid_hit(*document, state_, x, y);
  if (!hit.hit()) return;
  if (state_.isolation_active) {
    (void)host.alert(1000, 1004U, 1U);
    return;
  }
  bool changed = false;
  if (hit.kind == OriginalElevatorControlGridKind::service_floor) {
    const auto elevator_index = state_.elevator_index;
    const auto floor = hit.floor;
    const auto& elevator = document->elevators[elevator_index];
    const bool serviced =
        floor >= 0 && floor < 120 &&
        elevator.serviced_floors[static_cast<std::size_t>(floor)] !=
            std::byte{0};
    if (!serviced) {
      changed = original_elevator_control_add_service_floor(
          *document, elevator_index, floor);
    } else {
      const auto warning = original_elevator_service_floor_warning_code(
          *document, elevator_index, floor);
      if (warning != 0U) {
        if (host.alert(1005, 1005U, warning) == 2) return;
      }
      const auto removal = original_elevator_control_remove_service_floor(
          *document, elevator_index, floor, host.part(), host.rent_income());
      changed = removal.cleanup.status ==
                OriginalElevatorFloorPeopleCleanupStatus::cleaned;
      if (changed) host.consume_family_dispatches(removal.family_dispatches);
    }
  } else if (hit.kind == OriginalElevatorControlGridKind::car) {
    changed = original_elevator_control_set_car_home(
        *document, state_.elevator_index,
        static_cast<std::size_t>(hit.car_index), hit.floor);
  }
  if (changed) mark_changed(true, ops, host);
}

void OriginalElevatorControlController::resume(
    OriginalDialogOps& ops, OriginalElevatorControlHost& host) {
  // nm resume_original_elevator_control (10f0:009c).
  auto* document = host.document();
  if (!document || !state_.isolation_active) return;
  if (resume_original_elevator_control_isolation(*document, state_,
                                                 host.build_mode())) {
    host.clear_transfer_visuals();
    ops.invalidate();
    host.invalidate_main();
    host.invalidate_map();
    host.invalidate_info();
    host.invalidate_command();
  }
}

void OriginalElevatorControlController::toggle_simulation(
    OriginalDialogOps& ops, OriginalElevatorControlHost& host) {
  // nm toggle_original_elevator_control_simulation (1098:0f66 -> 10f0).
  auto* document = host.document();
  if (!document) return;
  if (state_.isolation_active) {
    resume(ops, host);
    return;
  }
  if (!begin_original_elevator_control_isolation(*document, state_,
                                                 host.build_mode())) {
    return;
  }
  const auto preview = prepare_original_elevator_control_preview(
      *document, state_, host.part(), host.rent_income());
  if (!preview.prepared) {
    (void)host.alert(1000, 1004U, 2U);
    resume(ops, host);
    return;
  }
  for (std::size_t index = 0U; index < preview.movement_sound_requests;
       ++index) {
    host.play_movement_sound();
  }
  // 10f0:0090-0097 calls 1080:0a1e(0) after masking the isolated shaft.
  host.rebuild_main_without_sky();
  ops.invalidate();
  host.invalidate_command();
}

void OriginalElevatorControlController::close(
    OriginalDialogOps& ops, OriginalElevatorControlHost& host) {
  // nm close_original_elevator_control (1098:0ece-0f63).
  const auto plan = original_elevator_control_close_plan();
  if (plan.resume_isolation) resume(ops, host);
  if (plan.clear_published_window_before_destroy) host.unpublish();
  for (const auto window : plan.enable_order) host.enable_window(window, true);
  if (plan.explicitly_activate_main) host.activate_main();
  closed_ = true;
  host.destroy();
}

// --- ELVPOPUP -------------------------------------------------------------

OriginalDialogOutcome OriginalElevatorPopupController::signal(
    const OriginalDialogSignal& signal, OriginalDialogOps& ops) {
  // nm elevator_popup_dialog_proc (1098:22f8, table at 230d/27a5).
  const auto selection = [&] {
    return original_elevator_control_popup_selection(
        signal.x, signal.y, kOriginalElevatorControlPopupWidth,
        kOriginalElevatorControlPopupHeight);
  };
  switch (signal.kind) {
    case Kind::mouse_move: {
      const auto selected = selection();
      // 1098:2675 shares its out-of-bounds leg with 26b2's close path.
      if (!selected) return {original_mode_, false};
      if (static_cast<std::int16_t>(*selected) != hover_mode_) {
        hover_mode_ = static_cast<std::int16_t>(*selected);
        ops.invalidate();
        ops.update_window();
      }
      return {};
    }
    case Kind::button_up:
      return {static_cast<int>(selection().value_or(original_mode_)), false};
    case Kind::right_button_up:
      return {original_mode_, false};
    default:
      return {};
  }
}

// --- Painters -------------------------------------------------------------

void paint_original_elevator_control(
    OriginalDrawContext& context, const OriginalResources& resources,
    const OriginalElevatorControlController& controller,
    const OriginalTdtDocument* document) {
  // nm paint_original_elevator_control (1098:0068 / 12e9).
  context.draw_dib(resources.find("BITMAP", 400), 0, 0);
  const auto& state = controller.state();
  if (!document || !state.valid ||
      state.elevator_index >= document->elevators.size()) {
    return;
  }
  const auto& elevator = document->elevators[state.elevator_index];
  if (state.isolation_active) {
    if (const auto* rect = controller.rectangle(2U)) {
      draw_region(context, resources, 410, *rect);
    }
  }
  if (const auto* rect =
          controller.rectangle(12U + state.schedule_bank)) {
    draw_region(context, resources, 401, *rect);
  }
  if (controller.pressed_item() == 1) {
    if (const auto* rect = controller.rectangle(1U)) {
      draw_region(context, resources, 401, *rect);
    }
  } else if (controller.pressed_item() == 2) {
    if (const auto* rect = controller.rectangle(2U)) {
      draw_region(context, resources, state.isolation_active ? 411 : 401,
                  *rect);
    }
  }
  // 1098:1502 phase buttons and 1098:1498 selected-phase double frame.
  for (std::uint8_t phase = 0U; phase < 6U; ++phase) {
    if (const auto* rect = controller.rectangle(23U + phase)) {
      const auto mode = std::min<std::uint8_t>(
          2U, std::to_integer<std::uint8_t>(
                  elevator.schedule[original_elevator_control_schedule_index(
                      28U, state.schedule_bank, phase)]));
      context.draw_dib(resources.find("BITMAP", 402U + mode), rect->left,
                       rect->top);
    }
  }
  if (const auto* selected = controller.rectangle(23U + state.day_phase)) {
    auto frame = original_dtmp_rect(*selected);
    original_frame_rect(context, frame, kBlack);
    frame = {frame.left + 1, frame.top + 1, frame.right - 1,
             frame.bottom - 1};
    original_frame_rect(context, frame, kBlack);
  }
  if (const auto* rect = controller.rectangle(5U)) {
    context.draw_dib(
        resources.find("BITMAP",
                       original_elevator_control_show_bitmap(elevator)),
        rect->left, rect->top);
  }
  {
    // 1098:27bd / 2893: DTMP 41 waiting count and 42 departure delay.
    OriginalSavedDrawState saved(context);
    context.set_text_align(kOriginalTaUpdateCp);
    context.set_bk_transparent();
    context.set_text_color(kBlack);
    OriginalSelectedFont font(context, 16);
    const std::array values{
        std::pair<std::size_t, std::string>{
            kOriginalElevatorControlWaitingRectangle,
            original_elevator_control_waiting_text(elevator, state)},
        std::pair<std::size_t, std::string>{
            kOriginalElevatorControlDepartureRectangle,
            original_elevator_control_departure_text(elevator, state)},
    };
    for (const auto& [index, value] : values) {
      const auto* rect = controller.rectangle(index);
      if (!rect) continue;
      text_at(context, static_cast<int>(rect->left) + 2,
              static_cast<int>(rect->top) + 3, value);
    }
  }
  paint_grid(context, resources, controller, *document);
}

void paint_original_elevator_popup(OriginalDrawContext& context,
                                   const OriginalResources& resources,
                                   std::int16_t hover_mode) {
  // nm paint_original_elevator_popup (1098:26f2-2773).
  context.draw_dib(resources.find("BITMAP", 405), 0, 0);
  if (hover_mode < 0) return;
  const auto highlight = original_elevator_control_popup_highlight(
      static_cast<std::uint8_t>(hover_mode),
      kOriginalElevatorControlPopupWidth,
      kOriginalElevatorControlPopupHeight);
  if (!highlight) return;
  const int top = static_cast<int>(highlight->top);
  draw_dib_region(context, resources.find("BITMAP", 406), 0, top, 0, top,
                  static_cast<int>(highlight->right - highlight->left),
                  static_cast<int>(highlight->bottom - highlight->top));
}

// --- Isolation views ------------------------------------------------------

const OriginalTdtDocument* original_elevator_control_presentation_document(
    const OriginalTdtDocument& document,
    const OriginalElevatorControlState* state,
    std::optional<OriginalTdtDocument>& storage) {
  // nm original_presentation_document: 10f0 masks the shafts for dispatch,
  // while 10f0:0121/01f9's b3ae-aware presentation keeps drawing every
  // visible floor and tenant through 11a0:0000.
  if (!state || !state->isolation_active) return &document;
  storage = document;
  for (std::size_t index = 0U; index < storage->elevators.size(); ++index) {
    storage->elevators[index].used = state->saved_elevator_used[index];
  }
  return &*storage;
}

std::optional<OriginalElevatorWaitingIsolationView>
original_elevator_control_waiting_view(
    const OriginalElevatorControlState* state) {
  if (!state || !state->isolation_active || !state->saved_elevator_record) {
    return std::nullopt;
  }
  return OriginalElevatorWaitingIsolationView{
      state->elevator_index, &*state->saved_elevator_record};
}

}  // namespace simtower
