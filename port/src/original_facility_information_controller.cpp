#include "original_facility_information_controller.hpp"

namespace simtower {

void OriginalFacilityInformationController::initialize(
    OriginalDialogOps& ops) const {
  if (!information_.rent_control_visible) return;
  for (const auto& choice : information_.rent_choices) {
    ops.list_add(kOriginalFacilityChoiceId, choice);
  }
  ops.list_set_selection(kOriginalFacilityChoiceId,
                         information_.selected_rent_rate);
  ops.enable_item(kOriginalFacilityChoiceId,
                  information_.rent_control_enabled);
}

bool OriginalFacilityInformationController::initial_focus(
    OriginalDialogOps& ops) const {
  const auto plan = original_painted_dialog_initialization_focus_plan();
  if (plan.set_explicit_focus) ops.focus_item(kOriginalFacilityOkId);
  return plan.consume;
}

void OriginalFacilityInformationController::refresh(
    OriginalDialogOps& ops, OriginalFacilityInformationHost& host) {
  auto* document = host.document();
  if (!document) return;
  information_ = original_facility_information(
      host.resources(), *document, host.part(), floor_, tenant_index_);
  if (information_.rent_control_visible) {
    ops.list_set_selection(kOriginalFacilityChoiceId,
                           information_.selected_rent_rate);
    ops.enable_item(kOriginalFacilityChoiceId,
                    information_.rent_control_enabled);
  }
  ops.invalidate();
  ops.update_window();
}

OriginalDialogOutcome OriginalFacilityInformationController::signal(
    const OriginalDialogSignal& signal, OriginalDialogOps& ops,
    OriginalFacilityInformationHost& host, bool* handled) {
  if (handled) *handled = false;
  switch (signal.kind) {
    case OriginalDialogSignal::Kind::button_down: {
      // nm 4445-4485: a live portrait (1100:35b7) opens Person
      // Information; the dialog refreshes after it whatever it did.
      if (handled) {
        *handled = original_facility_information_click_plan().consume;
      }
      if (!host.document()) return {};
      const auto person = original_information_person_sprite_hit(
          information_.person_sprites, signal.x, signal.y);
      if (!person) return {};
      const bool changed = host.person_information(*person);
      changed_ = changed_ || changed;
      refresh(ops, host);
      return {};
    }
    case OriginalDialogSignal::Kind::mouse_move: {
      // 1100:5043 (items 4 and 9) for groups 9..11, else 1100:4fba (item
      // 4 only).
      if (handled) *handled = true;
      const bool include_item_9 = information_.dialog_group >= 9U &&
                                  information_.dialog_group <= 11U;
      const bool portrait_panel = original_information_portrait_panel_hit(
          dtmp_, signal.x, signal.y, include_item_9);
      host.set_cursor(portrait_panel ? kOriginalInformationPortraitCursor : 0U);
      return {};
    }
    case OriginalDialogSignal::Kind::command:
      break;
    default:
      return {};
  }
  using Action = OriginalFacilityInformationCommandAction;
  const auto plan = original_facility_information_command_plan(
      signal.id, signal.notify_code, information_.dialog_group);
  if (handled) *handled = plan.consume;
  switch (plan.action) {
    case Action::close:
      return {1, false};
    case Action::rename: {
      if (!host.document()) return {};
      const bool renamed = host.rename_tenant(floor_, tenant_index_);
      if (renamed) {
        changed_ = true;
        host.mark_document_dirty();
      }
      refresh(ops, host);
      return {};
    }
    case Action::change_rent: {
      auto* document = host.document();
      if (!document) return {};
      const int selection = ops.list_selection(kOriginalFacilityChoiceId);
      if (selection >= 0 && selection <= 3 &&
          set_original_facility_rent_rate(
              *document, host.part(), floor_, tenant_index_,
              static_cast<std::uint8_t>(selection))) {
        changed_ = true;
        host.mark_document_dirty();
        refresh(ops, host);
      }
      return {};
    }
    case Action::choose_movie:
      if (host.document() &&
          information_.linked_record_index !=
              OriginalMagnifierTarget::kNoIndex &&
          host.choose_movie(information_.linked_record_index)) {
        changed_ = true;
        host.mark_document_dirty();
        refresh(ops, host);
      }
      return {};
    case Action::none:
      return {};
  }
  return {};
}

}  // namespace simtower
