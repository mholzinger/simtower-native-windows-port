#include "original_world_input.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace simtower {

void reset_original_world_input_state(OriginalWorldInputState& state,
                                      bool startup_suppressed) {
  // 10d0:086c initializes these before both the New and Open reconstruction
  // chains. The selected build shape is the mandatory first Lobby while mode
  // index 3 is what the original command surface highlights. At
  // 10d0:08cc-08e1, DS:31c4 keeps construction disabled only for the
  // pre-dialog bootstrap invoked by 1128:00c4.
  state.build_mode_enabled = !startup_suppressed;
  state.command_toggle_pressed = false;
  state.command_mode = 3U;
  state.map_mode = 0U;
  state.map_drag_active = false;
  state.selected_build_type = 24U;
  state.office_variant = 0U;
  state.facility_variants.fill(0U);
  state.lobby_drag = {};
  state.floor_drag = {};
  state.parking_drag = {};
  state.completion = {};
  state.retained_press.reset();
  state.retained_placement.reset();
  state.preview_scratch = {};
}

void mark_original_world_interaction_changed(OriginalWorldInputHost& host) {
  const auto plan = original_world_mutation_presentation_plan(
      true, false);
  if (plan.mark_document_dirty) host.mark_document_dirty();
  if (plan.invalidate_main_surface) host.invalidate_main();
  if (plan.repaint_info_balance_synchronously) {
    host.present_info_status();
  }
  if (plan.invalidate_map_surface) {
    host.invalidate_map();
  }
}

