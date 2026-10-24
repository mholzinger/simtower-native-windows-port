#pragma once

// Platform-free game session for the SDL3 frontend. It owns the state that
// native_main.cpp keeps in Win32 globals (resources, tables, document, clock,
// palette, sky decorations, transfer visuals) plus a plain viewport in place
// of the Main window's scrollbars, and drives the shared idle pass
// (original_idle_pass.hpp: 1258:023a -> 1200:0196 -> 1090:03ab(1)) as its
// host. The 1258:0195-023a prelude and 1258:0285-02d9 epilogue are host-owned
// (see run_idle_pass). Host requests (dialogs, sounds, focus, Info/Map/Command
// repaints) are logged instead of acted on; sounds are also played through
// the shared OriginalAudioRuntime when the host supplies an audio device.
// Nothing here includes SDL, so the session can also drive headless runs
// with a virtual clock.

#include "sdl_dialog.hpp"
#include "sdl_dialog_descriptors.hpp"
#include "sdl_digest.hpp"
#include "sdl_layout.hpp"

#include "original_audio.hpp"
#include "original_audio_device.hpp"
#include "original_command_palette.hpp"
#include "original_idle_pass.hpp"
#include "original_info.hpp"
#include "original_main_command.hpp"
#include "original_menu.hpp"
#include "original_people.hpp"
#include "original_resources.hpp"
#include "original_simulation.hpp"
#include "original_tables.hpp"
#include "original_tdt.hpp"
#include "original_tower_file.hpp"
#include "original_ui.hpp"
#include "original_world.hpp"
#include "original_world_input.hpp"
#include "test_scenario.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace simtower::sdl {

// Replaces the scrollbar positions (current_original_view) and the
// Main client size (GetClientRect). x/y are the world origin of the view.
struct Viewport {
  int x{};
  int y{};
  int width{};
  int height{};
};

// Host flags with the Win32 defaults (native_main.cpp:301-325).
struct SessionFlags {
  bool people_animation_enabled{true};  // 1128:0545, DS:de30
  bool effects_animation_enabled{true}; // 1128:054b, DS:de32
  bool fast_mode_enabled{true};         // 1128:0551, DS:de34
  bool world_control_modifier{false};   // DS:3218
  // The master sound switch (DS:02a8 after 1128:0443). It is passed to
  // select_original_ambient_sound (11c8:03ab), as the Win32 host passes
  // g_audio->sound_enabled(), so turning it on changes RNG consumption. The
  // default false matches a Win32 run without a sound device (no RNG draw)
  // and is what the regression oracle runs with. The audio runtime's master
  // switch is set from this flag, never the other way round, except that a
  // failed device initialization turns it off as in WinMain (11c8:006b).
  bool sound_enabled{false};
  // Log every sound request (elevator movement sounds fire every frame).
  bool log_sounds{false};
  // Print every digest-log entry to stdout ("request <kind> <fields>"), so
  // two runs whose digests differ can be diffed line by line.
  bool trace_requests{false};
};

// One mouse event in window logical coordinates (the Win16 desktop
// coordinates of sdl_layout.hpp). keys carries MK_SHIFT / MK_CONTROL only;
// the session adds MK_LBUTTON from its own tracked button state.
enum class InputKind : std::uint8_t {
  move,
  down,
  up,
  double_click,
  right_down,
};

struct InputEvent {
  InputKind kind{InputKind::move};
  int x{};
  int y{};
  std::uint16_t keys{};
};

// One input for an open dialog (Session::dialogs().back()).
struct DialogInput {
  enum class Kind : std::uint8_t {
    pointer,       // a mouse event in window coordinates
    key,           // a key press
    timer,         // fire the dialog's timer now (scripted WM_TIMER)
    wait_elapsed,  // time passed (elapsed_ms); due timers fire
  };
  Kind kind{Kind::pointer};
  InputEvent pointer{};
  DialogKey key{DialogKey::other};
  // A character key's lower-case letter (DialogKey::other), else 0.
  char character{};
  // Dialog-clock milliseconds that passed before this input (the window's
  // wait, or a script's "dialog-wait MS"). The dialog timers run on this
  // clock only, never on the session clock.
  std::uint32_t elapsed_ms{};
};

