#pragma once

// Portable world input shared by the Win32 host (native_main.cpp) and the SDL
// session (sdl/sdl_session.cpp): MAINWNDPROC's mouse branches 1158:028c-0314
// and the 1058:0000 world dispatch (Bulldozer, Finger, Magnifier and the
// 11f8:07d8 construction transaction), CMDBTNWNDPROC 1050:0000 with its
// 1050:05a7 selector, and MAPWNDPROC's 1168:0000 pointer branches.
//
// One state struct replaces the native_main.cpp globals (each field names
// the DS word it mirrors). Every Win32 call those functions made inline goes
// through OriginalWorldInputHost at the same point, following the
// OriginalIdlePassHost pattern, so document mutation, RNG use and the order
// of sounds, status text, dialogs and repaints stay the original order.

#include "original_command_palette.hpp"
#include "original_construction.hpp"
#include "original_host_services.hpp"
#include "original_information.hpp"
#include "original_map.hpp"
#include "original_people.hpp"
#include "original_resources.hpp"
#include "original_simulation.hpp"
#include "original_tables.hpp"
#include "original_tdt.hpp"
#include "original_ui.hpp"
#include "original_world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace simtower {

// Win16 MK_* key-state bits carried by every mouse message's wParam.
inline constexpr std::uint16_t kOriginalKeyLButton = 0x0001U;
inline constexpr std::uint16_t kOriginalKeyShift = 0x0004U;
inline constexpr std::uint16_t kOriginalKeyControl = 0x0008U;

// A mouse message's decoded LPARAM (signed 16-bit client coordinates) and
// wParam key state. Main messages use Main client coordinates.
struct OriginalWorldInputPoint {
  int x{};
  int y{};
  std::uint16_t keys{};

  friend bool operator==(const OriginalWorldInputPoint&,
                         const OriginalWorldInputPoint&) = default;
};

// Floor (type 0) and Lobby (type 24) captured spans.
struct OriginalSpanDragState {
  bool active{};
  std::int16_t floor{};
  std::int32_t anchor_left{};
  std::int32_t anchor_right{};
};

enum class OriginalParkingDragKind : std::uint8_t {
  none,
  parking,
  ramp,
};

struct OriginalParkingDragState {
  OriginalParkingDragKind kind{OriginalParkingDragKind::none};
  std::int16_t press_floor{};
  std::int32_t snapped_left{};
  std::int32_t snapped_right{};
  OriginalParkingDragRunState parking{};
  OriginalParkingRampDragRunState ramp{};
};

// Mirrors of 11f8's shared 24cc successful-step counter and 24ce down-time
// balance snapshot. They span Floor, Lobby, Parking, and Ramp and are
// consumed only by the common button-up completion boundary.
struct OriginalConstructionDragCompletionState {
  bool active{};
  std::uint16_t type{};
  std::uint16_t successful_steps{};
  std::int32_t balance_at_press{};
  bool priority_sound_latch_armed{};  // DS:24ca
};

enum class OriginalElevatorShaftDragDirection : std::uint8_t {
  none,
  upper,
  lower,
};

struct OriginalElevatorShaftDragState {
  bool capture_active{};  // DS:02a6, set by every 10a0:0544 finger press
  OriginalElevatorShaftDragDirection direction{
      OriginalElevatorShaftDragDirection::none};
  std::size_t elevator_index{};
};

// The last Main client point sampled by 1258:0256 (or a Main mouse move).
struct OriginalMainPointerSample {
  bool inside{};
  int client_x{};
  int client_y{};
};

// Which surface owns the mouse capture. Only Main (1058:0000) and Map
// (1058:01d6) ever capture; CMDBTNWNDPROC never calls SetCapture itself.
enum class OriginalInputSurface : std::uint8_t {
  main,
  map,
};

