#pragma once

// Portable preparation of the dialogs both hosts show: the AHOTTADLOGFILTER
// event dialogs (DIALOG/DTMP 3000-3040), the About box (TOWER_TITLE) and the
// message boxes (ALRT presentations and the literal MessageBox sites). Each
// prepare_* function holds exactly what the Win32 host computed inline before
// it built a DLGTEMPLATE, so the Win32 host and the SDL dialog runtime start
// from the same template, layout and texts. No windows.h here.

#include "original_about.hpp"
#include "original_alert.hpp"
#include "original_dialog.hpp"
#include "original_draw_context.hpp"
#include "original_dtmp.hpp"
#include "original_resources.hpp"
#include "original_simulation.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace simtower {

// AHOTTADLOGFILTER constants, read by both hosts from the model (the Win32
// event_dialog_proc through OriginalEventDialogContext).
// 1068:00a1 WM_INITDIALOG: SetTimer(dialog, 10, 0x93e0 = 37856 ms).
inline constexpr std::uint16_t kOriginalEventDialogTimerId = 10U;
inline constexpr std::uint32_t kOriginalEventDialogTimerMs = 0x93e0U;
// The 1208 bank font WM_CTLCOLORSTATIC selects into every static's DC
// (Win32 only: event_dialog_proc keeps creating and selecting it). The
// static control then draws with its own WM_SETFONT font, the template's
// 8-point face, so no host draws text with this size: the SDL runtime draws
// static text at original_dialog_font_pixels(kOriginalEventDialogPointSize)
// (owner decision 2026-10-23; Wine's static control behaves the same way).
inline constexpr std::int16_t kOriginalEventDialogCtlColorFontPixels = 14;
// 1008:0085 forces 8 points; MulDiv(8, 96, 72) at the 96-DPI desktop.
inline constexpr std::uint16_t kOriginalEventDialogPointSize = 8U;
// original_facility_control_background(true) = gray_cc, the static brush of
// the event and transport-information filters.
inline constexpr std::uint32_t kOriginalDialogStaticBackgroundRgb =
    0x00CCCCCCU;
// ABOUTDLGPROC: timer 9 and the 1010:0758 RGB(230) control brush.
inline constexpr std::uint16_t kOriginalAboutTimerId = 9U;
inline constexpr std::uint32_t kOriginalAboutBackgroundRgb = 0x00E6E6E6U;

struct OriginalEventDialogModel {
  // The resource template after the 1008:0085 point-size patch (8) and the
  // 1068:0439 / 1068:0175 text passes.
  OriginalDialog dialog{};
  OriginalDtmp dtmp{};
  std::int32_t argument{};
  std::uint16_t timer_id{kOriginalEventDialogTimerId};
  std::uint32_t timer_ms{kOriginalEventDialogTimerMs};
  // WM_CTLCOLORSTATIC's selected font (Win32 only; never the drawn size,
  // see kOriginalEventDialogCtlColorFontPixels).
  std::int16_t ctlcolor_font_pixels{kOriginalEventDialogCtlColorFontPixels};
  std::uint32_t static_background_rgb{kOriginalDialogStaticBackgroundRgb};
  // The control the dialog manager focuses after WM_INITDIALOG returns TRUE
  // (original_painted_dialog_initialization_focus_plan: no explicit focus):
  // the first enabled WS_TABSTOP control in template order that is visible
  // once render_original_dtmp has shown the DTMP-placed children; 0 when
  // there is none. The Win32 host leaves this to the real dialog manager;
  // the SDL runtime uses it.
  std::uint16_t initial_focus{};
};

// Exact 1068:0000 -> 1008:0085 launcher preparation used by
// run_original_event_dialog. Throws std::runtime_error (from the resource
// lookup or the template parser) for an id without a DIALOG resource.
//
// Order note: the old inline Win32 code built the DLGTEMPLATE before it
// parsed DTMP/<id>; here DTMP is parsed first and the host builds the
// template afterwards. Valid resources give identical results; only which
// exception comes first differs when several resources are malformed.
[[nodiscard]] OriginalEventDialogModel prepare_original_event_dialog(
    const OriginalResources& resources,
    const OriginalEventDialogRequest& request);