// Supplies events while a modal loop owns input (the 1050:05a7 selector).
// Win32 modals run their own message loop, so no idle pass runs until the
// modal ends. The window host waits for SDL events; the script player reads
// the next script lines.
class InputSource {
 public:
  virtual ~InputSource() = default;
  // std::nullopt ends the modal as a cancel.
  virtual std::optional<InputEvent> next_modal_event() = 0;
  // The modal's overlay changed (the window host re-presents it).
  virtual void modal_changed() {}
  // The result of an Open / Save As sheet that is opening now, when this
  // source supplies one (the script player reads a following "pick-file"
  // line); the outer nullopt means it has none.
  virtual std::optional<std::optional<std::filesystem::path>>
  next_file_pick() {
    return std::nullopt;
  }
  // Whether this source answers the dialog that is about to open (its
  // ModalDialog::label() names it). False keeps the session's logged
  // stand-in, exactly as before the dialog runtime existed, so runs without
  // dialog input keep their digests by construction. The window host
  // answers every dialog; the script player only one that the next line
  // names with "expect-dialog" (it throws ScriptFailure for a mismatch).
  virtual bool drives_dialog(const ModalDialog& /*dialog*/) { return false; }
  // The next input for the open, driven dialog. wait_ms is the dialog-clock
  // time until its timer is due (nullopt: no live timer). std::nullopt
  // ends the dialog with its default answer. The window host waits for SDL
  // events; the script player reads following "dialog-*" lines and returns
  // nullopt at any other line.
  virtual std::optional<DialogInput> next_dialog_input(
      std::optional<std::uint32_t> /*wait_ms*/) {
    return std::nullopt;
  }
};

// The native Open and Save As pickers (the window host's
// SDL_ShowOpenFileDialog / SDL_ShowSaveFileDialog). Headless runs install
// none: a scripted "pick-file" result is used instead, and without one the
// sheet is cancelled, so no dialog ever opens there.
class FilePicker {
 public:
  virtual ~FilePicker() = default;
  // nullopt is a cancel.
  virtual std::optional<std::filesystem::path> pick(bool save) = 0;
};

// What the menu bar shows: the open drop-down (bar index, -1 for none), its
// highlighted row, and an open cascade (Options > Animation / Sound) with
// its highlighted row. Presentation only; the window's menu loop and the
// "menu-show" script command set it.
struct MenuView {
  int bar{-1};
  int item{-1};
  bool cascade_open{};
  int cascade_item{-1};
  // The bar title is highlighted without a drop-down (keyboard Alt/F10).
  bool title_only{};

  friend bool operator==(const MenuView&, const MenuView&) = default;
};

// Counts of host requests that were logged instead of performed.
struct HostRequestCounts {
  std::size_t dialogs{};
  std::size_t sounds{};
  std::size_t focus{};
  std::size_t status{};
};

// Regression oracle (--state-digest). Each component is an FNV-1a 64 hash;
// `combined` hashes the other four. See Session::state_digest.
struct SessionDigest {
  std::uint64_t tower{};      // serialize_original_tdt of the live document
  std::uint64_t process{};    // document fields absent from the TDT stream
  std::uint64_t host{};       // host-owned simulation/presentation state
  std::uint64_t requests{};   // ordered log of host requests, per pass
  std::uint64_t combined{};
};

class Session : private OriginalIdlePassHost {
  class WorldInputHost;
  friend class WorldInputHost;
  class MainCommandHost;
  friend class MainCommandHost;
  class TowerFileHost;
  friend class TowerFileHost;

 public:
  // clock_ms replaces GetTickCount: every Win32 read maps to one call.
  // With flags.sound_enabled and a non-null audio_device (which must
  // outlive the session), sound requests also play through an
  // OriginalAudioRuntime on that device; otherwise they are only logged.
  Session(std::vector<std::byte> pack,
          std::function<std::uint32_t()> clock_ms,
          SessionFlags flags = {},
          OriginalAudioDevice* audio_device = nullptr);
  // The audio runtime refers to resources_; the session never moves.
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // Main window activation and minimized state, read by the
  // 1258:0195-01c3 audio prelude of every idle pass. Headless runs keep
  // the default: active and not minimized.
  void set_window_activation(bool active, bool iconic) {
    window_active_ = active;
    window_iconic_ = iconic;
  }
  // 1128:00ce-00da: WAVE/20000 once at priority 1, before New/Open. Only
  // the interactive window calls it; headless runs start with the first
  // idle-pass request.
  void play_startup_sound();
  [[nodiscard]] const OriginalResources& resources() const {
    return resources_;
  }
  // Null when sound is off or no device was supplied.
  [[nodiscard]] const OriginalAudioRuntime* audio() const {
    return audio_.get();
  }