struct OriginalWorldInputState {
  // Command state (10d0:086c resets it before New and Open).
  bool build_mode_enabled{true};        // DS:783e
  bool command_toggle_pressed{};        // 1080:07a6 BITMAP/601 or 603 frame
  std::uint16_t command_mode{3U};       // DS:783c
  std::uint16_t selected_build_type{24U};  // mandatory initial Lobby
  std::uint16_t map_mode{};             // DS:7840
  // Native resource-handle equivalent of DS:7960. 10d0:086c resets the
  // document rating before Open I/O but does not replace this table until
  // the successful 10d0:0ac2 -> 1140:010d reconstruction boundary.
  std::uint16_t active_command_rating{1U};
  // Mutable TABL copies: the original writes the chosen group index back
  // into the active TABL (1140:0394).
  std::array<OriginalCommandRatingState, 6> command_ratings{};
  std::uint16_t office_variant{};       // DS:7954, cycles modulo six
  // Build type is the raw original type byte (currently through 0x2c). Keep
  // selectors addressable by that byte even for one-variant composite types.
  std::array<std::uint16_t, 256> facility_variants{};

  // Main pointer.
  bool pointer_armed{};      // DS:0242
  bool paint_active{};       // DS:0244
  bool control_modifier{};   // DS:3218, last mouse wParam & 8
  bool shift_modifier{};     // DS:321a, last mouse wParam & 4
  // Models GetCapture() == Main for hosts without a window manager capture.
  bool capture_held{};
  OriginalSpanDragState lobby_drag{};
  OriginalSpanDragState floor_drag{};
  OriginalParkingDragState parking_drag{};
  OriginalConstructionDragCompletionState completion{};
  std::optional<OriginalWorldInputPoint> retained_press{};
  std::optional<OriginalLobbyPlacement> retained_placement{};
  OriginalElevatorShaftDragState shaft_drag{};

  // Construction preview.
  OriginalMainPointerSample pointer{};
  // Mirror of DS:77ac, the last construction-outline rectangle derived by
  // 11f8:3c13. 1090:03ab compares this rectangle, not the raw mouse point,
  // to decide whether its preview-only pass has any pixels to present.
  std::optional<OriginalConstructionPreviewRect> preview_rect{};
  // Mirror of DS:025c, the scratch-backing latch shared by 11f8:3b94/3c13.
  OriginalConstructionPreviewScratchState preview_scratch{};

  // Map.
  bool map_drag_active{};    // DS:3216 for MAPWNDPROC
  bool map_pointer_down{};   // DS:0248
  // Mirror of DS:7796, the last focus rectangle XOR-presented on Map.
  std::optional<OriginalMapRect> map_focus{};
};

// The edit/command part of 10d0:086c (reset_original_command_state minus
// the Info status and Find marker, which hosts reset themselves). The
// DS:31c4 startup bootstrap keeps construction disabled.
void reset_original_world_input_state(OriginalWorldInputState& state,
                                      bool startup_suppressed);

// One CMDBTNSUBWNDPROC (1050:05a7) message's effect on the host modal.
struct OriginalCommandSelectorStep {
  bool redraw{};              // InvalidateRect + UpdateWindow
  bool end{};                 // EndDialog
  bool accepted{};            // EndDialog result 1 (otherwise 0)
  bool unclip_and_release{};  // ClipCursor(NULL), then ReleaseCapture
  bool handled{true};         // the dialog procedure's return value

  friend bool operator==(const OriginalCommandSelectorStep&,
                         const OriginalCommandSelectorStep&) = default;
};

// Per-message state machine of the grouped facility selector. The Win32
// dialog procedure forwards its messages here; the SDL host drives the same
// object from its own events. Rows are 32 pixels tall in a 32-pixel-wide
// client; a row is std::nullopt when the point lies outside that client.
class OriginalCommandSelectorSession {
 public:
  OriginalCommandSelectorSession(OriginalCommandGroup group,
                                 int anchor_left,
                                 int anchor_top);