// Win32 dialog-manager initial focus: the first control in template order
// with WS_TABSTOP that is not disabled and is visible, either from its
// template WS_VISIBLE or because its id is in shown_ids (the children
// render_original_dtmp moves and shows with SW_SHOW during WM_INITDIALOG).
// 0 when none qualifies.
[[nodiscard]] std::uint16_t original_dialog_first_tab_stop(
    const OriginalDialog& dialog,
    std::span<const std::uint16_t> shown_ids = {}) noexcept;

// The ids of the children the DTMP render pass places (place_child ops whose
// id the template has), in pass order.
[[nodiscard]] std::vector<std::uint16_t> original_dtmp_shown_children(
    const OriginalDialog& dialog,
    const OriginalDtmp& dtmp);

// Whether a template item is a push button (BUTTON class, ordinal 0x80,
// with BS_PUSHBUTTON or BS_DEFPUSHBUTTON).
[[nodiscard]] bool original_dialog_item_is_push_button(
    const OriginalDialogItem& item) noexcept;

// IsDialogMessage(VK_RETURN): DM_GETDEFID's default id, sent as WM_COMMAND.
// Focusing a push button makes it the default (DEFDLG_SetDefButton), so the
// focused push button wins; otherwise the first BS_DEFPUSHBUTTON in the
// template; otherwise IDOK (1). Templates without BS_DEFPUSHBUTTON (3001,
// 3012, 3020) therefore answer Enter with the focused button.
[[nodiscard]] std::uint16_t original_dialog_enter_command(
    const OriginalDialog& dialog,
    std::uint16_t focused_id) noexcept;

// The pixel height of a template font at the 96-DPI desktop (the
// CreateFont height the dialog manager uses): MulDiv(points, 96, 72).
[[nodiscard]] int original_dialog_font_pixels(std::uint16_t points) noexcept;

// The kinds of dialog-template control a dialog manager knows, from the
// class (and, for BUTTON, the style): the predefined Win16 classes the
// game's templates use. The SDL runtime gives push buttons, text statics,
// single-line edits, list boxes, drop-down-list combo boxes and scroll bars
// behavior and a Win3.1-style look; `other` is any other class or style
// (check boxes, group boxes, icon or frame statics, private classes), drawn
// by nothing.
enum class OriginalWidgetKind : std::uint8_t {
  push_button,
  default_push_button,
  static_text,
  edit,
  list_box,
  combo_box,
  scroll_bar,
  other,
};

// The widget kind of one template item: the predefined class ordinals
// (0x80-0x85) or their class names (case-insensitive).
[[nodiscard]] OriginalWidgetKind original_dialog_item_widget_kind(
    const OriginalDialogItem& item) noexcept;

[[nodiscard]] constexpr bool original_widget_is_push_button(
    OriginalWidgetKind kind) noexcept {
  return kind == OriginalWidgetKind::push_button ||
         kind == OriginalWidgetKind::default_push_button;
}