  // Win32 New path: begin_original_new_tower. The current viewport
  // origin is kept, as 10d0:086c leaves DS:b3f0/b3f2 untouched.
  void new_tower();
  // Win32 Open path: load_original_tower_from_path. Throws
  // OriginalTdtError / OriginalTdtFileError on failure. Used for --open
  // (1128:00e5's command-line path); the File menu runs the shared
  // original_tower_file.hpp transactions instead.
  void open_tower(const std::filesystem::path& path);

  // ---- Menu and WM_COMMAND (original_menu.hpp, original_main_command.hpp)
  // TOWER_MENU as Main's HMENU would hold it now: parsed from the pack,
  // then every CheckMenuItem/EnableMenuItem/DeleteMenu site applied.
  [[nodiscard]] const OriginalMenuItems& menu() const { return menu_; }
  // A menu pick: USER sends WM_COMMAND only for an enabled item of the live
  // menu, so anything else is logged and dropped. Returns whether the
  // command was dispatched.
  bool choose_menu_command(std::uint16_t command);
  // TranslateAccelerator against TOWER_MENU's table (F1 -> 40021) for a
  // Win16 virtual key; a grayed menu item suppresses the command. Returns
  // whether a command was dispatched.
  bool accelerator(std::uint16_t virtual_key);
  // The Open / Save As sheets: a queued scripted result first, then the
  // input source's (a script's next "pick-file" line), then the installed
  // picker, else a cancel.
  void set_file_picker(FilePicker* picker) { file_picker_ = picker; }
  void queue_picked_file(std::optional<std::filesystem::path> path) {
    picked_files_.push_back(std::move(path));
  }
  [[nodiscard]] std::size_t queued_picked_files() const {
    return picked_files_.size();
  }
  // File > Exit destroyed Main (original_main_shutdown_plan).
  [[nodiscard]] bool quit_requested() const { return quit_requested_; }
  // "SimTower - <title>" (original_tower_window_title), UTF-8.
  [[nodiscard]] std::string window_title() const;
  [[nodiscard]] const MenuView& menu_view() const { return menu_view_; }
  void set_menu_view(const MenuView& view) { menu_view_ = view; }

  // One empty-queue idle pass (run_original_idle_simulation).
  // Returns true when the main raster changed and should be presented.
  bool run_idle_pass();

  // Milliseconds until the next idle pass can do work: the fast-mode frame
  // adapter or the six-tick gate, and the 15-tick effect palette gate.
  [[nodiscard]] std::uint32_t milliseconds_until_due() const;

  // Position-only view restore (restore_original_derived_view_position):
  // clamp the origin without a rebuild. Used before New/Open.
  void restore_view(int x, int y) { set_viewport_origin(x, y); }
  // Scrollbar path (scroll_original): clamp the requested origin and
  // run 1058:05f8's synchronous 1080:0a1e(1) rebuild.
  void scroll_to(int x, int y);
  // One line (codes 0/1) or page (codes 2/3) step through the exact
  // original_main_scroll_request_position helper (1158:015b/01e7).
  void scroll_by_code(bool horizontal, std::uint16_t scroll_code);
  // WM_SIZE path (main_window_proc). The first call stands in for window
  // creation (1128:08d6): it installs the initial view and only dirties the
  // backing. Every later call is a live restore/maximize: it clamps the
  // saved origin to the new ranges and runs the synchronous 1080:0a1e(1)
  // rebuild, which advances the sky decorations and visible facility people.
  void resize(int width, int height);
  // Processes a pending repaint (1158:0a3c) without stepping the scheduler,
  // the palette or any presentation state.
  void present();

