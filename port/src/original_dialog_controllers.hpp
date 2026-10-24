#pragma once

// Portable dialog controllers for the Find, Rename, New Movie and Finance
// dialogs: the transaction half of FINDDIALOGFILTER (10d8:006f),
// NAMEPEPLEDIALOGFILTER / NAMETENANTDIALOGFILTER (1100:3a39 / 3dc4),
// MOVIETITLEDIALOGFILTER (1100:4138) and COUNTDLOGMAIN (1060:00d3), over
// OriginalDialogOps (the dialog's own controls) and a small host interface
// (the document and the windows outside the dialog). Both hosts run them:
// the SDL dialog runtime over DialogWindow, and the Win32 procedures in
// native_main.cpp over an HWND adapter (find_dialog_proc's WM_INITDIALOG
// list and WM_COMMAND, finance_dialog_proc's WM_KEYUP / WM_LBUTTON* and
// painter, the rename procedures' WM_INITDIALOG and WM_COMMAND, the New
// Movie procedure's WM_INITDIALOG and WM_COMMAND). Each Win32 procedure
// keeps only its own close tail (DTMP release, brush, capture, EndDialog).
// The frozen Win32 copies in dialog_controller_tests.cpp prove the
// behaviour is unchanged. No windows.h here.

#include "original_dialog_model.hpp"
#include "original_draw_context.hpp"
#include "original_dtmp.hpp"
#include "original_finance.hpp"
#include "original_find.hpp"
#include "original_information.hpp"
#include "original_resources.hpp"
#include "original_tables.hpp"
#include "original_tdt.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace simtower {

// The dialog item ids the procedures address.
inline constexpr std::uint16_t kOriginalFindListId = 5U;
inline constexpr std::uint16_t kOriginalFindRemoveId = 3U;
inline constexpr std::uint16_t kOriginalFindFindId = 4U;
inline constexpr std::uint16_t kOriginalRenameOkId = 1U;
inline constexpr std::uint16_t kOriginalRenameRemoveId = 3U;
inline constexpr std::uint16_t kOriginalRenameEditId = 4U;
inline constexpr std::uint16_t kOriginalMovieNewId = 1U;
inline constexpr std::uint16_t kOriginalMovieClassicId = 3U;

// --- Find (FINDDIALOGFILTER) -------------------------------------------

// What FINDDIALOGFILTER reaches outside its own controls.
class OriginalFindDialogHost {
 public:
  virtual ~OriginalFindDialogHost() = default;
  // The live tower (null: none; the commands then do nothing).
  virtual OriginalTdtDocument* document() = 0;
  // GetClientRect(Main): the viewport the resolution centers in.
  virtual int main_client_width() = 0;
  virtual int main_client_height() = 0;
  // 10e0:04cf(0): clears the DS:77b4 marker.
  virtual void reset_marker() = 0;
  // 10d8:02ce: records the resolved target with the person's link (or
  // FFFFFFFF for a tenant) at the current 1208:05e6 coarse tick.
  virtual void start_marker(const OriginalFindResolution& resolution,
                            std::uint32_t selected_person) = 0;
  // show_original_alert owned by the dialog (ALRT/1002 or 1003).
  virtual void alert(int alert_id,
                     const std::array<std::string, 4>& substitutions) = 0;
  // A name was removed: the document is dirty and the title changes.
  virtual void entry_removed() = 0;
  // InvalidateRect(Main).
  virtual void invalidate_main() = 0;
  // SetCursor(arrow) before an alert.
  virtual void set_arrow_cursor() {}
};

class OriginalFindController {
 public:
  OriginalFindController(OriginalFindMode mode,
                         std::vector<OriginalFindEntry> entries)
      : mode_(mode), entries_(std::move(entries)) {}

  // WM_INITDIALOG (nm 5396-5439): LB_RESETCONTENT, LB_ADDSTRING of each
  // name in order, Remove and Find disabled.
  void initialize(OriginalDialogOps& ops) const;
  // The end of WM_INITDIALOG: original_find_initialization_focus_plan's
  // SetFocus (none: the list is not focused) and its return value (TRUE:
  // the dialog manager focuses the first tab stop, OK).
  [[nodiscard]] bool initial_focus(OriginalDialogOps& ops) const;
  // WM_COMMAND through original_find_dialog_command_plan: OK closes (marker
  // reset, Main invalidated, 1); Remove deletes the selected name; Find or
  // a double click resolves (alerts keep it open, a focus closes with 1);
  // LBN_SELCHANGE / LBN_SELCANCEL enable / disable Remove and Find.
  [[nodiscard]] OriginalDialogOutcome signal(const OriginalDialogSignal& signal,
                                             OriginalDialogOps& ops,
                                             OriginalFindDialogHost& host);