// BN_CLICKED: the notification of a push button's WM_COMMAND (and of the
// dialog manager's Enter / Esc commands).
inline constexpr std::uint16_t kOriginalNotifyClicked = 0U;
// The control notifications the SDL widgets send (WM_COMMAND HIWORD).
inline constexpr std::uint16_t kOriginalListBoxSelChange = 1U;  // LBN_
inline constexpr std::uint16_t kOriginalListBoxDblClk = 2U;
inline constexpr std::uint16_t kOriginalComboBoxSelChange = 1U;  // CBN_
inline constexpr std::uint16_t kOriginalComboBoxDropDown = 7U;
inline constexpr std::uint16_t kOriginalEditChange = 0x0300U;    // EN_
inline constexpr std::uint16_t kOriginalEditMaxText = 0x0501U;
// WM_VSCROLL / WM_HSCROLL codes (SB_*).
inline constexpr std::uint16_t kOriginalScrollLineUp = 0U;
inline constexpr std::uint16_t kOriginalScrollLineDown = 1U;
inline constexpr std::uint16_t kOriginalScrollPageUp = 2U;
inline constexpr std::uint16_t kOriginalScrollPageDown = 3U;
inline constexpr std::uint16_t kOriginalScrollThumbPosition = 4U;
inline constexpr std::uint16_t kOriginalScrollThumbTrack = 5U;
inline constexpr std::uint16_t kOriginalScrollTop = 6U;
inline constexpr std::uint16_t kOriginalScrollBottom = 7U;
inline constexpr std::uint16_t kOriginalScrollEnd = 8U;

// What a dialog manager passes to a dialog procedure, in host-neutral form.
struct OriginalDialogSignal {
  enum class Kind : std::uint8_t {
    command,    // WM_COMMAND id + notify_code (a released button, Enter...)
    timer,      // WM_TIMER of the dialog's timer (id)
    close,      // ABOUTDLGPROC's WM_KEYDOWN / WM_LBUTTONUP / WM_RBUTTONUP
    draw_item,  // reserved: WM_DRAWITEM of owner-drawn control id
    scroll,     // WM_VSCROLL / WM_HSCROLL of control id: notify_code is the
                // SB_* code, value the thumb position (THUMBTRACK/POSITION)
    paint,      // the dialog's own WM_PAINT: sent when it opens, after a
                // procedure invalidates it and when a nested dialog that
                // covered it closes; never once per frame
    key_up,     // WM_KEYUP reaching the procedure: id is the virtual key
    button_down,  // WM_LBUTTONDOWN reaching the procedure at client (x, y)
    button_up,    // WM_LBUTTONUP reaching the procedure at client (x, y)
  };
  Kind kind{Kind::command};
  std::uint16_t id{};
  // The WM_COMMAND notification (HIWORD of lParam in Win16): BN_CLICKED (0)
  // from buttons, LBN_* / CBN_* / EN_* from the other controls.
  std::uint16_t notify_code{kOriginalNotifyClicked};
  std::int32_t value{};
  int x{};
  int y{};

  friend bool operator==(const OriginalDialogSignal&,
                         const OriginalDialogSignal&) = default;
};

// What a dialog procedure may do to its own dialog while it handles a
// signal: the host-neutral form of ShowWindow / EnableWindow / SetFocus on a
// child, SetDlgItemText / GetDlgItemText, the LB_* / CB_* list messages,
// SetScrollRange / SetScrollPos, InvalidateRect and MoveWindow. The SDL
// runtime implements it on the open dialog; the Win32 procedures keep their
// own calls. Every call names the child by its template id; a missing id
// does nothing (returns empty / -1).
class OriginalDialogOps {
 public:
  virtual ~OriginalDialogOps() = default;
  // ShowWindow(GetDlgItem(dialog, id), shown ? SW_SHOW : SW_HIDE).
  virtual void show_item(std::uint16_t id, bool shown) = 0;
  // InvalidateRect(dialog, nullptr, ...): repaint the whole dialog (and send
  // it a WM_PAINT).
  virtual void invalidate() = 0;
  // InvalidateRect(dialog, &rect, FALSE): repaint part of the client (a
  // host without partial repaints repaints all of it).
  virtual void invalidate_rect(const OriginalRect& /*rect*/) { invalidate(); }
  // UpdateWindow(dialog): paint a pending invalidation now, before the
  // procedure goes on (COUNTDLOGMAIN's pressed then released surface).
  virtual void update_window() {}
  // MoveWindow to a new client size (position kept by the placement rule).
  virtual void resize(int client_width, int client_height) = 0;
  // EnableWindow(GetDlgItem(dialog, id), enabled).
  virtual void enable_item(std::uint16_t /*id*/, bool /*enabled*/) {}
  // SetFocus(GetDlgItem(dialog, id)) (no selection change).
  virtual void focus_item(std::uint16_t /*id*/) {}
  // SetDlgItemText / GetDlgItemText (Windows-1252 bytes; the getter is
  // capped like 11e0:0000, original_dialog_item_text_contract).
  virtual void set_item_text(std::uint16_t /*id*/,
                             std::string_view /*cp1252*/) {}
  [[nodiscard]] virtual std::string item_text(std::uint16_t /*id*/) const {
    return {};
  }
  // LB_RESETCONTENT / LB_ADDSTRING (unsorted: appended) / LB_DELETESTRING /
  // LB_GETCURSEL (-1 for none) / LB_SETCURSEL, and the CB_ equivalents on a
  // combo box.
  virtual void list_reset(std::uint16_t /*id*/) {}
  virtual void list_add(std::uint16_t /*id*/, std::string_view /*cp1252*/) {}
  virtual void list_delete(std::uint16_t /*id*/, int /*index*/) {}
  [[nodiscard]] virtual int list_selection(std::uint16_t /*id*/) const {
    return -1;
  }
  virtual void list_set_selection(std::uint16_t /*id*/, int /*index*/) {}
  // SetScrollRange(SB_CTL) / SetScrollPos(SB_CTL, redraw).
  virtual void set_scroll_range(std::uint16_t /*id*/, int /*min*/,
                                int /*max*/) {}
  virtual void set_scroll_pos(std::uint16_t /*id*/, int /*position*/) {}
};

