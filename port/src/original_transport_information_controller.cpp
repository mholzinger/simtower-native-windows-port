#include "original_transport_information_controller.hpp"

namespace simtower {

bool OriginalTransportInformationController::initial_focus(
    OriginalDialogOps& ops) const {
  const auto plan = original_painted_dialog_initialization_focus_plan();
  if (plan.set_explicit_focus) ops.focus_item(kOriginalTransportInfoOkId);
  return plan.consume;
}

void OriginalTransportInformationController::refresh(
    OriginalDialogOps& ops, OriginalTransportInformationHost& host) {
  if (auto* document = host.document()) {
    text_ = original_transport_information_text(host.resources(), *document,
                                                target_);
  }
  ops.invalidate();
  ops.update_window();
}

OriginalDialogOutcome OriginalTransportInformationController::signal(
    const OriginalDialogSignal& signal, OriginalDialogOps& ops,
    OriginalTransportInformationHost& host, bool* handled) {
  if (handled) *handled = false;
  switch (signal.kind) {
    case OriginalDialogSignal::Kind::button_down: {
      // 1100:113a-11a8 / 13fc-14e9: every press is consumed; a live
      // portrait (1100:35b7) opens Person Information, then the strip is
      // repainted (1100:362a-363c).
      if (!click_plan().consume) return {};
      if (handled) *handled = true;
      if (!host.document()) return {};
      const auto person = original_information_person_sprite_hit(
          text_.person_sprites, signal.x, signal.y);
      if (!person) return {};
      const bool changed = host.person_information(*person);
      changed_ = changed_ || changed;
      refresh(ops, host);
      return {};
    }
    case OriginalDialogSignal::Kind::mouse_move: {
      // 1100:5043 (items 4 and 9).
      if (handled) *handled = true;
      const bool portrait_panel = original_information_portrait_panel_hit(
          dtmp_, signal.x, signal.y, true);
      host.set_cursor(portrait_panel ? kOriginalInformationPortraitCursor
                                     : 0U);
      return {};
    }
    case OriginalDialogSignal::Kind::command:
      // 1100:1113 / 13d5: only IDOK, whatever the notification.
      if (signal.id != kOriginalTransportInfoOkId) return {};
      if (handled) *handled = true;
      return {1, false};
    default:
      return {};
  }
}

}  // namespace simtower