  [[nodiscard]] const OriginalWorldRaster& main_raster() const {
    return main_raster_;
  }
  // The Info client (kOriginalInfoWidth x 49) as the Win32 Info window last
  // painted it: rows 8.. hold draw_original_info's output, rows 0..7 stay
  // white for the palette frame, which compose_window draws on top.
  [[nodiscard]] const OriginalWorldRaster& info_raster() const {
    return info_raster_;
  }

  // ---- Input (original_world_input.hpp) ----
  // Routes one mouse event: the capture owner first, otherwise the top panel
  // at the point (Command, Map, Info, then Main), each in its own client
  // coordinates, into the shared module. A palette press on its frame drag
  // row or border is nonclient, as Win32's WM_NCHITTEST makes it: it only
  // activates the panel, and its moves and release reach no module.
  void pointer_event(const InputEvent& event);
  // Source for modal loops; null cancels every modal immediately.
  void set_input_source(InputSource* source) { input_source_ = source; }
  // Answer for the next alert (1 = OK/Yes, 2 = Cancel/No); then back to 1.
  void set_alert_answer(int answer) { alert_answer_ = answer; }
  // Test shortcut (scripts' "set-balance"): replaces the live tower's cash
  // balance (header.balance), e.g. to reach the short-of-funds paths of the
  // event dialogs. Nothing without a tower; the Info panel shows the new
  // value on its next repaint.
  void set_balance_for_test(std::int32_t balance) {
    if (document_) document_->header.balance = balance;
  }
  // Test shortcut (scripts' "tool"): selects an edit mode (0 Bulldozer,
  // 1 Finger, 2 Magnifier) or a construction type. A type selects its
  // catalog cell (index + 3) when the active rating lists it, else mode 3.
  void select_tool(std::optional<std::uint16_t> edit_mode,
                   std::optional<std::uint16_t> build_type);
  [[nodiscard]] const OriginalWorldInputState& world_input() const {
    return world_input_;
  }
  [[nodiscard]] const WindowLayout& layout() const { return layout_; }
  [[nodiscard]] bool panel_visible(Panel panel) const;
  [[nodiscard]] Panel active_panel() const { return active_panel_; }
  // The open dialogs, bottom to top (a nested alert is above its event
  // dialog). Empty unless an input source drives one.
  [[nodiscard]] std::vector<const ModalDialog*> dialogs() const {
    return {dialogs_.begin(), dialogs_.end()};
  }
  // The running selector modal, or null.
  [[nodiscard]] const OriginalCommandSelectorSession* active_selector()
      const {
    return selector_;
  }
  [[nodiscard]] const OriginalPaletteRuntime& palette_runtime() const {
    return palette_runtime_;
  }
  [[nodiscard]] const OriginalCommandRatingState& active_rating_state() const;
  // Cursor id last set through the module (resolve_original_cursor ids).
  [[nodiscard]] std::uint16_t cursor_id() const { return cursor_id_; }
  // FNV-1a over the input state, the sampled pointer and the preview, for
  // the scripted digest's "input" field.
  [[nodiscard]] std::uint64_t input_digest() const;
  [[nodiscard]] const Viewport& viewport() const { return viewport_; }
  [[nodiscard]] const OriginalTdtDocument* document() const {
    return document_ ? &*document_ : nullptr;
  }
  [[nodiscard]] const HostRequestCounts& host_requests() const {
    return host_requests_;
  }

  // Test-only (--direct-build): one constructor call plus the common
  // success tail, as test_scenario.cpp's Builder runs it. Returns whether
  // the constructor succeeded.
  bool direct_build(const DirectBuild& build);
  // Test-only: stages a deterministic tower (test_scenario.hpp) on the live
  // document after New, through the portable construction functions only.
  ScenarioSummary apply_scenario(std::string_view name);
  // Test-only: runs a hidden menu event command between idle passes
  // through the shared WM_COMMAND dispatcher: "bomb" is 9000 (10c8:006e),
  // "fire" is 9002 (10e8:0029). Returns whether the event started.
  bool trigger_event(std::string_view name);
  // Hashes every piece of state the idle pass can mutate. Read-only.
  [[nodiscard]] SessionDigest state_digest() const;
  // Diagnostic tallies of logged host requests by kind and source (not part
  // of the digest; the ordered request log is).
  [[nodiscard]] const std::map<std::string, std::size_t>& request_tally()
      const {
    return request_tally_;
  }

