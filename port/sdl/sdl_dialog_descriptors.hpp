#pragma once

// The dialogs the SDL host runs through the generic dialog manager
// (sdl_dialog.hpp), one descriptor each, built from the shared preparation
// the Win32 host uses (original_dialog_model.hpp). No SDL here.
//
// - Event dialogs DIALOG/DTMP 3000-3040 (run_original_event_dialog,
//   AHOTTADLOGFILTER 1068:00a1): the DTMP-sized client, the 1068:0567 chrome
//   (paint_original_dtmp_chrome), the 1070:0231 bitmaps and child placement
//   (original_dtmp_render_ops; DTMP rectangles override the template
//   coordinates, as MoveWindow does), statics on the gray_cc brush and push
//   buttons, both in the template's 8-point font (11 pixels; the static's
//   own WM_SETFONT font wins over the 14-pixel WM_CTLCOLORSTATIC font, owner
//   decision 2026-10-23), and timer 10 (0x93e0 ms, one-shot). Event
//   templates are 0x92000042 (no WS_CAPTION), so they have no caption bar.
//   Esc sends IDCANCEL = 2, AHOTTADLOGFILTER's accept button (it pays the
//   ransom on 3020 and hires the crew on 3012), as the Win32 dialog manager
//   does (owner decision 2026-10-23). The DTMP bitmaps are redrawn on every
//   frame (owner decision 2026-10-23; the Win32 WM_PAINT gap is not
//   reproduced).
// - Message boxes (show_original_alert's ALRT boxes, including the STRL/1001
//   three-button transition box, and the literal File boxes): TEMP layout,
//   because USER builds MessageBox at run time and no resource holds it.
//   The caption is Win3.1 navy (owner decision 2026-10-23); the metrics are
//   TEMP Win3.1-like constants with 13-pixel Liberation Sans until a
//   reference exists (owner decision 2026-10-23). TEMP, not Win3.1: the
//   icon is a drawn yellow triangle with '!' (the Windows 95 shape; the
//   Win3.1 MB_ICONEXCLAMATION glyph looked different and is not
//   reproduced, and no Windows system image is ever used), and the caption
//   has no Win3.1 system-menu box at its left.
// - About (DIALOG/TOWER_TITLE, ABOUTDLGPROC 1010:053f): the 604x290 client,
//   chrome, BITMAP/257 and the TEXT/128 credits scrolled by timer 9; any
//   key the dialog manager passes on, and a button release, close it.
//
// - Find Person / Tenant (DIALOG/DTMP 510 / 520, FINDDIALOGFILTER): the
//   BITMAP/510 client (no generic chrome), OK, Remove, Find and the list
//   box placed by the DTMP; the list on the white WM_CTLCOLORLISTBOX brush,
//   everything in the template's 10-point font (13 pixels).
// - Rename Person / Tenant (DIALOG/DTMP 730 / 732): the 1068:0567 chrome,
//   the edit on the 0xCC WM_CTLCOLOREDIT brush, the centered static.
//   TEMP: Person Rename's SetCapture(dialog) is not reproduced; see
//   make_rename_dialog in the .cpp for what is known and still open.
// - New Movie (DIALOG/DTMP 731): WS_CAPTION "New Movie" with a TEMP Win3.1
//   frame (one-pixel border, navy caption), the chrome, three buttons.
// - Finance (DIALOG/DTMP 500, COUNTDLOGMAIN): BITMAP/500 with BITMAP/501
//   over the pressed button and the twelve values
//   (paint_original_finance); no controls, key-ups and clicks reach the
//   procedure. The painter reads the procedure's pressed state through
//   DialogWindow::paint_state (Session sets it to the controller's).
//
// - Elevator Control (DIALOG/DTMP 400, ELVDLOGMAIN, modeless): a fixed
//   panel at 11e0:0b52's requested left 8 on the fixed 1024x768 desktop
//   (owner decision: fixed in-window panels), the 200x428 client
//   (size_original_dialog_client) inside the TEMP WS_CAPTION frame with
//   the STRL/400 caption the Win32 port sets, scroll bar 7 placed by the
//   DTMP on the white WM_CTLCOLORSCROLLBAR brush; the client painter is
//   the shared paint_original_elevator_control. Presses, releases and
//   moves over the client (not the scroll bar) reach the procedure.
// - The elevator popup (DIALOG 124, ELVPOPUP): 82x67, no frame, at its
//   phase button raised by the mode rows, with SetCapture and ClipCursor;
//   moves, left and right releases reach the procedure. The painter reads
//   the procedure's hover band through the hover_mode accessor the
//   session passes in.
//
// - Facility Information (DIALOG/DTMP 748-760, TENANTINFODLOGFILTER): the
//   chrome, preview, meters, portraits and texts of
//   paint_original_facility_information (the session's painter), the
//   rent / price combo, Rename and New Movie placed by the DTMP; the
//   procedure is the shared OriginalFacilityInformationController.
//
// - Person Information (DIALOG/DTMP 763 / 764, PEPLEINFODLOGFILTER): the
//   chrome, portrait, texts and meters of paint_original_person_information
//   (the session's painter), OK and Rename placed by the DTMP; the
//   procedure is the shared OriginalPersonInformationController.
//
// - Transport Information (DIALOG/DTMP 761 / 762, ELVINFODLOGFILTER and
//   ESCINFODLOGFILTER): the chrome, the two text lines and the portrait
//   strip of paint_original_transport_information (the session's
//   painter), OK placed by the DTMP; the procedure is the shared
//   OriginalTransportInformationController.
//
// Deferred (they keep their logged or auto-answered SDL behavior for a
// later step): the Command selector's own window chrome,
// the startup (New/Load) dialog and the splash. Each becomes another
// descriptor.

