#pragma once

// Platform-free game session for the SDL3 frontend. It owns the state that
// native_main.cpp keeps in Win32 globals (resources, tables, document, clock,
// palette, sky decorations, transfer visuals) plus a plain viewport in place
// of the Main window's scrollbars, and replays the portable half of
// run_original_idle_simulation (1258:0186-02d9 -> 1200:0196 -> 1090:03ab(1)).
// Host requests (dialogs, sounds, focus, Info/Map/Command repaints) are
// logged instead of acted on. Nothing here includes SDL, so the session can
// also drive headless runs with a virtual clock.

#include "original_command_palette.hpp"
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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace simtower::sdl {

// Replaces the scrollbar positions (current_original_view, N:1863) and the
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
};

// Counts of host requests that were logged instead of performed.
struct HostRequestCounts {
  std::size_t dialogs{};
  std::size_t sounds{};
  std::size_t focus{};
  std::size_t status{};
};

class Session {
 public:
  // clock_ms replaces GetTickCount: every Win32 read maps to one call.
  Session(std::vector<std::byte> pack,
          std::function<std::uint32_t()> clock_ms,
          SessionFlags flags = {});

  // Win32 New path: begin_original_new_tower (N:3315). The current viewport
  // origin is kept, as 10d0:086c leaves DS:b3f0/b3f2 untouched.
  void new_tower();
  // Win32 Open path: load_original_tower_from_path (N:3699). Throws
  // OriginalTdtError / OriginalTdtFileError on failure.
  void open_tower(const std::filesystem::path& path);

  // One empty-queue idle pass (run_original_idle_simulation, N:1150).
  // Returns true when the main raster changed and should be presented.
  bool run_idle_pass();

  // Milliseconds until the next idle pass can do work: the fast-mode frame
  // adapter or the six-tick gate, and the 15-tick effect palette gate.
  [[nodiscard]] std::uint32_t milliseconds_until_due() const;

  // Position-only view restore (restore_original_derived_view_position,
  // N:1962): clamp the origin without a rebuild. Used before New/Open.
  void restore_view(int x, int y) { set_viewport_origin(x, y); }
  // Scrollbar path (scroll_original, N:8250): clamp the requested origin and
  // run 1058:05f8's synchronous 1080:0a1e(1) rebuild.
  void scroll_to(int x, int y);
  // One line (codes 0/1) or page (codes 2/3) step through the exact
  // original_main_scroll_request_position helper (1158:015b/01e7).
  void scroll_by_code(bool horizontal, std::uint16_t scroll_code);
  // WM_SIZE path (N:9182-9230). The first call stands in for window
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

 private:
  [[nodiscard]] std::uint32_t coarse_tick() const;
  void set_viewport_origin(int x, int y);
  void reset_simulation_state();
  void reset_command_state();
  void install_document(OriginalTdtDocument document);

  // Mirrors of the native_main.cpp host helpers of the same purpose.
  void rebuild_main(OriginalMainSurfacePass pass,
                    bool advance_state,
                    bool full_frame_surface_dirty = false);
  bool advance_main_surface_state(OriginalMainSurfacePass pass);
  bool step_effect_palette();
  void present_main();
  void run_scheduler_pass();
  void focus_event_coordinate(std::int16_t floor, std::int16_t x);
  std::uint16_t event_dialog(const OriginalEventDialogRequest& dialog,
                             std::uint16_t choice,
                             std::string_view meaning);
  void sound(std::int32_t resource, std::string_view source);
  void notification_status(std::uint16_t code);
  void income_status(std::uint16_t code);

  bool dispatch_scheduler_call(const OriginalSimulationCall& call);
  bool run_bomb_offer_boundary();
  bool run_fire_offer_boundary();
  bool apply_event_damage(
      std::span<const OriginalFacilityDamageRequest> requests);
  void play_event_sounds(std::span<const OriginalEventSoundRequest> requests);
  bool consume_bomb_action(const OriginalEventActionResult& action);
  bool consume_fire_action(const OriginalEventActionResult& action);
  bool consume_fire_crew_resolution(
      const OriginalFireCrewResolution& resolution);
  void consume_person_host_requests(
      const std::vector<OriginalPersonHostRequest>& requests);
  void consume_person_family_dispatch(
      const OriginalPersonFamilyDispatchResult& dispatch);
  void consume_cathedral_ceremony(
      std::int16_t floor, std::int16_t x,
      const OriginalEventDialogRequest& rating_dialog,
      std::int32_t wave_resource);
  void run_full_frame(bool world_changed);

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

  // reset_original_command_state (N:821) fields this milestone consults.
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
};

}  // namespace simtower::sdl