 private:
  // 1208:05e6 on a fresh clock read. Not the coarse_tick(now_ms) host hook,
  // which converts a time the shared pass already read.
  // It also runs the audio pump, so it is not const.
  [[nodiscard]] std::uint32_t coarse_tick_now();
  void set_viewport_origin(int x, int y);
  void reset_simulation_state();
  void reset_command_state();
  void install_document(OriginalTdtDocument document);
  // 10d0:0225 before disk I/O: a fresh tower carrying the process RNG.
  void install_empty_document();
  void apply_menu_ops(std::span<const OriginalMenuOp> ops) {
    apply_original_menu_ops(menu_, ops);
  }
  void remove_recent_file_items();
  // The Open / Save As sheet result (see queue_picked_file).
  std::optional<std::filesystem::path> next_picked_file(bool save);

  // Mirrors of the native_main.cpp host helpers of the same purpose.
  void rebuild_main(OriginalMainSurfacePass pass,
                    bool advance_state,
                    bool full_frame_surface_dirty = false);
  bool advance_main_surface_state(OriginalMainSurfacePass pass);
  void present_main();
  // Repaints info_raster_ from the live document and status text through
  // the software canvas. Called exactly where the Win32 host paints the
  // Info window synchronously: its direct 1090:0661 present, and every
  // InvalidateRect+UpdateWindow (1118:0000 status changes, the day change,
  // the rating promotion, the bomb ransom and the document transition).
  // Presentation only: it reads no clock and changes no game state.
  void paint_info();
  void focus_event_coordinate(std::int16_t floor, std::int16_t x);
  // play_dialog_sound: run_original_event_dialog's stop-all and WAVE
  // request (false when run_event_dialog already made it before opening).
  std::uint16_t log_dialog(const OriginalEventDialogRequest& dialog,
                           std::uint16_t choice,
                           std::string_view meaning,
                           bool play_dialog_sound = true);
  // run_original_event_dialog: when the input source drives it the dialog
  // runs modally (sdl_dialog.hpp) and its AHOTTADLOGFILTER answer
  // (OriginalEventDialogController) is returned; otherwise, or when the
  // source supplies no answer, default_choice, exactly as before.
  std::uint16_t run_event_dialog(const OriginalEventDialogRequest& dialog,
                                 std::uint16_t default_choice,
                                 std::string_view meaning);
  // One MessageBox through the dialog runtime: the raw button id (IDOK,
  // IDYES, ...) when answered, nullopt when no input source drives it or it
  // supplied no answer (the caller then uses its scripted default).
  std::optional<int> run_message_box(const OriginalMessageBoxRequest& request,
                                     std::string label);
  // show_original_alert (1208:0133): ALRT/alert_id formatted with the
  // substitutions, through the dialog runtime when driven and normalized by
  // 1208:0369; otherwise the scripted "alert" answer. Logged and tallied
  // either way with the same {id, answer} request.
  int show_alert(int alert_id,
                 const std::array<std::string_view, 4>& substitutions);
  // run_original_about_dialog's 1010:049e launcher around the About modal.
  void run_about_dialog();
  // The dialog modal loop: input from input_source_, signals to procedure,
  // which returns the EndDialog result to close (nullopt: no answer) and
  // acts on the open dialog through ops.
  using DialogProcedure = std::function<std::optional<int>(
      const DialogSignal& signal, OriginalDialogOps& ops)>;
  // Runs only when drives_dialog(dialog) was true.
  std::optional<int> run_dialog_modal(ModalDialog& dialog,
                                      const DialogProcedure& procedure);
  [[nodiscard]] bool drives(const ModalDialog& dialog);
  void sound(std::int32_t resource, std::string_view source);
  // 11c8:0167 / 11c8:0100 on the runtime, when there is one.
  void play_audio(std::int32_t resource,
                  std::uint16_t repeat,
                  std::uint16_t priority,
                  bool reserved_if_idle = false);
  void log_fire_menu(bool enabled);
  OriginalIdlePassContext idle_pass_context();
  OriginalWorldInputContext world_input_context();
  // The 1050:05a7 modal loop over input_source_.
  bool run_selector_modal(OriginalCommandSelectorSession& selector);
  void route_pointer(Panel panel, const InputEvent& event,
                     std::uint16_t keys);
  // WM_NCHITTEST for a point inside a palette's outer rectangle.
  enum class PaletteHit : std::uint8_t { client, caption, border };
  [[nodiscard]] PaletteHit palette_hit(Panel panel, int x, int y) const;

