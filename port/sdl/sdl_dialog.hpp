#pragma once

// Portable dialog manager of the SDL host: one modal dialog drawn into the
// single window from the original Win16 resources, identical on every
// platform (no native OS controls). No SDL here, so headless runs and the
// portable tests use it too.
//
// The manager is generic: a DialogWindow (one open dialog; nothing in it is
// modal, only Session::run_dialog_modal is) holds the controls built from a
// DIALOG template (plus the DTMP child placement), the focus, the pressed
// (captured) push button, Tab / Enter / Esc / Space / mnemonics, pointer
// capture, the dialog timer and the placement. Everything a particular
// dialog adds comes from its DialogDescriptor: the placement rule, the
// timer (id, interval, repeat), the control font and static colours, the
// IsDialogMessage Esc rule, the painters of its frame and client, and hooks
// for the input the manager does not consume (it reaches the dialog
// procedure, e.g. ABOUTDLGPROC's close on any key or click). The ported
// dialogs are descriptors in sdl_dialog_descriptors.hpp: the event
// dialogs, the message boxes, About, Find Person / Tenant, Rename Person /
// Tenant, New Movie, Finance, Facility / Person / Transport Information,
// the modeless Elevator Control panel and its ELVPOPUP.
//
// Session::run_dialog_modal feeds a DialogWindow input and passes the
// signals it produces (WM_COMMAND, WM_TIMER, pointer and key messages the
// manager does not consume, the About close) to a procedure that mirrors
// the Win32 dialog procedure (for Find, Rename, New Movie, Finance,
// Facility / Person / Transport Information and ELVPOPUP, the shared
// controller of original_dialog_controllers.hpp,
// original_facility_information_controller.hpp,
// original_person_information_controller.hpp,
// original_transport_information_controller.hpp or
// original_elevator_control_controller.hpp that the Win32 host runs too;
// the Elevator Control panel's controller runs modeless, below). The
// procedure acts on the dialog only through OriginalDialogOps
// (original_dialog_model.hpp has the full op set). draw_dialog paints it.
//
// Controls (their painting is in sdl_dialog_widgets.cpp, their behavior in
// the DialogWindow members in sdl_dialog.cpp): push buttons, text statics,
// single-line edits, list boxes, drop-down-list combo boxes and vertical
// SB_CTL scroll bars, each with the Win32 messages the
// game's procedures use (OriginalDialogOps) and the notifications they read
// (LBN_SELCHANGE / LBN_DBLCLK, CBN_SELCHANGE, EN_CHANGE, WM_VSCROLL). The
// dialog manager keeps IsDialogMessage's keyboard rules: Tab / Shift-Tab
// over the WS_TABSTOP controls, the arrows over the WS_GROUP group of the
// focused control unless it keeps them (edit, list, combo, scroll bar),
// Enter = the focused or default push button, Esc = IDCANCEL, Space presses
// a focused push button on key down and clicks it on key up, and a letter
// clicks the push button whose mnemonic it is unless the focused control
// takes characters. Alt and F10 alone (WM_SYSKEYDOWN) reach no procedure.
//
// Scroll bars are vertical only: a template SBS_HORZ bar (style bit 0
// clear) is laid out, painted and hit-tested as a vertical one; none of
// the ported dialogs has one.
//
// Modeless use (Elevator Control 400, CreateDialogIndirectParamW): the
// session keeps one DialogWindow beside the main loop and feeds it the
// pointer events over it (or all of them while it holds the pointer
// capture) and the keys while it is active; see Session::elevator_control.
// WM_MOUSEMOVE reaches a procedure through DialogDescriptor::mouse_move,
// SetCapture / ClipCursor through capture_pointer / clip_pointer, and the
// WM_CTLCOLORSCROLLBAR brush through scroll_bar_background.
//
// TEMP (no Win3.1 reference yet, owner decision 2026-10-23): the Win3.1
// look and metrics of these USER-drawn controls (bevels, edit text inset,
// caret blink, list item height, scroll bar width, combo field height) and
// the 13-pixel Liberation Sans of their text.

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
  // The window text (Windows-1252): a label, or an edit's contents.
  std::string text{};
  // WS_VISIBLE, or shown by the DTMP render pass or show_item.
  bool visible{};
  bool enabled{};
  bool tab_stop{};
  // Edit: the selection runs from anchor to caret (the caret is its active
  // end); no selection when they are equal.
  std::size_t caret{};
  std::size_t anchor{};
  // List box / combo box: the strings in order (unsorted templates only),
  // the selection (-1: none), the first visible row and the caret row
  // (focus rectangle). A combo box's list is open while dropped.
  std::vector<std::string> items{};
  int selection{-1};
  int top_index{};
  int caret_index{};
  bool dropped{};
  // Scroll bar (SB_CTL): range and thumb position (the procedure owns the
  // position; the control only moves its tracking ghost while dragged).
  int scroll_min{};
  int scroll_max{};
  int scroll_pos{};
};

