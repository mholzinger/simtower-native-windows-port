#pragma once

// Portable MAINWNDPROC WM_COMMAND dispatcher (1158:046f, the 27-entry table
// at 1158:06b9/09d0 and its 1158:0992/09b1 fallthrough). The Win32 host
// (native_main.cpp main_window_proc) forwards LOWORD(wParam) here and calls
// DefWindowProc when told to; the SDL session dispatches menu picks, the F1
// accelerator and scripted "menu <id>" commands through the same function.
// Every effect outside the game model goes through OriginalMainCommandHost,
// in the order the Win32 host performed it inline.

#include "original_find.hpp"
#include "original_menu.hpp"
#include "original_simulation.hpp"
#include "original_tables.hpp"
#include "original_tdt.hpp"
#include "original_tower_file.hpp"
#include "original_ui.hpp"

#include <cstddef>
#include <cstdint>

namespace simtower {

enum class OriginalMainCommandResult : std::uint8_t {
  handled,          // MAINWNDPROC returns 0
  def_window_proc,  // the caller passes the message to DefWindowProc
};

// The three Options words the toggles flip.
enum class OriginalMainCommandFlag : std::uint8_t {
  fast_mode,          // DS:de34, 40007
  people_animation,   // DS:de30, 40009
  effects_animation,  // DS:de32, 40010
};

class OriginalMainCommandHost {
 public:
  virtual ~OriginalMainCommandHost() = default;

  // DS:24b8: while set, every command is consumed without effect.
  virtual bool modal_input_locked() = 0;
  // 1068:0000 (run_original_event_dialog); the result is the dialog's.
  virtual int event_dialog(const OriginalEventDialogRequest& request) = 0;

  // File menu. 40001: 10d0:0604's confirmation/latch/audio transaction and
  // the Main teardown (original_main_shutdown_plan, exit_command).
  virtual void exit_command() = 0;
  // 40002-40005 run the shared original_tower_file.hpp transactions on
  // this host.
  virtual OriginalTowerFileHost& tower_file() = 0;

  // The Options words, read and written in place.
  virtual bool& command_flag(OriginalMainCommandFlag flag) = 0;
  // One CheckMenuItem/EnableMenuItem on Main's menu.
  virtual void apply_menu_op(const OriginalMenuOp& op) = 0;

  // The live tower (null without one) and the PART table.
  virtual OriginalTdtDocument* document() = 0;
  virtual const OriginalPartTable& part() = 0;
  // consume_original_fire_crew_resolution through the host's idle-pass
  // host; true when Main and Info must be invalidated.
  virtual bool consume_fire_crew_resolution(
      const OriginalFireCrewResolution& resolution) = 0;
  // InvalidateRect on Main (backing dirty) and on Info.
  virtual void fire_crew_hired() = 0;

  // The audio runtime; the category calls are made only when it exists.
  virtual bool audio_available() = 0;
  virtual bool audio_category_enabled(std::size_t category) = 0;
  virtual void set_audio_category_enabled(std::size_t category,
                                          bool enabled) = 0;
  virtual void stop_all_audio(bool force) = 0;

  // DS:31a8/31aa/31ac, read and written in place.
  virtual bool& auxiliary_visible(OriginalAuxiliaryWindow window) = 0;
  // SetWindowPos(flags 0x43) at insert_after, or ShowWindow(SW_HIDE).
  virtual void show_auxiliary(OriginalAuxiliaryWindow window,
                              OriginalAuxiliaryInsertAfter insert_after) = 0;
  virtual void hide_auxiliary(OriginalAuxiliaryWindow window) = 0;

  virtual void finance_dialog() = 0;                  // 40017, 1060:0083
  virtual void about_dialog() = 0;                    // 40018, 1010:049e
  virtual void find_dialog(OriginalFindMode mode) = 0;  // 40019/40020
  virtual void help_contents() = 0;                   // 40021, WinHelp 3

  // Hidden commands (deleted from the menu at WM_CREATE, still in the
  // table): 9000 10c8:006e and 9002 10e8:0029 return whether the event
  // started; world_interaction_changed is mark_original_world_interaction_
  // changed. 9001 marks the document dirty and repaints Info (1118:0000).
  virtual bool bomb_offer() = 0;
  virtual bool fire_offer() = 0;
  virtual void world_interaction_changed() = 0;
  virtual void mark_document_dirty() = 0;
  virtual void present_info_status() = 0;
  // 9003: 1158:0946 presents the retained Main backing directly.
  virtual void present_main_direct() = 0;
};

// Dispatches one WM_COMMAND id.
OriginalMainCommandResult dispatch_original_main_command(
    OriginalMainCommandHost& host,
    std::uint16_t command);

}  // namespace simtower