  // OriginalIdlePassHost (sdl_session.cpp maps each hook to its Win32
  // counterpart). Window-only hooks keep the interface's no-op defaults.
  std::uint32_t now_ms() override { return clock_ms_(); }
  std::uint32_t coarse_tick(std::uint32_t now_ms) override;
  void expire_find_marker() override;
  bool build_mode_enabled() override {
    return world_input_.build_mode_enabled;
  }
  bool elevator_finger_capture_active() override {
    return world_input_.shaft_drag.capture_active;
  }
  bool fast_mode_enabled() override { return flags_.fast_mode_enabled; }
  // No Elevator Control window yet, so never isolated (DS:b3ae).
  bool elevator_isolation_active() override { return false; }
  bool ambient_sound_enabled() override { return flags_.sound_enabled; }
  OriginalIdleView main_view() override;
  bool step_effect_palette() override;
  bool refresh_time_palette() override;
  bool advance_visible_people() override;
  std::uint16_t event_dialog(const OriginalEventDialogRequest& dialog,
                             OriginalIdleDialog kind) override;
  void focus(std::int16_t floor, std::int16_t x) override;
  void notification_status(std::uint16_t code) override;
  void income_status(std::uint16_t code) override;
  void expire_info_status() override;
  void play_sound(std::int32_t resource,
                  std::uint16_t repeat,
                  std::uint16_t priority,
                  OriginalIdleSound source) override;
  void play_event_sounds(
      std::span<const OriginalEventSoundRequest> requests) override;
  void stop_all_audio(bool force) override;
  void checkpoint(OriginalIdleCheckpoint) override;
  // Applied to the menu and logged.
  void fire_menu_enabled(bool enabled) override { log_fire_menu(enabled); }
  void idle_palette_changed() override;
  void rating_promoted_before_dialog(std::uint16_t rating) override;
  void rating_promoted_after_dialog(std::uint16_t rating) override;
  void before_scheduler_step(bool sample_cursor) override;
  void begin_full_frame(bool day_changed) override;
  void bomb_ransom_paid() override { paint_info(); }
  void preview_only_pass() override;
  void restore_preview() override;
  bool render_full_frame(bool renderer_surface_dirty) override;
  void commit_full_frame_preview() override;
  void palette_recolor_rebuild() override;
  void present_main_direct() override { present_main(); }
  void present_info_direct(OriginalIdleInfoLatches& latches) override;
  // The panels are recomposed on every present. The idle pass calls this
  // only right after present_main_direct (1090:064d), which already
  // requested one; world input calls it through services().
  void invalidate_command() override { present_requested_ = true; }
  void final_effect_palette_changed() override;

  // Appends one host request to the ordered digest log.
  enum class RequestKind : std::uint8_t {
    pass_begin = 1,
    pass_end,
    dialog,
    sound,
    focus,
    notification_status,
    income_status,
    fire_menu,
    rating_promoted,
    command_mode,
    // Logged only when input happens, so runs without input keep their
    // digests.
    alert,
    info_dialog,
    elevator_control,
    capture,
    construction_status,
    command_status,
    beep,
    selector,
    panel,
    view,
    menu,
    picker,
    file,
    help,
    command_dialog,
    shutdown,
  };
  void log_request(RequestKind kind,
                   std::initializer_list<std::int64_t> fields);
  void tally(const std::string& key) { ++request_tally_[key]; }

  // The pack bytes must outlive resources_: find() returns spans into them.
  std::vector<std::byte> pack_;
  OriginalResources resources_;
  std::function<std::uint32_t()> clock_ms_;
  SessionFlags flags_;
  // Declared after resources_ and clock_ms_, which it refers to, so it is
  // destroyed (and stops its device) first.
  std::unique_ptr<OriginalAudioRuntime> audio_{};
  bool window_active_{true};   // Main/Elevator Control activation word
  bool window_iconic_{false};  // IsIconic(Main)