namespace {

// SetCapture(Main) / ReleaseCapture, with the capture mirrored in the state
// for hosts that track it themselves.
void capture_main(const OriginalWorldInputContext& ctx,
                  OriginalWorldInputHost& host) {
  ctx.state.capture_held = true;
  host.set_capture(OriginalInputSurface::main);
}

void release_main_capture(const OriginalWorldInputContext& ctx,
                          OriginalWorldInputHost& host) {
  ctx.state.capture_held = false;
  host.release_capture();
}

void keep_original_pointer_visible(OriginalWorldInputHost& host,
                                   int client_x,
                                   int client_y,
                                   bool horizontal_axis) {
  const auto client = host.main_client();
  if (!client) return;
  const auto view = host.main_view();
  const auto adjusted = original_keep_pointer_visible(
      view, *client, {client_x, client_y}, horizontal_axis);
  if (adjusted == view) return;

  // 1080:0054 commits through 1080:00d7 (the scrollbar clamps the raw
  // position) and immediately executes 1080:055d's focus transaction.
  host.set_main_view(adjusted.x, adjusted.y);
}

void play_original_construction_success_audio(OriginalWorldInputHost& host,
                                              std::uint16_t type) {
  const auto plan = original_construction_success_audio_plan(
      type, host.beep_only());
  // 11f8:0e21-0e67 clears the status first, then tests DS:de28 for the exact
  // BeepOnly value one before applying the five-type WAVE/7000 exclusion.
  if (plan.beep) {
    host.beep();
  }
  if (plan.play_general_wave) {
    host.play_sound(7000, 0, 4);
  }
}

void apply_original_construction_mutation_presentation(
    OriginalWorldInputHost& host,
    bool document_changed,
    bool balance_changed) {
  const auto plan =
      original_world_mutation_presentation_plan(
          document_changed, balance_changed);
  if (plan.mark_document_dirty) {
    host.mark_document_dirty();
  }
  if (plan.invalidate_main_surface) {
    // 1038:002f marks the affected cells in the Win16 tile caches. Native's
    // retained RGB transport derives those cells from the document on demand.
    host.invalidate_main();
  }
  if (plan.repaint_info_balance_synchronously) {
    host.present_info_status();
  }
  if (plan.invalidate_map_surface) {
    host.invalidate_map();
  }
}

bool apply_original_lobby_result(const OriginalWorldInputContext& ctx,
                                 OriginalWorldInputHost& host,
                                 const OriginalConstructionResult& result,
                                 std::int32_t old_balance,
                                 std::uint16_t old_lobby_height,
                                 bool continuous_drag = false) {
  auto& state = ctx.state;
  const bool priority_sound_latch_armed =
      continuous_drag && state.completion.active &&
      state.completion.priority_sound_latch_armed;
  const auto helper_completion =
      original_floor_lobby_helper_completion_plan(
          result.succeeded(), result.construction_sound_requested,
          priority_sound_latch_armed);
  if (helper_completion.sound ==
          OriginalCapturedHelperSound::priority_five) {
    host.play_sound(7001, 0, 5);
  } else if (helper_completion.sound ==
                 OriginalCapturedHelperSound::reserved_if_idle) {
    host.play_reserved_if_idle(7001, 0, host.now_ms());
  }
  if (helper_completion.clear_priority_sound_latch) {
    state.completion.priority_sound_latch_armed = false;
  }
  if (!result.succeeded()) {
    if (result.construction_status_code != 0U) {
      host.construction_status(result.construction_status_code);
    }
    // 11f8:0ec0 uses WAVE/7002 for a rejected single-click command. The
    // continuous Floor/Lobby/Parking/Ramp branches return through 11f8:0e09 and
    // defer one WAVE/7002 to button-up only if 24cc has no accepted step.
    if (!continuous_drag) {
      host.play_sound(7002, 0, 4);
    }
    const bool changed = result.document_changed ||
        (ctx.document &&
         (ctx.document->header.balance != old_balance ||
          ctx.document->header.lobby_height != old_lobby_height));
    if (changed) {
      const bool balance_changed =
          ctx.document && ctx.document->header.balance != old_balance;
      apply_original_construction_mutation_presentation(
          host, true, balance_changed);
    }
    return changed;
  }
  if (continuous_drag && state.completion.active &&
      helper_completion.increment_successful_step) {
    ++state.completion.successful_steps;
  }
  if (!continuous_drag) {
    // 11f8:0e21 clears STRL/1003, 0e29 applies BeepOnly, and 0e58 plays the
    // type-filtered command-success sound.
    host.construction_status(0U);
    play_original_construction_success_audio(host,
                                             state.selected_build_type);
  }
  // 1148:0163 is downstream of 07d8's common success boundary. Captured
  // tools return at 0e02 after each accepted step and reach that boundary
  // only once, on their successful button-up.
  const auto rating_completion =
      !continuous_drag && ctx.document
          ? complete_original_rating_construction(
                *ctx.document, ctx.part, state.selected_build_type)
          : OriginalRatingConstructionResult{};
  if (rating_completion.dialog.valid()) {
    (void)host.event_dialog(rating_completion.dialog);
  }
  if (!continuous_drag && ctx.document &&
      original_construction_rebuilds_routes(
          state.selected_build_type)) {
    // Literal 0ed8 table: 1148:0163 precedes 11b0:049f then 11b0:00f2.
    rebuild_original_transport_route_graphs(*ctx.document);
  }
  const bool changed = result.document_changed || rating_completion.changed ||
      (ctx.document &&
       (ctx.document->header.balance != old_balance ||
        ctx.document->header.lobby_height != old_lobby_height));
  if (changed) {
    const bool balance_changed =
        ctx.document && ctx.document->header.balance != old_balance;
    apply_original_construction_mutation_presentation(
        host, true, balance_changed);
  }
  return changed;
}


void run_original_facility_bulldozer(const OriginalWorldInputContext& ctx,
                                     OriginalWorldInputHost& host,
                                     OriginalWorldInputPoint position) {
  if (!ctx.document) return;
  const auto old_balance = ctx.document->header.balance;
  const auto view = host.main_view();
  const int client_x = position.x;
  const int client_y = position.y;

  // Exact 1058:0084/0097/00aa precedence: an Elevator or vertical transport
  // owns the click before 11f8:0793 is allowed to inspect floor facilities.
  // Elevator demolition owns the click before vertical/facility handling.
  const auto elevator_hit = original_elevator_hit_from_client(
      *ctx.document, client_x, client_y, view.x, view.y);
  if (elevator_hit.hit) {
    const auto& elevator =
        ctx.document->elevators[elevator_hit.elevator_index];
    const auto action = original_elevator_bulldozer_action(
        true, elevator_hit.car_index, elevator.cars, elevator_hit.floor,
        elevator.bottom_floor, elevator.top_floor, elevator.word_3c);
    if (action == OriginalElevatorBulldozerAction::consume) {
      return;
    }
    if (action == OriginalElevatorBulldozerAction::remove_car ||
        action == OriginalElevatorBulldozerAction::remove_shaft) {
      OriginalElevatorDemolitionResult demolition{};
      if (action == OriginalElevatorBulldozerAction::remove_car) {
        demolition = remove_original_elevator_car(
            *ctx.document, elevator_hit.elevator_index,
            static_cast<std::size_t>(elevator_hit.car_index), ctx.part,
            ctx.rent_income);
      } else {
        // A cap click, a click outside the stored span, or a click on the
        // last car enters 0179's serviced-floor connectivity confirmation.
        if (original_elevator_shaft_demolition_requires_confirmation(
                *ctx.document, elevator_hit.elevator_index)) {
          const auto text = original_strl_entry(
              ctx.resources.find("STRL", 1005), 5U);
          if (host.alert(1005, text) != 1) {
            return;
          }
        }
        demolition = remove_original_elevator_shaft(
            *ctx.document, elevator_hit.elevator_index, ctx.part,
            ctx.rent_income);
      }
      if (demolition.removed) {
        host.consume_person_family_dispatches(
            demolition.family_dispatches);
        host.play_sound(7003, 0, 4);
        mark_original_world_interaction_changed(host);
        if (ctx.document->header.balance != old_balance) {
          host.present_info_status();
        }
      }
      return;
    }
    // The pass-through result is the literal zero returned by 0201 when an
    // in-span shaft-body hit has word_3c == 0. Preserve 1058's next two legs.
  }
  const auto vertical_hit =
      original_vertical_transport_hit_from_client(
          *ctx.document, client_x, client_y, view.x, view.y);
  if (vertical_hit.hit) {
    const auto demolition = remove_original_vertical_transport(
        *ctx.document, vertical_hit.transport_index, ctx.part,
        ctx.rent_income);
    if (demolition.removed) {
      host.consume_person_family_dispatches(
          demolition.family_dispatches);
      host.play_sound(7003, 0, 4);
      mark_original_world_interaction_changed(host);
      if (ctx.document->header.balance != old_balance) {
        host.present_info_status();
      }
    }
    return;
  }
  const auto click = apply_original_facility_click_damage(
      *ctx.document, ctx.rent_income, client_x, client_y, view.x, view.y);
  if (!click.hit.hit) return;
  const auto& result = click.damage;
  host.play_event_sounds(result.sound_requests);
  if (result.alert_code != 0) {
    host.construction_status(
        static_cast<std::uint16_t>(result.alert_code));
  }
  for (const auto code : result.notification_codes) {
    host.notification_status(code);
  }
  if (result.changed) {
    mark_original_world_interaction_changed(host);
    if (ctx.document->header.balance != old_balance) {
      // Exact 1178:0697 direct demolition/structure debit repaint.
      host.present_info_status();
    }
  }
}

void run_original_magnifying_glass(const OriginalWorldInputContext& ctx,
                                   OriginalWorldInputHost& host,
                                   OriginalWorldInputPoint position) {
  if (!ctx.document) return;
  const auto view = host.main_view();
  const auto target = select_original_magnifier_target(
      *ctx.document,
      static_cast<std::int16_t>(position.x),
      static_cast<std::int16_t>(position.y), view.x, view.y);
  switch (target.kind) {
    case OriginalMagnifierTargetKind::elevator_car_information:
    case OriginalMagnifierTargetKind::vertical_transport_information:
      host.transport_information(target);
      break;
    case OriginalMagnifierTargetKind::elevator_control:
      host.open_elevator_control(
          target.elevator_index,
          static_cast<std::int16_t>(position.x),
          static_cast<std::int16_t>(position.y));
      break;
    case OriginalMagnifierTargetKind::facility_information:
      host.facility_information(target.floor, target.tenant_index);
      break;
    case OriginalMagnifierTargetKind::none:
      break;
    case OriginalMagnifierTargetKind::waiting_person_information:
      host.person_information(target.person_index);
      break;
  }
}

bool apply_original_shift_replacement(const OriginalWorldInputContext& ctx,
                                      OriginalWorldInputHost& host,
                                      std::uint16_t keys,
                                      std::uint16_t selected_type,
                                      const OriginalLobbyPlacement& placement) {
  if (!ctx.document || (keys & kOriginalKeyShift) == 0U) return true;
  const auto old_balance = ctx.document->header.balance;
  const auto result = apply_original_replacement_demolition(
      *ctx.document, ctx.rent_income, selected_type, placement.floor,
      static_cast<std::int16_t>(placement.left),
      static_cast<std::int16_t>(placement.right));
  host.play_event_sounds(result.sound_requests);
  for (const auto code : result.alert_codes) {
    if (code != 0) {
      host.construction_status(static_cast<std::uint16_t>(code));
    }
  }
  for (const auto code : result.notification_codes) {
    host.notification_status(code);
  }
  if (result.changed != 0U) {
    mark_original_world_interaction_changed(host);
    if (ctx.document->header.balance != old_balance) {
      host.present_info_status();
    }
  }
  return result.completed;
}

void run_original_elevator_finger(const OriginalWorldInputContext& ctx,
                                  OriginalWorldInputHost& host,
                                  OriginalWorldInputPoint position) {
  if (!ctx.document) return;
  const auto view = host.main_view();
  const int client_x = position.x;
  const int client_y = position.y;
  const auto hit = original_elevator_hit_from_client(
      *ctx.document, client_x, client_y, view.x, view.y);
  if (!hit.hit) return;

  if (add_original_elevator_service_floor(
          *ctx.document, hit.elevator_index, hit.floor)) {
    // 1038:0000 only rebuilds the original's transient per-view tile cache.
    // The native renderer derives those frames directly, so invalidation is
    // its equivalent after the persisted graph mutation.
    mark_original_world_interaction_changed(host);
    return;
  }

  const auto& elevator =
      ctx.document->elevators[hit.elevator_index];
  if (hit.floor < 0 || hit.floor >= 120 ||
      elevator.serviced_floors[static_cast<std::size_t>(hit.floor)] ==
          std::byte{0}) {
    return;
  }

  const auto warning = original_elevator_service_floor_warning_code(
      *ctx.document, hit.elevator_index, hit.floor);
  if (warning != 0U) {
    const auto text = original_strl_entry(
        ctx.resources.find("STRL", 1005), warning);
    if (host.alert(1005, text) == 2) {
      return;
    }
  }
  const auto removal = remove_original_elevator_service_floor(
      *ctx.document, hit.elevator_index, hit.floor, ctx.part,
      ctx.rent_income);
  if (removal.cleanup.status ==
      OriginalElevatorFloorPeopleCleanupStatus::cleaned) {
    host.consume_person_family_dispatches(removal.family_dispatches);
    mark_original_world_interaction_changed(host);
  }
}

void begin_original_elevator_finger(const OriginalWorldInputContext& ctx,
                                    OriginalWorldInputHost& host,
                                    OriginalWorldInputPoint position) {
  auto& state = ctx.state;
  if (!ctx.document) return;
  const auto view = host.main_view();
  const int client_x = position.x;
  const int client_y = position.y;
  const auto hit = original_elevator_hit_from_client(
      *ctx.document, client_x, client_y, view.x, view.y);
  const auto* elevator = hit.hit
      ? &ctx.document->elevators[hit.elevator_index]
      : nullptr;
  const auto path = original_elevator_finger_press_path(
      hit.hit, elevator && elevator->word_3c != 0U, hit.floor,
      elevator ? elevator->bottom_floor : 0,
      elevator ? elevator->top_floor : 0);
  if (path == OriginalElevatorFingerPressPath::service_floor) {
    // 1058:00d9 first gives an in-span, initialized shaft to 10a0:0000.
    // That path consumes the click even when a service-floor mutation is
    // rejected, so it never enters 10a0:0544's capture transaction.
    run_original_elevator_finger(ctx, host, position);
    return;
  }

  // 10a0:0562-059f captures every otherwise-unhandled Finger press and sets
  // DS:02a6 before scanning the shaft caps. Even a press in empty space keeps
  // the idle scheduler suppressed until the matching button-up.
  state.shaft_drag.capture_active = true;
  capture_main(ctx, host);
  if (path ==
      OriginalElevatorFingerPressPath::capture_upper_cap) {
    // 10a0:0544 stores direction one for the upper cap, captures the mouse,
    // and feeds every subsequent point through 07b7 -> 0819.
    state.shaft_drag = {
        true, OriginalElevatorShaftDragDirection::upper, hit.elevator_index};
    host.set_cursor(1000U);
    return;
  }
  if (path ==
      OriginalElevatorFingerPressPath::capture_lower_cap) {
    state.shaft_drag = {
        true, OriginalElevatorShaftDragDirection::lower, hit.elevator_index};
    host.set_cursor(1000U);
    return;
  }
}

void update_original_elevator_finger(const OriginalWorldInputContext& ctx,
                                     OriginalWorldInputHost& host,
                                     OriginalWorldInputPoint position) {
  auto& state = ctx.state;
  if (!ctx.document ||
      state.shaft_drag.direction ==
          OriginalElevatorShaftDragDirection::none) {
    return;
  }
  const auto index = state.shaft_drag.elevator_index;
  if (index >= ctx.document->elevators.size()) return;

  const auto view = host.main_view();
  const int client_x = position.x;
  const int client_y = position.y;
  // Exact 10a0:07b7 captured-shaft pointer transform: add the live view,
  // convert world y through the 120-story/36-pixel grid, then offset one floor
  // inward before dispatching the 0819/0b87 upper/lower transaction.
  const auto pointer = original_floor_placement_from_client(
      client_x, client_y, view.x, view.y);
  const auto target = static_cast<std::int16_t>(
      pointer.floor +
      (state.shaft_drag.direction ==
               OriginalElevatorShaftDragDirection::upper
           ? -1
           : 1));
  const auto old_top = ctx.document->elevators[index].top_floor;
  const auto old_bottom = ctx.document->elevators[index].bottom_floor;
  const auto old_balance = ctx.document->header.balance;
  bool succeeded = false;
  if (state.shaft_drag.direction ==
      OriginalElevatorShaftDragDirection::upper) {
    if (target > old_top) {
      succeeded = extend_original_elevator_shaft(
          *ctx.document, index, target, ctx.construction_costs).succeeded();
    } else {
      const auto shrink = shrink_original_elevator_shaft(
          *ctx.document, index,
          OriginalElevatorShaftEnd::upper, target, ctx.part,
          ctx.rent_income);
      succeeded = shrink.succeeded();
      if (succeeded) {
        host.consume_person_family_dispatches(shrink.family_dispatches);
      }
    }
  } else {
    if (target < old_bottom) {
      succeeded = extend_original_elevator_shaft(
          *ctx.document, index, target, ctx.construction_costs).succeeded();
    } else {
      const auto shrink = shrink_original_elevator_shaft(
          *ctx.document, index,
          OriginalElevatorShaftEnd::lower, target, ctx.part,
          ctx.rent_income);
      succeeded = shrink.succeeded();
      if (succeeded) {
        host.consume_person_family_dispatches(shrink.family_dispatches);
      }
    }
  }
  if (succeeded &&
      (ctx.document->elevators[index].top_floor != old_top ||
       ctx.document->elevators[index].bottom_floor != old_bottom)) {
    mark_original_world_interaction_changed(host);
    if (ctx.document->header.balance != old_balance) {
      host.present_info_status();
    }
    keep_original_pointer_visible(host, client_x, client_y, true);
  }
}

void continue_original_elevator_finger(const OriginalWorldInputContext& ctx,
                                       OriginalWorldInputHost& host,
                                       OriginalWorldInputPoint position) {
  auto& state = ctx.state;
  if (!ctx.document) return;
  if (state.shaft_drag.direction !=
      OriginalElevatorShaftDragDirection::none) {
    update_original_elevator_finger(ctx, host, position);
    return;
  }

  // 10a0:0667-079a scans for a cap on every armed left-button move while
  // DS:0080 has no direction. This can begin a shaft drag after the initial
  // press occurred in empty space or was consumed by 10a0:0000 in-span.
  const auto view = host.main_view();
  const auto hit = original_elevator_hit_from_client(
      *ctx.document,
      static_cast<std::int16_t>(position.x),
      static_cast<std::int16_t>(position.y), view.x, view.y);
  if (!hit.hit) return;
  const auto& elevator = ctx.document->elevators[hit.elevator_index];
  const auto path = original_elevator_finger_press_path(
      true, elevator.word_3c != 0U, hit.floor,
      elevator.bottom_floor, elevator.top_floor);
  if (path ==
      OriginalElevatorFingerPressPath::capture_upper_cap) {
    state.shaft_drag.direction =
        OriginalElevatorShaftDragDirection::upper;
  } else if (path ==
             OriginalElevatorFingerPressPath::capture_lower_cap) {
    state.shaft_drag.direction =
        OriginalElevatorShaftDragDirection::lower;
  } else {
    return;
  }
  state.shaft_drag.elevator_index = hit.elevator_index;
  host.set_cursor(1000U);
  update_original_elevator_finger(ctx, host, position);
}

void double_click_original_elevator_finger(const OriginalWorldInputContext& ctx,
                                           OriginalWorldInputHost& host,
                                           OriginalWorldInputPoint position) {
  auto& state = ctx.state;
  // 10a0:05bf-062c opens Elevator Control for any shaft hit. Both branches
  // clear direction, restore the Finger cursor, and release capture, but the
  // miss branch deliberately leaves DS:02a6 armed until the following up.
  std::optional<std::size_t> hit_index{};
  if (ctx.document) {
    const auto view = host.main_view();
    const auto hit = original_elevator_hit_from_client(
        *ctx.document,
        static_cast<std::int16_t>(position.x),
        static_cast<std::int16_t>(position.y), view.x, view.y);
    if (hit.hit) {
      host.set_cursor(0U);
      hit_index = hit.elevator_index;
    }
  }
  const auto plan = original_elevator_finger_double_click_plan(
      hit_index.has_value());
  if (plan.open_control) {
    host.open_elevator_control(
        *hit_index,
        static_cast<std::int16_t>(position.x),
        static_cast<std::int16_t>(position.y));
  }
  if (plan.clear_capture_latch) {
    state.shaft_drag = {};
  } else {
    state.shaft_drag.direction =
        OriginalElevatorShaftDragDirection::none;
  }
  host.set_cursor(1004U);
  release_main_capture(ctx, host);
}

void update_original_parking_drag(const OriginalWorldInputContext& ctx,
                                  OriginalWorldInputHost& host,
                                  OriginalWorldInputPoint position,
                                  bool refresh_horizontal_snap = true);

void begin_original_lobby_drag(const OriginalWorldInputContext& ctx,
                               OriginalWorldInputHost& host,
                               std::uint16_t keys,
                               OriginalWorldInputPoint position,
                               bool setup_press = true) {
  auto& state = ctx.state;
  if (!ctx.document) return;
  // Exact 1058:0000 edit-mode dispatch. Mode zero is the Bulldozer, one is
  // the elevator Finger tool, and two is the Magnifying Glass. Only modes
  // three and above enter 11f8:07d8 construction.
  if (state.command_mode == 0U) {
    if (state.build_mode_enabled) {
      run_original_facility_bulldozer(ctx, host, position);
    }
    return;
  }
  if (state.command_mode == 1U) {
    if (state.build_mode_enabled) {
      begin_original_elevator_finger(ctx, host, position);
    }
    return;
  }
  if (state.command_mode == 2U) {
    run_original_magnifying_glass(ctx, host, position);
    return;
  }
  if (state.command_mode < 3U || !state.build_mode_enabled) return;
  const auto view = host.main_view();
  const int client_x = position.x;
  const int client_y = position.y;
  std::optional<OriginalLobbyPlacement> bonus_placement;
  if (state.selected_build_type == 0U) {
    bonus_placement = original_floor_placement_from_client(
        client_x, client_y, view.x, view.y);
  } else if (state.selected_build_type == 7U) {
    bonus_placement = original_office_placement_from_client(
        client_x, client_y, view.x, view.y);
  } else if (state.selected_build_type == 24U) {
    bonus_placement = original_lobby_placement_from_client(
        client_x, client_y, view.x, view.y);
  } else if (original_facility_width_cells(
                 state.selected_build_type) != 0U) {
    bonus_placement = original_facility_placement_from_client(
        state.selected_build_type, client_x, client_y, view.x, view.y);
  }

  // 11f8:090e-0950 resets 24cc and arms every continuous tool before the
  // hidden 0955 initial-balance branch and before Shift replacement. Keep the
  // capture alive even when either later prepass returns early.
  if (setup_press) {
    state.retained_press =
        OriginalWorldInputPoint{position.x, position.y, keys};
    state.retained_placement = bonus_placement;
    state.completion = {};
    if (original_continuous_construction_type(
            state.selected_build_type) && bonus_placement) {
      state.completion = {
          true, state.selected_build_type, 0U,
          ctx.document->header.balance, true};
      const auto& placement = *bonus_placement;
      if (state.selected_build_type == 0U) {
        state.floor_drag = {
            true, placement.floor, placement.left, placement.right};
      } else if (state.selected_build_type == 24U) {
        state.lobby_drag = {
            true, placement.floor, placement.left, placement.right};
      }
      capture_main(ctx, host);
    }
  }
  if (bonus_placement && apply_original_initial_balance_bonus(
                             *ctx.document, bonus_placement->floor,
                             bonus_placement->left)) {
    // 11f8:0980 calls 1178:076f directly. This changes only finance state and
    // immediately paints Info; it does not dirty Main or Map pixels.
    host.mark_document_dirty();
    host.present_info_status();
    return;
  }
  if (state.selected_build_type == 0U) {
    const auto placement = original_floor_placement_from_client(
        client_x, client_y, view.x, view.y);
    const auto old_balance = ctx.document->header.balance;
    const auto old_height = ctx.document->header.lobby_height;
    const auto result = build_original_floor(
        *ctx.document, placement.floor,
        static_cast<std::uint16_t>(placement.left),
        static_cast<std::uint16_t>(placement.right), ctx.construction_costs);
    (void)apply_original_lobby_result(ctx, host, result, old_balance,
                                      old_height, true);
    if (result.succeeded()) {
      keep_original_pointer_visible(host, client_x, client_y, true);
    }
    return;
  }
  if (state.selected_build_type == 22U || state.selected_build_type == 27U) {
    const auto type = static_cast<std::uint8_t>(state.selected_build_type);
    const auto placement = original_facility_placement_from_client(
        type, client_x, client_y, view.x, view.y);
    const auto old_balance = ctx.document->header.balance;
    const auto old_height = ctx.document->header.lobby_height;
    OriginalConstructionResult result{
        OriginalConstructionStatus::invalid_span, 0, 20U};
    result = build_original_vertical_transport(
        *ctx.document, type, placement.floor,
        static_cast<std::uint16_t>(placement.left), ctx.construction_costs);
    (void)apply_original_lobby_result(ctx, host, result, old_balance,
                                      old_height);
    return;
  }
  if (state.selected_build_type == 11U || state.selected_build_type == 0x2cU) {
    const auto type = state.selected_build_type;
    const auto placement = original_facility_placement_from_client(
        type, client_x, client_y, view.x, view.y);
    if (setup_press && !apply_original_shift_replacement(
            ctx, host, keys, type, placement)) {
      return;
    }

    // Unlike Floor/Lobby, 0946 captures Parking/Ramp without setting DS:3216.
    // Their 240d/25a2 helpers initialize only when actually reached. The floor
    // remains 08ce's down-time snap, while a later move refreshes c6/c8 first.
    if (state.parking_drag.kind == OriginalParkingDragKind::none) {
      state.parking_drag.kind = type == 11U ? OriginalParkingDragKind::parking
                                        : OriginalParkingDragKind::ramp;
      state.parking_drag.press_floor =
          state.retained_placement
              ? state.retained_placement->floor
              : placement.floor;
      state.parking_drag.snapped_left = placement.left;
      state.parking_drag.snapped_right = placement.right;
    }
    update_original_parking_drag(ctx, host, position, true);
    return;
  }
  if ((state.selected_build_type >= 3U && state.selected_build_type <= 5U) ||
      state.selected_build_type == 6U || state.selected_build_type == 9U ||
      state.selected_build_type == 10U || state.selected_build_type == 12U ||
      state.selected_build_type == 13U || state.selected_build_type == 14U ||
      state.selected_build_type == 15U || state.selected_build_type == 17U ||
      state.selected_build_type == 18U || state.selected_build_type == 20U ||
      state.selected_build_type == 29U || state.selected_build_type == 31U ||
      state.selected_build_type == 36U) {
    const auto type = static_cast<std::uint8_t>(state.selected_build_type);
    const auto placement = original_facility_placement_from_client(
        type, client_x, client_y, view.x, view.y);
    const auto old_balance = ctx.document->header.balance;
    const auto old_height = ctx.document->header.lobby_height;
    OriginalConstructionResult result{
        OriginalConstructionStatus::invalid_span, 0, 20U};
    // 11f8:09c1-09e3 runs Shift replacement before the type jump table. The
    // constructor itself owns span validation and any preceding type limit.
    if (setup_press &&
        !apply_original_shift_replacement(ctx, host, keys, type, placement)) {
      return;
    }
    if (type == 9U) {
      result = build_original_condo(
          *ctx.document, placement.floor,
          static_cast<std::uint16_t>(placement.left),
          static_cast<std::uint8_t>(state.facility_variants[type]),
          ctx.construction_costs);
    } else if (type == 10U) {
      result = build_original_retail_shop(
          *ctx.document, placement.floor,
          static_cast<std::uint16_t>(placement.left), ctx.construction_costs);
    } else if (type == 6U) {
      result = build_original_restaurant(
          *ctx.document, placement.floor,
          static_cast<std::uint16_t>(placement.left), ctx.construction_costs);
    } else if (type == 12U) {
      result = build_original_fast_food(
          *ctx.document, placement.floor,
          static_cast<std::uint16_t>(placement.left), ctx.construction_costs);
    } else if (type == 14U) {
      result = build_original_security(
          *ctx.document, placement.floor,
          static_cast<std::uint16_t>(placement.left), ctx.construction_costs);
    } else if (type == 15U) {
      result = build_original_housekeeping(
          *ctx.document, placement.floor,
          static_cast<std::uint16_t>(placement.left), ctx.construction_costs);
    } else if (type == 17U) {
      result = build_original_secom_center(
          *ctx.document, placement.floor,
          static_cast<std::uint16_t>(placement.left), ctx.construction_costs);
    } else if (type == 18U) {
      result = build_original_movie_theater(
          *ctx.document, placement.floor,
          static_cast<std::uint16_t>(placement.left), ctx.construction_costs);
    } else if (type == 20U) {
      result = build_original_recycling_center(
          *ctx.document, placement.floor,
          static_cast<std::uint16_t>(placement.left), ctx.construction_costs);
    } else if (type == 29U) {
      result = build_original_party_hall(
          *ctx.document, placement.floor,
          static_cast<std::uint16_t>(placement.left), ctx.construction_costs);
    } else if (type == 31U) {
      result = build_original_metro_station(
          *ctx.document, placement.floor,
          static_cast<std::uint16_t>(placement.left), ctx.construction_costs);
    } else if (type == 36U) {
      result = build_original_cathedral(
          *ctx.document, placement.floor,
          static_cast<std::uint16_t>(placement.left), ctx.construction_costs);
    } else if (type == 13U) {
      result = build_original_medical_center(
          *ctx.document, placement.floor,
          static_cast<std::uint16_t>(placement.left),
          static_cast<std::uint8_t>(state.facility_variants[type]),
          ctx.construction_costs);
    } else {
      result = build_original_hotel_room(
          *ctx.document, type, placement.floor,
          static_cast<std::uint16_t>(placement.left),
          static_cast<std::uint8_t>(state.facility_variants[type]),
          ctx.construction_costs);
    }
    if (type == 13U) {
      // 11f8:0c46-0c7f checks the ten-center sentinel before entering the
      // constructor/variant path; all attempted constructors advance 795c.
      if (result.construction_status_code != 30U) {
        state.facility_variants[type] = static_cast<std::uint16_t>(
            (state.facility_variants[type] + 1U) % 3U);
      }
    } else if (type != 6U && type != 10U && type != 12U && type != 14U &&
               type != 15U && type != 17U && type != 18U && type != 20U &&
               type != 29U && type != 31U && type != 36U) {
      // 11f8:0b21/0b57/0b8d/0bf9 advances these selectors even after a
      // rejected attempt. Retail has no appearance selector to advance.
      const std::uint16_t cycle = type == 4U ? 4U : (type == 9U ? 3U : 2U);
      state.facility_variants[type] = static_cast<std::uint16_t>(
          (state.facility_variants[type] + 1U) % cycle);
    }
    (void)apply_original_lobby_result(ctx, host, result, old_balance,
                                      old_height);
    return;
  }
  if (state.selected_build_type == 7U) {
    const auto placement = original_office_placement_from_client(
        client_x, client_y, view.x, view.y);
    const auto old_balance = ctx.document->header.balance;
    const auto old_height = ctx.document->header.lobby_height;
    OriginalConstructionResult result{
        OriginalConstructionStatus::invalid_span, 0, 20U};
    if (setup_press &&
        !apply_original_shift_replacement(ctx, host, keys, 7U, placement)) {
      return;
    }
    result = build_original_office(
        *ctx.document, placement.floor,
        static_cast<std::uint16_t>(placement.left),
        static_cast<std::uint8_t>(state.office_variant),
        ctx.construction_costs);
    // 11f8:0bc3-0bd0 advances DS:7954 after every type-7 attempt, including
    // rejected placement, and keeps the command palette on Office.
    state.office_variant =
        static_cast<std::uint16_t>((state.office_variant + 1U) % 6U);
    (void)apply_original_lobby_result(ctx, host, result, old_balance,
                                      old_height);
    return;
  }
  if (state.selected_build_type == 1U || state.selected_build_type == 42U ||
      state.selected_build_type == 43U) {
    const auto placement = original_facility_placement_from_client(
        state.selected_build_type, client_x, client_y, view.x, view.y);
    const auto old_balance = ctx.document->header.balance;
    const auto old_height = ctx.document->header.lobby_height;
    const auto result = build_original_elevator(
        *ctx.document, state.selected_build_type, placement.floor,
        static_cast<std::uint16_t>(placement.left),
        ctx.construction_costs, ctx.part);
    if (apply_original_lobby_result(ctx, host, result, old_balance,
                                    old_height) &&
        result.new_elevator_shaft_created) {
      // 11f8:140d is reached only after allocating a new shaft. The existing
      // add-car branch jumps from 122f straight to 1446 and leaves the active
      // construction command untouched.
      state.command_mode = 1U;
      // 1080:05a1 composes and presents Command before 0fea returns.
      host.present_command_now();
    }
    return;
  }
  const auto placement = original_lobby_placement_from_client(
      client_x, client_y, view.x, view.y);
  if (state.selected_build_type != 24U) {
    return;
  }
  const auto old_balance = ctx.document->header.balance;
  const auto old_height = ctx.document->header.lobby_height;
  OriginalConstructionResult result{};
  if (placement.floor == 10 && old_height == 0U) {
    // 11f8:098f-09b3: Control selects a two-story lobby; Control+Shift
    // selects three. Shift alone leaves the default one-story selection.
    std::uint16_t height = 1U;
    if ((keys & kOriginalKeyControl) != 0U) {
      height = (keys & kOriginalKeyShift) != 0U ? 3U : 2U;
    }
    result = build_original_initial_lobby(
        *ctx.document, static_cast<std::uint16_t>(placement.left),
        static_cast<std::uint16_t>(placement.right), height,
        ctx.construction_costs);
  } else if (placement.floor == 10) {
    result = extend_original_lobby(
        *ctx.document, static_cast<std::uint16_t>(placement.left),
        static_cast<std::uint16_t>(placement.right), ctx.construction_costs);
  } else {
    result = build_original_sky_lobby(
        *ctx.document, placement.floor,
        static_cast<std::uint16_t>(placement.left),
        static_cast<std::uint16_t>(placement.right), ctx.construction_costs);
  }
  (void)apply_original_lobby_result(ctx, host, result, old_balance,
                                    old_height, true);
  if (result.succeeded()) {
    keep_original_pointer_visible(host, client_x, client_y, true);
  }
}

void update_original_lobby_drag(const OriginalWorldInputContext& ctx,
                                OriginalWorldInputHost& host,
                                OriginalWorldInputPoint position) {
  auto& state = ctx.state;
  if (!state.lobby_drag.active || !ctx.document) {
    return;
  }
  const auto view = host.main_view();
  const int client_x = position.x;
  const int client_y = position.y;
  const auto placement = original_lobby_placement_from_client(
      client_x, client_y, view.x, view.y);
  const std::int32_t left =
      std::min(state.lobby_drag.anchor_left, placement.left);
  const std::int32_t right =
      std::max(state.lobby_drag.anchor_right, placement.right);
  const auto old_balance = ctx.document->header.balance;
  const auto old_height = ctx.document->header.lobby_height;
  const auto result = state.lobby_drag.floor == 10
      ? extend_original_lobby(
            *ctx.document, static_cast<std::uint16_t>(left),
            static_cast<std::uint16_t>(right), ctx.construction_costs)
      : build_original_sky_lobby(
            *ctx.document, state.lobby_drag.floor,
            static_cast<std::uint16_t>(left),
            static_cast<std::uint16_t>(right), ctx.construction_costs);
  (void)apply_original_lobby_result(ctx, host, result, old_balance,
                                    old_height, true);
  if (result.succeeded()) {
    keep_original_pointer_visible(host, client_x, client_y, true);
  }
}

void update_original_floor_drag(const OriginalWorldInputContext& ctx,
                                OriginalWorldInputHost& host,
                                OriginalWorldInputPoint position) {
  auto& state = ctx.state;
  if (!state.floor_drag.active || !ctx.document) {
    return;
  }
  const auto view = host.main_view();
  const int client_x = position.x;
  const int client_y = position.y;
  const auto placement = original_floor_placement_from_client(
      client_x, client_y, view.x, view.y);
  const std::int32_t left =
      std::min(state.floor_drag.anchor_left, placement.left);
  const std::int32_t right =
      std::max(state.floor_drag.anchor_right, placement.right);
  const auto old_balance = ctx.document->header.balance;
  const auto old_height = ctx.document->header.lobby_height;
  const auto result = build_original_floor(
      *ctx.document, state.floor_drag.floor,
      static_cast<std::uint16_t>(left), static_cast<std::uint16_t>(right),
      ctx.construction_costs);
  (void)apply_original_lobby_result(ctx, host, result, old_balance,
                                    old_height, true);
  if (result.succeeded()) {
    keep_original_pointer_visible(host, client_x, client_y, true);
  }
}

void update_original_parking_drag(const OriginalWorldInputContext& ctx,
                                  OriginalWorldInputHost& host,
                                  OriginalWorldInputPoint position,
                                  bool refresh_horizontal_snap) {
  auto& state = ctx.state;
  if (state.parking_drag.kind == OriginalParkingDragKind::none ||
      !ctx.document) {
    return;
  }
  const auto view = host.main_view();
  const int client_x = position.x;
  const int client_y = position.y;
  const auto type = state.parking_drag.kind == OriginalParkingDragKind::parking
      ? 11U
      : 44U;
  const auto placement = original_facility_placement_from_client(
      type, client_x, client_y, view.x, view.y);
  if (refresh_horizontal_snap) {
    // 07d8 refreshes c6/c8 for move but not for double-click. Ramp passes
    // these live horizontal words to every 17fd attempt in the helper call.
    state.parking_drag.snapped_left = placement.left;
    state.parking_drag.snapped_right = placement.right;
  }

  std::vector<std::int32_t> parking_units;
  std::vector<OriginalParkingRampDragAttempt> ramp_attempts;
  OriginalParkingDragRunState parking_next{};
  OriginalParkingRampDragRunState ramp_next{};
  if (state.parking_drag.kind == OriginalParkingDragKind::parking) {
    auto plan = original_parking_drag_run_plan(
        state.parking_drag.parking,
        state.parking_drag.snapped_left,
        state.parking_drag.snapped_right,
        placement.left,
        placement.right);
    parking_units = std::move(plan.unit_lefts);
    parking_next = plan.next_state;
  } else {
    auto plan = original_parking_ramp_drag_run_plan(
        state.parking_drag.ramp,
        state.parking_drag.press_floor,
        placement.floor,
        state.parking_drag.snapped_left);
    ramp_attempts = std::move(plan.attempts);
    ramp_next = plan.next_state;
  }

  const auto old_balance = ctx.document->header.balance;
  bool attempted = false;
  bool final_attempt_succeeded = false;
  bool any_succeeded = false;
  const auto consume_result = [&](
      const OriginalConstructionResult& result) {
    attempted = true;
    final_attempt_succeeded = result.succeeded();
    any_succeeded = any_succeeded || final_attempt_succeeded;
    if (!result.succeeded() && result.construction_status_code != 0U) {
      host.construction_status(result.construction_status_code);
    }
  };
  for (const auto left : parking_units) {
    consume_result(build_original_parking(
        *ctx.document, state.parking_drag.press_floor,
        static_cast<std::uint16_t>(left), ctx.construction_costs));
  }
  for (const auto& attempt : ramp_attempts) {
    consume_result(build_original_parking_ramp(
        *ctx.document, attempt.floor,
        static_cast<std::uint16_t>(attempt.left),
        ctx.construction_costs));
  }

  if (any_succeeded) {
    apply_original_construction_mutation_presentation(
        host, true,
        ctx.document->header.balance != old_balance);
  }

  const auto completion = original_captured_helper_completion_plan(
      attempted, final_attempt_succeeded,
      state.completion.priority_sound_latch_armed);
  if (completion.increment_successful_step &&
      state.completion.active) {
    ++state.completion.successful_steps;
  }
  if (completion.sound ==
          OriginalCapturedHelperSound::priority_five) {
    host.play_sound(7001, 0, 5);
  } else if (completion.sound ==
                 OriginalCapturedHelperSound::reserved_if_idle) {
    host.play_reserved_if_idle(7001, 0, host.now_ms());
  }
  if (completion.clear_priority_sound_latch) {
    state.completion.priority_sound_latch_armed = false;
  }
  if (completion.returned_success) {
    // 240d passes one for horizontal auto-scroll; 25a2 passes zero for the
    // vertical axis. Both calls precede the helper's retained-pointer update.
    keep_original_pointer_visible(
        host, client_x, client_y,
        state.parking_drag.kind == OriginalParkingDragKind::parking);
  }

  // 2570/26a6 re-snap LPARAM after possible auto-scroll, so the next helper
  // call consumes the post-scroll world coordinate rather than the one above.
  const auto post_view = host.main_view();
  const auto post_placement =
      original_facility_placement_from_client(
          type, client_x, client_y, post_view.x, post_view.y);
  if (state.parking_drag.kind == OriginalParkingDragKind::parking) {
    parking_next.retained_left = post_placement.left;
    parking_next.retained_right = post_placement.right;
    state.parking_drag.parking = parking_next;
  } else {
    ramp_next.retained_floor = post_placement.floor;
    ramp_next.retained_upper_exclusive = static_cast<std::int16_t>(
        post_placement.floor + 1);
    state.parking_drag.ramp = ramp_next;
  }
}

void finish_original_construction_drag(const OriginalWorldInputContext& ctx,
                                       OriginalWorldInputHost& host) {
  auto& state = ctx.state;
  if (!ctx.document) return;
  const auto type = state.selected_build_type;
  const auto plan = original_construction_release_plan(
      type, state.completion.successful_steps,
      state.completion.balance_at_press,
      ctx.document->header.balance);
  if (!plan.handled) return;

  // Floor/Lobby's balance-change WAVE/7000 precedes RELEASECAPTURE at 0839;
  // Parking/Ramp deliberately never play this or the general success wave.
  if (plan.play_drag_success_wave) {
    host.play_sound(7000, 0, 4);
  }

  state.lobby_drag = {};
  state.floor_drag = {};
  state.parking_drag = {};
  state.completion = {};
  release_main_capture(ctx, host);

  if (!plan.complete_success_tail) {
    if (plan.play_failure_wave) {
      host.play_sound(7002, 0, 4);
    }
    return;
  }

  // 0e21's common success tail is reached once per captured command, never
  // once per accepted drag step. It clears the status, runs 1148:0163, then
  // executes the literal route-rebuild table before repaint/dirty handling.
  host.construction_status(0U);
  play_original_construction_success_audio(host, type);
  const auto old_balance = ctx.document->header.balance;
  const auto rating_completion =
      complete_original_rating_construction(
          *ctx.document, ctx.part, type);
  if (rating_completion.dialog.valid()) {
    (void)host.event_dialog(rating_completion.dialog);
  }
  if (original_construction_rebuilds_routes(type)) {
    rebuild_original_transport_route_graphs(*ctx.document);
  }
  apply_original_construction_mutation_presentation(
      host, true, ctx.document->header.balance != old_balance);
}


void forward_original_armed_double_click(const OriginalWorldInputContext& ctx,
                                         OriginalWorldInputHost& host,
                                         std::uint16_t keys,
                                         OriginalWorldInputPoint position) {
  auto& state = ctx.state;
  // MAINWNDPROC forwards WM_LBUTTONDBLCLK without arming DS:0242; 1058:003a
  // then makes it effective only if a prior down is still armed. The ordinary
  // Windows sequence clears that latch on the preceding button-up, but retain
  // the exact behavior for reentrant/synthetic sequences.
  if (!state.pointer_armed || !ctx.document) return;
  if (state.command_mode == 0U || state.command_mode == 2U) return;
  if (state.command_mode == 1U) {
    const auto view = host.main_view();
    const auto hit = original_elevator_hit_from_client(
        *ctx.document,
        static_cast<std::int16_t>(position.x),
        static_cast<std::int16_t>(position.y), view.x, view.y);
    if (hit.hit) {
      host.open_elevator_control(
          hit.elevator_index,
          static_cast<std::int16_t>(position.x),
          static_cast<std::int16_t>(position.y));
    }
    return;
  }
  if (!state.build_mode_enabled) return;
  if (state.parking_drag.kind != OriginalParkingDragKind::none) {
    update_original_parking_drag(ctx, host, position, false);
  } else if (state.floor_drag.active) {
    update_original_floor_drag(ctx, host, position);
  } else if (state.lobby_drag.active) {
    update_original_lobby_drag(ctx, host, position);
  } else if (state.completion.active &&
             (state.selected_build_type == 11U ||
              state.selected_build_type == 44U) &&
             state.retained_placement) {
    // 0x0203 does not refresh c4/c6/c8. A helper skipped by the down-time
    // bonus/Shift path therefore initializes from those retained snaps, while
    // its tail still publishes the double-click LPARAM for the following call.
    state.parking_drag.kind = state.selected_build_type == 11U
        ? OriginalParkingDragKind::parking
        : OriginalParkingDragKind::ramp;
    state.parking_drag.press_floor = state.retained_placement->floor;
    state.parking_drag.snapped_left = state.retained_placement->left;
    state.parking_drag.snapped_right = state.retained_placement->right;
    update_original_parking_drag(ctx, host, position, false);
  } else if (state.retained_press) {
    // 0x0203 bypasses 08ce's placement recomputation, 090e's counter reset,
    // 0946's capture, and 09c1's Shift replacement. Re-run the type attempt
    // at the coordinates retained by its already-armed down only.
    begin_original_lobby_drag(ctx, host, keys, *state.retained_press,
                              false);
  }
}

void publish_original_world_input_modifiers(OriginalWorldInputState& state,
                                            std::uint16_t keys) {
  const auto modifiers = original_world_input_modifiers(
      static_cast<std::uint16_t>(keys));
  state.control_modifier = modifiers.control;
  state.shift_modifier = modifiers.shift;
}

void dispatch_original_world_input(const OriginalWorldInputContext& ctx,
                                   OriginalWorldInputHost& host,
                                   OriginalWorldInputMessage message,
                                   std::uint16_t keys,
                                   OriginalWorldInputPoint position) {
  auto& state = ctx.state;
  // 1058 publishes modifiers even when one of the following gates suppresses
  // every tool action.
  publish_original_world_input_modifiers(state, keys);
  const bool isolation_active = host.elevator_isolation_active();
  const bool emergency_active =
      ctx.document &&
      original_emergency_people_pass_active(*ctx.document);
  const auto plan = original_world_input_plan(
      message, state.pointer_armed, isolation_active,
      emergency_active, state.command_mode, state.build_mode_enabled,
      (keys & kOriginalKeyLButton) != 0U);

  switch (plan.action) {
    case OriginalWorldInputAction::none:
      break;
    case OriginalWorldInputAction::emergency_feedback:
      host.play_sound(7002, 0, 4);
      break;
    case OriginalWorldInputAction::bulldozer:
      run_original_facility_bulldozer(ctx, host, position);
      break;
    case OriginalWorldInputAction::elevator_finger:
      switch (message) {
        case OriginalWorldInputMessage::button_down:
          begin_original_elevator_finger(ctx, host, position);
          break;
        case OriginalWorldInputMessage::mouse_move:
          continue_original_elevator_finger(ctx, host, position);
          break;
        case OriginalWorldInputMessage::button_up:
          end_original_lobby_drag(ctx, host);
          break;
        case OriginalWorldInputMessage::double_click:
          double_click_original_elevator_finger(ctx, host, position);
          break;
      }
      break;
    case OriginalWorldInputAction::magnifier:
      run_original_magnifying_glass(ctx, host, position);
      break;
    case OriginalWorldInputAction::construction:
      switch (message) {
        case OriginalWorldInputMessage::button_down:
          begin_original_lobby_drag(ctx, host, keys, position);
          break;
        case OriginalWorldInputMessage::mouse_move:
          if ((keys & kOriginalKeyLButton) != 0U) {
            if (state.parking_drag.kind != OriginalParkingDragKind::none) {
              update_original_parking_drag(ctx, host, position);
            } else if (state.floor_drag.active) {
              update_original_floor_drag(ctx, host, position);
            } else if (state.lobby_drag.active) {
              update_original_lobby_drag(ctx, host, position);
            } else if (state.completion.active &&
                       (state.selected_build_type == 11U ||
                        state.selected_build_type == 44U)) {
              begin_original_lobby_drag(ctx, host, keys, position, false);
            }
          }
          break;
        case OriginalWorldInputMessage::button_up:
          finish_original_construction_drag(ctx, host);
          break;
        case OriginalWorldInputMessage::double_click:
          forward_original_armed_double_click(ctx, host, keys, position);
          break;
      }
      break;
  }

  // Mode two performs this tail after its down-only hit chain and after every
  // other routed mouse phase. 1100:0000 may set DS:77c0 while the Person
  // Information modal runs, so inspect the live post-action latch here.
  // 1058:033c owns the complete Find/build toggle.
  if (plan.check_find_exit_latch && host.find_exit_latch_set()) {
    apply_original_construction_toggle(ctx, host);
  }
}

}  // namespace

