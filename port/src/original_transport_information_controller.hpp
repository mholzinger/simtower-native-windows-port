#pragma once

// Portable controller of Transport Information: the elevator car
// (DIALOG/DTMP: elevator type 0 (express) -> 761, every other type ->
// 762, as original_elevator_information_dialog_id; ELVINFODLOGFILTER
// 1100:0f10, opened at 1100:0e86) and the stairs / escalator (DIALOG/DTMP
// 761, ESCINFODLOGFILTER 1100:1248, opened at 1100:11da), over
// OriginalDialogOps (the dialog's own invalidation) and a host interface
// (the document, Person Information and the cursor). Both hosts run it:
// the Win32 procedure (transport_information_dialog_proc in
// native_main.cpp: WM_LBUTTONDOWN's portrait hit, WM_MOUSEMOVE's cursor,
// WM_COMMAND) over Win32DialogOps, and the SDL session over its
// DialogWindow. The Win32 procedure keeps its DC, palette, font, brush,
// WM_CTLCOLOR*, WM_ACTIVATE, the DS:31a4 / TOPMOST restoration around
// every click and EndDialog. The frozen copy of the old Win32 branches
// (event_dialog_proc with transport_target) in
// transport_information_controller_tests.cpp proves the call order is
// unchanged. No windows.h here.
//
// Painting is not shared yet: the Win32 procedure keeps its GDI painters
// (paint_original_information_text and paint_original_information_sprites
// in native_main.cpp, over OriginalTransportDialogContext) so its GDI call
// sequence stays exactly as before this controller; the SDL host paints
// with paint_original_transport_information
// (original_information_paint.hpp). The portable painter's test checks the
// Win32 painters' text origins and draw_original_dib_region_scaled
// arguments; there is no pixel (DIB memcmp) parity between the two.
// Moving Win32 onto the portable painter over OriginalGdiDrawContext is a
// separate change.
//
// Both filters have the same seven-message table (cs:11be / cs:14ff):
// WM_INITDIALOG, WM_PAINT, WM_CTLCOLOR, WM_ACTIVATE, WM_MOUSEMOVE,
// WM_COMMAND and WM_LBUTTONDOWN. WM_COMMAND compares the whole wParam: 1
// closes with 1 (1100:1113-1137 / 13d5-13f9), anything else (IDCANCEL from
// Esc included) is not handled and the dialog stays open. WM_LBUTTONDOWN
// runs the portrait strip's hit test (1100:327f / 3431 -> 35b7); a live
// portrait opens Person Information (1100:0000, DS:b3a6 = 1), whose exit
// may set the Find latch DS:77c0, then the strip is repainted. The dialog
// itself changes nothing in the document.
//
// What the original does that the Win32 port (and so this controller) does
// differently is listed for the owner, not applied (fdis.py of 1100:0e86,
// 0f10, 11da, 1248, 327f, 3431 and 35b7): Person Information is owned by
// the transport dialog where 1100:00d5 always uses DS:3258 (Main); the
// elevator strip stops at item 9's right edge where 327f's second row
// does not check it; 1070:051f (the DTMP teardown before EndDialog) and
// the INITDIALOG audio pump 11e0:0e84 are not run; the text and portraits
// are also painted at WM_INITDIALOG; after a drill-down the whole client
// is invalidated where the original invalidates the sprite rectangle only
// (the pixels are the same); DS:b3a8 / b3a6 are not shared with an open
// Elevator Control (its state is OriginalElevatorControlState).

#include "original_dialog_model.hpp"
#include "original_dtmp.hpp"
#include "original_information.hpp"
#include "original_information_host.hpp"
#include "original_resources.hpp"
#include "original_tdt.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace simtower {

// The dialog item the filters address (BS_DEFPUSHBUTTON OK).
inline constexpr std::uint16_t kOriginalTransportInfoOkId = 1U;

// What ELVINFODLOGFILTER / ESCINFODLOGFILTER reach outside their own
// controls (document() null: a press hits no portrait; part() and
// mark_document_dirty are not used, the dialog changes nothing itself).
class OriginalTransportInformationHost : public OriginalInformationHostBase {
 public:
  // 1100:35b7 -> 1100:0000: Person Information opened from this dialog
  // (DS:b3a6 = 1), with its Find exit latch; whether it changed the
  // document.
  virtual bool person_information(std::size_t person_index) = 0;
};

class OriginalTransportInformationController {
 public:
  OriginalTransportInformationController(OriginalMagnifierTarget target,
                                         OriginalTransportInformationText text,
                                         OriginalDtmp dtmp)
      : target_(target), text_(std::move(text)), dtmp_(std::move(dtmp)) {}

  // The end of WM_INITDIALOG: the painted-dialog focus plan
  // (original_painted_dialog_initialization_focus_plan: no SetFocus; TRUE
  // lets the dialog manager focus OK).
  [[nodiscard]] bool initial_focus(OriginalDialogOps& ops) const;
  // WM_COMMAND 1 closes with 1 (the caller clears DS:31a4 and ends the
  // dialog); WM_LBUTTONDOWN with a tower runs the portrait hit (a live
  // portrait opens Person Information, then the text and strip are
  // recomputed and the client repainted now); WM_MOUSEMOVE sets the
  // portrait-panel cursor over item 4 or 9. Everything else is not
  // handled. The caller keeps the click's DC, palette and restorations
  // (click_plan()). *handled (when given) receives whether this signal
  // was handled (the Win32 procedure's TRUE), per call, so a signal nested
  // inside Person Information cannot overwrite it.
  [[nodiscard]] OriginalDialogOutcome signal(
      const OriginalDialogSignal& signal,
      OriginalDialogOps& ops,
      OriginalTransportInformationHost& host,
      bool* handled = nullptr);
  // The text and strip recomputed (1100:1b53 / 1cbb's fields) and the
  // whole client repainted now: InvalidateRect(all, FALSE) + UpdateWindow.
  void refresh(OriginalDialogOps& ops,
               OriginalTransportInformationHost& host);

  // The outer WM_LBUTTONDOWN contract of this target's filter.
  [[nodiscard]] OriginalTransportInformationClickPlan click_plan() const {
    return original_transport_information_click_plan(target_.kind);
  }
  [[nodiscard]] const OriginalMagnifierTarget& target() const {
    return target_;
  }
  [[nodiscard]] const OriginalTransportInformationText& text() const {
    return text_;
  }
  [[nodiscard]] const OriginalDtmp& dtmp() const { return dtmp_; }
  [[nodiscard]] bool changed() const { return changed_; }

 private:
  OriginalMagnifierTarget target_{};
  OriginalTransportInformationText text_{};
  OriginalDtmp dtmp_{};
  bool changed_{};
};

}  // namespace simtower
