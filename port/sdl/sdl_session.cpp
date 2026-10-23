#include "sdl_session.hpp"

#include "original_construction.hpp"
#include "original_resources.generated.hpp"
#include "original_startup_data.hpp"
#include "original_tdt_file.hpp"
#include "original_time.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

namespace simtower::sdl {
namespace {

// Event-dialog choices used while dialogs are logged instead of shown. They
// are explicit so the run stays deterministic: DIALOG/3020 returns 1 = Find
// the Bomb (only 2 pays, 10c8:006e), and the scheduled fire-crew offer
// treats 1 as No (10e8:01e2; every other value hires).
constexpr std::uint16_t kBombOfferChoice = 1U;
constexpr std::uint16_t kFireCrewOfferChoice = 1U;
constexpr std::uint16_t kAcknowledgeChoice = 1U;

void log_line(const std::string& text) {
  std::fprintf(stderr, "simtower_sdl: %s\n", text.c_str());
}

// find() bounds-checks each slice but never compares the pack with the
// compiled descriptor table, so a mismatched pack would yield wrong bytes.
void verify_pack_extent(std::span<const std::byte> pack) {
  std::size_t required = 0U;
  for (const auto& descriptor : generated::kResources) {
    required = std::max<std::size_t>(
        required, static_cast<std::size_t>(descriptor.offset) +
                      static_cast<std::size_t>(descriptor.size));
  }
  if (pack.size() != required) {
    throw std::runtime_error(
        "Resource pack size " + std::to_string(pack.size()) +
        " does not match the compiled descriptor table (" +
        std::to_string(required) + " bytes)");
  }
}

}  // namespace

Session::Session(std::vector<std::byte> pack,
                 std::function<std::uint32_t()> clock_ms,
                 SessionFlags flags)
    : pack_(std::move(pack)),
      clock_ms_(std::move(clock_ms)),
      flags_(flags) {
  verify_pack_extent(pack_);
  // N:9803. The portable span constructor replaces from_current_module.
  resources_ = OriginalResources(std::span<const std::byte>(pack_));

  // validate_original_startup_data (N:9781): the same portable loader the
  // Win32 host calls, so a truncated or mismatched pack fails here. The
  // command ratings feed the Command panel, which is not drawn yet.
  auto startup = load_original_startup_data(resources_);
  part_ = std::move(startup.part);
  construction_costs_ = std::move(startup.construction_costs);
  rent_income_ = std::move(startup.rent_income);
  maintenance_costs_ = std::move(startup.maintenance_costs);
  command_ratings_ = std::move(startup.command_ratings);

  // Deliberately omitted Win32 startup steps (N:9804-9879), none of which
  // touches the document RNG or simulation state:
  // - WAVMIX init, SIMTOWER.INI profile, logical palette, font bank, cursors,
  //   icon, accelerators and menu (no audio, menus or fonts yet);
  // - the BITMAP/128 -> 256 splash and its 180-tick wait (1128:0042-00a7);
  // - the bootstrap reset_original_command_state(true),
  //   refresh_original_rating_command(rating, 0) and Map-focus adjustment
  //   (10d0:0ac2): New/Open reset the command state again before any pass;
  // - the fire-menu update and WAVE/20000.
  // N:9814 base palette, then N:9869 with the never-stored bootstrap tower
  // (its frame 0x09e5 selects the CLUT 1002->1001 transition).
  reset_original_palette_runtime(resources_, nullptr, palette_runtime_,
                                 coarse_tick());
  const auto startup_bootstrap = make_original_new_tdt();
  reset_original_palette_runtime(resources_, &startup_bootstrap,
                                 palette_runtime_, coarse_tick());
}

std::uint32_t Session::coarse_tick() const {
  // 1208:05e6. The Win32 wrapper also pumps audio here; there is none yet.
  return original_coarse_tick(clock_ms_());
}

void Session::set_viewport_origin(int x, int y) {
  // SetScrollInfo's range clamp, as installed by 1158:05ef / 1080:00d7.
  viewport_.x = original_main_scrollbar_resize_state(
                    x, viewport_.width, kOriginalWorldWidth)
                    .position;
  viewport_.y = original_main_scrollbar_resize_state(
                    y, viewport_.height, kOriginalWorldHeight)
                    .position;
}

void Session::resize(int width, int height) {
  const bool first_size = viewport_.width == 0 && viewport_.height == 0;
  viewport_.width = std::max(0, width);
  viewport_.height = std::max(0, height);
  if (first_size) {
    // Window creation: 1128:08d6 (N:9699) installs the initial view,
    // centred horizontally with the bottom 324 px above the world floor,
    // while the DS:02a4 gate (N:9186, set at N:9926) makes the creation-time
    // WM_SIZE a no-op. So only mark the backing dirty for the first paint.
    const auto initial =
        original_initial_view(viewport_.width, viewport_.height);
    set_viewport_origin(initial.x, initial.y);
    main_backing_dirty_ = true;
    present_requested_ = true;
    return;
  }
  // Live restore/maximize WM_SIZE (N:9195-9230): 1158:05ef clamps the saved
  // origin to the new ranges, then completes through the synchronous
  // 1080:0a1e(1) pass, which advances the sky decorations and the visible
  // facility people (both consume the document RNG).
  set_viewport_origin(viewport_.x, viewport_.y);
  rebuild_main(OriginalMainSurfacePass::rebuild_with_sky, true);
  present_requested_ = true;
}

void Session::scroll_to(int x, int y) {
  set_viewport_origin(x, y);
  // 1058:05f8: synchronous 1080:0a1e(1), which advances the sky and the
  // visible facility people (both consume the document RNG). Like
  // scroll_original (N:8252-8295) this runs for every accepted request,
  // including one that is clamped back to the current origin at a world edge.
  rebuild_main(OriginalMainSurfacePass::rebuild_with_sky, true);
  present_requested_ = true;
}

void Session::scroll_by_code(bool horizontal, std::uint16_t scroll_code) {
  const int current = horizontal ? viewport_.x : viewport_.y;
  const int extent = horizontal ? viewport_.width : viewport_.height;
  const int world = horizontal ? kOriginalWorldWidth : kOriginalWorldHeight;
  const auto position = original_main_scroll_request_position(
      scroll_code, current, 0U, extent, world - extent);
  if (!position) return;
  if (horizontal) {
    scroll_to(*position, viewport_.y);
  } else {
    scroll_to(viewport_.x, *position);
  }
}

void Session::reset_simulation_state() {
  // reset_original_simulation_state (N:930-962).
  transfer_visuals_.clear();
  main_backing_dirty_ = true;
  if (!document_) {
    simulation_state_ = {};
    palette_runtime_ = {};
    return;
  }
  reset_original_loaded_simulation_state(*document_);
  const auto now = clock_ms_();
  simulation_state_.frame_time = document_->header.frame_time;
  simulation_state_.current_day = document_->header.current_day;
  simulation_state_.day_phase =
      original_day_phase(simulation_state_.frame_time);
  simulation_state_.calendar_phase =
      original_calendar_phase(simulation_state_.current_day);
  simulation_state_.last_tick = original_coarse_tick(now);
  // The first Fast Mode frame is immediately eligible (N:956).
  fast_frame_ms_ = now - kNativeFastModeFramePeriodMs;
  reset_original_palette_runtime(resources_, &*document_, palette_runtime_,
                                 original_coarse_tick(now));
}

void Session::reset_command_state() {
  // reset_original_command_state (N:821) fields this frontend consults.
  build_mode_enabled_ = true;
  command_mode_ = 3U;
  map_mode_ = 0U;
  info_status_ = {};
}

void Session::install_document(OriginalTdtDocument document) {
  carry_original_process_random_state(document_ ? &*document_ : nullptr,
                                      document);
  document_ = std::move(document);
  reset_simulation_state();
  reset_command_state();
  // refresh_original_rating_command(rating, 0) only repaints Command and
  // dirties the backing here. restore_original_derived_view_position then
  // clamps the header view, and the document transition's Main step is a
  // synchronous 1080:0a1e(1) (present_original_document_transition_...).
  set_viewport_origin(document_->header.view_x, document_->header.view_y);
  rebuild_main(OriginalMainSurfacePass::rebuild_with_sky, true);
  present_requested_ = true;
  document_->header.view_x = static_cast<std::uint16_t>(viewport_.x);
  document_->header.view_y = static_cast<std::uint16_t>(viewport_.y);
}

void Session::new_tower() {
  // begin_original_new_tower (N:3315): 10d0:001d -> 086c keeps the prior
  // view, so the fresh header's zero view words are replaced first.
  auto document = make_original_new_tdt();
  document.header.view_x = static_cast<std::uint16_t>(viewport_.x);
  document.header.view_y = static_cast<std::uint16_t>(viewport_.y);
  install_document(std::move(document));
  log_line("new tower");
}

void Session::open_tower(const std::filesystem::path& path) {
  // load_original_tower_from_path (N:3699): an empty tower is installed
  // and reset before disk I/O, carrying the process RNG through both.
  auto empty_document = make_original_new_tdt();
  carry_original_process_random_state(document_ ? &*document_ : nullptr,
                                      empty_document);
  document_ = std::move(empty_document);
  reset_simulation_state();
  reset_command_state();
  install_document(load_original_tdt_file(path));
  log_line("opened " + path.string());
}

void Session::rebuild_main(OriginalMainSurfacePass pass,
                           bool advance_state,
                           bool full_frame_surface_dirty) {
  // rebuild_original_main_backing (N:7946) without Elevator Control
  // isolation, the Find marker and the construction preview.
  const auto plan = original_main_surface_pass_plan(pass);
  if (advance_state) (void)advance_main_surface_state(pass);
  if (plan.clear_elevator_transfer_visuals_before_rebuild) {
    // Every 1080:0a1e context first calls 10a8:0000.
    transfer_visuals_.clear();
  }
  if (viewport_.width <= 0 || viewport_.height <= 0) return;
  main_raster_ = render_original_world(
      resources_, document(), viewport_.x, viewport_.y, viewport_.width,
      viewport_.height, transfer_visuals_,
      palette_runtime_.initialized ? &palette_runtime_.colors : nullptr,
      &sky_decorations_, map_mode_, nullptr, {}, full_frame_surface_dirty);
  main_backing_dirty_ = false;
  if (plan.consume_elevator_transfer_visuals) transfer_visuals_.clear();
}

bool Session::advance_main_surface_state(OriginalMainSurfacePass pass) {
  // advance_original_main_surface_state (N:7914).
  if (!document_) return false;
  const auto plan = original_main_surface_pass_plan(pass);
  if (plan.advance_sky_decorations) {
    // 1080:0a1e calls 1048:03a3 before 1038:050e; it draws RNG first.
    (void)step_original_sky_decorations(
        resources_, *document_, sky_decorations_, viewport_.x, viewport_.y,
        viewport_.width, viewport_.height);
  }
  if (plan.advance_visible_facility_people) {
    const auto people = step_original_visible_facility_people(
        *document_, viewport_.x, viewport_.y, viewport_.width,
        viewport_.height, flags_.people_animation_enabled,
        flags_.world_control_modifier);
    return people.dispatched_tenants != 0U;
  }
  return false;
}

bool Session::step_effect_palette() {
  // 1020:00cb. No AnimatePalette sync: colours apply while composing.
  return document_ &&
         step_original_effect_palette(*document_, palette_runtime_,
                                      flags_.effects_animation_enabled,
                                      coarse_tick());
}

void Session::present() {
  // A pending WM_PAINT without an idle pass: presentation only.
  present_main();
  present_requested_ = false;
}

void Session::present_main() {
  // 1158:0a3c: rebuild only if a model change dirtied the backing.
  if (main_backing_dirty_) {
    rebuild_main(OriginalMainSurfacePass::window_paint, false);
  }
  present_requested_ = true;
}

void Session::focus_event_coordinate(std::int16_t floor, std::int16_t x) {
  // focus_original_event_coordinate (N:996) -> set_original_view, whose
  // camera refresh is a synchronous 1080:0a1e(1). Not cosmetic: it advances
  // the sky decorations and visible facility people.
  const auto view = original_facility_focus_view(x, floor, viewport_.width,
                                                 viewport_.height);
  ++host_requests_.focus;
  log_line("focus floor " + std::to_string(floor) + " x " +
           std::to_string(x));
  set_viewport_origin(view.x, view.y);
  rebuild_main(OriginalMainSurfacePass::rebuild_with_sky, true);
  present_requested_ = true;
}

std::uint16_t Session::event_dialog(const OriginalEventDialogRequest& dialog,
                                    std::uint16_t choice,
                                    std::string_view meaning) {
  // run_original_event_dialog (N:4821) stand-in.
  ++host_requests_.dialogs;
  log_line("dialog " + std::to_string(dialog.dialog_id) + " argument " +
           std::to_string(dialog.argument) + " wave " +
           std::to_string(dialog.wave_resource) + " -> " +
           std::to_string(choice) + " (" + std::string(meaning) + ")");
  return choice;
}

void Session::sound(std::int32_t resource, std::string_view source) {
  ++host_requests_.sounds;
  if (flags_.log_sounds) {
    log_line("sound " + std::to_string(resource) + " (" +
             std::string(source) + ")");
  }
}

void Session::notification_status(std::uint16_t code) {
  ++host_requests_.status;
  if (set_original_info_notification_status(resources_, info_status_, code,
                                            coarse_tick())) {
    log_line("info notification " + std::to_string(code) + ": " +
             info_status_.text);
  }
}

void Session::income_status(std::uint16_t code) {
  ++host_requests_.status;
  (void)set_original_info_income_status(resources_, info_status_, code,
                                        coarse_tick());
}

void Session::play_event_sounds(
    std::span<const OriginalEventSoundRequest> requests) {
  for (const auto& request : requests) sound(request.resource, "event");
}

bool Session::apply_event_damage(
    std::span<const OriginalFacilityDamageRequest> requests) {
  if (!document_ || requests.empty()) return false;
  const auto result = apply_original_facility_damage_sequence(
      *document_, rent_income_, requests);
  play_event_sounds(result.sound_requests);
  for (const auto code : result.notification_codes) {
    notification_status(code);
  }
  return result.changed != 0U;
}

bool Session::consume_bomb_action(const OriginalEventActionResult& action) {
  // consume_original_bomb_action (N:1016).
  bool changed = action.changed;
  for (const auto operation :
       original_bomb_event_host_plan(action).sequence()) {
    switch (operation) {
      case OriginalEventHostOperation::play_sounds:
        play_event_sounds(action.sound_requests);
        break;
      case OriginalEventHostOperation::apply_damage:
        changed = apply_event_damage(action.damage_requests) || changed;
        break;
      case OriginalEventHostOperation::dispatch_security:
        dispatch_original_security_response(*document_,
                                            action.security_dispatch_flags);
        changed = true;
        break;
      case OriginalEventHostOperation::focus_coordinate:
        focus_event_coordinate(action.focus_floor, action.focus_x);
        break;
      case OriginalEventHostOperation::show_dialog:
        (void)event_dialog(action.dialog, kAcknowledgeChoice, "bomb");
        break;
      case OriginalEventHostOperation::complete_deferred_action:
        complete_original_event_action(*document_,
                                       action.deferred_completion);
        changed = true;
        break;
      case OriginalEventHostOperation::update_fire_menu:
        break;
    }
  }
  return changed;
}

bool Session::consume_fire_action(const OriginalEventActionResult& action) {
  // consume_original_fire_action (N:1055).
  bool changed = action.changed;
  for (const auto operation :
       original_fire_event_host_plan(action).sequence()) {
    switch (operation) {
      case OriginalEventHostOperation::apply_damage:
        changed = apply_event_damage(action.damage_requests) || changed;
        break;
      case OriginalEventHostOperation::play_sounds:
        play_event_sounds(action.sound_requests);
        break;
      case OriginalEventHostOperation::update_fire_menu:
        log_line(std::string("fire menu ") +
                 (*action.fire_menu_enabled ? "enabled" : "disabled"));
        break;
      case OriginalEventHostOperation::show_dialog:
        (void)event_dialog(action.dialog, kAcknowledgeChoice, "fire");
        break;
      case OriginalEventHostOperation::complete_deferred_action:
        complete_original_event_action(*document_,
                                       action.deferred_completion);
        changed = true;
        break;
      case OriginalEventHostOperation::dispatch_security:
      case OriginalEventHostOperation::focus_coordinate:
        break;
    }
  }
  return changed;
}

bool Session::consume_fire_crew_resolution(
    const OriginalFireCrewResolution& resolution) {
  // consume_original_fire_crew_resolution (N:1086).
  if (!resolution.handled) return false;
  if (resolution.followup_dialog.valid()) {
    (void)event_dialog(resolution.followup_dialog, kAcknowledgeChoice,
                       "fire crew follow-up");
  }
  if (resolution.security_dispatch_pending) {
    dispatch_original_security_response(*document_,
                                        resolution.security_dispatch_flags);
  }
  if (resolution.focus_requested) {
    focus_event_coordinate(resolution.focus_floor, resolution.focus_x);
  }
  if (resolution.fire_menu_enabled) {
    log_line(std::string("fire menu ") +
             (*resolution.fire_menu_enabled ? "enabled" : "disabled"));
  }
  return resolution.hired || resolution.security_dispatch_pending;
}

bool Session::run_bomb_offer_boundary() {
  // 10c8:006e (N:1112).
  const auto offer = prepare_original_bomb_event(*document_, part_);
  if (!offer.offered) return false;
  const auto choice =
      event_dialog(offer.dialog, kBombOfferChoice, "bomb offer: search");
  const auto resolution =
      resolve_original_bomb_event(*document_, part_, choice);
  if (resolution.direct_wave_resource != 0) {
    sound(resolution.direct_wave_resource, "bomb payment");
  }
  if (resolution.followup_dialog.valid()) {
    (void)event_dialog(resolution.followup_dialog, kAcknowledgeChoice,
                       "bomb follow-up");
  }
  if (resolution.started) commit_original_bomb_event(*document_);
  return resolution.started;
}

bool Session::run_fire_offer_boundary() {
  // 10e8:0029 (N:1139).
  const auto fire = prepare_original_fire_event(*document_, part_,
                                                simulation_state_.day_phase);
  if (!fire.offered) return false;
  (void)event_dialog(fire.dialog, kAcknowledgeChoice, "fire");
  commit_original_fire_event(*document_, part_);
  focus_event_coordinate(fire.floor, fire.x);
  return true;
}

void Session::consume_cathedral_ceremony(std::int16_t floor,
                                         std::int16_t x,
                                         std::int32_t wave_resource) {
  focus_event_coordinate(floor, x);
  if (wave_resource != 0) sound(wave_resource, "cathedral");
}

void Session::consume_person_host_requests(
    const std::vector<OriginalPersonHostRequest>& requests) {
  // consume_original_person_host_requests (N:2326).
  for (const auto& request : requests) {
    switch (request.kind) {
      case OriginalPersonHostRequestKind::income_status:
        income_status(request.code);
        break;
      case OriginalPersonHostRequestKind::notification_status:
        notification_status(request.code);
        break;
      case OriginalPersonHostRequestKind::hotel_dialog:
        (void)event_dialog({request.code, request.argument, 10000},
                           kAcknowledgeChoice, "hotel");
        break;
    }
  }
}

void Session::consume_person_family_dispatch(
    const OriginalPersonFamilyDispatchResult& dispatch) {
  // consume_original_person_family_dispatch (N:2302).
  consume_person_host_requests(dispatch.host_requests);
  if (dispatch.security_effect.valid()) {
    focus_event_coordinate(dispatch.security_effect.floor,
                           dispatch.security_effect.x);
  }
  if (dispatch.cathedral_arrival &&
      dispatch.cathedral_arrival->status ==
          OriginalCathedralArrivalStatus::ceremony_started) {
    const auto& ceremony = *dispatch.cathedral_arrival;
    consume_cathedral_ceremony(ceremony.effect_floor,
                               static_cast<std::int16_t>(ceremony.effect_x),
                               ceremony.wave_resource);
  }
}

bool Session::dispatch_scheduler_call(const OriginalSimulationCall& call) {
  // Scheduler dispatch of run_original_idle_simulation (N:1324-1503), in
  // the emitted order. Returns whether the call dirtied the world.
  auto& document = *document_;
  const auto argument = [&call](std::size_t index) {
    return static_cast<std::uint16_t>(call.arguments[index]);
  };
  const auto at = [&call](std::uint16_t selector, std::uint16_t offset) {
    return call.selector == selector && call.offset == offset;
  };
  if (at(0x11c8U, 0x0167U) && call.arguments.size() == 3U) {
    sound(call.arguments[0], "scheduler");
    return false;
  }
  if (at(0x11c8U, 0x03abU)) {
    const auto resource = select_original_ambient_sound(
        document, flags_.sound_enabled, viewport_.x, viewport_.y,
        viewport_.width, viewport_.height);
    if (resource) sound(*resource, "ambient");
    return false;
  }
  if (at(0x1228U, 0x0968U)) {
    (void)advance_original_facilities_for_day(
        document, simulation_state_.calendar_phase);
    return true;
  }
  if (at(0x1228U, 0x0b59U)) {
    (void)advance_original_facilities_for_evening(document);
    return true;
  }
  if (at(0x1228U, 0x086bU)) {
    (void)prepare_original_facilities_for_night(document);
    return true;
  }
  if (at(0x1198U, 0x01abU)) {
    refresh_original_parking_for_day(document);
    return false;
  }
  if (at(0x1170U, 0x011fU)) {
    refresh_original_medical_for_day(document);
    return true;
  }
  if (at(0x1020U, 0x0dcbU)) {
    return raise_original_periodic_b406_flag(document);
  }
  if (at(0x1020U, 0x0e0bU)) {
    // 1020:0e1f sets the window-dirty word unconditionally.
    (void)clear_original_periodic_b406_flag(document);
    return true;
  }
  if (at(0x1240U, 0x01deU)) {
    (void)reset_original_periodic_b924_state(document);
    return false;
  }
  if (at(0x1060U, 0x003aU)) {
    reset_original_quarter_finance(document);
    return false;
  }
  if (at(0x1178U, 0x0b44U)) {
    charge_original_three_day_maintenance(document, maintenance_costs_,
                                          part_);
    return true;
  }
  if (at(0x1180U, 0x05afU)) {
    reset_original_entertainment_for_day(document, part_);
    return true;
  }
  if (at(0x1180U, 0x06a8U) && call.arguments.size() == 2U) {
    begin_original_entertainment_arrivals(document, argument(0),
                                          argument(1));
    return true;
  }
  if (at(0x1180U, 0x0826U) && call.arguments.size() == 2U) {
    advance_original_entertainment_show(document, argument(0), argument(1));
    return true;
  }
  if (at(0x1180U, 0x090aU) && call.arguments.size() == 2U) {
    const auto income = finish_original_entertainment_phase(
        document, part_, argument(0), argument(1));
    for (const auto code : income.codes) income_status(code);
    return true;
  }
  if (at(0x11a8U, 0x0184U)) {
    reset_original_commercial_for_day(document, part_);
    return true;
  }
  if (at(0x11a8U, 0x0250U)) {
    reset_original_restaurants_for_evening(document, part_);
    return true;
  }
  if (at(0x11a8U, 0x0554U)) {
    const auto income =
        close_original_nonrestaurant_commercial(document, part_);
    for (const auto code : income.codes) income_status(code);
    return true;
  }
  if (at(0x11a8U, 0x0603U)) {
    const auto income = close_original_restaurants_for_night(document, part_);
    for (const auto code : income.codes) income_status(code);
    return true;
  }
  if (at(0x1130U, 0x0000U)) {
    advance_original_tenants_at_midnight(document, part_, rent_income_);
    return true;
  }
  if (at(0x1130U, 0x0109U)) {
    advance_original_hotels_for_evening(document, part_);
    return true;
  }
  if (at(0x1130U, 0x01e2U)) {
    return repair_original_hotel_pair_states(
               document, simulation_state_.day_phase) != 0U;
  }
  if (at(0x1188U, 0x0977U)) {
    (void)remove_original_nightly_person_links(document);
    return false;
  }
  if (at(0x1188U, 0x0a20U)) {
    (void)remove_original_hotel_person_links(document);
    return false;
  }
  if (at(0x1088U, 0x00deU)) {
    const auto result = reset_original_recycling_for_day(document);
    if (result.play_transition_sound) sound(0x08e8, "recycling");
    if (result.notification_code != 0U) {
      notification_status(result.notification_code);
    }
    return result.touched != 0U;
  }
  if (at(0x1088U, 0x01d1U)) {
    return finish_original_recycling_day_start(document) != 0U;
  }
  if (at(0x1088U, 0x0000U) && call.arguments.size() == 1U) {
    const auto result = advance_original_recycling_phase(
        document, static_cast<std::uint8_t>(call.arguments[0]));
    if (result.notification_code != 0U) {
      notification_status(result.notification_code);
    }
    return result.touched != 0U;
  }
  if (at(0x11e8U, 0x0273U)) {
    const auto result = pulse_original_metro_effects(document);
    if (result.play_transition_sound) sound(0x271a, "metro");
    return result.touched != 0U;
  }
  if (at(0x11b8U, 0x0028U)) {
    const auto result = start_original_annual_effect(document);
    if (result.notification_code != 0U) {
      notification_status(result.notification_code);
    }
    return result.started;
  }
  if (at(0x1040U, 0x0000U)) {
    (void)reset_original_cathedral_for_day(document);
    return true;
  }
  if (at(0x1040U, 0x0179U)) {
    (void)close_original_cathedral_for_day(document);
    return true;
  }
  if (at(0x1220U, 0x0000U)) {
    (void)reset_original_people_for_day(document);
    return false;
  }
  if (at(0x1220U, 0x1059U)) {
    (void)sweep_original_people_transit(document,
                                        simulation_state_.frame_time);
    return true;
  }
  if (at(0x10c8U, 0x006eU)) return run_bomb_offer_boundary();
  if (at(0x10e8U, 0x0029U)) return run_fire_offer_boundary();
  throw std::runtime_error(
      "Recovered scheduler call has no SDL host dispatch");
}

bool Session::run_idle_pass() {
  // run_original_idle_simulation (N:1150). WAVMIX activation and the
  // TOPMOST window repair (1258:0195-023a) have no single-window equivalent.

  // 1258:023a: the effects palette runs on every empty-queue pass.
  if (step_effect_palette()) {
    main_backing_dirty_ = true;
    present_requested_ = true;
  }
  // 1258:0186 Find-marker timeout: the marker is only set by Find, which
  // this frontend does not have yet, so it can never expire here.
  if (document_) run_scheduler_pass();
  // A pending WM_PAINT is processed before the next empty-queue pass.
  if (present_requested_) present_main();
  const bool present = present_requested_;
  present_requested_ = false;
  return present;
}

void Session::run_scheduler_pass() {
  // 1258:0244-02d1: frozen while construction mode is off. Elevator-finger
  // capture does not exist yet.
  const auto idle_gate =
      original_idle_world_pass_plan(build_mode_enabled_, false, false);
  if (!idle_gate.run_scheduler) return;

  OriginalSimulationStep step{};
  const auto scheduler_now_ms = clock_ms_();
  if (!flags_.fast_mode_enabled ||
      native_fast_mode_frame_due(fast_frame_ms_, scheduler_now_ms)) {
    step = step_original_simulation(simulation_state_, *document_,
                                    original_coarse_tick(scheduler_now_ms),
                                    flags_.fast_mode_enabled);
    if (flags_.fast_mode_enabled && step.advanced) {
      fast_frame_ms_ = scheduler_now_ms;
    }
  }
  // 1090:03ab(0), the preview-only pass, only matters for the construction
  // outline, which is not drawn yet.
  if (!step.advanced) return;

  // 1258:026e: every scheduled far call, then 1200:0529's tail tick.
  bool world_changed = false;
  for (const auto& call : step.calls) {
    if (!original_simulation_call_supported(call)) {
      throw std::runtime_error(
          "Recovered scheduler emitted an unsupported native call");
    }
    world_changed = dispatch_scheduler_call(call) || world_changed;
  }
  finish_original_simulation_step(simulation_state_, coarse_tick());
  run_full_frame(world_changed);
}

void Session::run_full_frame(bool world_changed) {
  // 1090:03ab(1) (N:1512-1832). Audio checkpoints are omitted.
  auto& document = *document_;

  // 11f0:0211 construction queue.
  world_changed = step_original_pending_construction(document) ==
                      OriginalPendingStepStatus::activated ||
                  world_changed;

  // 1090:0439: 1140:002d rating step.
  const auto rating = step_original_rating_progress(
      document, part_, simulation_state_.calendar_phase,
      simulation_state_.day_phase);
  if (rating.notification_code) notification_status(*rating.notification_code);
  if (rating.promoted) {
    log_line("rating promoted to " + std::to_string(document.header.rating));
    if (rating.dialog.valid()) {
      (void)event_dialog(rating.dialog, kAcknowledgeChoice, "rating");
    }
    // 1140:010d(1) resets DS:783c to edit mode two.
    if (original_rating_command_refresh_plan(1U, false, false)
            .force_command_mode_two) {
      command_mode_ = 2U;
    }
    world_changed = true;
  }

  // Bomb, then Fire.
  world_changed =
      consume_bomb_action(advance_original_bomb_event(document, part_)) ||
      world_changed;
  if (original_fire_event_active(document)) {
    if (document.header.frame_time == 2000U) {
      world_changed =
          consume_fire_action(finish_original_fire_event(document)) ||
          world_changed;
    } else {
      if (original_fire_crew_offer_due(document, part_)) {
        const auto choice =
            event_dialog(original_fire_crew_offer(part_),
                         kFireCrewOfferChoice, "fire crew offer: no");
        world_changed = consume_fire_crew_resolution(
                            resolve_original_fire_crew_offer(
                                document, part_, choice, true)) ||
                        world_changed;
      }
      world_changed = consume_fire_action(
                          advance_original_fire_event(document, part_)) ||
                      world_changed;
    }
  }

  // First of four in-frame 1020:00cb calls (1090:0448).
  bool palette_recolor_required = step_effect_palette();

  if (original_emergency_people_pass_active(document)) {
    // 1090:0452-045e Security pass while Bomb or Fire is active.
    const auto security = step_original_security_people(document, part_);
    for (const auto& effect : security.effects) {
      focus_event_coordinate(effect.floor, effect.x);
    }
    world_changed = security.changed != 0U || world_changed;
  } else {
    // 1090:0460 normal one-sixteenth person pass.
    const auto people =
        step_original_translated_people(document, part_, rent_income_);
    consume_person_host_requests(people.host_requests);
    if (people.cathedral_ceremony) {
      const auto& ceremony = *people.cathedral_ceremony;
      consume_cathedral_ceremony(ceremony.effect_floor,
                                 static_cast<std::int16_t>(ceremony.effect_x),
                                 ceremony.wave_resource);
    }
    world_changed = people.changed != 0U || world_changed;
  }

  // Second 1020:00cb (1090:0465).
  palette_recolor_required = step_effect_palette() || palette_recolor_required;

  // 1090:046f Map cadence and 1090:04a7 preview restore: no Map panel or
  // construction outline yet.

  // 1090:04c0-0542 Elevator cars and passengers.
  const auto elevators = step_original_elevator_frame(
      document, part_, rent_income_, false,
      OriginalElevatorFrameHostHooks{
          [this] { sound(0x1772, "elevator"); },
          [this](const OriginalPersonFamilyDispatchResult& dispatch) {
            consume_person_family_dispatch(dispatch);
          },
          {}});
  transfer_visuals_.clear();
  for (const auto& event : elevators.transfer_visuals) {
    transfer_visuals_.push_back(OriginalElevatorTransferVisual{
        event.elevator_index, event.floor, event.boarding,
        event.direction_up, static_cast<std::uint32_t>(event.person_index)});
  }
  world_changed = elevators.changed || world_changed;
  // 1090:0551 10c0:002e Stair/Escalator animation dirty scan.
  world_changed = original_vertical_transport_animation_active(document) ||
                  world_changed;
  // 1090:0567 visible facility people (consumes RNG).
  world_changed =
      advance_main_surface_state(OriginalMainSurfacePass::simulation_frame) ||
      world_changed;
  // 1090:05db 11b8:0060 annual effect.
  world_changed = advance_original_annual_effect(document) || world_changed;

  // 1090:0580 renderer, then 1090:060f/0615 time palette and third 00cb.
  rebuild_main(OriginalMainSurfacePass::simulation_frame, false,
               world_changed);
  const bool time_palette_changed =
      refresh_original_time_palette(resources_, document, palette_runtime_);
  const bool effect_palette_changed = step_effect_palette();
  palette_recolor_required = time_palette_changed || effect_palette_changed ||
                             palette_recolor_required;
  if (palette_recolor_required) {
    rebuild_main(OriginalMainSurfacePass::palette_repaint, false);
  }

  // Event completion writes the b3de clock directly; resync the mirror.
  simulation_state_.frame_time = document.header.frame_time;
  simulation_state_.current_day = document.header.current_day;
  simulation_state_.day_phase =
      original_day_phase(simulation_state_.frame_time);
  simulation_state_.calendar_phase =
      original_calendar_phase(simulation_state_.current_day);

  auto presentation =
      original_full_frame_auxiliary_presentation_plan(world_changed);
  if (palette_recolor_required) presentation.present_main_direct = true;
  for (const auto tail_step : original_full_frame_tail_order()) {
    switch (tail_step) {
      case OriginalFullFrameTailStep::present_main_if_dirty:
        if (presentation.present_main_direct) present_main();
        break;
      case OriginalFullFrameTailStep::invalidate_command_if_dirty:
        break;
      case OriginalFullFrameTailStep::present_info_direct:
        if (presentation.present_info_direct) {
          // The Info painter is not drawn yet, but 1118:0143 still drains
          // the Hotel checkout latch once per frame.
          if (consume_original_hotel_checkout_presentation(document)
                  .play_cash_sound) {
            sound(10013, "hotel checkout");
          }
        }
        break;
      case OriginalFullFrameTailStep::expire_info_status:
        (void)expire_original_info_status(info_status_, coarse_tick());
        break;
      case OriginalFullFrameTailStep::step_final_effect_palette:
        // Fourth 1020:00cb (1090:06dc).
        if (step_effect_palette()) {
          rebuild_main(OriginalMainSurfacePass::palette_repaint, false);
          present_main();
        }
        break;
      case OriginalFullFrameTailStep::final_audio_checkpoint:
        break;
    }
  }
}

std::uint32_t Session::milliseconds_until_due() const {
  const std::uint32_t now = clock_ms_();
  const auto remaining = [now](std::uint32_t due_ms) -> std::uint32_t {
    const auto delta = static_cast<std::int32_t>(due_ms - now);
    return delta > 0 ? static_cast<std::uint32_t>(delta) : 0U;
  };
  std::uint32_t wait = 100U;
  if (document_ && build_mode_enabled_) {
    wait = std::min(
        wait, flags_.fast_mode_enabled
                  ? remaining(fast_frame_ms_ + kNativeFastModeFramePeriodMs)
                  : remaining((simulation_state_.last_tick +
                               kOriginalSimulationGateTicks)
                              << 4U));
  }
  if (document_ && flags_.effects_animation_enabled &&
      palette_runtime_.initialized) {
    wait = std::min(wait, remaining((palette_runtime_.last_effect_tick + 15U)
                                    << 4U));
  }
  return wait;
}

}  // namespace simtower::sdl