void end_original_lobby_drag(const OriginalWorldInputContext& ctx,
                             OriginalWorldInputHost& host) {
  auto& state = ctx.state;
  const bool elevator_finger_capture_active =
      state.shaft_drag.capture_active;
  if (state.lobby_drag.active || state.floor_drag.active ||
      state.parking_drag.kind != OriginalParkingDragKind::none ||
      state.completion.active ||
      elevator_finger_capture_active) {
    state.lobby_drag = {};
    state.floor_drag = {};
    state.parking_drag = {};
    state.shaft_drag = {};
    state.completion = {};
    release_main_capture(ctx, host);
    // 10a0:05a2 restores the Finger cursor immediately after releasing its
    // capture, independently of whether a cap drag became active.
    if (elevator_finger_capture_active) {
      host.set_cursor(1004U);
    }
  }
}

void original_main_pointer_down(const OriginalWorldInputContext& ctx,
                                OriginalWorldInputHost& host,
                                OriginalWorldInputPoint point,
                                bool double_click) {
  // MAINWNDPROC WM_LBUTTONDOWN / WM_LBUTTONDBLCLK (1158:028c/029f).
  auto& state = ctx.state;
  const auto phase = double_click
      ? OriginalMainPointerMessagePhase::double_click
      : OriginalMainPointerMessagePhase::button_down;
  const auto plan = original_main_pointer_message_plan(
      phase, host.modal_input_locked(), state.paint_active,
      (point.keys & kOriginalKeyLButton) != 0U, state.pointer_armed);
  if (!plan.forward_to_world_dispatch) return;
  if (plan.arm_interaction) state.pointer_armed = true;
  dispatch_original_world_input(
      ctx, host,
      phase == OriginalMainPointerMessagePhase::button_down
          ? OriginalWorldInputMessage::button_down
          : OriginalWorldInputMessage::double_click,
      point.keys, point);
}

