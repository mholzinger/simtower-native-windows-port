// Command (CMDBTNWNDPROC 1050:0000, 1058:03a9), its grouped selector
// (CMDBTNSUBWNDPROC 1050:05a7), Info's close box (INFOWNDPROC 1120:0111)
// and Map (MAPWNDPROC 1168:0000, 1058:01d6) pointer input. See
// original_world_input.hpp.
#include "original_world_input.hpp"

#include "original_palette_frame.hpp"

#include <algorithm>
#include <utility>

namespace simtower {

OriginalCommandSelectorSession::OriginalCommandSelectorSession(
    OriginalCommandGroup group,
    int anchor_left,
    int anchor_top)
    : group_(std::move(group)),
      choice_(group_.selection_index),
      highlight_(group_.selection_index),
      anchor_left_(anchor_left),
      anchor_top_(anchor_top) {}

int OriginalCommandSelectorSession::top(int desktop_bottom) const {
  return original_command_selector_top(anchor_top_, choice_,
                                       group_.catalog_icons.size(),
                                       desktop_bottom);
}

std::optional<int> OriginalCommandSelectorSession::row_at(int x,
                                                          int y,
                                                          int client_width,
                                                          int client_height) {
  if (x < 0 || y < 0 || x >= client_width || y >= client_height) {
    return std::nullopt;
  }
  return y / 32;
}

OriginalCommandSelectorStep OriginalCommandSelectorSession::on_move(
    std::optional<int> row) {
  if (!row) {
    // 1050:07b9 closes through the common FALSE return, unlike every
    // in-bounds mouse route.
    return {.end = true, .unclip_and_release = true, .handled = false};
  }
  const std::uint16_t highlight = static_cast<std::uint16_t>(
      group_.catalog_icons[static_cast<std::size_t>(*row)] + 1U);
  if (highlight != highlight_) {
    highlight_ = highlight;
    return {.redraw = true};
  }
  return {};
}

OriginalCommandSelectorStep OriginalCommandSelectorSession::on_lbutton_down(
    std::optional<int> row) {
  if (!row) return {.end = true, .unclip_and_release = true};
  choice_ = static_cast<std::uint16_t>(*row + 1);
  // 1050:080c-0836 commits on button-down but, unlike the release route,
  // does not explicitly unclip the pointer or release capture first.
  return {.end = true, .accepted = true};
}

OriginalCommandSelectorStep OriginalCommandSelectorSession::on_lbutton_up(
    std::optional<int> row) {
  if (!row) return {.end = true, .unclip_and_release = true};
  choice_ = static_cast<std::uint16_t>(*row + 1);
  return {.end = true, .accepted = true, .unclip_and_release = true};
}

OriginalCommandSelectorStep
OriginalCommandSelectorSession::on_rbutton_down() {
  return {.end = true, .unclip_and_release = true};
}

void select_original_map_mode(const OriginalWorldInputContext& ctx,
                              OriginalWorldInputHost& host,
                              std::uint16_t mode) {
  auto& state = ctx.state;
  const std::uint16_t maximum =
      ctx.document && ctx.document->header.rating == 1U ? 2U : 3U;
  if (mode > maximum) return;

  // 11d0:0000 couples every map overlay selection to edit mode two. Mode
  // zero restores construction; modes one through three disable it.
  state.map_mode = mode;
  state.build_mode_enabled = mode == 0U;
  state.command_mode = 2U;
  if (mode == 1U && ctx.document) {
    // 11d0:0031-0039 invokes the complete 1130:00b5 all-tenant
    // satisfaction refresh before invalidating the Map and world surfaces.
    refresh_original_map_tenant_satisfaction(*ctx.document, ctx.part);
  }
  if (mode == 0U) {
    // 11d0:001d clears the transient Find target while restoring
    // construction mode.
    host.reset_find_marker();
  }
  host.invalidate_map();
  host.invalidate_command();
  // 11d0:0066 supplies zero: the overlay redraw includes facility people but
  // deliberately leaves the 1048:03a3 sky-decoration state untouched.
  host.request_main_pass(OriginalMainSurfacePass::rebuild_without_sky, true);
}

void apply_original_construction_toggle(const OriginalWorldInputContext& ctx,
                                        OriginalWorldInputHost& host) {
  auto& state = ctx.state;
  const auto plan = original_construction_toggle_plan(
      state.build_mode_enabled, state.map_mode);
  if (plan.path == OriginalConstructionTogglePath::exit_map_overlay) {
    // 1058:0352 clears DS:7840 first and delegates the complete repaint/state
    // transaction to 11d0:0000, bypassing the ordinary WAVMIX branch.
    select_original_map_mode(ctx, host, 0U);
    return;
  }

  state.build_mode_enabled = plan.build_mode_after;
  if (plan.force_command_mode_two) state.command_mode = 2U;
  if (plan.reset_find_marker) {
    host.reset_find_marker();
  }
  host.audio_transition(plan.audio);
  if (plan.refresh_command_synchronously) {
    host.present_command_now();
  }
  if (plan.restore_preview_scratch) {
    // 11f8:3b94 clears DS:77ac after restoring the retained old outline.
    state.preview_rect.reset();
  }
  if (plan.present_main_synchronously) {
    host.request_main_pass(OriginalMainSurfacePass::preview_repaint, true);
  }
}

OriginalCommandRatingState& active_original_command_rating(
    const OriginalWorldInputContext& ctx,
    std::uint16_t rating) {
  const std::uint16_t bounded = std::clamp<std::uint16_t>(rating, 1U, 6U);
  auto& state = ctx.state.command_ratings[bounded - 1U];
  if (state.encoded_entries.empty()) {
    state = original_command_rating_state(ctx.resources, bounded);
  }
  return state;
}

namespace {

void resize_original_command_window(const OriginalWorldInputContext& ctx,
                                    OriginalWorldInputHost& host,
                                    std::uint16_t rating) {
  const auto count = original_command_catalog(
                         ctx.resources,
                         active_original_command_rating(ctx, rating))
                         .size();
  // 1140:010d passes the result of 1140:03f8 directly to MoveWindow as the
  // outer height, preserving the existing screen position and width.
  host.resize_command(
      original_rating_window_height(static_cast<std::uint16_t>(count)));
}

void run_original_command_selector(const OriginalWorldInputContext& ctx,
                                   OriginalWorldInputHost& host,
                                   OriginalCommandRatingState& state,
                                   std::size_t catalog_index) {
  // Native transaction for 1050:0533 and 1058:04e0: launch the modal command
  // selector, resolve TABM group/current choice,
  // present CMDBTNSUBWNDPROC only while selecting, persist an accepted choice,
  // then let the caller resolve TABL/1000's construction type.
  const auto group =
      original_command_group(ctx.resources, state, catalog_index);
  if (!group) {
    return;
  }

  // 1058:0517-053f opens the captured selector only while the physical
  // primary button is down. This matters when CMDBTNWNDPROC's button-up path
  // re-enters a grouped command after a cancelled drag: the original keeps
  // the current choice instead of opening a second modal popup.
  const bool primary_button_down = host.primary_button_down();
  if (!original_command_selector_transaction_plan(primary_button_down, false)
           .show_modal) {
    return;
  }

  const auto cell = original_command_facility_rect(
      static_cast<std::uint16_t>(catalog_index));
  const auto command_outer = host.command_outer_origin();
  OriginalCommandSelectorSession session{
      *group, command_outer.x + cell.left, command_outer.y + cell.top};
  const bool accepted = host.run_command_selector(session);
  if (original_command_selector_transaction_plan(primary_button_down,
                                                 accepted)
          .write_choice) {
    original_command_select_group_choice(ctx.resources, state,
                                         catalog_index, session.choice());
  }
}

void activate_original_command_point(const OriginalWorldInputContext& ctx,
                                     OriginalWorldInputHost& host,
                                     int x,
                                     int y) {
  // 1058:03a9 ignores command activation while 10f0's isolated Elevator
  // simulation owns the mutable world, although the pressed frame above may
  // still be presented by CMDBTNWNDPROC.
  if (host.elevator_isolation_active()) {
    return;
  }
  auto& state = ctx.state;
  auto& rating_state =
      active_original_command_rating(ctx, state.active_command_rating);
  const auto hit = original_command_hit_test(
      ctx.resources, rating_state, state.build_mode_enabled, x, y);
  switch (hit.kind) {
    case OriginalCommandHitKind::build_toggle:
      apply_original_construction_toggle(ctx, host);
      return;
    case OriginalCommandHitKind::edit_mode:
      state.command_mode = hit.mode_index;
      // 1058:0439 calls 1118:0ad5(0) when an edit tool is chosen.
      host.command_status(0U);
      break;
    case OriginalCommandHitKind::facility:
      state.command_mode = hit.mode_index;
      run_original_command_selector(
          ctx, host, rating_state,
          static_cast<std::size_t>(hit.mode_index - 3U));
      state.selected_build_type = original_command_build_type(
          ctx.resources,
          original_command_catalog(ctx.resources, rating_state)
              [hit.mode_index - 3U]);
      // 11f8:0f63 loads TABL/1000's raw type byte and calls
      // 1118:0ad5(type+1), selecting the matching STRL/1009 cost line.
      host.command_status(
          static_cast<std::uint16_t>(state.selected_build_type + 1U));
      break;
    case OriginalCommandHitKind::none:
      return;
  }
  host.invalidate_command();
  host.invalidate_main();
  host.invalidate_map();
}

void update_original_map_drag(const OriginalWorldInputContext& ctx,
                              OriginalWorldInputHost& host,
                              int frame_x,
                              int frame_y) {
  // Exact 1058:0284 Map content drag: remove the eight-pixel client frame,
  // map the clicked overview point to the centered world viewport, and publish
  // both scroll axes synchronously through the native view boundary.
  auto& state = ctx.state;
  // Without a Main client there is no view to center; the inline Win32
  // code returned here when g_main_window was null.
  //
  // Intentional deviation: main_client() runs before the Map bounds check
  // and its failure returns early. The inline code checked g_main_window,
  // then the bounds, and only then called GetClientRect unchecked (a failed
  // call left a zero RECT). So the Win32 host now calls GetClientRect for
  // out-of-bounds points too, and a failed GetClientRect on a live Main
  // returns instead of centering on an empty client. Neither changes any
  // state: GetClientRect has no side effects and does not fail on the
  // valid Main HWND.
  const auto client = host.main_client();
  if (!client) return;
  const auto& main_client = *client;
  const int map_x = frame_x;
  const int map_y = frame_y - kOriginalMapClientTop;
  if (map_x < 0 || map_x >= kOriginalMapWidth ||
      map_y < kOriginalMapToolbarHeight ||
      map_y >= kOriginalMapBackingHeight) {
    return;
  }
  const auto view = host.main_view();
  const auto centered = original_map_centered_view(
      map_x, map_y, view.x, view.y,
      main_client.right - main_client.left,
      main_client.bottom - main_client.top);
  // 1058:06df returns false when EQUALRECT says the focus rectangle is
  // unchanged. 0284 then skips both scroll publication and its two XOR focus
  // draws, so do not invalidate either native surface on that no-op path.
  if (centered == view) return;

  // 1058:02d7-032e pumps, XOR-erases the old Map focus, commits/clamps both
  // scroll positions, XOR-draws the new focus, pumps, presents Main through
  // 1080:0a1e(1), releases the Map DC, and pumps once more in that order.
  host.audio_pump();
  host.begin_map_focus();
  const auto focus_for_view = [&](OriginalWorldPoint focus_view) {
    return original_map_view_rect(focus_view.x, focus_view.y,
                                  main_client.right - main_client.left,
                                  main_client.bottom - main_client.top);
  };
  const auto previous_focus = state.map_focus.value_or(focus_for_view(view));
  host.xor_map_focus(previous_focus);

  host.set_main_scroll_position(centered.x, centered.y);
  const auto updated_focus = focus_for_view(host.main_view());
  host.xor_map_focus(updated_focus);
  state.map_focus = updated_focus;

  host.audio_pump();
  host.request_main_pass(OriginalMainSurfacePass::rebuild_with_sky, true);
  host.end_map_focus();
  host.audio_pump();
}

}  // namespace

void refresh_original_rating_command(const OriginalWorldInputContext& ctx,
                                     OriginalWorldInputHost& host,
                                     std::uint16_t rating,
                                     std::uint16_t argument,
                                     bool command_composition_allowed) {
  auto& state = ctx.state;
  const auto plan = original_rating_command_refresh_plan(
      argument, host.elevator_isolation_active(),
      state.preview_rect.has_value());

  // Native keeps immutable value views for 1140:010d's rating TABL and
  // 11f8:06cd's rating-dependent facility sheets, so selecting the cached
  // rating state replaces both Win16 resource-handle transactions.
  auto& rating_state = active_original_command_rating(ctx, rating);
  state.active_command_rating = rating_state.rating;
  resize_original_command_window(ctx, host, state.active_command_rating);
  if (plan.force_command_mode_two) state.command_mode = 2U;
  if (plan.present_command_synchronously && command_composition_allowed) {
    host.present_command_now();
  }
  if (plan.restore_preview_scratch) {
    state.preview_rect.reset();
  }
  if (plan.rebuild_visible_tile_scratch) {
    // 1038:0000 rebuilds a transient indexed tile cache. Native derives that
    // cache from the document on demand, so invalidate only its retained RGB
    // transport; do not invent a synchronous Main presentation here.
    host.mark_main_backing_dirty();
  }
}

void original_command_pointer_down(const OriginalWorldInputContext& ctx,
                                   OriginalWorldInputHost& host,
                                   int frame_x,
                                   int frame_y) {
  const auto plan = original_command_pointer_plan(
      OriginalCommandPointerPhase::button_down, frame_x, frame_y);
  if (plan.close_palette) {
    host.hide_palette(OriginalAuxiliaryWindow::command);
    return;
  }
  // 1050:0219 delays only the build toggle. Every edit/facility point
  // enters 1058:03a9 on mouse-down, allowing 1050:05a7's grouped selector
  // to capture the still-held button and consume its eventual release.
  if (plan.press_toggle) {
    ctx.state.command_toggle_pressed = true;
    host.present_command_now();
  }
  if (plan.activate_point) {
    activate_original_command_point(ctx, host, frame_x, frame_y);
  }
  // 1050:02a4-02ad enters 1208:05e6 after every non-close button-down,
  // even when the grouped selector held the call until button release.
  // Preserve both DS:31b0/31b2 and 11e0:0e84's pump side effect.
  if (plan.sample_coarse_tick) {
    host.sample_auxiliary_pointer_tick();
  }
}

void original_command_pointer_up(const OriginalWorldInputContext& ctx,
                                 OriginalWorldInputHost& host,
                                 int frame_x,
                                 int frame_y) {
  const auto plan = original_command_pointer_plan(
      OriginalCommandPointerPhase::button_up, frame_x, frame_y);
  // 1050:02b3 restores 1080:07a6's normal header unconditionally before
  // applying the release point outside the close box.
  if (plan.restore_toggle) {
    ctx.state.command_toggle_pressed = false;
    host.present_command_now();
  }
  if (plan.activate_point) {
    activate_original_command_point(ctx, host, frame_x, frame_y);
  }
}

void original_map_pointer_down(const OriginalWorldInputContext& ctx,
                               OriginalWorldInputHost& host,
                               int frame_x,
                               int frame_y) {
  auto& state = ctx.state;
  if (original_palette_frame_hit_test(frame_x, frame_y) ==
      OriginalPaletteFrameHit::close) {
    // 1168:0112 clears DS:31ac and hides the palette.
    host.hide_palette(OriginalAuxiliaryWindow::map);
    return;
  }
  const auto pointer_plan = original_map_pointer_message_plan(
      OriginalMapPointerMessage::button_down, state.map_drag_active, true);
  if (pointer_plan.set_pointer_down) {
    state.map_pointer_down = true;
  }
  // Exact 1058:01d6 mouse-down split: 1058:085c's four 50x18 toolbar
  // rectangles select a mode; the content area captures and calls 0284.
  const int x = frame_x;
  const int map_y = frame_y - kOriginalMapClientTop;
  const std::uint16_t rating =
      ctx.document ? ctx.document->header.rating : 1U;
  if (const auto mode = original_map_toolbar_mode_at(x, map_y, rating)) {
    select_original_map_mode(ctx, host, *mode);
  } else if (x >= 0 && x < kOriginalMapWidth &&
             map_y >= kOriginalMapToolbarHeight &&
             map_y < kOriginalMapBackingHeight) {
    state.map_drag_active = true;
    host.set_capture(OriginalInputSurface::map);
    update_original_map_drag(ctx, host, frame_x, frame_y);
  }
}

bool original_info_pointer_down(const OriginalWorldInputContext&,
                                OriginalWorldInputHost& host,
                                int frame_x,
                                int frame_y) {
  // INFOWNDPROC 1120:0111: only the close box reacts to the mouse.
  if (original_palette_frame_hit_test(frame_x, frame_y) !=
      OriginalPaletteFrameHit::close) {
    return false;
  }
  host.hide_palette(OriginalAuxiliaryWindow::info);
  return true;
}

bool original_map_pointer_move(const OriginalWorldInputContext& ctx,
                               OriginalWorldInputHost& host,
                               int frame_x,
                               int frame_y,
                               std::uint16_t keys) {
  const auto plan = original_map_pointer_message_plan(
      OriginalMapPointerMessage::mouse_move, ctx.state.map_drag_active,
      (keys & kOriginalKeyLButton) != 0U);
  if (plan.update_drag) update_original_map_drag(ctx, host, frame_x, frame_y);
  return plan.consume_message;
}

bool original_map_pointer_up(const OriginalWorldInputContext& ctx,
                             OriginalWorldInputHost& host) {
  // MAPWNDPROC 1168:0156 clears both pointer/drag words and releases
  // capture on every Map button-up, not only after a content drag.
  auto& state = ctx.state;
  const auto plan = original_map_pointer_message_plan(
      OriginalMapPointerMessage::button_up, state.map_drag_active, false);
  if (plan.clear_pointer_down) state.map_pointer_down = false;
  if (plan.clear_drag) state.map_drag_active = false;
  if (plan.release_capture) host.release_capture();
  return plan.consume_message;
}

}  // namespace simtower
