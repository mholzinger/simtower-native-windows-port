#pragma once

// Platform-free game session for the SDL3 frontend. It owns the state that
// native_main.cpp keeps in Win32 globals (resources, tables, document, clock,
// palette, sky decorations, transfer visuals) plus a plain viewport in place
// of the Main window's scrollbars, and drives the shared idle pass
// (original_idle_pass.hpp: 1258:023a -> 1200:0196 -> 1090:03ab(1)) as its
// host. The 1258:0195-023a prelude and 1258:0285-02d9 epilogue are host-owned
// (see run_idle_pass). Host requests (dialogs, sounds, focus, Info/Map/Command
// repaints) are logged instead of acted on. Nothing here includes SDL, so
// the session can also drive headless runs with a virtual clock.

#include "sdl_digest.hpp"
#include "sdl_scenario.hpp"

#include "original_command_palette.hpp"
#include "original_idle_pass.hpp"
#include "original_info.hpp"
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
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <map>
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
  // Passed to select_original_ambient_sound (11c8:03ab). The Win32 host
  // passes g_audio->sound_enabled(); this frontend has no audio yet, so the
  // default false matches a Win32 run without a sound device (no RNG draw).
  bool sound_enabled{false};
  // Log every sound request (elevator movement sounds fire every frame).
  bool log_sounds{false};
  // Print every digest-log entry to stdout ("request <kind> <fields>"), so
  // two runs whose digests differ can be diffed line by line.
  bool trace_requests{false};
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
 public:
  // clock_ms replaces GetTickCount: every Win32 read maps to one call.
  Session(std::vector<std::byte> pack,
          std::function<std::uint32_t()> clock_ms,
          SessionFlags flags = {});

  // Win32 New path: begin_original_new_tower. The current viewport
  // origin is kept, as 10d0:086c leaves DS:b3f0/b3f2 untouched.
  void new_tower();
  // Win32 Open path: load_original_tower_from_path. Throws
  // OriginalTdtError / OriginalTdtFileError on failure.
  void open_tower(const std::filesystem::path& path);

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
  [[nodiscard]] const Viewport& viewport() const { return viewport_; }
  [[nodiscard]] const OriginalTdtDocument* document() const {
    return document_ ? &*document_ : nullptr;
  }
  [[nodiscard]] const HostRequestCounts& host_requests() const {
    return host_requests_;
  }

  // Test-only: stages a deterministic tower (sdl_scenario.hpp) on the live
  // document after New, through the portable construction functions only.
  ScenarioSummary apply_scenario(std::string_view name);
  // Test-only: runs a hidden menu event command between idle passes, as
  // main_window_proc's WM_COMMAND does: "bomb" is 9000 (10c8:006e), "fire"
  // is 9002 (10e8:0029). Returns whether the event started.
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
  [[nodiscard]] std::uint32_t coarse_tick_now() const;
  void set_viewport_origin(int x, int y);
  void reset_simulation_state();
  void reset_command_state();
  void install_document(OriginalTdtDocument document);

  // Mirrors of the native_main.cpp host helpers of the same purpose.
  void rebuild_main(OriginalMainSurfacePass pass,
                    bool advance_state,
                    bool full_frame_surface_dirty = false);
  bool advance_main_surface_state(OriginalMainSurfacePass pass);
  void present_main();
  void focus_event_coordinate(std::int16_t floor, std::int16_t x);
  std::uint16_t log_dialog(const OriginalEventDialogRequest& dialog,
                           std::uint16_t choice,
                           std::string_view meaning);
  void sound(std::int32_t resource, std::string_view source);
  void log_fire_menu(bool enabled);
  OriginalIdlePassContext idle_pass_context();

  // OriginalIdlePassHost (sdl_session.cpp maps each hook to its Win32
  // counterpart). Window-only hooks keep the interface's no-op defaults.
  std::uint32_t now_ms() override { return clock_ms_(); }
  std::uint32_t coarse_tick(std::uint32_t now_ms) override;
  void expire_find_marker() override;
  bool build_mode_enabled() override { return build_mode_enabled_; }
  bool elevator_finger_capture_active() override { return false; }
  bool fast_mode_enabled() override { return flags_.fast_mode_enabled; }
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
  void fire_menu_enabled(bool enabled) override { log_fire_menu(enabled); }
  void idle_palette_changed() override;
  void rating_promoted_before_dialog(std::uint16_t rating) override;
  void rating_promoted_after_dialog(std::uint16_t rating) override;
  void cathedral_rating_promoted() override;
  bool render_full_frame(bool renderer_surface_dirty) override;
  void palette_recolor_rebuild() override;
  void present_main_direct() override { present_main(); }
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
  };
  void log_request(RequestKind kind,
                   std::initializer_list<std::int64_t> fields);
  void tally(const std::string& key) { ++request_tally_[key]; }

  // The pack bytes must outlive resources_: find() returns spans into them.
  std::vector<std::byte> pack_;
  OriginalResources resources_;
  std::function<std::uint32_t()> clock_ms_;
  SessionFlags flags_;

  // validate_original_startup_data tables (load_original_startup_data).
  OriginalPartTable part_{};
  OriginalYenTable construction_costs_{};
  OriginalYenTable rent_income_{};
  OriginalYenTable maintenance_costs_{};
  std::array<OriginalCommandRatingState, 6> command_ratings_{};

  std::optional<OriginalTdtDocument> document_;
  OriginalSimulationState simulation_state_{};
  std::uint32_t fast_frame_ms_{};
  OriginalPaletteRuntime palette_runtime_{};
  OriginalSkyDecorationState sky_decorations_{};
  OriginalInfoStatusState info_status_{};
  std::vector<OriginalElevatorTransferVisual> transfer_visuals_{};

  // reset_original_command_state fields this milestone consults.
  bool build_mode_enabled_{true};  // DS:783e
  std::uint16_t command_mode_{3U}; // DS:783c
  std::uint16_t map_mode_{0U};     // DS:7840

  Viewport viewport_{};
  // Native mirror of the DS:3264 backing (g_original_main_backing).
  OriginalWorldRaster main_raster_{};
  bool main_backing_dirty_{true};
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
