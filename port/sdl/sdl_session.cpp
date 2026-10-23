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
  // WinMain: the portable span constructor replaces from_current_module.
  resources_ = OriginalResources(std::span<const std::byte>(pack_));

  // validate_original_startup_data: the same portable loader the
  // Win32 host calls, so a truncated or mismatched pack fails here. The
  // command ratings feed the Command panel, which is not drawn yet.
  auto startup = load_original_startup_data(resources_);
  part_ = std::move(startup.part);
  construction_costs_ = std::move(startup.construction_costs);
  rent_income_ = std::move(startup.rent_income);
  maintenance_costs_ = std::move(startup.maintenance_costs);
  command_ratings_ = std::move(startup.command_ratings);

  // Deliberately omitted Win32 startup steps (WinMain), none of which
  // touches the document RNG or simulation state:
  // - WAVMIX init, SIMTOWER.INI profile, logical palette, font bank, cursors,
  //   icon, accelerators and menu (no audio, menus or fonts yet);
  // - the BITMAP/128 -> 256 splash and its 180-tick wait (1128:0042-00a7);
  // - the bootstrap reset_original_command_state(true),
  //   refresh_original_rating_command(rating, 0) and Map-focus adjustment
  //   (10d0:0ac2): New/Open reset the command state again before any pass;
  // - the fire-menu update and WAVE/20000.
  // WinMain's base palette, then its never-stored bootstrap tower
  // (its frame 0x09e5 selects the CLUT 1002->1001 transition).
  reset_original_palette_runtime(resources_, nullptr, palette_runtime_,
                                 coarse_tick_now());
  const auto startup_bootstrap = make_original_new_tdt();
  reset_original_palette_runtime(resources_, &startup_bootstrap,
                                 palette_runtime_, coarse_tick_now());
}

std::uint32_t Session::coarse_tick_now() const {
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
    // Window creation: 1128:08d6 (create_original_windows) installs the
    // initial view, centred horizontally with the bottom 324 px above the
    // world floor, while the DS:02a4 gate (g_main_message_runtime_initialized,
    // set late in WinMain) makes the creation-time WM_SIZE a no-op. So only
    // mark the backing dirty for the first paint.
    const auto initial =
        original_initial_view(viewport_.width, viewport_.height);
    set_viewport_origin(initial.x, initial.y);
    main_backing_dirty_ = true;
    present_requested_ = true;
    return;
  }
  // Live restore/maximize WM_SIZE (main_window_proc): 1158:05ef clamps the
  // saved origin to the new ranges, then completes through the synchronous
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
  // scroll_original this runs for every accepted request,
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
  // reset_original_simulation_state.
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
  // The first Fast Mode frame is immediately eligible, as in
  // reset_original_simulation_state.
  fast_frame_ms_ = now - kNativeFastModeFramePeriodMs;
  reset_original_palette_runtime(resources_, &*document_, palette_runtime_,
                                 original_coarse_tick(now));
}

void Session::reset_command_state() {
  // reset_original_command_state fields this frontend consults.
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
  // begin_original_new_tower: 10d0:001d -> 086c keeps the prior
  // view, so the fresh header's zero view words are replaced first.
  auto document = make_original_new_tdt();
  document.header.view_x = static_cast<std::uint16_t>(viewport_.x);
  document.header.view_y = static_cast<std::uint16_t>(viewport_.y);
  install_document(std::move(document));
  log_line("new tower");
}

void Session::open_tower(const std::filesystem::path& path) {
  // load_original_tower_from_path: an empty tower is installed
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
  // rebuild_original_main_backing without Elevator Control
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
  // advance_original_main_surface_state.
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
  // focus_original_event_coordinate -> set_original_view, whose
  // camera refresh is a synchronous 1080:0a1e(1). Not cosmetic: it advances
  // the sky decorations and visible facility people.
  const auto view = original_facility_focus_view(x, floor, viewport_.width,
                                                 viewport_.height);
  ++host_requests_.focus;
  log_request(RequestKind::focus, {floor, x});
  tally("focus");
  log_line("focus floor " + std::to_string(floor) + " x " +
           std::to_string(x));
  set_viewport_origin(view.x, view.y);
  rebuild_main(OriginalMainSurfacePass::rebuild_with_sky, true);
  present_requested_ = true;
}

