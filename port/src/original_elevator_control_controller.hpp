#pragma once

// Portable ELVDLOGMAIN (1098:0628) and ELVPOPUP (1098:22f8) procedures:
// the message-to-action half of Elevator Control (DIALOG/400) and of its
// phase popup (DIALOG/124), over OriginalDialogOps (the dialog's own
// scroll bar and repaints) and OriginalElevatorControlHost (the document,
// the windows outside the dialog, sounds, alerts and the popup). Both hosts
// run them: elevator_control_dialog_proc / elevator_popup_dialog_proc in
// native_main.cpp over their HWNDs, and the SDL session over its fixed
// in-window panel. The decisions are the Win32 port's as they were before
// the procedures shared them, except where owner decisions D1 / D2 / D3
// moved both hosts to the original's behaviour (elevator_control_controller_
// tests.cpp keeps frozen copies of those branches, each change noted, and
// compares them with the controller);
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
#include <functional>
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
  // 1098:0c68-0c90: SwapMouseButton(0) then SwapMouseButton(prev) probe
  // the swap setting, then GetAsyncKeyState(prev ? VK_RBUTTON :
  // VK_LBUTTON); a result >= 0 (button up) jumps to 0cf2. A phase press
  // opens ELVPOPUP only while the primary button is still physically down.
  // Each host answers from its real button state (owner decision D1).
  virtual bool primary_button_down() = 0;
  // 11e0:0e84: the 48-ms WaveMix pump (OriginalAudioRuntime::pump), which
  // ELVDLOGMAIN calls directly and through 1208:09cf's palette realize
  // (its pump at 09db). Owner decision D3: both hosts run it from their
  // audio runtime. The WM_PAINT pumps (0939, 0975 via 09cf, 097b) stay in
  // each host's paint path. ELVPOPUP's (WM_PAINT 1208:09cf at 24ef, the
  // commit tail's at 25f8) run through OriginalElevatorPopupController,
  // which the hosts give this pump.
  // TEMP (D3 scope, owner sign-off pending): pumps the original runs on
  // this path that neither host routes, all inside shared runtimes:
  // - the DTMP runtime: 1070:0005 at 0193 (called from 1098:06c3) and the
  //   1070:0231 walk at 0350, 035d, 0420, 042d (from 089c), so
  //   the port's WM_INITDIALOG runs 7 of the original's 12 (more on a
  //   font-bank miss);
  // - the 1208:0ba7 font bank's pump at 0c2b on a cache miss (from
  //   1098:088c and the painters 1098:1895, 27bd, 2893);
  // - Simulate / Resume: 10f0:0000 (at 0081) and 10f0:009c (at 00fd) call
  //   1080:05a1, whose 1208:09cf at 05df pumps, and 10f0:0092 calls
  //   1080:0a1e, which pumps at 0a66, 0a8b, 0a9f, 0ab3 and 0afa. The
  //   hosts' rebuild_main_without_sky / invalidate_command passes run
  //   none of them (the Win32 main pass pumps only for simulation_frame,
  //   the SDL one never).
  virtual void audio_pump() = 0;
  // 1208:05e6 into DS:31b0/31b2 (1098:0a57): the 11e0:0e84 pump, then
  // GetTickCount >> 4 (original_runtime_coarse_tick), stored as the last
  // auxiliary pointer tick. The world input's sample has the same name.
  virtual void sample_auxiliary_pointer_tick() = 0;
  // DestroyWindow(dialog). The controller makes no further call after it.
  virtual void destroy() = 0;
};

// ELVDLOGMAIN's dialog context: the DTMP/400 layout, the DS:b3a6..b3ae
// state and the DS:31b4 pressed item.
class OriginalElevatorControlController {
 public:
  OriginalElevatorControlController(OriginalDtmp dtmp,
                                    OriginalElevatorControlState state);