  [[nodiscard]] OriginalFindMode mode() const { return mode_; }
  [[nodiscard]] const std::vector<OriginalFindEntry>& entries() const {
    return entries_;
  }
  [[nodiscard]] const OriginalFindResolution& resolution() const {
    return resolution_;
  }
  [[nodiscard]] bool changed() const { return changed_; }

 private:
  bool resolve(OriginalDialogOps& ops, OriginalFindDialogHost& host);

  OriginalFindMode mode_{};
  std::vector<OriginalFindEntry> entries_{};
  OriginalFindResolution resolution_{};
  bool changed_{};
};

// --- Rename Person / Rename Tenant (DIALOG 730 / 732) -------------------

struct OriginalRenameTarget {
  bool person{true};
  std::size_t person_index{};
  std::int16_t floor{};
  std::size_t tenant_index{};
};

// The Save branch of both rename procedures (nm 4011-4045 / 4486-4514)
// after the empty-text check: set_original_person_name or
// set_original_tenant_name, and what the procedure does with the status.
struct OriginalRenameSaveResult {
  // EndDialog(1) follows; false keeps the dialog open (too long).
  bool close{};
  // ALRT/1000 with STRL/1005[index] first: 4 for too long, 6 (person) or 7
  // (tenant) for a full name table.
  std::optional<std::uint16_t> alert_strl_index{};
  bool changed{};

  friend bool operator==(const OriginalRenameSaveResult&,
                         const OriginalRenameSaveResult&) = default;
};
[[nodiscard]] OriginalRenameSaveResult original_rename_save(
    OriginalTdtDocument& document,
    const OriginalRenameTarget& target,
    std::string_view cp1252);
// The Delete branch: remove_original_person_name / _tenant_name; whether it
// changed the document.
[[nodiscard]] bool original_rename_remove(OriginalTdtDocument& document,
                                          const OriginalRenameTarget& target);
// The saved name SetDlgItemText shows at WM_INITDIALOG.
[[nodiscard]] std::string original_rename_initial_text(
    const OriginalTdtDocument& document,
    const OriginalRenameTarget& target);

class OriginalRenameDialogHost {
 public:
  virtual ~OriginalRenameDialogHost() = default;
  virtual OriginalTdtDocument* document() = 0;
  // ALRT/1000 owned by the dialog with STRL/1005[strl_index].
  virtual void alert_strl(std::uint16_t strl_index) = 0;
};

class OriginalRenameController {
 public:
  explicit OriginalRenameController(OriginalRenameTarget target)
      : target_(target) {}

  // WM_INITDIALOG: the saved name into edit 4, Delete enabled for a
  // non-empty name, the Rename gate (original_rename_ok_enabled).
  void initialize(OriginalDialogOps& ops,
                  const OriginalTdtDocument& document) const;
  // The end of WM_INITDIALOG: original_rename_dialog_focus_plan(initialize)'s
  // SetFocus (none: edit 4 is focused by the WM_PAINT tail) and its return
  // value (TRUE: the dialog manager focuses the first tab stop), as Find's
  // initial_focus.
  [[nodiscard]] bool initial_focus(OriginalDialogOps& ops) const;
  // WM_PAINT's tail enables and focuses edit 4 (no selection change);
  // WM_COMMAND through original_rename_dialog_command_plan (notification
  // codes ignored): 4 re-runs the gate, 1 saves (empty text stays open),
  // 2 cancels, 3 deletes; every close is EndDialog(1).
  [[nodiscard]] OriginalDialogOutcome signal(const OriginalDialogSignal& signal,
                                             OriginalDialogOps& ops,
                                             OriginalRenameDialogHost& host);
  [[nodiscard]] bool changed() const { return changed_; }
  [[nodiscard]] const OriginalRenameTarget& target() const { return target_; }

 private:
  OriginalRenameTarget target_{};
  bool changed_{};
};

// --- New Movie (DIALOG 731) ---------------------------------------------

