#pragma once

// Portable ELVDLOGMAIN (1098:0628) and ELVPOPUP (1098:22f8) procedures:
// the message-to-action half of Elevator Control (DIALOG/400) and of its
// phase popup (DIALOG/124), over OriginalDialogOps (the dialog's own
// scroll bar and repaints) and OriginalElevatorControlHost (the document,
// the windows outside the dialog, sounds, alerts and the popup). Both hosts
// run them: elevator_control_dialog_proc / elevator_popup_dialog_proc in
// native_main.cpp over their HWNDs, and the SDL session over its fixed
// in-window panel. The decisions are the Win32 port's as they were before
// the procedures shared them (elevator_control_controller_tests.cpp keeps
// frozen copies of those branches and compares them with the controller);
// the shared rules they call (schedule edits, scrolling, isolation, grid
// hit tests) stay in original_elevator_control.hpp.
//
// Also here: the portable painters of the control's client and of
// ELVPOPUP, which both hosts run (Win32 through OriginalGdiDrawContext;
// elevator_control_painter_parity_tests proves the pixels equal the direct
// GDI painters the Win32 port had), and the two isolation views every
// Main, Info and Map presentation uses while Simulate runs (the paint-only
// document with the saved `used` bytes and the waiting-isolation view),
// shared so both hosts draw the same tower. No windows.h here.

#include "original_dialog_model.hpp"
#include "original_draw_context.hpp"
#include "original_dtmp.hpp"
#include "original_elevator_control.hpp"
#include "original_people.hpp"
#include "original_resources.hpp"
#include "original_tables.hpp"
#include "original_tdt.hpp"
#include "original_world.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace simtower {

// The one child of DIALOG/400: SCROLLBAR 7 (SBS_VERT, WS_TABSTOP).
inline constexpr std::uint16_t kOriginalElevatorControlScrollBarId = 7U;
// DTMP/400 rectangles 1..28 are the hit-tested items.
inline constexpr std::size_t kOriginalElevatorControlItemCount = 28U;
// 10f0:0318's movement sound, once per preview request.
inline constexpr std::int32_t kOriginalElevatorControlMovementSound = 0x1772;
inline constexpr std::uint16_t kOriginalElevatorPopupDialogId = 124U;
// The DTMP/400 rectangles (one-based) the painter fills with text.
inline constexpr std::size_t kOriginalElevatorControlWaitingRectangle = 41U;
inline constexpr std::size_t kOriginalElevatorControlDepartureRectangle = 42U;

// Everything ELVDLOGMAIN reaches outside its own dialog.
class OriginalElevatorControlHost {
 public:
  virtual ~OriginalElevatorControlHost() = default;
  // The live tower (null: none; the item actions then do nothing).
  virtual OriginalTdtDocument* document() = 0;
  virtual const OriginalPartTable& part() = 0;
  virtual const OriginalYenTable& rent_income() = 0;
  // DS:783e, which Simulate forces off and Resume restores.
  virtual bool& build_mode() = 0;
  // show_original_alert owned by the dialog: ALRT/alert_id with
  // STRL/strl_id[strl_index] as its text. Returns the normalized answer.
  virtual int alert(int alert_id, std::uint16_t strl_id,
                    std::uint16_t strl_index) = 0;
  // The modal ELVPOPUP for phase item 23..28 (its DTMP rectangle in the
  // control's client coordinates) showing original_mode; returns the raw
  // EndDialog result (the controller keeps 0..2 and maps anything else to
  // original_mode).
  virtual int popup(const OriginalDtmpRect& phase_rectangle,
                    std::size_t phase_item, std::uint8_t original_mode) = 0;
  // consume_original_person_family_dispatches after a removed service floor.
  virtual void consume_family_dispatches(
      std::span<const OriginalPersonFamilyDispatchResult> dispatches) = 0;
  // 10f0:0318's movement sound (kOriginalElevatorControlMovementSound).
  virtual void play_movement_sound() = 0;
  // request_original_main_surface_pass(rebuild_without_sky, synchronous).
  virtual void rebuild_main_without_sky() = 0;
  // 10a8:0000's transfer-slot reset (g_elevator_transfer_visuals.clear()).
  virtual void clear_transfer_visuals() = 0;
  // g_tower_dirty.
  virtual void mark_document_dirty() = 0;
  // InvalidateRect of the other windows.
  virtual void invalidate_main() = 0;
  virtual void invalidate_map() = 0;
  virtual void invalidate_info() = 0;
  virtual void invalidate_command() = 0;
  // EnableWindow of Main, Map, Info or Command.
  virtual void enable_window(OriginalElevatorControlCloseWindow window,
                             bool enabled) = 0;
  // DS:31a0 = 0 (the published window) before DestroyWindow.
  virtual void unpublish() = 0;
  // SetActiveWindow(Main) (the close plan never asks for it).
  virtual void activate_main() {}
  // 1098:0c68-0c8e: SwapMouseButton probe, then GetAsyncKeyState of the
  // primary button. A phase press opens ELVPOPUP only while it is still
  // physically down. The default (always down) is the Win32 port's
  // behavior before the procedures shared the controller; Win32 keeps it
  // (a known gap to the original, left for the owner), SDL follows the
  // original. Both hosts see the button down on a routed press today.
  virtual bool primary_button_down() { return true; }
  // DestroyWindow(dialog). The controller makes no further call after it.
  virtual void destroy() = 0;
};

