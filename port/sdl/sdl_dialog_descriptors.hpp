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
// Deferred (they keep their logged or auto-answered SDL behavior for a
// later step): facility and person information, transport information
// (elevator/escalator magnifier), Finance, Find person/tenant and its
// results, Rename, the movie theater dialog, the elevator popup, the Command
// selector's own window chrome, the startup (New/Load) dialog, the splash,
// and Elevator Control. Each becomes another descriptor.

#include "sdl_dialog.hpp"

#include "original_dialog_model.hpp"
#include "original_resources.hpp"

#include <string>
#include <vector>

namespace simtower::sdl {

// run_original_event_dialog's dialog from its prepared model.
[[nodiscard]] ModalDialog make_event_dialog(const OriginalResources& resources,
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
[[nodiscard]] ModalDialog make_message_box(OriginalMessageBoxRequest request,
                                           std::string label);

[[nodiscard]] ModalDialog make_about_dialog(OriginalAboutDialogModel model);

}  // namespace simtower::sdl