struct OriginalMovieChoiceEnabled {
  bool new_release{};
  bool classic{};

  friend bool operator==(const OriginalMovieChoiceEnabled&,
                         const OriginalMovieChoiceEnabled&) = default;
};
// nm 4562-4572: New and Classic are enabled while the balance covers PART
// words 34 / 35 (signed 16-bit costs).
[[nodiscard]] OriginalMovieChoiceEnabled original_movie_choice_enabled(
    const OriginalTdtDocument& document,
    const OriginalPartTable& part) noexcept;

class OriginalMovieChoiceController {
 public:
  explicit OriginalMovieChoiceController(std::size_t linked_record_index)
      : record_(linked_record_index) {}
  // WM_INITDIALOG: enables 1 and 3 by original_movie_choice_enabled.
  void initialize(OriginalDialogOps& ops,
                  const OriginalTdtDocument& document,
                  const OriginalPartTable& part) const;
  // WM_COMMAND through original_movie_choice_dialog_command_plan: 2 is
  // EndDialog(0); 1 and 3 run choose_original_movie and EndDialog(1).
  [[nodiscard]] OriginalDialogOutcome signal(const OriginalDialogSignal& signal,
                                             OriginalTdtDocument* document,
                                             const OriginalPartTable& part);
  [[nodiscard]] bool changed() const { return changed_; }

 private:
  std::size_t record_{};
  bool changed_{};
};

// --- Finance (DIALOG 500, COUNTDLOGMAIN) ---------------------------------

// The DTMP/500 rectangle (one-based) of the OK surface.
inline constexpr std::size_t kOriginalFinanceButtonRectangle = 1U;

class OriginalFinanceController {
 public:
  explicit OriginalFinanceController(OriginalRect button) : button_(button) {}
  // WM_KEYUP (original_finance_key_presentation: Return or Space draw the
  // pressed then the released surface and close with 0); WM_LBUTTONDOWN in
  // the button draws it pressed; WM_LBUTTONUP draws it released and closes
  // when the release is inside the button, wherever the press was. Every
  // other message (WM_COMMAND from Enter / Esc included) is ignored. Each
  // repaint is InvalidateRect(button) + UpdateWindow (ops.invalidate_rect,
  // ops.update_window), so the pressed surface is presented before the
  // released one.
  [[nodiscard]] OriginalDialogOutcome signal(const OriginalDialogSignal& signal,
                                             OriginalDialogOps& ops);
  [[nodiscard]] bool pressed() const { return pressed_; }
  [[nodiscard]] const OriginalRect& button() const { return button_; }

 private:
  void repaint_button(OriginalDialogOps& ops) const;

  OriginalRect button_{};
  bool pressed_{};
};

// One value COUNTDLOGMAIN draws (1060:0479), in its draw order.
struct OriginalFinanceTextOp {
  // The DTMP/500 rectangle (one-based) it belongs to.
  std::size_t rectangle{};
  // 14 for rectangles 6-7, 12 for the rest.
  int font_pixels{};
  // TA_UPDATECP | TA_RIGHT | TA_BASELINE at (x, y) for the ten-row columns
  // (rectangles 3-5); TA_UPDATECP (left, top) at (x, y) otherwise, where x
  // still needs the text width (original_finance_value_position).
  bool right_baseline{};
  int x{};
  int y{};
  std::string text{};

  friend bool operator==(const OriginalFinanceTextOp&,
                         const OriginalFinanceTextOp&) = default;
};
// The values in draw order: rectangles 6..14 (summary, x positioned with
// measure(font_pixels, text) through original_finance_value_position), then
// the ten rows of rectangles 3, 4, 5 at (right - 1, top + (row + 1) * 13 -
// 2).
[[nodiscard]] std::vector<OriginalFinanceTextOp> original_finance_text_ops(
    const OriginalFinanceView& view,
    const OriginalDtmp& dtmp,
    const std::function<int(int font_pixels, std::string_view text)>&
        measure);

// The Finance client through a draw context: BITMAP/500, BITMAP/501 over
// the button rectangle while pressed, then the text ops (transparent, black,
// the fonts selected through the context's bank).
void paint_original_finance(OriginalDrawContext& context,
                            const OriginalResources& resources,
                            const OriginalDtmp& dtmp,
                            const OriginalFinanceView& view,
                            bool pressed);

}  // namespace simtower
