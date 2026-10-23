#include "original_idle_pass.hpp"

#include "original_construction.hpp"
#include "original_map.hpp"
#include "original_time.hpp"
#include "original_ui.hpp"

#include <stdexcept>

namespace simtower {
namespace {

bool apply_original_event_damage(
    OriginalIdlePassContext& context,
    OriginalIdlePassHost& host,
    std::span<const OriginalFacilityDamageRequest> requests) {
  if (!context.document || requests.empty()) {
    return false;
  }
  const auto result = apply_original_facility_damage_sequence(
      *context.document, context.rent_income, requests);
  host.play_event_sounds(result.sound_requests);
  for (const auto code : result.notification_codes) {
    host.notification_status(code);
  }
  return result.changed != 0U;
}

bool consume_original_bomb_action(OriginalIdlePassContext& context,
                                  OriginalIdlePassHost& host,
                                  const OriginalEventActionResult& action) {
  if (!context.document) {
    return false;
  }
  auto& document = *context.document;
  bool changed = action.changed;
  // sequence() is a span into the plan, so the plan must stay a named local
  // for the whole loop.
  const auto host_plan = original_bomb_event_host_plan(action);
  for (const auto operation : host_plan.sequence()) {
    switch (operation) {
      case OriginalEventHostOperation::play_sounds:
        host.play_event_sounds(action.sound_requests);
        break;
      case OriginalEventHostOperation::apply_damage:
        changed = apply_original_event_damage(context, host,
                                              action.damage_requests) ||
                  changed;
        break;
      case OriginalEventHostOperation::dispatch_security:
        dispatch_original_security_response(document,
                                            action.security_dispatch_flags);
        changed = true;
        break;
      case OriginalEventHostOperation::focus_coordinate:
        host.focus(action.focus_floor, action.focus_x);
        break;
      case OriginalEventHostOperation::show_dialog:
        (void)host.event_dialog(action.dialog, OriginalIdleDialog::bomb_event);
        break;
      case OriginalEventHostOperation::complete_deferred_action:
        complete_original_event_action(document, action.deferred_completion);
        changed = true;
        break;
      case OriginalEventHostOperation::update_fire_menu:
        break;
    }
  }
  return changed;
}

bool consume_original_fire_action(OriginalIdlePassContext& context,
                                  OriginalIdlePassHost& host,
                                  const OriginalEventActionResult& action) {
  if (!context.document) {
    return false;
  }
  auto& document = *context.document;
  bool changed = action.changed;
  // Named for the same reason as in consume_original_bomb_action.
  const auto host_plan = original_fire_event_host_plan(action);
  for (const auto operation : host_plan.sequence()) {
    switch (operation) {
      case OriginalEventHostOperation::apply_damage:
        changed = apply_original_event_damage(context, host,
                                              action.damage_requests) ||
                  changed;
        break;
      case OriginalEventHostOperation::play_sounds:
        host.play_event_sounds(action.sound_requests);
        break;
      case OriginalEventHostOperation::update_fire_menu:
        host.fire_menu_enabled(*action.fire_menu_enabled);
        break;
      case OriginalEventHostOperation::show_dialog:
        (void)host.event_dialog(action.dialog, OriginalIdleDialog::fire_event);
        break;
      case OriginalEventHostOperation::complete_deferred_action:
        complete_original_event_action(document, action.deferred_completion);
        changed = true;
        break;
      case OriginalEventHostOperation::dispatch_security:
      case OriginalEventHostOperation::focus_coordinate:
        break;
    }
  }
  return changed;
}

void consume_original_cathedral_ceremony(
    OriginalIdlePassHost& host,
    const OriginalCathedralArrivalResult& ceremony) {
  host.focus(ceremony.effect_floor,
             static_cast<std::int16_t>(ceremony.effect_x));
  if (ceremony.stop_both_audio_channels) host.stop_all_audio(true);
  if (ceremony.wave_resource != 0) {
    host.play_sound(ceremony.wave_resource, ceremony.wave_repeat,
                    ceremony.wave_priority, OriginalIdleSound::cathedral);
  }
}

// One far call emitted by 1200:0196. Returns whether it raised the shared
// world-dirty word; the caller folds it in after the call has run.
bool dispatch_original_scheduler_call(OriginalIdlePassContext& context,
                                      OriginalIdlePassHost& host,
                                      const OriginalSimulationCall& call) {
  auto& document = *context.document;
  const auto& part = context.part;
  const auto argument = [&call](std::size_t index) {
    return static_cast<std::uint16_t>(call.arguments[index]);
  };
  const auto at = [&call](std::uint16_t selector, std::uint16_t offset) {
    return call.selector == selector && call.offset == offset;
  };
  if (at(0x11c8U, 0x0167U) && call.arguments.size() == 3U) {
    host.play_sound(call.arguments[0], argument(1), argument(2),
                    OriginalIdleSound::scheduler);
    return false;
  }
  if (at(0x11c8U, 0x03abU)) {
    const auto view = host.main_view();
    const auto resource = select_original_ambient_sound(
        document, host.ambient_sound_enabled(), view.x, view.y, view.width,
        view.height);
    if (resource) {
      host.play_sound(*resource, 0U, 1U, OriginalIdleSound::ambient);
    }
    return false;
  }
  if (at(0x1228U, 0x0968U)) {
    (void)advance_original_facilities_for_day(
        document, context.simulation.calendar_phase);
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
    (void)clear_original_periodic_b406_flag(document);
    // 1020:0e1f sets the original window-dirty word unconditionally.
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
    charge_original_three_day_maintenance(document,
                                          context.maintenance_costs, part);
    return true;
  }
  if (at(0x1180U, 0x05afU)) {
    reset_original_entertainment_for_day(document, part);
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
        document, part, argument(0), argument(1));
    for (const auto code : income.codes) host.income_status(code);
    return true;
  }
  if (at(0x11a8U, 0x0184U)) {
    reset_original_commercial_for_day(document, part);
    return true;
  }
  if (at(0x11a8U, 0x0250U)) {
    reset_original_restaurants_for_evening(document, part);
    return true;
  }
  if (at(0x11a8U, 0x0554U)) {
    const auto income =
        close_original_nonrestaurant_commercial(document, part);
    for (const auto code : income.codes) host.income_status(code);
    return true;
  }
  if (at(0x11a8U, 0x0603U)) {
    const auto income = close_original_restaurants_for_night(document, part);
    for (const auto code : income.codes) host.income_status(code);
    return true;
  }
  if (at(0x1130U, 0x0000U)) {
    advance_original_tenants_at_midnight(document, part,
                                         context.rent_income);
    return true;
  }
  if (at(0x1130U, 0x0109U)) {
    advance_original_hotels_for_evening(document, part);
    return true;
  }
  if (at(0x1130U, 0x01e2U)) {
    return repair_original_hotel_pair_states(
               document, context.simulation.day_phase) != 0U;
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
    if (result.play_transition_sound) {
      host.play_sound(0x08e8, 0U, 1U, OriginalIdleSound::recycling);
    }
    if (result.notification_code != 0U) {
      host.notification_status(result.notification_code);
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
      host.notification_status(result.notification_code);
    }
    return result.touched != 0U;
  }
  if (at(0x11e8U, 0x0273U)) {
    const auto result = pulse_original_metro_effects(document);
    if (result.play_transition_sound) {
      host.play_sound(0x271a, 0U, 1U, OriginalIdleSound::metro);
    }
    return result.touched != 0U;
  }
  if (at(0x11b8U, 0x0028U)) {
    const auto result = start_original_annual_effect(document);
    if (result.notification_code != 0U) {
      host.notification_status(result.notification_code);
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
                                        context.simulation.frame_time);
    return true;
  }
  if (at(0x10c8U, 0x006eU)) {
    return run_original_bomb_offer_boundary(context, host);
  }
  if (at(0x10e8U, 0x0029U)) {
    return run_original_fire_offer_boundary(context, host);
  }
  throw std::runtime_error(
      "Recovered scheduler call has no native host dispatch");
}

// 1090:03ab(1) after the scheduler: construction queue, rating, Bomb/Fire,
// Security or normal people, Elevator cars, annual effects, the renderer,
// the time palette and the presentation tail.
void run_original_full_frame(OriginalIdlePassContext& context,
                             OriginalIdlePassHost& host,
                             bool world_changed) {
  auto& document = *context.document;
  auto& simulation = context.simulation;
  const auto& part = context.part;

  // Direct 1090:042a checkpoint before the full-frame construction queue.
  host.checkpoint(OriginalIdleCheckpoint::scheduler_tail);
  // Exact leading order inside 1090:03ab(1): 11f0:0211 construction queue,
  // then rating, Bomb/Fire, Security, normal people, Elevator cars, annual
  // effects, and ambient audio in their recovered host order.
  const auto pending = step_original_pending_construction(document);
  world_changed = world_changed ||
                  pending == OriginalPendingStepStatus::activated;
  // Direct 1090:0434 checkpoint between construction activation and rating.
  host.checkpoint(OriginalIdleCheckpoint::construction);

  // Exact 1090:0439 position: after the pending-construction pass and before
  // Bomb/Fire dispatch, 1140:002d evaluates one population-rating step and
  // 1148:007e enforces its persisted prerequisites. The modal is a host-only
  // boundary; all rating/VIP/latch mutation has already occurred headlessly.
  const auto rating = step_original_rating_progress(
      document, part, simulation.calendar_phase, simulation.day_phase);
  if (rating.notification_code) {
    host.notification_status(*rating.notification_code);
  }
  if (rating.promoted) {
    // 1140:00a8 is the rating-six form of this same promotion boundary: set
    // the final rating, present its modal, select the rating command layout,
    // resize Command, and repaint Info. Native state mutation occurs in the
    // headless rating step immediately above; the host provides its UI half.
    host.rating_promoted_before_dialog(document.header.rating);
    if (rating.dialog.valid()) {
      (void)host.event_dialog(rating.dialog, OriginalIdleDialog::rating);
    }
    // Both 1140:002d and the rating-six 1140:00a8 pass one to 1140:010d,
    // which resets DS:783c to edit mode two.
    host.rating_promoted_after_dialog(document.header.rating);
    world_changed = true;
  }
  // Direct 1090:043e checkpoint after rating (including its modal boundary)
  // and before Bomb/Fire progression.
  host.checkpoint(OriginalIdleCheckpoint::rating);

  const auto bomb = advance_original_bomb_event(document, part);
  world_changed =
      consume_original_bomb_action(context, host, bomb) || world_changed;

  if (original_fire_event_active(document)) {
    if (document.header.frame_time == 2000U) {
      world_changed =
          consume_original_fire_action(
              context, host, finish_original_fire_event(document)) ||
          world_changed;
    } else {
      if (original_fire_crew_offer_due(document, part)) {
        const auto choice =
            host.event_dialog(original_fire_crew_offer(part),
                              OriginalIdleDialog::fire_crew_offer);
        const auto resolution =
            resolve_original_fire_crew_offer(document, part, choice, true);
        world_changed =
            consume_original_fire_crew_resolution(context, host,
                                                  resolution) ||
            world_changed;
      }
      world_changed =
          consume_original_fire_action(
              context, host, advance_original_fire_event(document, part)) ||
          world_changed;
    }
  }

  // First 1020:00cb call at 1090:0448, after Bomb/Fire and before Security.
  // The original calls the same 15-coarse-tick (~240-ms)-gated routine four
  // times per frame; long frames can therefore advance it again between
  // expensive passes.
  bool palette_recolor_required = host.step_effect_palette();
  // Direct 1090:044d checkpoint after the first in-frame effects pass.
  host.checkpoint(OriginalIdleCheckpoint::first_effect_palette);

  if (original_emergency_people_pass_active(document)) {
    // 1090:0452-045e selects the type-14 emergency tenant/person scan only
    // while Bomb bit zero or Fire bit three is active. The translated family
    // returns 1080:0000 requests explicitly so this host boundary remains the
    // only view mutation.
    const auto security = step_original_security_people(document, part);
    for (const auto& effect : security.effects) {
      host.focus(effect.floor, effect.x);
    }
    world_changed = security.changed != 0U || world_changed;
  } else {
    // With both event bits clear, 1090:0460 runs the normal one-sixteenth
    // person pass instead of 0f85. The translated Hotel, commercial,
    // Housekeeping, Metro, Cathedral, and Movie/Party wrappers share one
    // ordered scan so route, finance, service, and queue mutations retain
    // original person-index order. Cross-family Elevator timeouts execute the
    // translated 1637/1b41/1aed ring path in that same pass.
    const auto people = step_original_translated_people(
        document, part, context.rent_income);
    consume_original_person_host_requests(host, people.host_requests);
    if (people.cathedral_ceremony) {
      consume_original_cathedral_ceremony(host, *people.cathedral_ceremony);
    }
    world_changed = people.changed != 0U || world_changed;
  }

  // Second 1020:00cb call at 1090:0465, after the normal people pass.
  palette_recolor_required =
      host.step_effect_palette() || palette_recolor_required;
  // Direct 1090:046a checkpoint after the selected people pass and second
  // effects pass.
  host.checkpoint(OriginalIdleCheckpoint::people);

  // 1090:046f-047c calls 1080:09c3 every sixteen clock ticks. That routine
  // invalidates and synchronously repaints the Map even when no simulation
  // mutation raised the shared world-dirty flag, advancing BITMAP/352's
  // horizontally cycled background on its original cadence.
  if (original_map_animation_refresh_due(document.header.frame_time)) {
    host.map_cadence_refresh();
  }
  // Direct 1090:0481 checkpoint after the independent Map cadence branch.
  host.checkpoint(OriginalIdleCheckpoint::map_cadence);
  // 1090:04a7 enters 11f8:3b94 after saving the old DS:77ac rectangle. Its
  // two nested checkpoints occur only when a nonempty outline is actually
  // restored outside Elevator-Control isolation.
  host.restore_preview();
  // Direct 1090:04b1 checkpoint after the backing/annual scratch restore and
  // before the per-Elevator state loops.
  host.checkpoint(OriginalIdleCheckpoint::preview_restore);

  // Exact 1090:04c0-0542 order: each used Elevator advances all eight cars,
  // runs all eight 1210:07a6/0351 passenger pairs, and enters 11e0:0e84
  // before the next Elevator. 10a8's process-local cache owns two entries per
  // visible floor/Elevator pair and consumes them in one top-to-bottom,
  // left-to-right paint.
  const auto elevators = step_original_elevator_frame(
      document, part, context.rent_income, host.elevator_isolation_active(),
      OriginalElevatorFrameHostHooks{
          [&host] {
            host.play_sound(0x1772, 0U, 0U, OriginalIdleSound::elevator);
          },
          [&host](const OriginalPersonFamilyDispatchResult& dispatch) {
            consume_original_person_family_dispatch(host, dispatch);
          },
          [&host] { host.checkpoint(OriginalIdleCheckpoint::elevator); }});
  context.transfer_visuals.clear();
  for (const auto& event : elevators.transfer_visuals) {
    context.transfer_visuals.push_back(OriginalElevatorTransferVisual{
        event.elevator_index, event.floor, event.boarding,
        event.direction_up, static_cast<std::uint32_t>(event.person_index)});
  }
  world_changed = elevators.changed || world_changed;
  // Exact 1090:0551 position: 10c0:002e raises the shared repaint flag when
  // any used Stair/Escalator has a nonzero wrapping word_6+word_8 passenger
  // count. This keeps its frame_time-selected BITMAP animation advancing on
  // frames where no unrelated simulation mutation dirties the world.
  world_changed =
      original_vertical_transport_animation_active(document) || world_changed;
  // Direct 1090:0556 checkpoint after the Stair/Escalator dirty scan.
  host.checkpoint(OriginalIdleCheckpoint::vertical_transport);
  // 1090:0567 advances the visible 1038:050e facility-person presentation
  // after Elevator/Stair work and before the later annual/palette tail. It
  // draws into DS:3264 directly; an unrelated WM_PAINT never repeats it.
  world_changed = host.advance_visible_people() || world_changed;
  // 1090:05db invokes 11b8:0060 later in the same frame, including the frame
  // that starts the annual effect.
  world_changed = advance_original_annual_effect(document) || world_changed;

  // 1090:05ee redraws the construction outline after every world layer and
  // before either 1020:098b or the third 1020:00cb palette call. Preserve the
  // pre-preview dirty word for 0580's conditional renderer family; EQUALRECT
  // contributes only to the later 061f presentation test.
  const bool renderer_surface_dirty = world_changed;
  const bool preview_changed = host.render_full_frame(renderer_surface_dirty);
  world_changed = preview_changed || world_changed;

  // 1090:060f/0615 applies the time palette and then makes the third
  // independently gated 1020:00cb effect-animation call. Both mutate one
  // persistent logical palette, including the Effects-menu freeze behavior.
  const bool palette_changed = host.refresh_time_palette();
  const bool effect_palette_changed = host.step_effect_palette();
  palette_recolor_required = palette_changed || effect_palette_changed ||
                             palette_recolor_required;
  if (palette_recolor_required) {
    // Win16 recolors the already-retained indexed bitmap in place. Native
    // rebuilds its RGB transport without replaying any simulation work.
    host.palette_recolor_rebuild();
  }
  // Direct 1090:061a checkpoint after time-palette realization and the third
  // in-frame effects pass.
  host.checkpoint(OriginalIdleCheckpoint::time_palette);

  // Event completion writes the shared b3de clock directly. Keep the native
  // scheduler mirror synchronized so the following tick cannot overwrite it.
  simulation.frame_time = document.header.frame_time;
  simulation.current_day = document.header.current_day;
  simulation.day_phase = original_day_phase(simulation.frame_time);
  simulation.calendar_phase = original_calendar_phase(simulation.current_day);
  host.commit_full_frame_preview();
  auto presentation =
      original_full_frame_auxiliary_presentation_plan(world_changed);
  if (palette_recolor_required) {
    // AnimatePalette recolors every visible indexed surface without touching
    // DS:31cc. Native's true-color transport needs an explicit equivalent,
    // but must not feed that adapter-only need back into 0580's dirty gate.
    presentation.present_main_direct = true;
    presentation.invalidate_command = true;
    presentation.invalidate_map = true;
    host.palette_recolor_invalidate_map();
  }
  for (const auto tail_step : original_full_frame_tail_order()) {
    switch (tail_step) {
      case OriginalFullFrameTailStep::present_main_if_dirty:
        if (presentation.present_main_direct) {
          // 1090:062c-0648 obtains the Main DC and calls 1158:0a3c directly.
          host.present_main_direct();
        }
        break;
      case OriginalFullFrameTailStep::invalidate_command_if_dirty:
        if (presentation.invalidate_command) {
          // 1090:064d-065b invalidates only DS:325a Command after Main.
          host.invalidate_command();
        }
        break;
      case OriginalFullFrameTailStep::present_info_direct:
        if (presentation.present_info_direct) {
          // 1090:0661-06cd always owns an Info DC and paints 1118:073d before
          // its conditional fields, without queueing a whole-window paint.
          OriginalIdleInfoLatches latches(document, host);
          host.present_info_direct(latches);
        }
        break;
      case OriginalFullFrameTailStep::expire_info_status:
        // 1118:08f3 runs only after the unconditional Info pass, so the text
        // remains visible in this frame and clears for the next presentation.
        host.expire_info_status();
        break;
      case OriginalFullFrameTailStep::step_final_effect_palette:
        // Win16 AnimatePalette recolors retained indexed pixels immediately.
        // Native RGB must rematerialize/present only when this fourth call at
        // 1090:06dc actually advances the effects palette.
        if (host.step_effect_palette()) host.final_effect_palette_changed();
        break;
      case OriginalFullFrameTailStep::final_audio_checkpoint:
        // 1090:06e1 restores the WinG backing selection before 06ed's final
        // direct 11e0:0e84 checkpoint.
        host.checkpoint(OriginalIdleCheckpoint::final_frame);
        break;
    }
  }
}

}  // namespace

