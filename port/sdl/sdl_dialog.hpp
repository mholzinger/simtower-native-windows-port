#pragma once

// Portable dialog manager of the SDL host: one modal dialog drawn into the
// single window from the original Win16 resources, identical on every
// platform (no native OS controls). No SDL here, so headless runs and the
// portable tests use it too.
//
// The manager is generic: a ModalDialog holds the controls built from a
// DIALOG template (plus the DTMP child placement), the focus, the pressed
// (captured) push button, Tab / Enter / Esc / Space / mnemonics, pointer
// capture, the dialog timer and the placement. Everything a particular
// dialog adds comes from its DialogDescriptor: the placement rule, the
// timer (id, interval, repeat), the control font and static colours, the
// IsDialogMessage Esc rule, the painters of its frame and client, and hooks
// for the input the manager does not consume (it reaches the dialog
// procedure, e.g. ABOUTDLGPROC's close on any key or click). The ported
// dialogs are three descriptors (sdl_dialog_descriptors.hpp): the event
// dialogs, the message boxes and About.
//
// Session::run_dialog_modal feeds a ModalDialog input and passes the
// signals it produces (WM_COMMAND, WM_TIMER, the About close) to a
// procedure that mirrors the Win32 dialog procedure; the procedure acts on
// the dialog through OriginalDialogOps (show_item / invalidate / resize).
// draw_dialog paints it.
//
// TEMP: push buttons use a fixed Win3.1-style look (USER draws them; no
// resource holds it). Space clicks the focused button on the key press, and
// the arrow keys do not move between grouped controls. Edit, list box,
// combo box and scroll bar controls are built (they keep their template
// rectangle) but have no behavior or painting yet.

#include "sdl_layout.hpp"

#include "original_canvas_font.hpp"
#include "original_dialog_model.hpp"
#include "original_draw_context.hpp"
#include "original_dtmp.hpp"
#include "original_resources.hpp"
#include "original_world.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace simtower::sdl {

using DialogControlKind = OriginalWidgetKind;

struct DialogControl {
  std::uint16_t id{};
  DialogControlKind kind{DialogControlKind::other};
  std::uint32_t style{};
  // Client coordinates (a GDI RECT).
  OriginalRect rect{};
  std::string text{};
  // WS_VISIBLE, or shown by the DTMP render pass or show_item.
  bool visible{};
  bool enabled{};
  bool tab_stop{};
};

// The keys the dialog manager (IsDialogMessage) and the procedures see.
// `other` with a letter is a character key (a button mnemonic: IsDialogMessage
// clicks the push button whose '&' letter matches, e.g. Y/N on a MessageBox).
enum class DialogKey : std::uint8_t {
  enter,
  escape,
  tab,
  shift_tab,
  space,
  arrow,
  other,
};

// What the dialog passes to its procedure (original_dialog_model.hpp).
using DialogSignal = OriginalDialogSignal;

struct DialogStep {
  bool redraw{};
  std::optional<DialogSignal> signal{};
};

// Window-coordinate mouse event (sdl_session.hpp's InputEvent, kept here
// without the session).
struct DialogPointer {
  enum class Kind : std::uint8_t { move, down, up, right_down };
  Kind kind{Kind::move};
  int x{};
  int y{};
};

class ModalDialog;

// Where a dialog goes in the desktop (the SDL window is the Win16 desktop).
enum class DialogPlacement : std::uint8_t {
  screen_position,  // 11e0:0b52 original_dialog_screen_position
  centered,         // 11e0:0c10 original_dialog_center_position
};

// SetTimer at WM_INITDIALOG / the first WM_PAINT; id 0 means none.
struct DialogTimerSpec {
  std::uint16_t id{};
  std::uint32_t interval_ms{};
  // KillTimer in the procedure (event dialogs, 1068:0380) vs a repeating
  // timer (About's credits).
  bool repeats{};
};

// The nonclient frame: the client's insets inside the outer rectangle.
struct DialogFrameInsets {
  int left{};
  int top{};
  int right{};
  int bottom{};
};