std::uint16_t Session::log_dialog(const OriginalEventDialogRequest& dialog,
                                  std::uint16_t choice,
                                  std::string_view meaning) {
  // run_original_event_dialog stand-in.
  ++host_requests_.dialogs;
  log_request(RequestKind::dialog, {dialog.dialog_id, dialog.argument,
                                    dialog.wave_resource, choice});
  tally("dialog " + std::to_string(dialog.dialog_id) + " (" +
        std::string(meaning) + ")");
  log_line("dialog " + std::to_string(dialog.dialog_id) + " argument " +
           std::to_string(dialog.argument) + " wave " +
           std::to_string(dialog.wave_resource) + " -> " +
           std::to_string(choice) + " (" + std::string(meaning) + ")");
  return choice;
}

void Session::sound(std::int32_t resource, std::string_view source) {
  ++host_requests_.sounds;
  log_request(RequestKind::sound, {resource});
  tally("sound " + std::string(source));
  if (flags_.log_sounds) {
    log_line("sound " + std::to_string(resource) + " (" +
             std::string(source) + ")");
  }
}

void Session::notification_status(std::uint16_t code) {
  ++host_requests_.status;
  const bool shown = set_original_info_notification_status(
      resources_, info_status_, code, coarse_tick_now());
  log_request(RequestKind::notification_status, {code, shown});
  tally("notification status");
  if (shown) {
    log_line("info notification " + std::to_string(code) + ": " +
             info_status_.text);
  }
}

void Session::income_status(std::uint16_t code) {
  ++host_requests_.status;
  const bool shown = set_original_info_income_status(resources_, info_status_,
                                                     code, coarse_tick_now());
  log_request(RequestKind::income_status, {code, shown});
  tally("income status");
}

void Session::log_fire_menu(bool enabled) {
  log_request(RequestKind::fire_menu, {enabled});
  tally("fire menu");
  log_line(std::string("fire menu ") + (enabled ? "enabled" : "disabled"));
}

OriginalIdlePassContext Session::idle_pass_context() {
  return {document_ ? &*document_ : nullptr, simulation_state_,
          fast_frame_ms_,                    transfer_visuals_,
          part_,                             rent_income_,
          maintenance_costs_};
}

bool Session::run_idle_pass() {
  // run_original_idle_simulation. The 1258:0195-023a prelude (WAVMIX
  // activation, TOPMOST window repair) has no single-window equivalent, and
  // neither has the 1258:0285-02d9 epilogue (no Elevator Control or audio
  // yet). The shared pass runs 1258:023a through 1090:03ab(1), calling back
  // into the hooks below.
  ++idle_passes_;
  log_request(RequestKind::pass_begin,
              {static_cast<std::int64_t>(idle_passes_)});
  auto context = idle_pass_context();
  (void)run_original_idle_pass(context, *this);
  // A pending WM_PAINT is processed before the next empty-queue pass.
  if (present_requested_) present_main();
  const bool present = present_requested_;
  present_requested_ = false;
  log_request(RequestKind::pass_end, {present});
  return present;
}

std::uint32_t Session::coarse_tick(std::uint32_t now_ms) {
  // 1208:05e6. The Win32 wrapper also pumps audio here; there is none yet.
  return original_coarse_tick(now_ms);
}

void Session::expire_find_marker() {
  // 1258:0186 Find-marker timeout: the marker is only set by Find, which
  // this frontend does not have yet, so it can never expire here.
}

OriginalIdleView Session::main_view() {
  return {viewport_.x, viewport_.y, viewport_.width, viewport_.height};
}

bool Session::step_effect_palette() {
  // 1020:00cb. No AnimatePalette sync: colours apply while composing.
  return document_ &&
         step_original_effect_palette(*document_, palette_runtime_,
                                      flags_.effects_animation_enabled,
                                      coarse_tick_now());
}

bool Session::refresh_time_palette() {
  return refresh_original_time_palette(resources_, *document_,
                                       palette_runtime_);
}

bool Session::advance_visible_people() {
  return advance_main_surface_state(OriginalMainSurfacePass::simulation_frame);
}

