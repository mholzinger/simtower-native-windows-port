#pragma once

// Portable controller of Person Information (DIALOG/DTMP 763 / 764,
// PEPLEINFODLOGFILTER 1100:0116), over OriginalDialogOps (the dialog's own
// invalidation) and a host interface (the document and Rename Person).
// Both hosts run it: the Win32 procedure (person_information_dialog_proc
// in native_main.cpp, its WM_COMMAND) over Win32DialogOps, and the SDL
// session over its DialogWindow. The Win32 procedure keeps its DC,
// palette, font, brush, WM_CTLCOLOR*, WM_ACTIVATE, the capture, the
// DS:31a4 close tail and EndDialog. The frozen copy of the old Win32
// branch in person_information_controller_tests.cpp proves the call order
// is unchanged. No windows.h here.
//
// Painting is not shared yet: the Win32 procedure keeps its GDI painter
// (paint_original_person_dialog in native_main.cpp) so its GDI call
// sequence stays exactly as before this controller; the SDL host paints
// with paint_original_person_information (original_information_paint.hpp).
// The portable painter's test checks it issues the Win32 painter's
// draw_original_dib_region_scaled arguments and text origins; there is no
// pixel (DIB memcmp) parity between the two painters. Moving Win32 onto
// the portable painter over OriginalGdiDrawContext is a separate change.
//
// PEPLEINFODLOGFILTER's message table (cs:0398) handles WM_ACTIVATE,
// WM_PAINT, WM_CTLCOLOR, WM_INITDIALOG and WM_COMMAND only: no mouse, key
// or timer message reaches it. WM_COMMAND compares the whole wParam and
// ignores the notification: 1 closes with 1, 4 opens Rename Person
// (1100:39df, DIALOG 730), anything else (IDCANCEL from Esc included) is
// not handled and the dialog stays open.
//
// What the original does that the Win32 port (and so this controller) does
// differently is listed for the owner, not applied (fdis.py of 1100:0000,
// 1100:0116 and 1100:35b7): the nested callers pass their own dialog as
// owner where 1100:00d5 always uses DS:3258 (Main); the Rename path
// dirties the document and the title where 1100:0334 ignores 39df's
// result; the information is cached and recomputed after Rename where
// 1100:151b recomputes on every paint (equivalent while the dialog is
// modal); DS:24a8 is a sign-extended 16-bit person index in the original
// (an index of 32768 or more shows the wrong person), a size_t here.

#include "original_dialog_model.hpp"
#include "original_dtmp.hpp"
#include "original_find.hpp"
#include "original_information.hpp"
#include "original_information_host.hpp"
#include "original_resources.hpp"
#include "original_tables.hpp"
#include "original_tdt.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace simtower {

// The dialog item ids PEPLEINFODLOGFILTER addresses.
inline constexpr std::uint16_t kOriginalPersonInfoOkId = 1U;
inline constexpr std::uint16_t kOriginalPersonInfoRenameId = 4U;

// What PEPLEINFODLOGFILTER reaches outside its own controls.
// (document() null: Rename is not handled; set_cursor is not used, the
// filter has no WM_MOUSEMOVE.)
class OriginalPersonInformationHost : public OriginalInformationHostBase {
 public:
  // 1100:032e-0359: DS:31a4 cleared for the nested modal, Rename Person
  // (1100:39df, DIALOG 730), then DS:31a4 and TOPMOST restored, also when
  // it was cancelled. Whether the name changed.
  virtual bool rename_person(std::size_t person_index) = 0;
};

class OriginalPersonInformationController {
 public:
  OriginalPersonInformationController(
      std::size_t person_index,
      OriginalPersonInformationContext context,
      OriginalPersonInformation information,
      OriginalDtmp dtmp)
      : person_index_(person_index),
        context_(context),
        information_(std::move(information)),
        dtmp_(std::move(dtmp)) {}

  // The end of WM_INITDIALOG: the painted-dialog focus plan
  // (original_painted_dialog_initialization_focus_plan: no SetFocus; TRUE
  // lets the dialog manager focus OK).
  [[nodiscard]] bool initial_focus(OriginalDialogOps& ops) const;
  // WM_COMMAND (nm 3883-3916 before the controller): 1 closes with 1 (the
  // caller keeps its close tail), 4 renames with a document. Every other
  // signal is not handled. *handled (when given) receives whether this
  // signal was handled (the Win32 procedure's TRUE); it is returned per
  // call so a signal nested inside Rename Person cannot overwrite it.
  [[nodiscard]] OriginalDialogOutcome signal(
      const OriginalDialogSignal& signal,
      OriginalDialogOps& ops,
      OriginalPersonInformationHost& host,
      bool* handled = nullptr);
  // The information recomputed (1100:151b's fields) and the whole client
  // repainted now: InvalidateRect(all, FALSE) + UpdateWindow.
  void refresh(OriginalDialogOps& ops, OriginalPersonInformationHost& host);

  [[nodiscard]] const OriginalPersonInformation& information() const {
    return information_;
  }
  [[nodiscard]] const OriginalDtmp& dtmp() const { return dtmp_; }
  [[nodiscard]] std::size_t person_index() const { return person_index_; }
  [[nodiscard]] OriginalPersonInformationContext context() const {
    return context_;
  }
  [[nodiscard]] bool changed() const { return changed_; }

 private:
  std::size_t person_index_{};
  OriginalPersonInformationContext context_{
      OriginalPersonInformationContext::main_world};
  OriginalPersonInformation information_{};
  OriginalDtmp dtmp_{};
  bool changed_{};
};

// 1100:00f3-0107 after DialogBoxParam returns: an active Find marker for
// this same person sets DS:77c0 (phase 1), which 1058:019f consumes after
// the Magnifier dispatch. Whether it was set.
inline bool apply_original_person_information_exit(
    OriginalFindMarkerState& marker, std::size_t person_index) noexcept {
  if (!original_person_information_sets_find_exit_latch(marker,
                                                        person_index)) {
    return false;
  }
  marker.phase = 1U;
  return true;
}

}  // namespace simtower