void original_main_pointer_move(const OriginalWorldInputContext& ctx,
                                OriginalWorldInputHost& host,
                                OriginalWorldInputPoint point) {
  // 1158:0314 obtains the live screen point and runs the complete
  // 1258:0505 cursor resolver before DS:24b8 can suppress world input.
  const auto move_plan =
      original_main_mouse_move_plan(host.modal_input_locked());
  if (move_plan.refresh_cursor) host.refresh_cursor();
  if (!move_plan.forward_to_world_dispatch) return;
  dispatch_original_world_input(ctx, host,
                                OriginalWorldInputMessage::mouse_move,
                                point.keys, point);
}

void original_main_pointer_up(const OriginalWorldInputContext& ctx,
                              OriginalWorldInputHost& host,
                              OriginalWorldInputPoint point) {
  // MAINWNDPROC WM_LBUTTONUP (1158:02d5).
  auto& state = ctx.state;
  const auto plan = original_main_pointer_message_plan(
      OriginalMainPointerMessagePhase::button_up, host.modal_input_locked(),
      state.paint_active, (point.keys & kOriginalKeyLButton) != 0U,
      state.pointer_armed);
  if (!plan.forward_to_world_dispatch) return;
  dispatch_original_world_input(ctx, host,
                                OriginalWorldInputMessage::button_up,
                                point.keys, point);
  if (plan.clear_interaction) {
    state.pointer_armed = false;
  }
}