// The per-dialog part of a ModalDialog.
struct DialogDescriptor {
  // The script name ("expect-dialog"): event_dialog_label and friends.
  std::string label{};
  DialogPlacement placement{DialogPlacement::centered};
  DialogTimerSpec timer{};
  // Whether the modal holds DS:24b8 (event dialogs and About; MessageBox
  // does not).
  bool counts_modal_lock{};
  // The pixel height of the font its push buttons and statics draw with.
  int font_pixels{};
  // The WM_CTLCOLORSTATIC brush behind static text.
  OriginalRgb static_background{};
  DialogFrameInsets frame{};
  // Painters: the frame in outer coordinates (null: none), and the client
  // in client coordinates before the controls (WM_ERASEBKGND / WM_PAINT of
  // the dialog itself; null: nothing).
  using Painter = std::function<void(OriginalDrawContext& context,
                                     OriginalCanvasFont& font,
                                     const ModalDialog& dialog,
                                     const OriginalResources& resources)>;
  Painter paint_frame{};
  Painter paint_client{};
  // IsDialogMessage(VK_ESCAPE): the WM_COMMAND id to send, or nullopt for
  // nothing. Null: IDCANCEL whether or not a control 2 exists (the dialog
  // manager's rule).
  std::function<std::optional<std::uint16_t>(const ModalDialog& dialog)>
      escape_command{};
  // A key the manager does not consume (Space without a focused push
  // button, a letter that is no mnemonic) reaches the dialog procedure as
  // WM_KEYDOWN; this maps it to a signal. Null ignores it.
  std::function<std::optional<DialogSignal>(DialogKey key, char character)>
      unhandled_key{};
  // A button event on no push button (and not ending a capture) reaches the
  // dialog procedure (WM_LBUTTONUP / WM_RBUTTONDOWN). Null ignores it.
  std::function<std::optional<DialogSignal>(const DialogPointer& pointer)>
      unhandled_pointer{};
};

// Which dialog is open, as scripts name it ("expect-dialog"): "event ID"
// (DIALOG/DTMP 3000-3040), "alert ID" (ALRT), "box replace" / "box
// invalid-filename" (the literal File MessageBoxes) or "about".
[[nodiscard]] std::string event_dialog_label(std::uint16_t dialog_id);
[[nodiscard]] std::string alert_dialog_label(int alert_id);
inline constexpr char kAboutDialogLabel[] = "about";
inline constexpr char kReplaceBoxLabel[] = "box replace";
inline constexpr char kInvalidFilenameBoxLabel[] = "box invalid-filename";

class ModalDialog final : public OriginalDialogOps {
 public:
  // dialog_template: the template the dialog-manager rules read (Enter's
  // default id); controls: its children in template order (Z order: the
  // first is on top), in client coordinates.
  ModalDialog(DialogDescriptor descriptor,
              OriginalDialog dialog_template,
              std::vector<DialogControl> controls,
              int client_width,
              int client_height,
              std::uint16_t initial_focus);

  [[nodiscard]] const DialogDescriptor& descriptor() const {
    return descriptor_;
  }
  // The script name of the dialog (event_dialog_label and friends).
  [[nodiscard]] const std::string& label() const { return descriptor_.label; }
  // The pixel height of the font its controls draw with.
  [[nodiscard]] int font_pixels() const { return descriptor_.font_pixels; }
  [[nodiscard]] bool counts_modal_lock() const {
    return descriptor_.counts_modal_lock;
  }

  // Positions the dialog in a desktop (the window) of this size by the
  // descriptor's placement rule; resize re-applies it.
  void place(int desktop_width, int desktop_height);

  // Outer rectangle in window coordinates.
  [[nodiscard]] PanelRect outer() const {
    return {x_, y_, outer_width(), outer_height()};
  }
  [[nodiscard]] int outer_width() const {
    return client_width_ + descriptor_.frame.left + descriptor_.frame.right;
  }
  [[nodiscard]] int outer_height() const {
    return client_height_ + descriptor_.frame.top + descriptor_.frame.bottom;
  }
  // Client origin in window coordinates and client size.
  [[nodiscard]] int client_x() const { return x_ + descriptor_.frame.left; }
  [[nodiscard]] int client_y() const { return y_ + descriptor_.frame.top; }
  [[nodiscard]] int client_width() const { return client_width_; }
  [[nodiscard]] int client_height() const { return client_height_; }
  [[nodiscard]] const std::vector<DialogControl>& controls() const {
    return controls_;
  }
  [[nodiscard]] const DialogControl* control(std::uint16_t id) const;
  [[nodiscard]] std::uint16_t focus() const { return focus_; }
  // The captured push button and whether it is drawn pressed.
  [[nodiscard]] std::uint16_t captured() const { return captured_; }
  [[nodiscard]] bool pressed() const { return pressed_; }
  // The push button drawn as the default (thick border): the Enter target
  // when it is a push button.
  [[nodiscard]] std::uint16_t default_button() const;
  // The WM_COMMAND id Enter sends (original_dialog_enter_command).
  [[nodiscard]] std::uint16_t enter_command() const;
  // Window-coordinate center of a visible, enabled push button, or of the
  // client for id 0 on a dialog without controls (About). For scripts:
  // nullopt for any other id (missing, hidden, disabled or not a button).
  [[nodiscard]] std::optional<std::pair<int, int>> click_point(
      std::uint16_t id) const;

