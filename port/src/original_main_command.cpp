#include "original_main_command.hpp"

namespace simtower {
namespace {

void toggle_flag(OriginalMainCommandHost& host,
                 std::uint16_t command,
                 OriginalMainCommandFlag flag) {
  bool& enabled = host.command_flag(flag);
  enabled = !enabled;
  host.apply_menu_op(original_menu_check_op(command, enabled));
}

void toggle_audio_category(OriginalMainCommandHost& host,
                           std::uint16_t command,
                           std::size_t category) {
  if (!host.audio_available()) return;
  const bool was_enabled = host.audio_category_enabled(category);
  // 1158:07c1/07fd/0839 flush every channel before disabling any category.
  if (was_enabled) host.stop_all_audio(true);
  host.set_audio_category_enabled(category, !was_enabled);
  host.apply_menu_op(original_menu_check_op(command, !was_enabled));
}

void toggle_auxiliary(OriginalMainCommandHost& host,
                      std::uint16_t command,
                      OriginalAuxiliaryWindow window,
                      OriginalAuxiliaryInsertAfter insert_after) {
  bool& visible = host.auxiliary_visible(window);
  const auto plan = original_auxiliary_visibility_plan(
      OriginalAuxiliaryVisibilityTrigger::menu_command, visible);
  visible = plan.visible;
  if (plan.operation == OriginalAuxiliaryWindowOperation::show) {
    // 1158:08b5-09a2 uses the literal flag word 0x43: keep the z-order
    // target while showing without moving or sizing.
    host.show_auxiliary(window, insert_after);
  } else {
    host.hide_auxiliary(window);
  }
  if (plan.update_menu_check) {
    host.apply_menu_op(original_menu_check_op(command, visible));
  }
}

}  // namespace

OriginalMainCommandResult dispatch_original_main_command(
    OriginalMainCommandHost& host,
    std::uint16_t command) {
  // MAINWNDPROC 1158:046f consumes all application commands while the
  // process-wide modal-manager latch at DS:24b8 is active.
  if (host.modal_input_locked()) return OriginalMainCommandResult::handled;
  switch (command) {
    case 3000U:
    case 3001U:
    case 3002U:
      (void)host.event_dialog({command, 0, 10000});
      break;
    case kOriginalCommandExit:
      // 1158:071a uses 10d0:0604's confirmation/latch/audio transaction,
      // then destroys Main without WM_CLOSE's DS:3258-clear tail.
      host.exit_command();
      break;
    case kOriginalCommandSaveAs:
      // 10d0:03f1.
      (void)save_original_tower_as(host.tower_file());
      break;
    case kOriginalCommandSave:
      // 10d0:0305 (Save As for an untitled tower).
      (void)save_original_tower(host.tower_file());
      break;
    case kOriginalCommandOpen:
      // 10d0:0122.
      (void)open_original_tower(host.tower_file());
      break;
    case kOriginalCommandNew:
      // 10d0:001d: the STRL/1001 action 1 confirmation, then the New path.
      if (confirm_original_tower_transition(host.tower_file(), 1)) {
        host.tower_file().begin_new_tower();
      }
      break;
    case kOriginalCommandFastMode:
      // 1158:0759 toggles Fast Mode at DS:de34.
      toggle_flag(host, command, OriginalMainCommandFlag::fast_mode);
      break;
    case kOriginalCommandFireRescue:
      // Exact 10e8:01e2 reconsideration path, available only after a
      // scheduled fire-crew offer was declined.
      if (auto* document = host.document();
          document && original_fire_crew_menu_offer_available(*document)) {
        const auto choice = static_cast<std::uint16_t>(
            host.event_dialog(original_fire_crew_offer(host.part())));
        const auto resolution = resolve_original_fire_crew_offer(
            *document, host.part(), choice, false);
        if (host.consume_fire_crew_resolution(resolution)) {
          host.fire_crew_hired();
        }
      }
      break;
    case kOriginalCommandPeople:
      // 1158:0723 toggles People animation at DS:de30.
      toggle_flag(host, command, OriginalMainCommandFlag::people_animation);
      break;
    case kOriginalCommandEffects:
      // 1158:073e toggles Effects animation at DS:de32.
      toggle_flag(host, command, OriginalMainCommandFlag::effects_animation);
      break;
    case kOriginalCommandSoundElevators:
      toggle_audio_category(host, command, 0);
      break;
    case kOriginalCommandSoundBackground:
      // The original command table associates 40012 with DS:de2c (Events),
      // despite the MENU resource labelling it Background.
      toggle_audio_category(host, command, 1);
      break;
    case kOriginalCommandSoundEvents:
      // Likewise 40013 controls DS:de2e (Background) in the binary.
      toggle_audio_category(host, command, 2);
      break;
    case kOriginalCommandToolBar:
      // The command palette is the one auxiliary window promoted with
      // HWND_TOPMOST by the original dispatcher.
      toggle_auxiliary(host, command, OriginalAuxiliaryWindow::command,
                       OriginalAuxiliaryInsertAfter::topmost);
      break;
    case kOriginalCommandInfoBar:
      toggle_auxiliary(host, command, OriginalAuxiliaryWindow::info,
                       OriginalAuxiliaryInsertAfter::top);
      break;
    case kOriginalCommandMapWindow:
      toggle_auxiliary(host, command, OriginalAuxiliaryWindow::map,
                       OriginalAuxiliaryInsertAfter::top);
      break;
    case kOriginalCommandFinance:
      // 1158:08fb -> 1060:0083 opens DIALOG/500's Finance modal.
      host.finance_dialog();
      break;
    case kOriginalCommandAbout:
      // 1158:0912 -> 1010:049e opens the animated resource-backed About.
      host.about_dialog();
      break;
    case kOriginalCommandFindPerson:
      // 1158:0903 -> 10d8:0000(1): Find a saved named person.
      host.find_dialog(OriginalFindMode::person);
      break;
    case kOriginalCommandFindTenant:
      // 1158:0907 -> 10d8:0000(0): Find a saved named tenant.
      host.find_dialog(OriginalFindMode::tenant);
      break;
    case kOriginalCommandHelpContents:
      // 1158:091a: the menu item and the F1 accelerator both request the
      // supplied SIMTOWER.HLP contents through WinHelp command 3.
      host.help_contents();
      break;
    case kOriginalCommandBomb:
      if (host.bomb_offer()) host.world_interaction_changed();
      break;
    case kOriginalCommandTreasure: {
      // Hidden Treasure command calls 1148:020f directly, bypassing the
      // normal construction/span predicate.
      auto* document = host.document();
      if (!document) break;
      const auto treasure =
          award_original_rating_treasure(*document, host.part());
      if (treasure.dialog.valid()) (void)host.event_dialog(treasure.dialog);
      if (treasure.changed) {
        host.mark_document_dirty();
        // 1148:025d-026c presents DIALOG/3040 first, then 1178:076f credits
        // and directly repaints Info. The b922 latch itself has no Main/Map
        // presentation side effect.
        if (treasure.treasure_awarded) host.present_info_status();
      }
      break;
    }
    case kOriginalCommandFire:
      // The same 10e8:0029 boundary the scheduler uses.
      if (host.fire_offer()) host.world_interaction_changed();
      break;
    case kOriginalCommandPresent:
      // 1158:0946 acquires the Main DC and calls 1158:0ba8 directly.
      host.present_main_direct();
      break;
    default:
      // 1158:0992 sends any non-table DIALOG id in 3000..4001 through
      // 1068:0000 with argument zero and WAVE/10000; 1158:09b1 then calls
      // DefWindowProc for every id outside the literal command table.
      if (original_main_command_route(command) ==
          OriginalMainCommandRoute::generic_dialog_then_def_window_proc) {
        (void)host.event_dialog({command, 0, 10000});
      }
      return OriginalMainCommandResult::def_window_proc;
  }
  return OriginalMainCommandResult::handled;
}

}  // namespace simtower