  [[nodiscard]] const OriginalCommandGroup& group() const { return group_; }
  [[nodiscard]] std::uint16_t choice() const { return choice_; }
  // WM_INITDIALOG passes the encoded one-based choice index, every later
  // in-bounds move TABM(icon)+1 (an original quirk kept by the renderer).
  [[nodiscard]] std::uint16_t highlight() const { return highlight_; }
  [[nodiscard]] int anchor_left() const { return anchor_left_; }
  [[nodiscard]] int anchor_top() const { return anchor_top_; }
  [[nodiscard]] int width() const { return 32; }
  [[nodiscard]] int height() const {
    return static_cast<int>(group_.catalog_icons.size()) * 32;
  }
  // 1050:05d6 placement: the selected row aligned with the clicked cell,
  // clamped to the desktop.
  [[nodiscard]] int top(int desktop_bottom) const;
  // PtInRect over the client, then y / 32.
  [[nodiscard]] static std::optional<int> row_at(int x,
                                                 int y,
                                                 int client_width,
                                                 int client_height);

  OriginalCommandSelectorStep on_move(std::optional<int> row);
  OriginalCommandSelectorStep on_lbutton_down(std::optional<int> row);
  OriginalCommandSelectorStep on_lbutton_up(std::optional<int> row);
  OriginalCommandSelectorStep on_rbutton_down();

 private:
  OriginalCommandGroup group_;
  std::uint16_t choice_{};
  std::uint16_t highlight_{};
  int anchor_left_{};
  int anchor_top_{};
};

class OriginalWorldInputHost {
 public:
  virtual ~OriginalWorldInputHost() = default;

  // The clock, DS:b3ae, the Info notification line and the Command
  // invalidation, shared with the idle pass: hosts return their
  // OriginalIdlePassHost (or another single implementation) here.
  virtual OriginalHostServices& services() = 0;
  std::uint32_t now_ms() { return services().now_ms(); }  // GetTickCount
  bool elevator_isolation_active() {                      // DS:b3ae
    return services().elevator_isolation_active();
  }
  void notification_status(std::uint16_t code) {
    services().notification_status(code);
  }
  void invalidate_command() { services().invalidate_command(); }

  // Game-relevant host state, queried at the original point.
  virtual bool beep_only() = 0;                     // DS:de28
  virtual bool modal_input_locked() = 0;            // DS:24b8
  virtual bool find_exit_latch_set() = 0;           // DS:77c0 phase != 0
  virtual void reset_find_marker() = 0;             // DS:77b4..77c0
  virtual bool primary_button_down() = 0;  // GetAsyncKeyState, SM_SWAPBUTTON

  // Capture and cursor.
  virtual void set_capture(OriginalInputSurface surface) = 0;
  virtual void release_capture() = 0;
  // SetCursor(resolve_original_cursor(id)): 0, 2, 4, 1000, 1002..1005.
  virtual void set_cursor(std::uint16_t /*cursor_id*/) {}
  // MAINWNDPROC 1158:0314: GetCursorPos and the 1258:0505 resolver.
  virtual void refresh_cursor() {}

  // Main view (the scrollbar positions and GetClientRect).
  virtual OriginalWorldPoint main_view() = 0;
  // Null when GetClientRect fails.
  virtual std::optional<OriginalMapRect> main_client() = 0;
  // set_original_view: 1080:0000/0054 commit with its camera refresh.
  virtual void set_main_view(int x, int y) = 0;
  // SetScrollInfo on both axes only (1058:0284's direct commit).
  virtual void set_main_scroll_position(int x, int y) = 0;

  // Presentation.
  virtual void mark_document_dirty() = 0;
  virtual void invalidate_main() = 0;          // backing dirty + repaint
  virtual void mark_main_backing_dirty() = 0;  // backing dirty only
  virtual void invalidate_map() = 0;
  virtual void present_info_status() = 0;      // 1118:0000 synchronous
  virtual void present_command_now() = 0;      // InvalidateRect+UpdateWindow
  virtual void resize_command(int outer_height) = 0;  // 1140:010d MoveWindow
  virtual void request_main_pass(OriginalMainSurfacePass pass,
                                 bool synchronous) = 0;
  // 1058:0284 holds one Map DC from just after its first pump until after
  // the synchronous Main present: begin_map_focus is GetDC, end_map_focus
  // ReleaseDC, and xor_map_focus (DrawFocusRect, an XOR: drawing twice
  // erases) only runs between them.
  virtual void begin_map_focus() {}
  virtual void xor_map_focus(const OriginalMapRect& /*focus*/) {}
  virtual void end_map_focus() {}
  virtual void hide_palette(OriginalAuxiliaryWindow kind) = 0;  // close box