void OriginalIdleInfoLatches::consume_hotel_checkout() {
  // The Hotel latch/audio mutation occurs after the 1118:073d painter and
  // the 1090:0691 checkpoint, before the Info DC is released.
  if (consume_original_hotel_checkout_presentation(document_)
          .play_cash_sound) {
    host_.play_sound(10013, 2U, 3U, OriginalIdleSound::hotel_checkout);
  }
}

bool run_original_bomb_offer_boundary(OriginalIdlePassContext& context,
                                      OriginalIdlePassHost& host) {
  if (!context.document) return false;
  auto& document = *context.document;
  const auto offer = prepare_original_bomb_event(document, context.part);
  if (!offer.offered) return false;
  const auto choice =
      host.event_dialog(offer.dialog, OriginalIdleDialog::bomb_offer);
  const auto resolution =
      resolve_original_bomb_event(document, context.part, choice);
  if (resolution.direct_wave_resource != 0) {
    host.play_sound(resolution.direct_wave_resource, 0U, 4U,
                    OriginalIdleSound::bomb_payment);
  }
  if (resolution.paid) {
    // 10c8:014f-016a plays WAVE/10015 before 1178:07e8 performs its direct
    // Info balance paint. Paying never raises the active Bomb/world flag.
    host.bomb_ransom_paid();
  }
  if (resolution.followup_dialog.valid()) {
    (void)host.event_dialog(resolution.followup_dialog,
                            OriginalIdleDialog::bomb_followup);
  }
  if (resolution.started) {
    commit_original_bomb_event(document);
  }
  return resolution.started;
}