  // validate_original_startup_data tables (load_original_startup_data).
  OriginalPartTable part_{};
  OriginalYenTable construction_costs_{};
  OriginalYenTable rent_income_{};
  OriginalYenTable maintenance_costs_{};

  std::optional<OriginalTdtDocument> document_;
  OriginalSimulationState simulation_state_{};
  std::uint32_t fast_frame_ms_{};
  OriginalPaletteRuntime palette_runtime_{};
  OriginalSkyDecorationState sky_decorations_{};
  OriginalInfoStatusState info_status_{};
  std::vector<OriginalElevatorTransferVisual> transfer_visuals_{};

  // Command, Main pointer, preview and Map state shared with the Win32 host
  // (original_world_input.hpp), including the mutable TABL rating copies.
  OriginalWorldInputState world_input_{};
  std::unique_ptr<WorldInputHost> world_host_;
  std::unique_ptr<MainCommandHost> command_host_;
  std::unique_ptr<TowerFileHost> file_host_;
  // Main's menu and accelerator table (WM_CREATE and every later op).
  OriginalMenuItems menu_{};
  std::vector<OriginalAccelerator> accelerators_{};
  MenuView menu_view_{};
  // DS:de2a/de2c/de2e. Kept here as well, so the Sound items toggle even
  // without an audio runtime (headless runs have none).
  std::array<bool, 3> sound_categories_{true, true, true};
  // Document identity (g_tower_path / g_tower_file_title).
  std::filesystem::path tower_path_{};
  std::filesystem::path tower_title_{"untitled"};
  bool main_closing_{};  // DS:31c6
  bool quit_requested_{};
  FilePicker* file_picker_{};
  std::deque<std::optional<std::filesystem::path>> picked_files_{};
  InputSource* input_source_{};
  bool left_button_down_{};
  // Last pointer position in Main client coordinates, sampled into
  // world_input_.pointer by 1258:0256 (before_scheduler_step). Win32 never
  // clears DS:77ac's "inside" once a point was sampled.
  std::optional<OriginalWorldPoint> last_pointer_{};
  WindowLayout layout_{};
  int command_outer_height_{100};  // 1128:05eb's raw 63x100 until 1140:010d
  bool map_visible_{true};         // DS:31ac
  bool info_visible_{true};        // DS:31aa
  bool command_visible_{true};     // DS:31a8
  Panel active_panel_{Panel::main};
  // A palette nonclient press is held until its release (SC_MOVE loop).
  bool nonclient_press_{};
  std::optional<OriginalInputSurface> capture_{};
  OriginalCommandSelectorSession* selector_{};
  std::vector<ModalDialog*> dialogs_{};
  int modal_depth_{};              // DS:24b8
  int alert_answer_{1};
  std::uint16_t cursor_id_{};
  bool document_dirty_{};
  // DS:77ac at idle-pass entry and the 1090:05ee rectangle.
  std::optional<OriginalConstructionPreviewRect> previous_preview_rect_{};
  std::optional<OriginalConstructionPreviewRect> current_preview_rect_{};
  // A non-synchronous request_main_pass, run before the next idle pass as
  // the pending WM_PAINT would be.
  std::optional<OriginalMainSurfacePass> pending_main_pass_{};
  std::uint32_t last_auxiliary_tick_{};  // DS:31b0/31b2

  Viewport viewport_{};
  // Native mirror of the DS:3264 backing (g_original_main_backing).
  OriginalWorldRaster main_raster_{};
  bool main_backing_dirty_{true};
  // Retained Info client image (see info_raster()). A future path that
  // shows a hidden Info panel again must repaint it, as WM_PAINT would.
  OriginalWorldRaster info_raster_{};
  // Stands in for InvalidateRect / direct Main blits until the next present.
  bool present_requested_{};
  HostRequestCounts host_requests_{};
  // Oracle-only state: never read by the simulation.
  Fnv64 request_log_{};
  std::uint64_t request_log_entries_{};
  std::uint64_t idle_passes_{};
  std::map<std::string, std::size_t> request_tally_{};
};

}  // namespace simtower::sdl
