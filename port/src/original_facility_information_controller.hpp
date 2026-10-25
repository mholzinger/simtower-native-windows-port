#pragma once

// Portable controller of Facility Information (DIALOG/DTMP 748-760,
// TENANTINFODLOGFILTER 1100:085b), over OriginalDialogOps (the dialog's own
// controls) and a host interface (the document and what the dialog opens
// outside itself). Both hosts run it: the Win32 procedure
// (facility_information_dialog_proc in native_main.cpp: WM_INITDIALOG's
// combo fill, WM_LBUTTONDOWN's portrait hit, WM_MOUSEMOVE's cursor,
// WM_COMMAND) over Win32DialogOps, and the SDL session over its
// DialogWindow. The Win32 procedure keeps its DC,
// palette, font, brush, WM_CTLCOLOR*, WM_ACTIVATE, the DS:31a4 / TOPMOST
// restoration around every click and EndDialog. The frozen copy of the old
// Win32 branches in facility_information_controller_tests.cpp proves the
// call order is unchanged. No windows.h here.
//
// Matches the original since 2026-10-25 on both hosts (owner decision;
// fdis.py of 1100:03ac, 1100:085b, 1100:1716 and 1228:0103):
// - The condo (group 4) price combo. 1100:096f-09a5 calls
//   EnableWindow(GetDlgItem(13), FALSE) only when DS:b3ac == 4 and record
//   byte +0xb (exact_bytes[5]) < 0x18: disabled for a sold condo ('Occupied')
//   and enabled for an unsold one ('For Sale'). The builder's
//   rent_control_enabled is `group != 4U || exact_bytes[5] >= 24`, and
//   set_original_facility_rent_rate has no condo test, as the CBN_SELCHANGE
//   tail 1100:0bae-0c2b (group <= 5, a changed selection, byte +0x16,
//   1130:06e9). Until then both were the wrong way round.
// - Yesterday's Profit for group 12 (DIALOG 760: types 6 and 12). 1100:1716
//   jumps through cs:1a40; group 12 goes to 194a, which calls 1100:2715
//   (signed Retail byte 10 plus STRL/713 item 7) with no type test. The
//   builder now fills it for restaurants too, not only fast food.
// - A Magnifier click on a Lobby (types 24..26) opens Finance. 1100:03ac's
//   cs:054e table sends them to 0468: 1060:0083 (DIALOG 500), return 0.
//   select_original_magnifier_target returns lobby_finance and
//   run_original_magnifying_glass calls the host's finance_dialog.
// - A shop's picture and name come from its own Retail record. 1100:465a
//   (477c) and 1100:2715 read record word +0xc unchecked; the cause of the
//   Soba picture was construction: 1228:02c2 (type 10) joins the type 6/12
//   tail at 0354, so 11a8:07d3 allocates a Retail record for every shop at
//   activation (allocate_original_commercial_service).
// Still different, listed for the owner: types 45 / 46 are swapped (1100:
// 03ac opens nothing for 45 and group 8 for 46), there is no Main / Map /
// Command repaint after the dialog closes, and refreshes repaint the whole
// client instead of items 5/6/8/10.
//
// There is no eviction: TENANTINFODLOGFILTER's WM_COMMAND (1100:0b20-0b2f)
// compares only ids 1 (OK), 7 (Rename) and 13 (rent combo / New Movie)
// and jumps to 0e54 for every other id, so the original has no evict
// control, no eviction state change and nothing to render for it.

#include "original_dialog_model.hpp"
#include "original_dtmp.hpp"
#include "original_information.hpp"
#include "original_information_host.hpp"
#include "original_resources.hpp"
#include "original_tables.hpp"
#include "original_tdt.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace simtower {

// The dialog item ids TENANTINFODLOGFILTER addresses.
inline constexpr std::uint16_t kOriginalFacilityOkId = 1U;
inline constexpr std::uint16_t kOriginalFacilityRenameId = 7U;
// The rent / price combo box (groups 0..5), or the New Movie button (758).
inline constexpr std::uint16_t kOriginalFacilityChoiceId = 13U;

// What TENANTINFODLOGFILTER reaches outside its own controls.
class OriginalFacilityInformationHost : public OriginalInformationHostBase {
 public:
  // 1100:0b4e-0b78: Rename Tenant (DIALOG 732) with DS:31a4 cleared for
  // the nested modal and the dialog restored (DS:31a4, TOPMOST) after it,
  // also when it was cancelled. Whether the name changed.
  virtual bool rename_tenant(std::int16_t floor, std::size_t tenant_index) = 0;
  // 1100:40d5: New Movie (DIALOG 731) for the linked record; whether it
  // ended with a choice (nonzero).
  virtual bool choose_movie(std::size_t linked_record_index) = 0;
  // 1100:0000: Person Information opened from this dialog; whether it
  // changed the document.
  virtual bool person_information(std::size_t person_index) = 0;
};

class OriginalFacilityInformationController {
 public:
  OriginalFacilityInformationController(std::int16_t floor,
                                        std::size_t tenant_index,
                                        OriginalFacilityInformation information,
                                        OriginalDtmp dtmp)
      : floor_(floor),
        tenant_index_(tenant_index),
        information_(std::move(information)),
        dtmp_(std::move(dtmp)) {}

  // WM_INITDIALOG's controls (nm 4373-4385): for the rent groups, the four
  // choices into combo 13, its selection and its enabled state.
  void initialize(OriginalDialogOps& ops) const;
  // The end of WM_INITDIALOG: the painted-dialog focus plan
  // (original_painted_dialog_initialization_focus_plan: no SetFocus; TRUE
  // lets the dialog manager focus OK).
  [[nodiscard]] bool initial_focus(OriginalDialogOps& ops) const;
  // WM_COMMAND through original_facility_information_command_plan (1
  // closes with 1; 7 renames; 13 changes the rent or opens New Movie),
  // WM_LBUTTONDOWN (a portrait opens Person Information) and WM_MOUSEMOVE
  // (the portrait-panel cursor). Every other signal is ignored. *handled
  // (when given) receives whether this signal was handled (the Win32
  // procedure's TRUE: the command plan's consume, the click plan's
  // consume, TRUE for WM_MOUSEMOVE), per call, as the Person and
  // Transport Information controllers report it.
  [[nodiscard]] OriginalDialogOutcome signal(
      const OriginalDialogSignal& signal,
      OriginalDialogOps& ops,
      OriginalFacilityInformationHost& host,
      bool* handled = nullptr);
  // refresh_original_facility_dialog: recompute, combo selection and
  // enabled state, InvalidateRect(all) + UpdateWindow.
  void refresh(OriginalDialogOps& ops, OriginalFacilityInformationHost& host);

  [[nodiscard]] const OriginalFacilityInformation& information() const {
    return information_;
  }
  [[nodiscard]] const OriginalDtmp& dtmp() const { return dtmp_; }
  [[nodiscard]] std::int16_t floor() const { return floor_; }
  [[nodiscard]] std::size_t tenant_index() const { return tenant_index_; }
  [[nodiscard]] bool changed() const { return changed_; }

 private:
  std::int16_t floor_{-1};
  std::size_t tenant_index_{};
  OriginalFacilityInformation information_{};
  OriginalDtmp dtmp_{};
  bool changed_{};
};

}  // namespace simtower