  // Status text and sound.
  virtual void construction_status(std::uint16_t code) = 0;
  virtual void command_status(std::uint16_t code) = 0;
  virtual void play_sound(std::int32_t resource,
                          std::uint16_t repeat,
                          std::uint16_t priority) = 0;  // 11c8:0167
  virtual void play_reserved_if_idle(std::int32_t resource,
                                     std::uint16_t repeat,
                                     std::uint32_t now_ms) = 0;  // 11c8:0100
  virtual void play_event_sounds(
      std::span<const OriginalEventSoundRequest> requests) = 0;
  virtual void beep() = 0;                     // MessageBeep(-1)
  virtual void audio_pump() = 0;               // 11e0:0e84
  virtual void audio_transition(OriginalIdleAudioTransition transition) = 0;
  // 1208:05e6 sampled into DS:31b0/31b2 (with its 11e0:0e84 pump).
  virtual void sample_auxiliary_pointer_tick() = 0;

  // Modal dialogs: synchronous requests that return the dialog result.
  virtual int alert(int resource_id, std::string_view text) = 0;
  virtual int event_dialog(const OriginalEventDialogRequest& request) = 0;
  virtual void open_elevator_control(std::size_t elevator_index,
                                     std::int16_t pointer_x,
                                     std::int16_t pointer_y) = 0;
  virtual void transport_information(
      const OriginalMagnifierTarget& target) = 0;
  virtual void facility_information(std::int16_t floor,
                                    std::size_t tenant_index) = 0;
  // A Magnifier click on a Lobby (1100:0468): Finance, 1060:0083.
  virtual void finance_dialog() = 0;
  virtual void person_information(std::size_t person_index) = 0;
  // Command outer origin (GetWindowRect) for the selector anchor.
  virtual OriginalWorldPoint command_outer_origin() = 0;
  // Runs the 1050:05a7 modal; true when it ended with result 1.
  virtual bool run_command_selector(
      OriginalCommandSelectorSession& session) = 0;

  // Process-only person-family callback work (0883).
  virtual void consume_person_family_dispatches(
      std::span<const OriginalPersonFamilyDispatchResult> dispatches) = 0;
};

struct OriginalWorldInputContext {
  // Null when no tower is loaded. Captured once per message, where the
  // Win32 code re-read g_tower_document after each nested modal. Hosts must
  // therefore not destroy the document (std::optional::reset) while a world
  // input handler runs, even from a nested modal; assigning a new tower
  // keeps the address. The Win32 host resets it only at process teardown.
  OriginalTdtDocument* document;
  const OriginalResources& resources;
  const OriginalPartTable& part;
  const OriginalYenTable& construction_costs;
  const OriginalYenTable& rent_income;
  OriginalWorldInputState& state;
};

// ---- Main (MAINWNDPROC 1158:028c-0314 -> 1058:0000) ----

// WM_LBUTTONDOWN, or WM_LBUTTONDBLCLK when double_click is set.
void original_main_pointer_down(const OriginalWorldInputContext& ctx,
                                OriginalWorldInputHost& host,
                                OriginalWorldInputPoint point,
                                bool double_click);
// WM_MOUSEMOVE, including the 1258:0505 cursor refresh.
void original_main_pointer_move(const OriginalWorldInputContext& ctx,
                                OriginalWorldInputHost& host,
                                OriginalWorldInputPoint point);
// WM_LBUTTONUP.
void original_main_pointer_up(const OriginalWorldInputContext& ctx,
                              OriginalWorldInputHost& host,
                              OriginalWorldInputPoint point);
// Cancels every captured construction or Finger drag and releases the
// capture (also run by 10e0:0cea when Find focuses a target).
void end_original_lobby_drag(const OriginalWorldInputContext& ctx,
                             OriginalWorldInputHost& host);
// The world-mutation presentation fan-out with no balance change (also
// used by the hidden Bomb and Fire menu commands).
void mark_original_world_interaction_changed(OriginalWorldInputHost& host);

// ---- Command and Map state transitions ----

