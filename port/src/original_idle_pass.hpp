#pragma once

// Portable part of the empty-queue idle pass 1258:0186, covering 1258:023a
// through 1090:03ab(1): the idle effects palette, the 10e0:051d Find-marker
// timeout (through the host), the 1200:0196 scheduler and its far-call
// dispatch, and the 1090:03ab(1) full frame. The host owns the 1258:0195-023a
// prelude (WAVMIX activation, TOPMOST window repair) and the 1258:0285-02d9
// epilogue (Elevator Control refresh, 11c8:0135(0) audio finish). Every
// host or presentation effect (dialogs, sounds, focus, status text, menus,
// repaints, audio checkpoints) goes through OriginalIdlePassHost at the exact
// point the original performs it, so game state and RNG consumption keep the
// original order. The Win32 host (native_main.cpp) and the SDL session
// (sdl/sdl_session.cpp) both call it.

#include "original_people.hpp"
#include "original_simulation.hpp"
#include "original_tables.hpp"
#include "original_tdt.hpp"
#include "original_world.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace simtower {

// Main view origin (scrollbar positions) and client size.
struct OriginalIdleView {
  int x{};
  int y{};
  int width{};
  int height{};
};

// Host-owned state the pass mutates. Built per call from the host's own
// storage; every member refers to it, nothing is copied.
struct OriginalIdlePassContext {
  OriginalTdtDocument* document;  // null when no tower is loaded
  OriginalSimulationState& simulation;
  std::uint32_t& fast_frame_tick;  // native Fast Mode frame adapter
  std::vector<OriginalElevatorTransferVisual>& transfer_visuals;
  const OriginalPartTable& part;
  const OriginalYenTable& rent_income;
  const OriginalYenTable& maintenance_costs;
};

// Which event dialog a host is asked to run. Only the offers' answers feed
// back into game state; every other result is discarded.
enum class OriginalIdleDialog : std::uint8_t {
  bomb_offer,          // 10c8:006e; 2 pays the ransom
  bomb_followup,       // 10c8:006e after the answer
  bomb_event,          // Bomb progression (found or exploded)
  fire_offer,          // 10e8:0029
  fire_event,          // Fire progression and finish
  fire_crew_offer,     // 10e8:01e2; 1 declines, every other value hires
  fire_crew_followup,  // after the fire-crew answer
  hotel,               // person-family Hotel request
  rating,              // 1140:002d / 1140:00a8 promotion
};

// Origin of a sound request, for host logging only.
enum class OriginalIdleSound : std::uint8_t {
  scheduler,       // 11c8:0167
  ambient,         // 11c8:03ab
  recycling,       // 1088:00de
  metro,           // 11e8:0273
  bomb_payment,    // 10c8:014f WAVE/10015
  cathedral,       // Cathedral ceremony
  elevator,        // Elevator movement 0x1772
  hotel_checkout,  // 1118:0143 WAVE/10013
};

// Direct 11e0:0e84 audio checkpoints inside 1090:03ab(1).
enum class OriginalIdleCheckpoint : std::uint8_t {
  scheduler_tail,        // 1090:042a
  construction,          // 1090:0434
  rating,                // 1090:043e
  first_effect_palette,  // 1090:044d
  people,                // 1090:046a
  map_cadence,           // 1090:0481
  preview_restore,       // 1090:04b1
  elevator,              // 1090:053d, once per used Elevator
  vertical_transport,    // 1090:0556
  time_palette,          // 1090:061a
  final_frame,           // 1090:06ed
};

enum class OriginalIdlePassOutcome : std::uint8_t {
  no_document,   // 1258:0186 Find timeout ran; no tower
  gated,         // construction off or Elevator finger capture
  not_advanced,  // 1200:0196 did not step; 1090:03ab(0) preview only
  full_frame,    // 1090:03ab(1) completed
};

class OriginalIdlePassHost;

// The latch-selected Info field painters of 1090:0661-06cd. The host calls
// them from present_info_direct at the point its own painter would.
class OriginalIdleInfoLatches {
 public:
  OriginalIdleInfoLatches(OriginalTdtDocument& document,
                          OriginalIdlePassHost& host) noexcept
      : document_(document), host_(host) {}
  // 1118:0143 Hotel checkout latch; plays WAVE/10013 through the host.
  void consume_hotel_checkout();

 private:
  OriginalTdtDocument& document_;
  OriginalIdlePassHost& host_;
};

class OriginalIdlePassHost {
 public:
  virtual ~OriginalIdlePassHost() = default;

  // Clock. Each call is one host clock read, at the original read point.
  virtual std::uint32_t now_ms() = 0;
  // 1208:05e6 for a time already read (the Win32 boundary pumps audio).
  virtual std::uint32_t coarse_tick(std::uint32_t now_ms) = 0;