std::optional<OriginalConstructionPreviewRect>
original_world_input_preview_rect(const OriginalWorldInputState& state,
                                  OriginalWorldPoint view) {
  if (!state.pointer.inside || !state.build_mode_enabled ||
      state.command_mode < 3U) {
    return std::nullopt;
  }
  return original_construction_preview_rect(
      state.selected_build_type, state.pointer.client_x,
      state.pointer.client_y, view.x, view.y);
}

OriginalPreviewOnlyPassPlan original_preview_only_pass_plan(
    OriginalWorldInputState& state,
    bool isolation_active,
    const std::optional<OriginalConstructionPreviewRect>& previous_rect,
    const std::optional<OriginalConstructionPreviewRect>& current_rect) {
  // 1090:03bf-0427 always restores/derives DS:77ac, then EQUALRECT alone
  // decides whether 1158:0ae5 presents the changed rectangle. This repaint
  // stays out of the full-frame RNG/person-animation path.
  //
  // The original derives the outline (and clears the scratch latch through
  // the draw plan) after the restore-side checkpoints and after clearing
  // DS:77ac. Computing the whole plan up front is equivalent: a checkpoint
  // only reaps finished audio buffers (OriginalAudioRuntime::pump, no
  // message dispatch), and current_rect depends on the pointer sample, the
  // tool and the view, never on DS:77ac. The latch is still advanced before
  // the host's rebuild_original_main_backing, as in the original.
  const auto restore = original_construction_preview_restore_plan(
      state.preview_scratch, isolation_active, previous_rect.has_value());
  const auto draw = original_construction_preview_draw_plan(
      state.preview_scratch, state.build_mode_enabled, state.command_mode);
  const auto presentation =
      original_preview_only_presentation_plan(previous_rect == current_rect);
  return {
      .checkpoints_before = restore.audio_checkpoints,
      .clear_previous = restore.clear_previous_rectangle,
      .new_rect = current_rect,
      .present_main_direct = presentation.present_main_direct,
      .checkpoints_after = draw.audio_checkpoints,
  };
}

}  // namespace simtower