  // WM_INITDIALOG's first step: the 0686 pump, EnableWindow(FALSE) on
  // Main (DS:3258), Info (325c), Command (325a), then Map (325e), the order
  // of 1098:0691-06b8 (owner decision D2; the close re-enables them in the
  // reverse order, 0f2f-0f56), then the 06cb and 06ec pumps (D3; between
  // them only the DTMP load, the class cursor and 1098:0068, none of which
  // reaches the host).
  void disable_windows(OriginalElevatorControlHost& host) const;
  // WM_INITDIALOG's scroll bar 7 setup (1098:07c5-085a), before the DTMP
  // walk (1070:0231 at 089c, render_original_dtmp) places and shows it.
  // A shaft spanning more than fourteen floors: SetScrollRange(scroll_min,
  // scroll_max, no redraw), SetScrollPos(scroll_position, redraw), then
  // EnableWindow(TRUE). Otherwise only EnableWindow(FALSE).
  void initialize_scroll_bar(OriginalDialogOps& ops) const;
  // WM_INITDIALOG's last step, after the DTMP walk: 1098:12e9's pumps
  // (12f8, 1311 via 1208:09cf, 1378, 13b8; D3). Its repaints of DTMP
  // 41/42, the bank, phase, SHOW and grid regions are paint-only (the
  // whole client is painted on the first WM_PAINT).
  void finish_initialization(OriginalElevatorControlHost& host) const;

  // One message: scroll (WM_VSCROLL: SB codes 0..5 move, the rest are
  // ignored), mouse_move (the port's repaint while Close or Simulate is
  // drawn pressed; the original only sets the arrow cursor),
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
  // DS:31b4: the item of the last press on an item (1..28), -1 after a
  // release on an item. A press or release on no item leaves it as it was
  // (1098:0a4e, 0eac), so it can be stale.
  [[nodiscard]] int pressed_item() const { return pressed_item_; }
  // The item (1 or 2) whose pressed look 1098:0a75 / 0ad8 drew into the
  // backing, -1 once a release in the dialog has restored it (0dea-0ea1,
  // whether or not the release hit an item). The painter draws from this,
  // not from the DS:31b4 latch. One value: a second press on 1 or 2
  // without a release in between keeps only the later look.
  [[nodiscard]] int pressed_visual() const { return pressed_visual_; }
  [[nodiscard]] bool closed() const { return closed_; }

 private:
  void button_down(int x, int y, OriginalDialogOps& ops,
                   OriginalElevatorControlHost& host);
  void button_up(int x, int y, OriginalDialogOps& ops,
                 OriginalElevatorControlHost& host);
  void scroll(std::uint16_t code, std::int16_t position,
              OriginalDialogOps& ops, OriginalElevatorControlHost& host);
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
  int pressed_visual_{-1};
  bool closed_{};
};

// ELVPOPUP's context (DS:1fb2 hover, the mode it opened with).
class OriginalElevatorPopupController {
 public:
  // audio_pump is the owner's OriginalElevatorControlHost::audio_pump
  // (11e0:0e84; empty: none).
  explicit OriginalElevatorPopupController(
      std::uint8_t original_mode, std::function<void()> audio_pump = {})
      : original_mode_(original_mode), audio_pump_(std::move(audio_pump)) {}

  // WM_PAINT (paint): 1208:09cf at 1098:24ef after BeginPaint pumps
  // (09db; D3); the host paints. WM_MOUSEMOVE: a band under the point
  // becomes the hover (repainted); a point outside the client ends the
  // popup with the original mode. WM_LBUTTONUP ends it with the band under
  // the point (outside: the original mode); WM_RBUTTONUP with the original
  // mode. Every end runs the commit tail at 258b, whose 1208:09cf at 25f8
  // pumps (D3) before the owner repaint; the result is the EndDialog
  // value, and the host then releases its cursor clip and capture.
  [[nodiscard]] OriginalDialogOutcome signal(
      const OriginalDialogSignal& signal, OriginalDialogOps& ops);

  [[nodiscard]] std::uint8_t original_mode() const { return original_mode_; }
  // -1 until a move selects a band.
  [[nodiscard]] std::int16_t hover_mode() const { return hover_mode_; }

 private:
  std::uint8_t original_mode_{};
  std::function<void()> audio_pump_{};
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