std::uint16_t Session::event_dialog(const OriginalEventDialogRequest& dialog,
                                    OriginalIdleDialog kind) {
  switch (kind) {
    case OriginalIdleDialog::bomb_offer:
      return log_dialog(dialog, kBombOfferChoice, "bomb offer: search");
    case OriginalIdleDialog::bomb_followup:
      return log_dialog(dialog, kAcknowledgeChoice, "bomb follow-up");
    case OriginalIdleDialog::bomb_event:
      return log_dialog(dialog, kAcknowledgeChoice, "bomb");
    case OriginalIdleDialog::fire_offer:
    case OriginalIdleDialog::fire_event:
      return log_dialog(dialog, kAcknowledgeChoice, "fire");
    case OriginalIdleDialog::fire_crew_offer:
      return log_dialog(dialog, kFireCrewOfferChoice, "fire crew offer: no");
    case OriginalIdleDialog::fire_crew_followup:
      return log_dialog(dialog, kAcknowledgeChoice, "fire crew follow-up");
    case OriginalIdleDialog::hotel:
      return log_dialog(dialog, kAcknowledgeChoice, "hotel");
    case OriginalIdleDialog::rating:
      return log_dialog(dialog, kAcknowledgeChoice, "rating");
  }
  return log_dialog(dialog, kAcknowledgeChoice, "event");
}

void Session::focus(std::int16_t floor, std::int16_t x) {
  focus_event_coordinate(floor, x);
}

void Session::expire_info_status() {
  (void)expire_original_info_status(info_status_, coarse_tick_now());
}

void Session::play_sound(std::int32_t resource,
                         std::uint16_t /*repeat*/,
                         std::uint16_t /*priority*/,
                         OriginalIdleSound source) {
  switch (source) {
    case OriginalIdleSound::scheduler:
      return sound(resource, "scheduler");
    case OriginalIdleSound::ambient:
      return sound(resource, "ambient");
    case OriginalIdleSound::recycling:
      return sound(resource, "recycling");
    case OriginalIdleSound::metro:
      return sound(resource, "metro");
    case OriginalIdleSound::bomb_payment:
      return sound(resource, "bomb payment");
    case OriginalIdleSound::cathedral:
      return sound(resource, "cathedral");
    case OriginalIdleSound::elevator:
      return sound(resource, "elevator");
    case OriginalIdleSound::hotel_checkout:
      return sound(resource, "hotel checkout");
  }
  sound(resource, "other");
}

void Session::play_event_sounds(
    std::span<const OriginalEventSoundRequest> requests) {
  for (const auto& request : requests) sound(request.resource, "event");
}

void Session::stop_all_audio(bool /*force*/) {
  // No audio yet: the Cathedral ceremony's channel stop is a no-op.
}

void Session::idle_palette_changed() {
  main_backing_dirty_ = true;
  present_requested_ = true;
}

void Session::rating_promoted_before_dialog(std::uint16_t rating) {
  log_request(RequestKind::rating_promoted, {rating});
  tally("rating promoted");
  log_line("rating promoted to " + std::to_string(rating));
}

void Session::rating_promoted_after_dialog(std::uint16_t /*rating*/) {
  // 1140:010d(1) resets DS:783c to edit mode two.
  if (original_rating_command_refresh_plan(1U, false, false)
          .force_command_mode_two) {
    command_mode_ = 2U;
    log_request(RequestKind::command_mode, {command_mode_});
  }
}

bool Session::render_full_frame(bool renderer_surface_dirty) {
  // 1090:0580 renderer. No construction outline yet, so the preview
  // rectangle never changes.
  rebuild_main(OriginalMainSurfacePass::simulation_frame, false,
               renderer_surface_dirty);
  return false;
}

void Session::palette_recolor_rebuild() {
  rebuild_main(OriginalMainSurfacePass::palette_repaint, false);
}

void Session::final_effect_palette_changed() {
  // Fourth 1020:00cb (1090:06dc).
  rebuild_main(OriginalMainSurfacePass::palette_repaint, false);
  present_main();
}

void Session::log_request(RequestKind kind,
                          std::initializer_list<std::int64_t> fields) {
  request_log_.byte(static_cast<std::uint8_t>(kind));
  request_log_.unsigned_integer(fields.size());
  for (const auto field : fields) request_log_.integer(field);
  ++request_log_entries_;
  if (flags_.trace_requests) {
    std::string line = "request " + std::to_string(static_cast<int>(kind));
    for (const auto field : fields) line += " " + std::to_string(field);
    std::printf("%s\n", line.c_str());
  }
}