// ELVDLOGMAIN's dialog context: the DTMP/400 layout, the DS:b3a6..b3ae
// state and the DS:31b4 pressed item.
class OriginalElevatorControlController {
 public:
  OriginalElevatorControlController(OriginalDtmp dtmp,
                                    OriginalElevatorControlState state);

  // WM_INITDIALOG's first step (nm 3491-3494): EnableWindow(FALSE) on
  // Main, Map, Info and Command, in that order. This is the Win32 port's
  // order, kept for parity; 1098:0691-06b8 disables Main (DS:3258), Info
  // (325c), Command (325a), then Map (325e). Only the order of the
  // WM_ENABLE / WM_CANCELMODE messages differs.
  void disable_windows(OriginalElevatorControlHost& host) const;
  // WM_INITDIALOG after render_original_dtmp shows scroll bar 7: enabled
  // with range [scroll_min, scroll_max] (SetScrollRange without redraw)
  // and the position when the shaft spans more than fourteen floors,
  // otherwise disabled.
  void initialize_scroll_bar(OriginalDialogOps& ops) const;

  // One message: scroll (WM_VSCROLL: SB codes 0..5 move, the rest are
  // ignored), mouse_move (the port's repaint while item 1 or 2 is pressed;
  // the original only sets the arrow cursor),
  // button_down / button_up at client (x, y). Every other kind is ignored
  // (ELVDLOGMAIN has no WM_COMMAND). A modeless dialog has no EndDialog
  // result: closed() reports whether a signal ran host.destroy(), after
  // which every signal is ignored.
  void signal(const OriginalDialogSignal& signal, OriginalDialogOps& ops,
              OriginalElevatorControlHost& host);

  // The DTMP item (1..28) at client (x, y), or -1.
  [[nodiscard]] int hit_item(int x, int y) const;
  // DTMP rectangle one_based (null when out of range).
  [[nodiscard]] const OriginalDtmpRect* rectangle(std::size_t one_based) const;

  [[nodiscard]] const OriginalDtmp& dtmp() const { return dtmp_; }
  [[nodiscard]] const OriginalElevatorControlState& state() const {
    return state_;
  }
  // DS:31b4: -1 while nothing is pressed.
  [[nodiscard]] int pressed_item() const { return pressed_item_; }
  [[nodiscard]] bool closed() const { return closed_; }