// What a dialog procedure decided for one signal.
struct OriginalDialogOutcome {
  // The EndDialog result; nullopt keeps the dialog open.
  std::optional<int> result{};
  // AHOTTADLOGFILTER: ALRT/1004 is shown first (owned by the dialog).
  bool warn_insufficient_funds{};

  friend bool operator==(const OriginalDialogOutcome&,
                         const OriginalDialogOutcome&) = default;
};

// AHOTTADLOGFILTER's WM_TIMER and WM_COMMAND handling (1068:0380-0406) for
// one open event dialog. WM_TIMER kills the timer, sets the timer flag and
// acts as command 2; WM_COMMAND 1 and 2 go through
// original_event_dialog_action; every other signal is ignored. The Win32
// event_dialog_proc keeps its own message switch (unchanged); this is the
// same mapping for the SDL runtime, tested against the action table.
class OriginalEventDialogController {
 public:
  explicit OriginalEventDialogController(std::int32_t argument) noexcept
      : argument_(argument) {}
  [[nodiscard]] OriginalDialogOutcome signal(
      const OriginalDialogSignal& signal,
      std::int32_t balance) noexcept;
  [[nodiscard]] bool timer_fired() const noexcept { return timer_fired_; }

 private:
  std::int32_t argument_{};
  bool timer_fired_{};
};

// ABOUTDLGPROC (1010:053f): EndDialog(1) on the close signal; WM_COMMAND
// (Enter / Esc through IsDialogMessage) keeps it open, and WM_TIMER 9
// (1010:0774: one credits step) invalidates it and keeps it open.
[[nodiscard]] OriginalDialogOutcome original_about_dialog_signal(
    const OriginalDialogSignal& signal,
    OriginalDialogOps& ops);

// MessageBox: a WM_COMMAND from one of its buttons ends it with that id.
[[nodiscard]] OriginalDialogOutcome original_message_box_signal(
    const OriginalDialogSignal& signal) noexcept;

// The SDL host's answers when nothing answers an event dialog (no input
// source, or a script without lines for that dialog). Every one is the
// EndDialog(1) of button 1: DIALOG/3020 "Find the Bomb" (only 2 pays,
// 10c8:006e), the fire-crew offer's "No" (10e8:01e2 hires on every other
// value) and the acknowledge buttons. Fixed so such runs keep their
// digests.
inline constexpr std::uint16_t kOriginalEventDialogBombOfferDefault = 1U;
inline constexpr std::uint16_t kOriginalEventDialogFireCrewOfferDefault = 1U;
inline constexpr std::uint16_t kOriginalEventDialogAcknowledgeDefault = 1U;