// The keys the dialog manager (IsDialogMessage) and the procedures see.
// `other` with a letter is a character key (a button mnemonic: IsDialogMessage
// clicks the push button whose '&' letter matches, e.g. Y/N on a MessageBox).
// `menu` is Alt or F10 pressed alone (WM_SYSKEYDOWN).
enum class DialogKey : std::uint8_t {
  enter,
  escape,
  tab,
  shift_tab,
  space,
  left,
  right,
  up,
  down,
  home,
  end,
  page_up,
  page_down,
  backspace,
  delete_key,
  f4,
  menu,
  other,
};

// The Win32 virtual key of a key (VK_RETURN, VK_SPACE, ...; the upper-case
// letter for `other` with a letter, else 0).
[[nodiscard]] std::uint16_t dialog_key_virtual_key(DialogKey key,
                                                   char character);

// What the dialog passes to its procedure (original_dialog_model.hpp).
using DialogSignal = OriginalDialogSignal;

struct DialogStep {
  bool redraw{};
  std::optional<DialogSignal> signal{};
  // A second signal the same input sends right after the first (a scroll
  // bar's SB_THUMBPOSITION then SB_ENDSCROLL).
  std::optional<DialogSignal> then{};
};

// Window-coordinate mouse event (sdl_session.hpp's InputEvent, kept here
// without the session).
struct DialogPointer {
  enum class Kind : std::uint8_t { move, down, up, right_down, right_up };
  Kind kind{Kind::move};
  int x{};
  int y{};
  // MK_SHIFT (an edit extends its selection).
  bool shift{};
};

class DialogWindow;

