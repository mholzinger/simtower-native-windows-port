#include "original_dialog_model.hpp"

#include "original_dialog_paint.hpp"

#include <algorithm>
#include <utility>

namespace simtower {
namespace {

const OriginalDialogItem* find_item(const OriginalDialog& dialog,
                                    std::uint16_t id) noexcept {
  for (const auto& item : dialog.items) {
    if (item.id == id) return &item;
  }
  return nullptr;
}

}  // namespace

std::uint16_t original_dialog_first_tab_stop(
    const OriginalDialog& dialog,
    std::span<const std::uint16_t> shown_ids) noexcept {
  for (const auto& item : dialog.items) {
    const bool shown =
        (item.style & kOriginalWindowStyleVisible) != 0U ||
        std::ranges::find(shown_ids, item.id) != shown_ids.end();
    if ((item.style & kOriginalWindowStyleTabStop) != 0U && shown &&
        (item.style & kOriginalWindowStyleDisabled) == 0U) {
      return item.id;
    }
  }
  return 0U;
}

std::vector<std::uint16_t> original_dtmp_shown_children(
    const OriginalDialog& dialog,
    const OriginalDtmp& dtmp) {
  std::vector<std::uint16_t> shown;
  for (const auto& op : original_dtmp_render_ops(dtmp)) {
    if (op.kind == OriginalDtmpRenderOp::Kind::place_child &&
        find_item(dialog, op.child_id) != nullptr) {
      shown.push_back(op.child_id);
    }
  }
  return shown;
}

bool original_dialog_item_is_push_button(
    const OriginalDialogItem& item) noexcept {
  const auto type = item.style & kOriginalButtonTypeMask;
  return item.window_class.kind == OriginalDialogValue::Kind::ordinal &&
         item.window_class.ordinal == kOriginalDialogClassButton &&
         (type == kOriginalButtonPushButton ||
          type == kOriginalButtonDefPushButton);
}

OriginalWidgetKind original_dialog_item_widget_kind(
    const OriginalDialogItem& item) noexcept {
  std::uint16_t ordinal = 0U;
  if (item.window_class.kind == OriginalDialogValue::Kind::ordinal) {
    ordinal = item.window_class.ordinal;
  } else if (item.window_class.kind == OriginalDialogValue::Kind::text) {
    static constexpr std::pair<std::string_view, std::uint16_t> kNames[] = {
        {"button", kOriginalDialogClassButton},
        {"edit", kOriginalDialogClassEdit},
        {"static", kOriginalDialogClassStatic},
        {"listbox", kOriginalDialogClassListBox},
        {"scrollbar", kOriginalDialogClassScrollBar},
        {"combobox", kOriginalDialogClassComboBox}};
    const auto& name = item.window_class.text;
    for (const auto& [known, value] : kNames) {
      if (name.size() == known.size() &&
          std::equal(name.begin(), name.end(), known.begin(),
                     [](char a, char b) {
                       return (a >= 'A' && a <= 'Z' ? a - 'A' + 'a' : a) == b;
                     })) {
        ordinal = value;
      }
    }
  }
  switch (ordinal) {
    case kOriginalDialogClassButton:
      switch (item.style & kOriginalButtonTypeMask) {
        case kOriginalButtonPushButton:
          return OriginalWidgetKind::push_button;
        case kOriginalButtonDefPushButton:
          return OriginalWidgetKind::default_push_button;
        default:
          return OriginalWidgetKind::other;
      }
    case kOriginalDialogClassStatic:
      return (item.style & kOriginalStaticTypeMask) <= kOriginalStaticRight
                 ? OriginalWidgetKind::static_text
                 : OriginalWidgetKind::other;
    case kOriginalDialogClassEdit:
      return OriginalWidgetKind::edit;
    case kOriginalDialogClassListBox:
      return OriginalWidgetKind::list_box;
    case kOriginalDialogClassScrollBar:
      return OriginalWidgetKind::scroll_bar;
    case kOriginalDialogClassComboBox:
      return OriginalWidgetKind::combo_box;
    default:
      return OriginalWidgetKind::other;
  }
}

std::uint16_t original_dialog_enter_command(
    const OriginalDialog& dialog,
    std::uint16_t focused_id) noexcept {
  if (const auto* focused = find_item(dialog, focused_id);
      focused && original_dialog_item_is_push_button(*focused)) {
    return focused_id;
  }
  for (const auto& item : dialog.items) {
    if (original_dialog_item_is_push_button(item) &&
        (item.style & kOriginalButtonTypeMask) ==
            kOriginalButtonDefPushButton) {
      return item.id;
    }
  }
  return kOriginalDialogIdOk;
}

int original_dialog_font_pixels(std::uint16_t points) noexcept {
  // MulDiv rounds half away from zero.
  return (static_cast<int>(points) * 96 + 36) / 72;
}

OriginalDialogOutcome OriginalEventDialogController::signal(
    const OriginalDialogSignal& signal,
    std::int32_t balance) noexcept {
  std::uint16_t command = 0U;
  switch (signal.kind) {
    case OriginalDialogSignal::Kind::timer:
      // 1068:0380: KillTimer(wParam), then the accept path with the timer
      // flag that suppresses the ALRT/1004 warning.
      timer_fired_ = true;
      command = 2U;
      break;
    case OriginalDialogSignal::Kind::command:
      // The id only, as event_dialog_proc reads LOWORD(wParam); every
      // sender is BN_CLICKED today.
      if (signal.id != 1U && signal.id != 2U) return {};
      command = signal.id;
      break;
    case OriginalDialogSignal::Kind::close:
    case OriginalDialogSignal::Kind::draw_item:
    case OriginalDialogSignal::Kind::scroll:
    case OriginalDialogSignal::Kind::paint:
    case OriginalDialogSignal::Kind::key_up:
    case OriginalDialogSignal::Kind::button_down:
    case OriginalDialogSignal::Kind::button_up:
      return {};
  }
  switch (original_event_dialog_action(command, argument_, balance,
                                       timer_fired_)) {
    case OriginalEventDialogAction::ignore:
      return {};
    case OriginalEventDialogAction::
        warn_insufficient_funds_then_close_decline:
      return {1, true};
    case OriginalEventDialogAction::close_decline:
      return {1, false};
    case OriginalEventDialogAction::close_accept:
      return {2, false};
  }
  return {};
}

OriginalDialogOutcome original_about_dialog_signal(
    const OriginalDialogSignal& signal,
    OriginalDialogOps& ops) {
  switch (signal.kind) {
    case OriginalDialogSignal::Kind::close:
      return {1, false};
    case OriginalDialogSignal::Kind::timer:
      // 1010:0774: the credits move one step (the manager counts the
      // ticks) and the dialog repaints.
      ops.invalidate();
      return {};
    case OriginalDialogSignal::Kind::command:
    case OriginalDialogSignal::Kind::draw_item:
    case OriginalDialogSignal::Kind::scroll:
    case OriginalDialogSignal::Kind::paint:
    case OriginalDialogSignal::Kind::key_up:
    case OriginalDialogSignal::Kind::button_down:
    case OriginalDialogSignal::Kind::button_up:
      return {};
  }
  return {};
}

OriginalDialogOutcome original_message_box_signal(
    const OriginalDialogSignal& signal) noexcept {
  if (signal.kind == OriginalDialogSignal::Kind::command && signal.id != 0U) {
    return {signal.id, false};
  }
  return {};
}

OriginalEventDialogModel prepare_original_event_dialog(
    const OriginalResources& resources,
    const OriginalEventDialogRequest& request) {
  OriginalEventDialogModel model{};
  model.argument = request.argument;
  auto& original = model.dialog;
  original = parse_original_dialog(
      resources.find("DIALOG", request.dialog_id));
  // Exact 1068:0000 -> 1008:0085 event-dialog launcher path: after loading
  // the resource template, the original forces its font point size to 8.
  apply_original_dialog_font_point_size(original,
                                        kOriginalEventDialogPointSize);
  // 1068:0439 scans every original control before AHOTTADLOGFILTER's item-3
  // fallback at 1068:0175. Apply the exact destructive caret-pair pass to all
  // text controls, then apply the item-3 #0 fallback.
  for (auto& item : original.items) {
    if (item.text.kind != OriginalDialogValue::Kind::text) continue;
    item.text.text = format_original_dialog_caret_arguments(
        std::move(item.text.text), request.argument);
  }
  const auto item_three = std::ranges::find_if(
      original.items, [](const auto& item) { return item.id == 3U; });
  if (item_three != original.items.end() &&
      item_three->text.kind == OriginalDialogValue::Kind::text) {
    item_three->text.text = format_original_dialog_argument(
        std::move(item_three->text.text), request.argument);
  }
  model.dtmp = parse_original_dtmp(resources.find("DTMP", request.dialog_id));
  // The dialog manager picks the focus after WM_INITDIALOG, by which time
  // render_original_dtmp has shown every DTMP-placed child.
  model.initial_focus = original_dialog_first_tab_stop(
      original, original_dtmp_shown_children(original, model.dtmp));
  return model;
}

OriginalAboutDialogModel prepare_original_about_dialog(
    const OriginalResources& resources) {
  OriginalAboutDialogModel model{};
  model.dialog =
      parse_original_dialog(resources.find("DIALOG", "TOWER_TITLE"));
  model.layout = derive_original_about_layout(resources);
  model.lines = original_about_credit_lines(resources);
  model.chrome.rectangles.push_back(model.layout.credits_outer);
  return model;
}

OriginalMessageBoxRequest build_original_message_box(
    const OriginalAlertPresentation& presentation) {
  return {kOriginalAlertCaption, presentation.message, presentation.style,
          presentation.button_mode, false};
}

OriginalMessageBoxRequest original_replace_existing_message_box() {
  return {kOriginalReplaceExistingCaption, kOriginalReplaceExistingText,
          kOriginalAlertButtonsYesNo | kOriginalAlertIconExclamation,
          static_cast<std::uint16_t>(kOriginalAlertButtonsYesNo), true};
}

OriginalMessageBoxRequest original_invalid_filename_message_box() {
  return {kOriginalInvalidFilenameCaption, kOriginalInvalidFilenameText,
          kOriginalAlertButtonsOk | kOriginalAlertIconExclamation,
          static_cast<std::uint16_t>(kOriginalAlertButtonsOk), false};
}

OriginalMessageBoxRequest original_about_timer_failed_message_box() {
  return {kOriginalAboutTimerFailedCaption, kOriginalAboutTimerFailedText,
          kOriginalAlertButtonsOk | kOriginalAlertIconExclamation,
          static_cast<std::uint16_t>(kOriginalAlertButtonsOk), true};
}

std::vector<int> original_message_box_buttons(OriginalAlertStyle style) {
  switch (style & kOriginalMessageBoxTypeMask) {
    case kOriginalAlertButtonsOkCancel:
      return {kOriginalAlertResultOk, kOriginalAlertResultCancel};
    case kOriginalAlertButtonsAbortRetryIgnore:
      return {kOriginalAlertResultAbort, kOriginalAlertResultRetry,
              kOriginalAlertResultIgnore};
    case kOriginalAlertButtonsYesNoCancel:
      return {kOriginalAlertResultYes, kOriginalAlertResultNo,
              kOriginalAlertResultCancel};
    case kOriginalAlertButtonsYesNo:
      return {kOriginalAlertResultYes, kOriginalAlertResultNo};
    case kOriginalAlertButtonsRetryCancel:
      return {kOriginalAlertResultRetry, kOriginalAlertResultCancel};
    case kOriginalAlertButtonsOk:
    default:
      return {kOriginalAlertResultOk};
  }
}

}  // namespace simtower
