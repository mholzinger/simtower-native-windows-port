#include "original_person_information_controller.hpp"

namespace simtower {

bool OriginalPersonInformationController::initial_focus(
    OriginalDialogOps& ops) const {
  const auto plan = original_painted_dialog_initialization_focus_plan();
  if (plan.set_explicit_focus) ops.focus_item(kOriginalPersonInfoOkId);
  return plan.consume;
}

void OriginalPersonInformationController::refresh(
    OriginalDialogOps& ops, OriginalPersonInformationHost& host) {
  if (auto* document = host.document()) {
    information_ = original_person_information(
        host.resources(), *document, host.part(), person_index_, context_);
  }
  ops.invalidate();
  ops.update_window();
}

OriginalDialogOutcome OriginalPersonInformationController::signal(
    const OriginalDialogSignal& signal, OriginalDialogOps& ops,
    OriginalPersonInformationHost& host, bool* handled) {
  if (handled) *handled = false;
  if (signal.kind != OriginalDialogSignal::Kind::command) return {};
  if (signal.id == kOriginalPersonInfoOkId) {
    // 1100:030d-032b: the caller's close tail, EndDialog(1).
    if (handled) *handled = true;
    return {1, false};
  }
  if (signal.id == kOriginalPersonInfoRenameId && host.document()) {
    // 1100:032e-0387: Rename Person, the restored modal slot, then the
    // whole client repainted now.
    if (handled) *handled = true;
    const bool renamed = host.rename_person(person_index_);
    if (renamed) {
      changed_ = true;
      host.mark_document_dirty();
    }
    refresh(ops, host);
    return {};
  }
  return {};
}

}  // namespace simtower