// Where a dialog goes in the desktop (the SDL window is the Win16 desktop).
enum class DialogPlacement : std::uint8_t {
  screen_position,  // 11e0:0b52 original_dialog_screen_position
  centered,         // 11e0:0c10 original_dialog_center_position
  // 11e0:0b52 with the descriptor's requested_left on the fixed
  // kDesktopWidth x kDesktopHeight desktop the palettes use (a fixed panel
  // that stays there while it fits; TEMP: a smaller window moves it up or
  // left just enough to keep its bottom and right edges inside).
  fixed_desktop,
  // An explicit outer origin (descriptor.origin) in window coordinates
  // (SetWindowPos of a popup, e.g. ELVPOPUP over its phase button).
  at,
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

// The per-dialog part of a DialogWindow.
struct DialogDescriptor {
  // The script name ("expect-dialog"): event_dialog_label and friends.
  std::string label{};
  DialogPlacement placement{DialogPlacement::centered};
  // 11e0:0b52's requested left (0: centered horizontally).
  int requested_left{};
  // DialogPlacement::at's outer origin.
  int origin_x{};
  int origin_y{};
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
                                     const DialogWindow& dialog,
                                     const OriginalResources& resources)>;
  // The frame painter also gets whether the window is the active one
  // (COLOR_ACTIVECAPTION; otherwise COLOR_INACTIVECAPTION).
  using FramePainter = std::function<void(OriginalDrawContext& context,
                                          OriginalCanvasFont& font,
                                          const DialogWindow& dialog,
                                          const OriginalResources& resources,
                                          bool active)>;
  FramePainter paint_frame{};
  Painter paint_client{};
  // IsDialogMessage(VK_ESCAPE): the WM_COMMAND id to send, or nullopt for
  // nothing. Null: IDCANCEL whether or not a control 2 exists (the dialog
  // manager's rule).
  std::function<std::optional<std::uint16_t>(const DialogWindow& dialog)>
      escape_command{};
  // A key the manager does not consume (Space without a focused push
  // button, a letter that is no mnemonic) reaches the dialog procedure as
  // WM_KEYDOWN; this maps it to a signal. Null ignores it.
  std::function<std::optional<DialogSignal>(DialogKey key, char character)>
      unhandled_key{};
  // A WM_KEYUP the manager does not consume (any key up but the Space that
  // clicks a pressed push button). Null ignores it.
  std::function<std::optional<DialogSignal>(DialogKey key, char character)>
      unhandled_key_up{};
  // A button event on no control that takes it (and not ending a capture)
  // reaches the dialog procedure (WM_LBUTTONDOWN / WM_LBUTTONUP /
  // WM_RBUTTONDOWN / WM_RBUTTONUP); the pointer is in window coordinates.
  // Null ignores it.
  std::function<std::optional<DialogSignal>(const DialogPointer& pointer,
                                            int client_x, int client_y)>
      unhandled_pointer{};
  // WM_MOUSEMOVE reaching the dialog procedure: a move over the client and
  // over no control that takes the mouse, or any move while capture_pointer
  // holds the capture (no control drag running). Null: no moves.
  std::function<std::optional<DialogSignal>(const DialogPointer& pointer,
                                            int client_x, int client_y)>
      mouse_move{};
  // SetCapture(dialog) for its whole life (ELVPOPUP 1098:22f8): moves and
  // releases anywhere reach mouse_move / unhandled_pointer.
  bool capture_pointer{};
  // ClipCursor to the client (TEMP emulation: incoming points are clamped
  // to [0, w-1] x [0, h-1] of the client).
  bool clip_pointer{};
  // WM_CTLCOLORSCROLLBAR's brush behind an SB_CTL scroll bar's trough
  // (null: the TEMP button face).
  std::optional<OriginalRgb> scroll_bar_background{};
  // WM_CTLCOLOREDIT / WM_CTLCOLORLISTBOX brushes (null: the control's own
  // COLOR_WINDOW white).
  std::optional<OriginalRgb> edit_background{};
  std::optional<OriginalRgb> list_background{};
};

// Which dialog is open, as scripts name it ("expect-dialog"): "event ID"
// (DIALOG/DTMP 3000-3040), "alert ID" (ALRT), "box replace" / "box
// invalid-filename" (the literal File MessageBoxes) or "about".
[[nodiscard]] std::string event_dialog_label(std::uint16_t dialog_id);
[[nodiscard]] std::string alert_dialog_label(int alert_id);
// The three Information dialogs: Facility (DIALOG/DTMP 748-760), Person
// (763 / 764) and Transport (761 / 762).
enum class InformationDialogKind : std::uint8_t { facility, person, transport };
// "facility", "person" or "transport".
[[nodiscard]] const char* information_dialog_kind_name(
    InformationDialogKind kind);
[[nodiscard]] std::optional<InformationDialogKind>
parse_information_dialog_kind(std::string_view name);
// "facility ID", "person ID" or "transport ID".
[[nodiscard]] std::string information_dialog_label(InformationDialogKind kind,
                                                   std::uint16_t dialog_id);
inline constexpr char kAboutDialogLabel[] = "about";
inline constexpr char kReplaceBoxLabel[] = "box replace";
inline constexpr char kInvalidFilenameBoxLabel[] = "box invalid-filename";
inline constexpr char kFinanceDialogLabel[] = "finance";
inline constexpr char kFindPersonDialogLabel[] = "find person";
inline constexpr char kFindTenantDialogLabel[] = "find tenant";
inline constexpr char kRenamePersonDialogLabel[] = "rename person";
inline constexpr char kRenameTenantDialogLabel[] = "rename tenant";
inline constexpr char kMovieDialogLabel[] = "movie";
inline constexpr char kElevatorControlDialogLabel[] = "elevator control";
inline constexpr char kElevatorPopupDialogLabel[] = "elevator popup";