ScenarioSummary Session::apply_scenario(std::string_view name) {
  if (!document_) throw std::runtime_error("scenario needs a tower");
  auto summary =
      build_scenario(name, *document_, part_, construction_costs_);
  // As apply_original_construction_mutation_presentation: the model changed,
  // so the backing is stale. No synchronous rebuild (no RNG draw) here.
  main_backing_dirty_ = true;
  present_requested_ = true;
  return summary;
}

bool Session::trigger_event(std::string_view name) {
  if (!document_) throw std::runtime_error("event needs a tower");
  bool started{};
  auto context = idle_pass_context();
  if (name == "bomb") {
    started = run_original_bomb_offer_boundary(context, *this);
  } else if (name == "fire") {
    started = run_original_fire_offer_boundary(context, *this);
  } else {
    throw std::runtime_error("unknown event: " + std::string(name));
  }
  // mark_original_world_interaction_changed: the backing is stale.
  if (started) {
    main_backing_dirty_ = true;
    present_requested_ = true;
  }
  return started;
}

SessionDigest Session::state_digest() const {
  SessionDigest digest{};
  if (document_) {
    Fnv64 tower;
    tower.bytes(serialize_original_tdt(*document_));
    digest.tower = tower.value();

    // OriginalTdtDocument members that serialize_original_tdt omits.
    Fnv64 process;
    process.unsigned_integer(document_->random_state);
    process.unsigned_integer(document_->restaurant_service_variant);
    process.unsigned_integer(document_->retail_service_variant);
    process.unsigned_integer(document_->fast_food_service_variant);
    process.boolean(document_->security_event_accelerated);
    process.unsigned_integer(document_->hotel_checkout_count);
    process.boolean(document_->hotel_checkout_effect_cadence);
    process.boolean(document_->hotel_checkout_effect_active);
    process.bytes(document_->medical_route_index);
    digest.process = process.value();
  }

  Fnv64 host;
  host.boolean(document_.has_value());
  host.unsigned_integer(simulation_state_.frame_time);
  host.integer(simulation_state_.current_day);
  host.integer(simulation_state_.day_phase);
  host.unsigned_integer(simulation_state_.calendar_phase);
  host.unsigned_integer(simulation_state_.last_tick);
  host.unsigned_integer(fast_frame_ms_);
  host.boolean(palette_runtime_.initialized);
  host.unsigned_integer(palette_runtime_.last_effect_tick);
  host.unsigned_integer(palette_runtime_.effect_counter);
  host.unsigned_integer(palette_runtime_.colors.size());
  for (const auto color : palette_runtime_.colors) {
    host.unsigned_integer(color);
  }
  for (const auto& placement : sky_decorations_.placements) {
    host.integer(placement.bitmap_index);
    host.integer(placement.left);
    host.integer(placement.top);
    host.integer(placement.right);
    host.integer(placement.bottom);
  }
  host.text(info_status_.text);
  host.unsigned_integer(info_status_.started_tick);
  host.integer(info_status_.priority);
  host.unsigned_integer(transfer_visuals_.size());
  for (const auto& visual : transfer_visuals_) {
    host.unsigned_integer(visual.elevator_index);
    host.integer(visual.floor);
    host.boolean(visual.boarding);
    host.boolean(visual.direction_up);
    host.unsigned_integer(visual.person_index);
  }
  host.boolean(build_mode_enabled_);
  host.unsigned_integer(command_mode_);
  host.unsigned_integer(map_mode_);
  host.integer(viewport_.x);
  host.integer(viewport_.y);
  host.integer(viewport_.width);
  host.integer(viewport_.height);
  host.boolean(main_backing_dirty_);
  host.boolean(present_requested_);
  host.unsigned_integer(host_requests_.dialogs);
  host.unsigned_integer(host_requests_.sounds);
  host.unsigned_integer(host_requests_.focus);
  host.unsigned_integer(host_requests_.status);
  digest.host = host.value();

  Fnv64 requests = request_log_;
  requests.unsigned_integer(request_log_entries_);
  requests.unsigned_integer(idle_passes_);
  digest.requests = requests.value();

  Fnv64 combined;
  combined.unsigned_integer(digest.tower);
  combined.unsigned_integer(digest.process);
  combined.unsigned_integer(digest.host);
  combined.unsigned_integer(digest.requests);
  digest.combined = combined.value();
  return digest;
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