bool run_original_fire_offer_boundary(OriginalIdlePassContext& context,
                                      OriginalIdlePassHost& host) {
  if (!context.document) return false;
  auto& document = *context.document;
  const auto fire = prepare_original_fire_event(
      document, context.part, context.simulation.day_phase);
  if (!fire.offered) return false;
  (void)host.event_dialog(fire.dialog, OriginalIdleDialog::fire_offer);
  commit_original_fire_event(document, context.part);
  host.focus(fire.floor, fire.x);
  return true;
}

bool consume_original_fire_crew_resolution(
    OriginalIdlePassContext& context,
    OriginalIdlePassHost& host,
    const OriginalFireCrewResolution& resolution) {
  if (!context.document || !resolution.handled) {
    return false;
  }
  if (resolution.followup_dialog.valid()) {
    (void)host.event_dialog(resolution.followup_dialog,
                            OriginalIdleDialog::fire_crew_followup);
  }
  if (resolution.security_dispatch_pending) {
    dispatch_original_security_response(*context.document,
                                        resolution.security_dispatch_flags);
  }
  if (resolution.focus_requested) {
    host.focus(resolution.focus_floor, resolution.focus_x);
  }
  if (resolution.fire_menu_enabled) {
    host.fire_menu_enabled(*resolution.fire_menu_enabled);
  }
  return resolution.hired || resolution.security_dispatch_pending;
}