#include "sdl_dialog.hpp"

#include "original_dialog_controllers.hpp"
#include "original_dialog_model.hpp"
#include "original_dtmp.hpp"
#include "original_finance.hpp"
#include "original_find.hpp"
#include "original_resources.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace simtower::sdl {

// run_original_event_dialog's dialog from its prepared model.
[[nodiscard]] DialogWindow make_event_dialog(const OriginalResources& resources,
                                            OriginalEventDialogModel model,
                                            std::string label);

// TEMP: the message box text, button and caption font (13-pixel Liberation
// Sans until a Win3.1 reference exists; owner decision 2026-10-23).
inline constexpr int kMessageBoxFontPixels = 13;

// The TEMP layout of one MessageBox, in client coordinates (the caption in
// outer coordinates).
struct MessageBoxLayout {
  std::vector<std::string> text_lines{};
  OriginalRect text_rect{};
  // Empty when the style has no icon.
  OriginalRect icon_rect{};
  OriginalRect caption_rect{};
  int client_width{};
  int client_height{};
  DialogFrameInsets frame{};
  // The button row, left to right (MB_DEFBUTTON1: the first is the default
  // and has the focus).
  std::vector<DialogControl> buttons{};
};
[[nodiscard]] MessageBoxLayout layout_message_box(
    const OriginalMessageBoxRequest& request);

// One MessageBox (TEMP layout, see the file comment).
[[nodiscard]] DialogWindow make_message_box(OriginalMessageBoxRequest request,
                                           std::string label);

[[nodiscard]] DialogWindow make_about_dialog(OriginalAboutDialogModel model);

// TEMP Win3.1 WS_CAPTION frame: a one-pixel border and an 18-pixel navy
// caption (with its one-pixel bottom line) above the client.
inline constexpr int kCaptionFrameBorder = 1;
inline constexpr int kCaptionFrameHeight = 18;

// The template + DTMP dialogs (configure_original_dtmp_window sizes the
// window, render_original_dtmp places and shows the children). The
// procedure's WM_INITDIALOG (the controllers' initialize) runs on the
// result, then focus_first_tab_stop.
[[nodiscard]] DialogWindow make_find_dialog(const OriginalResources& resources,
                                           OriginalFindMode mode);
[[nodiscard]] DialogWindow make_rename_dialog(
    const OriginalResources& resources,
    bool person);
[[nodiscard]] DialogWindow make_movie_dialog(
    const OriginalResources& resources);
// Elevator Control's panel at the launch block's requested left
// (original_elevator_control_launch_contract); client paints the client
// (the session's paint_original_elevator_control over its controller and
// tower).
[[nodiscard]] DialogWindow make_elevator_control_dialog(
    const OriginalResources& resources,
    int requested_left,
    std::string caption,
    DialogDescriptor::Painter client);
// ELVPOPUP with its outer origin at (x, y) in window coordinates;
// hover_mode reads the procedure's hover band for the painter
// (OriginalElevatorPopupController::hover_mode, -1: none; an empty
// function paints no hover).
[[nodiscard]] DialogWindow make_elevator_popup_dialog(
    const OriginalResources& resources, int x, int y,
    std::function<std::int16_t()> hover_mode);

// An Information dialog (DIALOG/DTMP dialog_id), named
// information_dialog_label(kind, dialog_id): no caption (style
// 0x92000040), the DTMP-sized client, statics (and the Facility combo) in
// the template's 10-point font (13 pixels) on the 0xCCCCCC
// WM_CTLCOLORSTATIC brush, the combo's own edit / list in COLOR_WINDOW
// white (as on Win32); client paints the whole client (the session's
// paint_original_facility / person / transport_information over its
// controller); dtmp is the controller's already parsed DTMP/dialog_id. Esc
// sends IDCANCEL, which none of the procedures handles.
// - facility (748-760, TENANTINFODLOGFILTER) and transport (elevator type
//   0 (express) -> 761, every other type -> 762, stairs / escalator 761;
//   ELVINFODLOGFILTER / ESCINFODLOGFILTER): presses on no control reach
//   the procedure as WM_LBUTTONDOWN, moves as WM_MOUSEMOVE.
// - person (763 / 764, PEPLEINFODLOGFILTER) handles no mouse or key
//   message: presses on no control and moves reach nothing. TEMP: its
//   SetCapture(dialog) (1100:0145) is not reproduced, the children take
//   clicks (the open question of Rename Person 730, make_rename_dialog in
//   the .cpp).
[[nodiscard]] DialogWindow make_information_dialog(
    const OriginalResources& resources,
    InformationDialogKind kind,
    std::uint16_t dialog_id,
    const OriginalDtmp& dtmp,
    DialogDescriptor::Painter client);

// paint_state() != 0 draws the OK surface pressed (BITMAP/501).
[[nodiscard]] DialogWindow make_finance_dialog(
    const OriginalResources& resources,
    OriginalFinanceView view);

}  // namespace simtower::sdl