// 1058:0352 construction toggle (Command build button, Find exit).
void apply_original_construction_toggle(const OriginalWorldInputContext& ctx,
                                        OriginalWorldInputHost& host);
// 11d0:0000 map overlay selection, coupled to edit mode two.
void select_original_map_mode(const OriginalWorldInputContext& ctx,
                              OriginalWorldInputHost& host,
                              std::uint16_t mode);

// ---- Construction preview (1090:03ab, 11f8:3b94/3c13) ----

// 11f8:0000/3da4 outline for the sampled Main pointer: none unless the
// pointer was sampled, construction is enabled, and a facility tool (mode
// three or above) is active.
[[nodiscard]] std::optional<OriginalConstructionPreviewRect>
original_world_input_preview_rect(const OriginalWorldInputState& state,
                                  OriginalWorldPoint view);

// 1090:03bf-0427, the preview-only pass. It advances the DS:025c scratch
// latch and returns what the host performs, in this order: run
// checkpoints_before audio pumps; clear DS:77ac when clear_previous; then
// either rebuild and present Main directly (present_main_direct, whose
// rebuild stores the new rectangle) or store new_rect; finally run
// checkpoints_after pumps (11f8:3c99/3cd2/3d19 surround the scratch capture
// and outline draw; all three still run when EQUALRECT suppresses the
// presentation).
struct OriginalPreviewOnlyPassPlan {
  std::uint8_t checkpoints_before{};
  bool clear_previous{};
  std::optional<OriginalConstructionPreviewRect> new_rect{};
  bool present_main_direct{};
  std::uint8_t checkpoints_after{};

  friend bool operator==(const OriginalPreviewOnlyPassPlan&,
                         const OriginalPreviewOnlyPassPlan&) = default;
};

OriginalPreviewOnlyPassPlan original_preview_only_pass_plan(
    OriginalWorldInputState& state,
    bool isolation_active,
    const std::optional<OriginalConstructionPreviewRect>& previous_rect,
    const std::optional<OriginalConstructionPreviewRect>& current_rect);

// ---- Command (CMDBTNWNDPROC 1050:0000) ----

// The cached mutable TABL copy for a rating (1140:010d).
OriginalCommandRatingState& active_original_command_rating(
    const OriginalWorldInputContext& ctx,
    std::uint16_t rating);
// 1140:010d: replace the rating TABL, resize Command, and apply the
// refresh plan (argument one forces edit mode two).
void refresh_original_rating_command(const OriginalWorldInputContext& ctx,
                                     OriginalWorldInputHost& host,
                                     std::uint16_t rating,
                                     std::uint16_t argument,
                                     bool command_composition_allowed = true);
// WM_LBUTTONDOWN / WM_LBUTTONUP in Command frame coordinates (the 8-pixel
// palette frame row included).
void original_command_pointer_down(const OriginalWorldInputContext& ctx,
                                   OriginalWorldInputHost& host,
                                   int frame_x,
                                   int frame_y);
void original_command_pointer_up(const OriginalWorldInputContext& ctx,
                                 OriginalWorldInputHost& host,
                                 int frame_x,
                                 int frame_y);

// ---- Info (INFOWNDPROC 1120:0000) ----

// WM_LBUTTONDOWN in Info frame coordinates: the close box hides the
// palette. Returns false (DefWindowProc) for every other point.
bool original_info_pointer_down(const OriginalWorldInputContext& ctx,
                                OriginalWorldInputHost& host,
                                int frame_x,
                                int frame_y);

// ---- Map (MAPWNDPROC 1168:0000) ----

// Map frame coordinates (frame row included). Move and up return whether
// MAPWNDPROC consumes the message (otherwise DefWindowProc runs).
void original_map_pointer_down(const OriginalWorldInputContext& ctx,
                               OriginalWorldInputHost& host,
                               int frame_x,
                               int frame_y);
bool original_map_pointer_move(const OriginalWorldInputContext& ctx,
                               OriginalWorldInputHost& host,
                               int frame_x,
                               int frame_y,
                               std::uint16_t keys);
bool original_map_pointer_up(const OriginalWorldInputContext& ctx,
                             OriginalWorldInputHost& host);

}  // namespace simtower