void consume_original_person_family_dispatch(
    OriginalIdlePassHost& host,
    const OriginalPersonFamilyDispatchResult& dispatch) {
  // Persisted mutation remains entirely in the translation layer; this host
  // boundary performs only Info, modal, focus, and audio work.
  consume_original_person_host_requests(host, dispatch.host_requests);
  if (dispatch.security_effect.valid()) {
    host.focus(dispatch.security_effect.floor, dispatch.security_effect.x);
  }
  if (dispatch.cathedral_arrival &&
      dispatch.cathedral_arrival->status ==
          OriginalCathedralArrivalStatus::ceremony_started) {
    consume_original_cathedral_ceremony(host, *dispatch.cathedral_arrival);
  }
}

void consume_original_person_host_requests(
    OriginalIdlePassHost& host,
    const std::vector<OriginalPersonHostRequest>& requests) {
  for (const auto& request : requests) {
    switch (request.kind) {
      case OriginalPersonHostRequestKind::income_status:
        host.income_status(request.code);
        break;
      case OriginalPersonHostRequestKind::notification_status:
        host.notification_status(request.code);
        break;
      case OriginalPersonHostRequestKind::hotel_dialog:
        (void)host.event_dialog({request.code, request.argument, 10000},
                                OriginalIdleDialog::hotel);
        break;
    }
  }
}