class DialogWindow final : public OriginalDialogOps {
 public:
  // dialog_template: the template the dialog-manager rules read (Enter's
  // default id); controls: its children in template order (Z order: the
  // first is on top), in client coordinates.
  DialogWindow(DialogDescriptor descriptor,
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
  // Whether the dialog holds the mouse capture: a pressed button, a drag
  // in an edit or list, a scroll bar click or thumb drag (USER tracks those
  // with the capture), or capture_pointer. A host routes every pointer
  // event to it then, wherever the pointer is.
  [[nodiscard]] bool has_pointer_capture() const {
    return captured_ != 0U || scroll_drag_.has_value() ||
           drag_ != Drag::none || descriptor_.capture_pointer;
  }
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

  // The dialog manager's focus after WM_INITDIALOG returns TRUE: the first
  // visible, enabled WS_TABSTOP control in template order (none: 0).
  void focus_first_tab_stop();
  // The focused control (null: none).
  [[nodiscard]] const DialogControl* focused_control() const {
    return control(focus_);
  }
  // Window-coordinate center of list item `index` of list box id when it is
  // a visible row (scripts' "dialog-list"); nullopt otherwise.
  [[nodiscard]] std::optional<std::pair<int, int>> list_item_point(
      std::uint16_t id, int index) const;
  // A visible, enabled drop-down combo box id: the window-coordinate
  // center of its field, and of row `index` of its dropped list (whether
  // or not it is dropped now; nullopt when the row does not fit).
  [[nodiscard]] std::optional<std::pair<int, int>> combo_field_point(
      std::uint16_t id) const;
  [[nodiscard]] std::optional<std::pair<int, int>> combo_item_point(
      std::uint16_t id, int index) const;
  // The dialog clock (milliseconds since it opened): double clicks and the
  // caret blink use it, never the session clock.
  void set_clock(std::uint64_t ms) { clock_ms_ = ms; }
  [[nodiscard]] std::uint64_t clock() const { return clock_ms_; }
  // Whether the edit caret is drawn now (focused edit, blink phase), and
  // the dialog-clock time it next blinks (nullopt: no focused edit).
  [[nodiscard]] bool caret_visible() const;
  [[nodiscard]] std::optional<std::uint64_t> next_caret_blink() const;

  DialogStep pointer(const DialogPointer& event);
  // A key press (WM_KEYDOWN through IsDialogMessage). character: the
  // lower-case letter of a character key (0 for none); shift: Shift is
  // down (Shift+arrows extend an edit selection).
  DialogStep key(DialogKey key, char character = '\0', bool shift = false);
  // A key release (WM_KEYUP).
  DialogStep key_up(DialogKey key, char character = '\0');
  // One typed character (WM_CHAR, Windows-1252) for the focused control: an
  // edit inserts it, a list box or combo box selects the next item starting
  // with it; anything else ignores it.
  DialogStep character(char cp1252);
  // WM_TIMER: counts the tick and passes the timer signal on.
  DialogStep timer();
  // Whether the dialog owes its procedure a WM_PAINT (when it opened, after
  // invalidate, after a covering dialog closed); clears it.
  [[nodiscard]] bool take_paint();
  // A nested dialog that covered this one closed.
  void uncovered() {
    paint_pending_ = true;
    invalidated_ = true;
  }

  // OriginalDialogOps, for the procedure. Each marks the dialog for a
  // repaint (take_invalidated).
  void show_item(std::uint16_t id, bool shown) override;
  void invalidate() override;
  void resize(int client_width, int client_height) override;
  void enable_item(std::uint16_t id, bool enabled) override;
  void focus_item(std::uint16_t id) override;
  void set_item_text(std::uint16_t id, std::string_view cp1252) override;
  [[nodiscard]] std::string item_text(std::uint16_t id) const override;
  void list_reset(std::uint16_t id) override;
  void list_add(std::uint16_t id, std::string_view cp1252) override;
  void list_delete(std::uint16_t id, int index) override;
  [[nodiscard]] int list_selection(std::uint16_t id) const override;
  void list_set_selection(std::uint16_t id, int index) override;
  void set_scroll_range(std::uint16_t id, int min, int max) override;
  void set_scroll_pos(std::uint16_t id, int position) override;
  // UpdateWindow: runs the host's synchronous repaint (set_update_window;
  // nothing without one).
  void update_window() override;
  // Whether an op asked for a repaint since the last call (then clears it).
  [[nodiscard]] bool take_invalidated();

  // The host's UpdateWindow (Session::run_dialog_modal presents the window
  // when the dialog is invalid); empty clears it.
  void set_update_window(std::function<void()> update) {
    update_window_ = std::move(update);
  }
  // The procedure's own state that the descriptor's painters read (the
  // DWLP_USER context of the Win32 procedure; Finance: nonzero while its
  // OK surface is drawn pressed). The host that runs the procedure sets the
  // source and keeps what it reads alive while the dialog is open; 0
  // without one.
  void set_paint_state(std::function<std::uint32_t()> source) {
    paint_state_ = std::move(source);
  }
  [[nodiscard]] std::uint32_t paint_state() const {
    return paint_state_ ? paint_state_() : 0U;
  }

  // Widget geometry shared by the painters, hit tests and tests.
  [[nodiscard]] OriginalRect scroll_track(const DialogControl& control) const;
  [[nodiscard]] int list_rows(const DialogControl& control) const;
  [[nodiscard]] bool list_has_scroll_bar(const DialogControl& control) const;
  // The tracking ghost of a dragged scroll bar thumb (nullopt: none).
  [[nodiscard]] std::optional<int> scroll_tracking(std::uint16_t id) const {
    return scroll_drag_ && scroll_drag_->id == id
               ? std::optional<int>(scroll_drag_->position)
               : std::nullopt;
  }
  // Whether a scroll bar press is being tracked (thumb drag, or an arrow
  // or page click until its release). USER runs its own message loop for
  // that, so the application's idle loop does not run meanwhile.
  [[nodiscard]] bool scroll_tracking_active() const {
    return scroll_drag_.has_value();
  }
  // WM_CANCELMODE for a tracked scroll bar: ends the tracking with no
  // further WM_VSCROLL (TEMP: USER's exact notifications on a cancelled
  // track are not confirmed). Returns whether one was tracked.
  bool cancel_scroll_tracking() {
    if (!scroll_drag_) return false;
    scroll_drag_.reset();
    invalidated_ = true;
    return true;
  }

 private:
  // The rectangle that takes the mouse: the control's, except a combo
  // box's field (plus its list while dropped).
  [[nodiscard]] OriginalRect hit_rect(const DialogControl& control) const;
  [[nodiscard]] const DialogControl* control_at(int x, int y) const;
  [[nodiscard]] DialogControl* mutable_control(std::uint16_t id);
  void move_focus(bool forward);
  void move_group_focus(bool forward);
  // WM_NEXTDLGCTL-style focus change: an edit gets its whole text selected.
  void set_focus(std::uint16_t id, bool select_edit_text);
  void close_drop_downs();
  DialogStep edit_key(DialogControl& edit, DialogKey key, bool shift);
  DialogStep edit_character(DialogControl& edit, char cp1252);
  DialogStep list_key(DialogControl& list, DialogKey key);
  DialogStep list_select(DialogControl& list, int index, bool notify);
  DialogStep combo_key(DialogControl& combo, DialogKey key);
  DialogStep scroll_key(DialogControl& bar, DialogKey key);
  DialogStep pointer_down(const DialogPointer& event, int x, int y);
  DialogStep scroll_bar_down(DialogControl& bar, const OriginalRect& area,
                             bool for_list, int x, int y);
  void list_scroll_to(DialogControl& list, int top);
  void ensure_visible(DialogControl& list);

  // The drag that a press on a control started (WM_LBUTTONDOWN capture).
  enum class Drag : std::uint8_t { none, button, edit, list, combo_list };
  struct ScrollDrag {
    std::uint16_t id{};
    bool for_list{};
    int grab{};       // pointer offset inside the thumb
    int position{};   // the tracking position
  };

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
  bool space_pressed_{};
  Drag drag_{Drag::none};
  std::optional<ScrollDrag> scroll_drag_{};
  bool invalidated_{};
  bool paint_pending_{true};
  std::uint64_t timer_ticks_{};
  std::uint64_t clock_ms_{};
  std::uint64_t caret_epoch_ms_{};
  // The previous press, for the double-click test (a second press on the
  // same list within 500 ms and 4 pixels).
  std::optional<std::uint64_t> last_press_ms_{};
  int last_press_x_{};
  int last_press_y_{};
  std::uint16_t last_press_id_{};
  std::function<void()> update_window_{};
  std::function<std::uint32_t()> paint_state_{};
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

// TEMP Win3.1-style control metrics (USER computes them; no resource
// holds them, and no reference exists yet).
inline constexpr int kDialogScrollBarWidth = 17;   // list box WS_VSCROLL
inline constexpr int kDialogComboButtonWidth = 17;
inline constexpr int kDialogComboFieldExtra = 6;   // field = line + 6
inline constexpr int kDialogEditInsetX = 2;        // inside the border
inline constexpr int kDialogEditInsetY = 2;
inline constexpr int kDialogListInsetX = 2;
inline constexpr int kDialogCaretBlinkMs = 530;
// GetDoubleClickTime and SM_CXDOUBLECLK defaults.
inline constexpr int kDialogDoubleClickMs = 500;
inline constexpr int kDialogDoubleClickDistance = 4;

// An edit's text origin (client coordinates) and the width its text may
// fill (no ES_AUTOHSCROLL: a character that would pass it is refused).
struct DialogEditLayout {
  int text_x{};
  int text_y{};
  int width{};
};
[[nodiscard]] DialogEditLayout dialog_edit_layout(const DialogControl& edit);
// The character boundary nearest to client x in an edit.
[[nodiscard]] std::size_t dialog_edit_hit(OriginalCanvasFont& font,
                                          int pixel_height,
                                          const DialogControl& edit,
                                          int x);
// The rows of a list box (its rectangle inside the border) and of an open
// combo box list, in client coordinates.
[[nodiscard]] OriginalRect dialog_list_rows_rect(const DialogControl& list);
[[nodiscard]] OriginalRect dialog_combo_field_rect(const DialogControl& combo,
                                                   int line_height);
[[nodiscard]] OriginalRect dialog_combo_list_rect(const DialogControl& combo,
                                                  int line_height);

// The parts of a vertical scroll bar (SB_CTL or a list's WS_VSCROLL) at a
// position in [min, max]: the arrow buttons (each min(width, height / 2)
// tall), the trough between them and the square thumb. The painter, the
// hit test and the thumb drag all use it.
struct DialogScrollParts {
  OriginalRect up{};
  OriginalRect down{};
  int track_top{};
  int track_bottom{};
  // Empty when the range is empty or there is no room (no thumb).
  OriginalRect thumb{};
};
[[nodiscard]] DialogScrollParts dialog_scroll_parts(const OriginalRect& bar,
                                                    int min,
                                                    int max,
                                                    int position);

// The controls, bottom up (the first template child is on top), each in
// its Win3.1-style look (TEMP), then an open combo box list over them.
void paint_dialog_controls(OriginalDrawContext& context,
                           OriginalCanvasFont& font,
                           const DialogWindow& dialog);

// Paints the dialog at its placement into a window raster: the frame
// painter (with its caption active or inactive), the client painter, then
// the controls bottom up.
void draw_dialog(OriginalWorldRaster& raster,
                 const DialogWindow& dialog,
                 const OriginalResources& resources,
                 bool active = true);

}  // namespace simtower::sdl