  // Game-relevant host state, queried at the original point, never cached.
  // 1258:0186 10e0:051d Find-marker timeout and its construction toggle.
  virtual void expire_find_marker() = 0;
  virtual bool build_mode_enabled() = 0;              // DS:783e
  virtual bool elevator_finger_capture_active() = 0;
  virtual bool fast_mode_enabled() = 0;               // DS:de34
  virtual bool elevator_isolation_active() = 0;
  virtual bool ambient_sound_enabled() = 0;
  virtual OriginalIdleView main_view() = 0;
  // 1020:00cb on the host palette runtime (reads the clock itself).
  virtual bool step_effect_palette() = 0;
  // 1020:098b time palette.
  virtual bool refresh_time_palette() = 0;
  // 1090:0567 visible facility people (RNG); true when the world changed.
  virtual bool advance_visible_people() = 0;
  // Runs a modal event dialog and returns its result.
  virtual std::uint16_t event_dialog(const OriginalEventDialogRequest& dialog,
                                     OriginalIdleDialog kind) = 0;
  // Event focus. Must perform the synchronous 1080:0a1e(1) camera refresh,
  // which advances the sky and visible people (RNG) at this point.
  virtual void focus(std::int16_t floor, std::int16_t x) = 0;
  virtual void notification_status(std::uint16_t code) = 0;
  virtual void income_status(std::uint16_t code) = 0;
  // 1118:08f3 Info status expiry.
  virtual void expire_info_status() = 0;

  // Audio and menu effects.
  virtual void play_sound(std::int32_t resource,
                          std::uint16_t repeat,
                          std::uint16_t priority,
                          OriginalIdleSound source) = 0;
  virtual void play_event_sounds(
      std::span<const OriginalEventSoundRequest> requests) = 0;
  virtual void stop_all_audio(bool force) = 0;
  virtual void fire_menu_enabled(bool enabled) = 0;  // command 0x9c48
  virtual void checkpoint(OriginalIdleCheckpoint) {}

  // Presentation.
  virtual void idle_palette_changed() = 0;  // 1258:023a changed the palette
  // 1258:0256 cursor sample and 1090:03ab's DS:77ac save.
  virtual void before_scheduler_step(bool /*sample_cursor*/) {}
  virtual void preview_only_pass() {}  // 1090:03ab(0)
  virtual void begin_full_frame(bool /*day_changed*/) {}
  virtual void bomb_ransom_paid() {}
  virtual void rating_promoted_before_dialog(std::uint16_t /*rating*/) {}
  virtual void rating_promoted_after_dialog(std::uint16_t /*rating*/) {}
  // The Cathedral's rating-six promotion 1140:00a8, after its 1148:003d
  // modal: 1140:010d(1) Command refresh and the 1118:0044 Info repaint.
  virtual void cathedral_rating_promoted() {}
  virtual void map_cadence_refresh() {}  // 1090:046f 1080:09c3
  virtual void restore_preview() {}      // 1090:04a7 11f8:3b94
  // 1090:0580 renderer and 1090:05ee outline. Returns whether the
  // construction preview rectangle changed.
  virtual bool render_full_frame(bool renderer_surface_dirty) = 0;
  // Native RGB rebuild after the time or effects palette changed.
  virtual void palette_recolor_rebuild() = 0;
  // After the simulation mirror resync, before the presentation plan.
  virtual void commit_full_frame_preview() {}
  // The recolor also invalidates the Map (native AnimatePalette equivalent).
  virtual void palette_recolor_invalidate_map() {}
  virtual void present_main_direct() = 0;  // 1090:062c
  virtual void invalidate_command() {}     // 1090:064d
  // 1090:0661-06cd. A host without an Info painter still drains the latches.
  virtual void present_info_direct(OriginalIdleInfoLatches& latches) {
    latches.consume_hotel_checkout();
  }
  // The fourth 1020:00cb (1090:06dc) advanced the effects palette.
  virtual void final_effect_palette_changed() = 0;
};

// One empty-queue idle pass from 1258:023a through 1090:03ab(1). The host
// runs its 1258:0195-023a prelude before and its 1258:0285-02d9 epilogue
// after.
OriginalIdlePassOutcome run_original_idle_pass(OriginalIdlePassContext& context,
                                               OriginalIdlePassHost& host);

// Event boundaries, also reached from the hidden menu commands 9000/9002 and
// the fire-crew reconsideration command 40008.
bool run_original_bomb_offer_boundary(OriginalIdlePassContext& context,
                                      OriginalIdlePassHost& host);
bool run_original_fire_offer_boundary(OriginalIdlePassContext& context,
                                      OriginalIdlePassHost& host);
bool consume_original_fire_crew_resolution(
    OriginalIdlePassContext& context,
    OriginalIdlePassHost& host,
    const OriginalFireCrewResolution& resolution);
// 0883's process-only callback work for one person-family dispatch.
void consume_original_person_family_dispatch(
    OriginalIdlePassHost& host,
    const OriginalPersonFamilyDispatchResult& dispatch);
void consume_original_person_host_requests(
    OriginalIdlePassHost& host,
    const std::vector<OriginalPersonHostRequest>& requests);

}  // namespace simtower
