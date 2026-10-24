#include "original_dialog_controllers.hpp"

#include "original_ui.hpp"

#include <bit>
#include <stdexcept>
#include <utility>

namespace simtower {
namespace {

const OriginalDtmpRect& finance_rectangle(const OriginalDtmp& dtmp,
                                          std::size_t one_based_index) {
  if (one_based_index == 0U || one_based_index > dtmp.rectangles.size()) {
    throw std::runtime_error("Original Finance DTMP rectangle is missing");
  }
  return dtmp.rectangles[one_based_index - 1U];
}

}  // namespace

// --- Find ---------------------------------------------------------------

void OriginalFindController::initialize(OriginalDialogOps& ops) const {
  ops.list_reset(kOriginalFindListId);
  for (const auto& entry : entries_) {
    ops.list_add(kOriginalFindListId, entry.name);
  }
  ops.enable_item(kOriginalFindRemoveId, false);
  ops.enable_item(kOriginalFindFindId, false);
}

bool OriginalFindController::initial_focus(OriginalDialogOps& ops) const {
  const auto focus = original_find_initialization_focus_plan();
  if (focus.focus_list) ops.focus_item(kOriginalFindListId);
  return focus.handled_result;
}

bool OriginalFindController::resolve(OriginalDialogOps& ops,
                                     OriginalFindDialogHost& host) {
  // resolve_original_find_selection (nm 5330-5391).
  auto* document = host.document();
  if (!document) return false;
  const int selected = ops.list_selection(kOriginalFindListId);
  if (selected < 0 || static_cast<std::size_t>(selected) >= entries_.size()) {
    return false;
  }
  const int width = host.main_client_width();
  const int height = host.main_client_height();
  const auto& entry = entries_[static_cast<std::size_t>(selected)];
  // A previous DS:77b4 target is cleared before resolving.
  host.reset_marker();
  if (mode_ == OriginalFindMode::person) {
    resolution_ =
        resolve_original_find_person(*document, entry.link, width, height);
  } else {
    resolution_ = resolve_original_find_tenant(
        *document, static_cast<std::uint16_t>(entry.link), width, height);
  }
  if (mode_ == OriginalFindMode::person && resolution_.alerts()) {
    // 10e0:0669/06cd: ALRT/1002 (not in the tower) or ALRT/1003 with the
    // lobby floor; Find stays open with the marker reset.
    host.set_arrow_cursor();
    if (resolution_.kind ==
        OriginalFindResolutionKind::not_in_tower_alert) {
      host.alert(1002, {entry.name, {}, {}, {}});
    } else {
      host.alert(1003, {entry.name,
                        original_find_floor_text(resolution_.floor), {}, {}});
    }
    resolution_ = {};
    host.invalidate_main();
    return false;
  }
  // 10e0:0cc9 keeps the modal open for a target that no longer resolves.
  if (!resolution_.focused()) return false;
  host.start_marker(resolution_, mode_ == OriginalFindMode::person
                                     ? entry.link
                                     : 0xffffffffU);
  return true;
}

OriginalDialogOutcome OriginalFindController::signal(
    const OriginalDialogSignal& signal, OriginalDialogOps& ops,
    OriginalFindDialogHost& host) {
  if (signal.kind != OriginalDialogSignal::Kind::command) return {};
  using Action = OriginalFindDialogCommandAction;
  const auto plan =
      original_find_dialog_command_plan(signal.id, signal.notify_code);
  switch (plan.action) {
    case Action::close:
      // 10d8:018c: 10e0:04cf(0), then cleanup and EndDialog(1).
      host.reset_marker();
      host.invalidate_main();
      return {1, false};
    case Action::remove: {
      auto* document = host.document();
      if (!document) return {};
      const int selected = ops.list_selection(kOriginalFindListId);
      if (selected >= 0 &&
          remove_original_find_entry(*document, mode_,
                                     static_cast<std::size_t>(selected))) {
        ops.list_delete(kOriginalFindListId, selected);
        entries_.erase(entries_.begin() +
                       static_cast<std::ptrdiff_t>(selected));
        changed_ = true;
        host.entry_removed();
      }
      return {};
    }
    case Action::resolve:
      if (resolve(ops, host)) return {1, false};
      return {};
    case Action::enable_actions:
    case Action::disable_actions: {
      const bool enabled = plan.action == Action::enable_actions;
      ops.enable_item(kOriginalFindRemoveId, enabled);
      ops.enable_item(kOriginalFindFindId, enabled);
      return {};
    }
    case Action::none:
      return {};
  }
  return {};
}

// --- Rename -------------------------------------------------------------

OriginalRenameSaveResult original_rename_save(
    OriginalTdtDocument& document, const OriginalRenameTarget& target,
    std::string_view cp1252) {
  OriginalRenameSaveResult result{};
  if (target.person) {
    const auto saved =
        set_original_person_name(document, target.person_index, cp1252);
    if (saved.status == OriginalPersonNameStatus::too_long) {
      result.alert_strl_index = 4U;
      return result;
    }
    if (saved.status == OriginalPersonNameStatus::full) {
      result.alert_strl_index = 6U;
    }
    result.changed = saved.changed;
  } else {
    const auto saved = set_original_tenant_name(
        document, target.floor, target.tenant_index, cp1252);
    if (saved.status == OriginalTenantNameStatus::too_long) {
      result.alert_strl_index = 4U;
      return result;
    }
    if (saved.status == OriginalTenantNameStatus::full) {
      result.alert_strl_index = 7U;
    }
    result.changed = saved.changed;
  }
  result.close = true;
  return result;
}

bool original_rename_remove(OriginalTdtDocument& document,
                            const OriginalRenameTarget& target) {
  if (target.person) {
    return remove_original_person_name(document, target.person_index).changed;
  }
  return remove_original_tenant_name(document, target.floor,
                                     target.tenant_index)
      .changed;
}

std::string original_rename_initial_text(const OriginalTdtDocument& document,
                                         const OriginalRenameTarget& target) {
  return target.person
             ? original_person_saved_name(document, target.person_index)
             : original_tenant_saved_name(document, target.floor,
                                          target.tenant_index);
}

void OriginalRenameController::initialize(
    OriginalDialogOps& ops, const OriginalTdtDocument& document) const {
  const auto name = original_rename_initial_text(document, target_);
  ops.set_item_text(kOriginalRenameEditId, name);
  ops.enable_item(kOriginalRenameRemoveId, !name.empty());
  ops.enable_item(kOriginalRenameOkId,
                  original_rename_ok_enabled(
                      ops.item_text(kOriginalRenameEditId).size()));
}

OriginalDialogOutcome OriginalRenameController::signal(
    const OriginalDialogSignal& signal, OriginalDialogOps& ops,
    OriginalRenameDialogHost& host) {
  if (signal.kind == OriginalDialogSignal::Kind::paint) {
    // 1100:3c60-3c7a / 3fd7-3ff1: after every paint.
    ops.enable_item(kOriginalRenameEditId, true);
    const auto focus = original_rename_dialog_focus_plan(
        OriginalRenameDialogFocusPhase::after_paint);
    if (focus.focus_edit) ops.focus_item(kOriginalRenameEditId);
    return {};
  }
  if (signal.kind != OriginalDialogSignal::Kind::command) return {};
  using Action = OriginalRenameDialogCommandAction;
  const auto plan = original_rename_dialog_command_plan(signal.id);
  const int close = plan.close_result;
  switch (plan.action) {
    case Action::refresh_edit_gate:
      ops.enable_item(kOriginalRenameOkId,
                      original_rename_ok_enabled(
                          ops.item_text(kOriginalRenameEditId).size()));
      return {};
    case Action::save: {
      auto* document = host.document();
      if (!document) return {};
      const auto text = ops.item_text(kOriginalRenameEditId);
      if (text.empty()) return {};
      const auto saved = original_rename_save(*document, target_, text);
      if (saved.alert_strl_index) host.alert_strl(*saved.alert_strl_index);
      if (!saved.close) return {};
      changed_ = changed_ || saved.changed;
      return {close, false};
    }
    case Action::cancel:
      return {close, false};
    case Action::remove:
      if (auto* document = host.document()) {
        changed_ = original_rename_remove(*document, target_) || changed_;
      }
      return {close, false};
    case Action::none:
      return {};
  }
  return {};
}

// --- New Movie ------------------------------------------------------------

OriginalMovieChoiceEnabled original_movie_choice_enabled(
    const OriginalTdtDocument& document,
    const OriginalPartTable& part) noexcept {
  const auto new_cost = std::bit_cast<std::int16_t>(part.words_52_to_ac[34]);
  const auto classic_cost =
      std::bit_cast<std::int16_t>(part.words_52_to_ac[35]);
  return {document.header.balance >= new_cost,
          document.header.balance >= classic_cost};
}

void OriginalMovieChoiceController::initialize(
    OriginalDialogOps& ops, const OriginalTdtDocument& document,
    const OriginalPartTable& part) const {
  const auto enabled = original_movie_choice_enabled(document, part);
  ops.enable_item(kOriginalMovieNewId, enabled.new_release);
  ops.enable_item(kOriginalMovieClassicId, enabled.classic);
}

OriginalDialogOutcome OriginalMovieChoiceController::signal(
    const OriginalDialogSignal& signal, OriginalTdtDocument* document,
    const OriginalPartTable& part) {
  if (signal.kind != OriginalDialogSignal::Kind::command) return {};
  using Action = OriginalMovieChoiceDialogCommandAction;
  const auto plan = original_movie_choice_dialog_command_plan(signal.id);
  switch (plan.action) {
    case Action::cancel:
      return {0, false};
    case Action::new_release:
    case Action::classic:
      if (document) {
        const auto choice = plan.action == Action::new_release
                                ? OriginalMovieChoice::new_release
                                : OriginalMovieChoice::classic;
        changed_ = choose_original_movie(*document, part, record_, choice)
                       .changed ||
                   changed_;
      }
      // 1100:43b4-43c2: always EndDialog(1).
      return {1, false};
    case Action::none:
      return {};
  }
  return {};
}

// --- Finance ------------------------------------------------------------

void OriginalFinanceController::repaint_button(OriginalDialogOps& ops) const {
  ops.invalidate_rect(button_);
  ops.update_window();
}

OriginalDialogOutcome OriginalFinanceController::signal(
    const OriginalDialogSignal& signal, OriginalDialogOps& ops) {
  switch (signal.kind) {
    case OriginalDialogSignal::Kind::key_up: {
      const auto presentation = original_finance_key_presentation(signal.id);
      // nm 5155-5176: each surface is painted synchronously
      // (InvalidateRect of the button, UpdateWindow).
      if (presentation.draw_pressed) {
        pressed_ = true;
        repaint_button(ops);
      }
      if (presentation.draw_released) {
        pressed_ = false;
        repaint_button(ops);
      }
      if (presentation.close) return {0, false};
      return {};
    }
    case OriginalDialogSignal::Kind::button_down:
      if (original_rect_contains(button_, signal.x, signal.y)) {
        pressed_ = true;
        repaint_button(ops);
      }
      return {};
    case OriginalDialogSignal::Kind::button_up: {
      const bool close = original_rect_contains(button_, signal.x, signal.y);
      pressed_ = false;
      repaint_button(ops);
      if (close) return {0, false};
      return {};
    }
    default:
      return {};
  }
}

std::vector<OriginalFinanceTextOp> original_finance_text_ops(
    const OriginalFinanceView& view, const OriginalDtmp& dtmp,
    const std::function<int(int, std::string_view)>& measure) {
  std::vector<OriginalFinanceTextOp> ops;
  const auto summary = [&](std::size_t index, std::int32_t value,
                           int pixels) {
    OriginalFinanceTextOp op{};
    op.rectangle = index;
    op.font_pixels = pixels;
    op.text = std::to_string(value);
    const auto& rect = finance_rectangle(dtmp, index);
    const auto position = original_finance_value_position(
        index, rect.right, rect.top, measure(pixels, op.text));
    op.x = position.x;
    op.y = position.y;
    ops.push_back(std::move(op));
  };
  // nm 5096-5114: 14 pixels for 6-7, then 12 pixels.
  summary(6U, view.total_income, 14);
  summary(7U, view.total_maintenance, 14);
  summary(8U, view.year, 12);
  summary(9U, view.quarter, 12);
  summary(10U, view.net_revenues, 12);
  summary(11U, view.other_income, 12);
  summary(12U, view.construction_costs, 12);
  summary(13U, view.last_quarter_balance, 12);
  summary(14U, view.total_balance, 12);
  for (std::size_t row = 0U; row < 10U; ++row) {
    const std::array values{view.population[row], view.income[row],
                            view.maintenance[row]};
    for (std::size_t column = 0U; column < values.size(); ++column) {
      const auto& rect = finance_rectangle(dtmp, 3U + column);
      OriginalFinanceTextOp op{};
      op.rectangle = 3U + column;
      op.font_pixels = 12;
      op.right_baseline = true;
      op.x = static_cast<int>(rect.right) - 1;
      op.y = static_cast<int>(rect.top) + static_cast<int>(row + 1U) * 13 - 2;
      op.text = std::to_string(values[column]);
      ops.push_back(std::move(op));
    }
  }
  return ops;
}

void paint_original_finance(OriginalDrawContext& context,
                            const OriginalResources& resources,
                            const OriginalDtmp& dtmp,
                            const OriginalFinanceView& view, bool pressed) {
  // 1060:0105/0479: BITMAP/500, and BITMAP/501's button region while
  // pressed (the two bitmaps differ only there).
  context.draw_dib(resources.find("BITMAP", 500), 0, 0);
  if (pressed) {
    const auto& button =
        finance_rectangle(dtmp, kOriginalFinanceButtonRectangle);
    OriginalSavedDrawState saved(context);
    context.intersect_clip({button.left, button.top, button.right,
                            button.bottom});
    context.draw_dib(resources.find("BITMAP", 501), 0, 0);
  }
  OriginalSavedDrawState saved(context);
  context.set_bk_transparent();
  context.set_text_color(0x000000U);
  const auto measure = [&](int pixels, std::string_view text) {
    OriginalSelectedFont selected(context, pixels);
    return context.text_extent_cx(text);
  };
  for (const auto& op : original_finance_text_ops(view, dtmp, measure)) {
    OriginalSelectedFont selected(context, op.font_pixels);
    context.set_text_align(op.right_baseline
                               ? static_cast<std::uint16_t>(
                                     kOriginalTaUpdateCp | kOriginalTaRight |
                                     kOriginalTaBaseline)
                               : kOriginalTaUpdateCp);
    context.move_to(op.x, op.y);
    context.text_out(op.text);
  }
}

}  // namespace simtower