  // The descriptor's timer, and how often it has fired.
  [[nodiscard]] std::uint16_t timer_id() const {
    return descriptor_.timer.id;
  }
  [[nodiscard]] std::uint32_t timer_interval_ms() const {
    return descriptor_.timer.interval_ms;
  }
  [[nodiscard]] bool timer_repeats() const {
    return descriptor_.timer.repeats;
  }
  [[nodiscard]] std::uint64_t timer_ticks() const { return timer_ticks_; }

  DialogStep pointer(const DialogPointer& event);
  // character: the lower-case letter of a character key (0 for none).
  DialogStep key(DialogKey key, char character = '\0');
  // WM_TIMER: counts the tick and passes the timer signal on.
  DialogStep timer();

  // OriginalDialogOps, for the procedure. Each marks the dialog for a
  // repaint (take_invalidated).
  void show_item(std::uint16_t id, bool shown) override;
  void invalidate() override;
  void resize(int client_width, int client_height) override;
  // Whether an op asked for a repaint since the last call (then clears it).
  [[nodiscard]] bool take_invalidated();

 private:
  [[nodiscard]] const DialogControl* control_at(int x, int y) const;
  void move_focus(bool forward);

  DialogDescriptor descriptor_{};
  OriginalDialog dialog_{};
  std::vector<DialogControl> controls_{};
  int x_{};
  int y_{};
  int desktop_width_{};
  int desktop_height_{};
  int client_width_{};
  int client_height_{};
  std::uint16_t focus_{};
  std::uint16_t captured_{};
  bool pressed_{};
  bool invalidated_{};
  std::uint64_t timer_ticks_{};
};

// Dialog base units of a font (GdiGetCharDimensions): the rounded average
// width of A-Z a-z, and the cell height (ascent + descent).
struct DialogBaseUnits {
  int x{};
  int y{};
};
[[nodiscard]] DialogBaseUnits dialog_base_units(OriginalCanvasFont& font,
                                                int pixel_height);
// MapDialogRect: dialog units to pixels.
[[nodiscard]] OriginalRect map_dialog_rect(const DialogBaseUnits& units,
                                           int x,
                                           int y,
                                           int width,
                                           int height);

// The dialog manager's children of a template: one control per item in
// template order, at its MapDialogRect rectangle, with its widget kind and
// its WS_VISIBLE / WS_DISABLED / WS_TABSTOP state.
[[nodiscard]] std::vector<DialogControl> dialog_template_controls(
    const OriginalDialog& dialog,
    const DialogBaseUnits& units);
// render_original_dtmp's MoveWindow + ShowWindow(SW_SHOW) of every
// DTMP-placed child that exists (the DTMP rectangle overrides the
// template's).
void apply_dtmp_child_placement(std::vector<DialogControl>& controls,
                                const OriginalDtmp& dtmp);

// A push button's label placement: the centered text origin and extent
// (shifted one pixel down-right while pressed), the dotted focus rectangle
// around it (the text cell inflated by one pixel, inside the bevel) and
// the '&' mnemonic underline (empty without a mnemonic). Shared by the
// painter and the tests.
struct PushButtonLabel {
  std::string text{};  // the '&' mnemonic removed
  int x{};
  int y{};
  int extent{};
  int height{};
  OriginalRect focus{};
  OriginalRect underline{};
};
[[nodiscard]] PushButtonLabel push_button_label(OriginalCanvasFont& font,
                                                const DialogControl& control,
                                                int pixel_height,
                                                bool pressed,
                                                bool is_default);
// The '&' mnemonic letter of a control text (lower case), or 0.
[[nodiscard]] char dialog_mnemonic(std::string_view text);
// The control text without the '&' prefixes ("&&" is a literal '&').
[[nodiscard]] std::string strip_dialog_mnemonic(std::string_view text);

// DrawText(DT_WORDBREAK) line breaking: '\n' ends a line, and words move to
// the next line when the line would exceed width (a word wider than width
// stays whole and is clipped).
[[nodiscard]] std::vector<std::string> wrap_dialog_text(
    OriginalCanvasFont& font,
    int pixel_height,
    std::string_view text,
    int width);

// Painting helpers shared by the descriptors' painters.
[[nodiscard]] int dialog_line_height(OriginalCanvasFont& font,
                                     int pixel_height);
// One line of text at (x, y) (TA_LEFT | TA_TOP), clipped to clip.
void draw_dialog_text_line(OriginalDrawContext& context,
                           OriginalCanvasFont& font,
                           int pixel_height,
                           std::string_view text,
                           int x,
                           int y,
                           const OriginalRect& clip);
// A one-pixel rectangle outline.
void draw_dialog_outline(OriginalDrawContext& context,
                         const OriginalRect& rect,
                         OriginalRgb rgb);

// Paints the dialog at its placement into a window raster: the frame
// painter, the client painter, then the controls bottom up.
void draw_dialog(OriginalWorldRaster& raster,
                 const ModalDialog& dialog,
                 const OriginalResources& resources);

}  // namespace simtower::sdl