OriginalIdlePassOutcome run_original_idle_pass(OriginalIdlePassContext& context,
                                               OriginalIdlePassHost& host) {
  // 1258:023a invokes 1020:00cb on every empty-queue pass, before both the
  // Find-marker timeout and 1200:0196's six-coarse-tick (~96-ms) simulation
  // gate. The original indexed palette changes in place; native direct
  // rasters need an explicit repaint when this independent pass advances it.
  if (context.document && host.step_effect_palette()) {
    host.idle_palette_changed();
  }

  // 1258:0186 invokes 10e0:051d on every empty-queue pass, independently of
  // whether the simulation clock advances. An active Map overlay exits via
  // 11d0:0000; otherwise 1058:033c performs the ordinary construction toggle.
  // The toggle can clear DS:783e, so the gate below must read it afterwards.
  host.expire_find_marker();
  if (!context.document) {
    return OriginalIdlePassOutcome::no_document;
  }
  auto& document = *context.document;
  const bool elevator_finger_capture_active =
      host.elevator_finger_capture_active();
  const auto idle_gate = original_idle_world_pass_plan(
      host.build_mode_enabled(), elevator_finger_capture_active, false);
  if (!idle_gate.run_scheduler) {
    return OriginalIdlePassOutcome::gated;
  }

  // 1258:0256 samples the cursor; 1090:03ab saves DS:77ac before either its
  // preview-only derivation or its complete frame.
  host.before_scheduler_step(idle_gate.sample_cursor);
  OriginalSimulationStep step{};
  const auto scheduler_now_ms = host.now_ms();
  const bool fast_mode_enabled = host.fast_mode_enabled();
  if (!fast_mode_enabled ||
      native_fast_mode_frame_due(context.fast_frame_tick, scheduler_now_ms)) {
    step = step_original_simulation(context.simulation, document,
                                    host.coarse_tick(scheduler_now_ms),
                                    fast_mode_enabled);
    if (fast_mode_enabled && step.advanced) {
      context.fast_frame_tick = scheduler_now_ms;
    }
  }
  const auto world_pass = original_idle_world_pass_plan(
      host.build_mode_enabled(), elevator_finger_capture_active,
      step.advanced);
  if (!step.advanced) {
    // 1090:03bf-0427 restores/derives DS:77ac only; it never runs the
    // full-frame RNG/person-animation path.
    if (world_pass.run_preview_only_pass) host.preview_only_pass();
    return OriginalIdlePassOutcome::not_advanced;
  }
  host.begin_full_frame(step.day_changed);

  // 1258:026e calls the scheduler first, including each translated far call,
  // then 1090:03ab(1); 1090:042f is the construction-queue pass.
  bool world_changed = false;
  for (const auto& call : step.calls) {
    if (!original_simulation_call_supported(call)) {
      throw std::runtime_error(
          "Recovered scheduler emitted an unsupported native call");
    }
    world_changed =
        dispatch_original_scheduler_call(context, host, call) ||
        world_changed;
  }
  // 1200:0529 samples 1208:05e6 only after all scheduled far calls return.
  // In particular, a midnight/event modal postpones the next 96-ms gate from
  // its close time; committing the entry sample would advance immediately
  // after a long modal. This tail precedes the separate 1090:03ab full frame.
  finish_original_simulation_step(context.simulation,
                                  host.coarse_tick(host.now_ms()));
  run_original_full_frame(context, host, world_changed);
  return OriginalIdlePassOutcome::full_frame;
}

}  // namespace simtower