 private:
  void button_down(int x, int y, OriginalDialogOps& ops,
                   OriginalElevatorControlHost& host);
  void button_up(int x, int y, OriginalDialogOps& ops,
                 OriginalElevatorControlHost& host);
  void scroll(std::uint16_t code, std::int16_t position,
              OriginalDialogOps& ops);
  std::uint8_t run_popup(std::size_t phase_item,
                         OriginalElevatorControlHost& host) const;
  void grid_action(int x, int y, OriginalDialogOps& ops,
                   OriginalElevatorControlHost& host);
  void resume(OriginalDialogOps& ops, OriginalElevatorControlHost& host);
  void toggle_simulation(OriginalDialogOps& ops,
                         OriginalElevatorControlHost& host);
  void close(OriginalDialogOps& ops, OriginalElevatorControlHost& host);
  void mark_changed(bool world_changed, OriginalDialogOps& ops,
                    OriginalElevatorControlHost& host) const;

  OriginalDtmp dtmp_{};
  OriginalElevatorControlState state_{};
  int pressed_item_{-1};
  bool closed_{};
};

// ELVPOPUP's context (DS:1fb2 hover, the mode it opened with).
class OriginalElevatorPopupController {
 public:
  explicit OriginalElevatorPopupController(std::uint8_t original_mode)
      : original_mode_(original_mode) {}

  // WM_MOUSEMOVE: a band under the point becomes the hover (repainted);
  // a point outside the client ends the popup with the original mode.
  // WM_LBUTTONUP ends it with the band under the point (outside: the
  // original mode); WM_RBUTTONUP with the original mode. The result is the
  // EndDialog value; the host releases its capture and cursor clip first.
  [[nodiscard]] OriginalDialogOutcome signal(
      const OriginalDialogSignal& signal, OriginalDialogOps& ops);

  [[nodiscard]] std::uint8_t original_mode() const { return original_mode_; }
  // -1 until a move selects a band.
  [[nodiscard]] std::int16_t hover_mode() const { return hover_mode_; }

 private:
  std::uint8_t original_mode_{};
  std::int16_t hover_mode_{-1};
};

// The popup's outer origin in the control's client coordinates: the phase
// rectangle's top-left, raised by mode rows of height/3 (nm 3212-3218).
[[nodiscard]] constexpr std::pair<int, int> original_elevator_popup_origin(
    const OriginalDtmpRect& phase_rectangle,
    std::uint8_t original_mode) noexcept {
  return {static_cast<int>(phase_rectangle.left),
          static_cast<int>(phase_rectangle.top) -
              original_mode * (kOriginalElevatorControlPopupHeight / 3)};
}

// The control's client (WM_PAINT, nm 3027-3137, 1098:0068/12e9): BITMAP/400,
// the Simulate (410) and bank / pressed (401, 411) regions, the six phase
// buttons (402..404) and the selected-phase double frame, SHOW (407/408),
// the waiting and departure texts, then the service grid: frame, lines,
// fifteen floor cells, car icons and current-car frames. Without a
// document or a valid state only BITMAP/400 is drawn.
void paint_original_elevator_control(
    OriginalDrawContext& context, const OriginalResources& resources,
    const OriginalElevatorControlController& controller,
    const OriginalTdtDocument* document);

// ELVPOPUP's WM_PAINT: BITMAP/405 and, for a hover band, its BITMAP/406
// highlight.
void paint_original_elevator_popup(OriginalDrawContext& context,
                                   const OriginalResources& resources,
                                   std::int16_t hover_mode);

// The document every Main and Info presentation draws while Simulate runs
// (state isolated): a copy in storage with the 24 saved `used` bytes put
// back, since 10f0:0121/01f9 keep drawing every visible floor while the
// shafts are masked for dispatch. Otherwise (no state, not isolated) the
// live document itself.
[[nodiscard]] const OriginalTdtDocument*
original_elevator_control_presentation_document(
    const OriginalTdtDocument& document,
    const OriginalElevatorControlState* state,
    std::optional<OriginalTdtDocument>& storage);

// The renderer's waiting-isolation view (the selected shaft with its saved
// record) while isolated with a saved record; nullopt otherwise.
[[nodiscard]] std::optional<OriginalElevatorWaitingIsolationView>
original_elevator_control_waiting_view(
    const OriginalElevatorControlState* state);

}  // namespace simtower