struct OriginalAboutDialogModel {
  OriginalDialog dialog{};  // DIALOG/TOWER_TITLE as loaded (no patch)
  OriginalAboutLayout layout{};
  std::vector<std::string> lines{};
  // One-rectangle chrome layout holding layout.credits_outer, painted by the
  // shared DTMP chrome painter as a placeholder bevel.
  OriginalDtmp chrome{};
  // 1010:0758: every control colour and the credits fill are RGB(230).
  std::uint32_t background_rgb{kOriginalAboutBackgroundRgb};
  std::uint16_t timer_id{kOriginalAboutTimerId};
};

// run_original_about_dialog's preparation before the 1010:049e launcher.
// Order note: the layout and credit lines are now read before the host
// builds the DLGTEMPLATE (the old inline code built it first); this only
// changes which exception comes first for malformed resources.
[[nodiscard]] OriginalAboutDialogModel prepare_original_about_dialog(
    const OriginalResources& resources);

// One MessageBox call: caption, text and style (MB_* bits, see
// original_alert.hpp), shown by show_original_message_box on Win32 and by
// the SDL dialog runtime. button_mode is always the ALRT button mode that
// original_alert_result expects. 1208:0385 ORs the ALRT mode straight into
// the style, so the ALRT modes are MB_* type values and button_mode equals
// style & kOriginalMessageBoxTypeMask for every request built here (the
// literal boxes use MB_OK = mode 0 and MB_YESNO = mode 4).
struct OriginalMessageBoxRequest {
  std::string caption{};
  std::string text{};
  OriginalAlertStyle style{};
  std::uint16_t button_mode{};
  // MessageBeep(-1) precedes the box (the replace prompt, SetTimer failure).
  bool beep_first{};

  friend bool operator==(const OriginalMessageBoxRequest&,
                         const OriginalMessageBoxRequest&) = default;
};

inline constexpr std::uint32_t kOriginalMessageBoxTypeMask = 0x0FU;

// Literal MessageBox texts of the Win32 host, shared so both hosts show the
// same strings.
inline constexpr char kOriginalAlertCaption[] = "SimTower";
inline constexpr char kOriginalInvalidFilenameText[] =
    "That is not a valid filename.";
inline constexpr char kOriginalInvalidFilenameCaption[] =
    "SimTower for Windows";
inline constexpr char kOriginalReplaceExistingText[] =
    "This file name already exists. Do you want to replace it?";
inline constexpr char kOriginalReplaceExistingCaption[] =
    "Save SimTower data file";
inline constexpr char kOriginalAboutTimerFailedText[] = "SetTimer failed";
inline constexpr char kOriginalAboutTimerFailedCaption[] =
    "SimTower - About";

// 11e0:0c9e: MessageBox(owner, formatted ALRT text, "SimTower", style).
[[nodiscard]] OriginalMessageBoxRequest build_original_message_box(
    const OriginalAlertPresentation& presentation);

// The literal boxes of the File transactions and the About timer.
[[nodiscard]] OriginalMessageBoxRequest
original_replace_existing_message_box();
[[nodiscard]] OriginalMessageBoxRequest
original_invalid_filename_message_box();
[[nodiscard]] OriginalMessageBoxRequest
original_about_timer_failed_message_box();

// The MessageBox button ids for a box style, left to right
// (kOriginalAlertResult*: IDOK = 1, IDCANCEL = 2, IDABORT = 3, IDRETRY = 4,
// IDIGNORE = 5, IDYES = 6, IDNO = 7). Every box here is MB_DEFBUTTON1, so
// the first button is the default and has the focus.
[[nodiscard]] std::vector<int> original_message_box_buttons(
    OriginalAlertStyle style);

}  // namespace simtower
