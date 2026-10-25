#pragma once

// Scripted mouse input for headless runs (simtower_sdl --input-script FILE).
// One command per line; '#' starts a comment. Coordinates are window logical
// coordinates (sdl_layout.hpp), so a script exercises the same panel router
// as the window.
//
//   pass N                 run idle passes until N have run in total
//                          (N non-decreasing and at most --passes)
//   (X and Y are signed 16-bit, like a Win16 mouse LPARAM)
//   down X Y [shift] [ctrl]
//   move X Y [shift] [ctrl]   MK_LBUTTON is set while a down is outstanding
//   up X Y [shift] [ctrl]
//   dblclick X Y [mods]    WM_LBUTTONDBLCLK (write the preceding down/up)
//   click X Y [mods]       down then up
//   click-command X Y      click (no modifiers); exit 2 unless X,Y is in
//                          the Command client
//   rdown X Y              right button down, no modifiers (cancels a
//                          selector)
//   tool bulldozer|finger|inspect|<type 0..44>
//                          TEST SHORTCUT, not original input: selects an
//                          edit mode, or a type and its catalog cell
//   alert 1|2|3            answer for the next alert that no dialog line
//                          answers (default 1): 1 Yes/OK, 2 No/Cancel, 3
//                          Cancel on a Yes/No/Cancel box (STRL/1001)
//   set-balance B          TEST SHORTCUT, not original input: sets the
//                          tower's cash balance (e.g. below a ransom)
//   expect-type T | expect-mode M | expect-balance B
//                          assertion; exit 3 on mismatch
//   digest                 print a digest line now
//   menu ID                pick menu command ID (WM_COMMAND), as a click on
//                          the item would: exit 2 if ID is not in the live
//                          menu or Main is disabled (Elevator Control
//                          open); a grayed item is logged and not sent
//   key f1                 the F1 accelerator (TranslateAccelerator, 40021)
//   pick-file PATH|cancel  the result of an Open / Save As sheet (PATH is
//                          the rest of the line). Right after the command
//                          that opens the sheet it answers that sheet;
//                          before it, it is queued and used in order. Exit 2
//                          if any is left unused when the script ends
//   menu-show BAR [ITEM [SUB]]
//                          draw drop-down BAR (0 File .. 3 Help) with row
//                          ITEM highlighted (and, for a cascade row, its
//                          drop-down with row SUB) in --headless-window;
//                          presentation only, until menu-hide
//   menu-hide              close it again
//   expect-menu ID checked|unchecked|grayed|enabled|absent
//   expect-title TEXT      the caption, e.g. "SimTower - untitled"
//   expect-saved PATH      PATH holds exactly the game-save serialization of
//                          the live tower (what a Save just wrote)
//   expect-dialog event ID | alert ID | about | box replace |
//                 box invalid-filename | finance | find person |
//                 find tenant | rename person | rename tenant | movie |
//                 elevator popup
//                          the dialog that is opening now (see Dialogs);
//                          exit 3 if another one opens, or none is open
//   dialog-click ID        press and release the center of push button ID
//                          of that dialog (sdl_dialog.hpp) through its hit
//                          test: the event dialogs' template ids (3020: 1
//                          Find the Bomb, 2 Pay Them; 3012: 1 No, 2 Yes),
//                          the MessageBox ids (1 OK, 2 Cancel, 6 Yes, 7 No;
//                          alert 1001 shows 6, 7, 2), or 0 for About (no
//                          controls: its center). Exit 2 if ID is not a
//                          visible, enabled push button
//   dialog-key KEY         a key stroke (press, then release) in that
//                          dialog: enter, esc, tab, shift-tab, space, left,
//                          right, up, down, home, end, pgup, pgdn,
//                          backspace, delete, f4, alt (Alt or F10 alone),
//                          other, a letter a-z (a button mnemonic, e.g.
//                          y / n on a MessageBox; also typed as WM_CHAR),
//                          or shift-left / shift-right / shift-home /
//                          shift-end (an edit extends its selection)
//   dialog-type TEXT       types TEXT (the rest of the line, as WM_CHAR
//                          characters) into the focused control; exit 2
//                          unless it is an edit
//   dialog-list ID INDEX [double]
//                          press and release on row INDEX of list box ID
//                          (twice for "double": LBN_DBLCLK); exit 2 unless
//                          that row is visible
//   dialog-at X Y [right]  press and release the left (or right) button at
//                          client point X,Y of that dialog
//   dialog-wait MS         MS milliseconds pass on the dialog clock; its
//                          due timers fire (event dialogs: one-shot at
//                          37856 ms; About: every interval)
//   dialog-timer           fire its timer now (WM_TIMER); nothing when it
//                          has no live timer
//   dialog-hold [FILE]     write the composed window, with the dialog, to
//                          FILE, or to the --headless-window file (which
//                          the run then does not overwrite at its end); exit
//                          2 with neither
//   open-elevator-control INDEX
//                          TEST SHORTCUT, not original input: opens
//                          Elevator Control on shaft INDEX as a Magnifier
//                          press would ("tool inspect" + a click on the
//                          shaft is the original path)
//   expect-panel elevator-control INDEX | none
//                          Elevator Control is open on shaft INDEX (or
//                          closed); exit 3 otherwise
//   panel-at X Y [right]   press and release at client point X,Y of the
//                          open Elevator Control (exit 2 when none); when
//                          the press runs a modal (ELVPOPUP), the modal
//                          took the release and none is sent
//   panel-down X Y | panel-move X Y | panel-up X Y
//                          one press, move or release there (drags: the
//                          scroll thumb, a pressed Close / Simulate)
//   panel-dblclick X Y     the second press of a double click there
//                          (WM_LBUTTONDBLCLK: the scroll bar takes it as a
//                          press, ELVDLOGMAIN ignores it)
//   panel-key KEY          a key stroke for its dialog manager (the
//                          dialog-key words: tab, up, down, pgup, pgdn...)
//   expect-served INDEX FLOOR yes|no
//                          shaft INDEX serves FLOOR (raw floor number)
//   expect-scroll POS      Elevator Control's scroll position
//   expect-waiting N       its displayed waiting count (DTMP 41) for the
//                          selected bank and phase
//   expect-isolation on|off
//                          Simulate's isolation (DS:b3ae)
//   expect-idle-passes N   the session ran N idle passes so far (a pass
//                          held by a scroll bar track does not count)
//   hold [FILE]            write the composed window now (as dialog-hold,
//                          outside dialogs too)
//   open-dialog rename-person PERSON | rename-tenant FLOOR TENANT |
//               movie RECORD
//                          TEST SHORTCUT, not original input: opens the
//                          Rename or New Movie dialog that Person / Facility
//                          Information would open (those windows are not
//                          ported yet), with the launcher's result handling
//
// pick-file, expect-title and expect-saved take the rest of the line, but
// '#' still starts a comment there, so PATH and TEXT cannot contain '#'.
//
// A File > Exit that destroys Main ("menu 40001") ends the run like the
// window's WM_QUIT: the rest of the script and any remaining --passes are
// skipped, and the final digest reports the passes actually run.
//
// Dialogs: a dialog opens modally when the session runs it (an event
// dialog, an alert or other MessageBox, About). If the line at the
// script's read position (right after the command that opened it, or, for
// a dialog an idle pass opens, after the "pass N" line that ran that pass)
// is "expect-dialog" naming it, the dialog is driven: it reads the
// following "dialog-*" lines and, at the first other line (another
// "expect-dialog" included), closes with its default answer unless one of
// them answered it. An "expect-dialog" naming a different dialog is exit 3;
// a "dialog-*" line there without "expect-dialog" is exit 2. Any other line
// leaves the dialog undriven: it takes the default answer (the event
// dialogs' fixed choice, or the "alert" answer) exactly as runs without
// dialog lines do, with identical digests. A "dialog-*" line with no
// dialog open is exit 2. The virtual clock does not move inside a dialog;
// only "dialog-wait" and "dialog-timer" move its timers.
//
// Determinism: commands after "pass N" run after N passes and before pass
// N+1 (the --event NAME@PASS convention). The virtual clock does not move
// within such a batch. While a modal selector runs, its events come from
// the following lines; "pass" there is an error (Win32 runs no idle pass
// inside a modal). A script that ends inside the selector cancels it (as a
// click outside it would) and the run continues normally.
//
// Exit codes (sdl_main.cpp): 2 for a syntax, range or ordering error,
// 3 for a failed expect-*. Every message starts with "FILE:LINE:".

#include "sdl_session.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace simtower::sdl {

// ScriptFailure (a script error, exit 2, or a failed expectation, exit 3)
// is declared in sdl_session.hpp.

enum class ScriptOp : std::uint8_t {
  pass,
  down,
  move,
  up,
  double_click,
  click,
  click_command,
  right_down,
  tool,
  alert,
  set_balance,
  expect_type,
  expect_mode,
  expect_balance,
  digest,
  menu,
  key,
  pick_file,
  menu_show,
  menu_hide,
  expect_menu,
  expect_title,
  expect_saved,
  expect_dialog,
  dialog_click,
  dialog_key,
  dialog_type,
  dialog_list,
  dialog_at,
  dialog_wait,
  dialog_timer,
  dialog_hold,
  open_dialog,
  open_elevator_control,
  expect_panel,
  panel_at,
  panel_down,
  panel_move,
  panel_up,
  panel_double_click,
  panel_key,
  expect_served,
  expect_scroll,
  expect_waiting,
  expect_isolation,
  expect_idle_passes,
  hold,
};

struct ScriptCommand {
  ScriptOp op{ScriptOp::pass};
  std::string where{};  // "FILE:LINE", the prefix of every error message
  int x{};
  int y{};
  std::uint16_t keys{};  // MK_SHIFT / MK_CONTROL
  std::int64_t value{};  // pass count, alert answer, expectation
  std::optional<std::uint16_t> edit_mode{};
  std::optional<std::uint16_t> build_type{};
  // pick-file / dialog-hold path, expect-* words, expect-dialog label
  std::string text{};
  DialogKey dialog_key{DialogKey::other};
  char character{};  // dialog-key LETTER
  bool shift{};      // dialog-key shift-KEY
  std::vector<int> numbers{};  // menu-show BAR [ITEM [SUB]]
};

// Throws ScriptFailure(2) with the file name and line on a syntax error.
[[nodiscard]] std::vector<ScriptCommand> parse_input_script(
    const std::filesystem::path& path);

// Throws ScriptFailure(2) naming the line and the failed condition unless
// every "pass N" is at least the previous one and at most max_passes. Run
// before any pass so a bad script fails without simulating anything.
void validate_pass_order(const std::vector<ScriptCommand>& commands,
                         int max_passes);

// "dialog-hold": writes the composed window to the path (empty: the
// --headless-window file). Throws ScriptFailure(2) with `where` when there
// is nowhere to write.
using DialogHoldWriter =
    std::function<void(const std::string& path, const std::string& where)>;

class ScriptPlayer final : public InputSource {
 public:
  // print_digest is called for "digest" with the passes run so far.
  ScriptPlayer(std::vector<ScriptCommand> commands,
               Session& session,
               std::function<void()> print_digest,
               DialogHoldWriter hold_dialog = {});

  // Executes commands up to the next "pass N" and returns that command,
  // or null when the script has ended or a command quit (File > Exit).
  const ScriptCommand* run_until_pass();

  std::optional<InputEvent> next_modal_event() override;
  // A sheet opened by a command: the next line, when it is "pick-file".
  std::optional<std::optional<std::filesystem::path>> next_file_pick()
      override;
  // True when the next line is "expect-dialog" naming this dialog (it is
  // consumed); throws ScriptFailure for another dialog or a bare
  // "dialog-*" line.
  bool drives_dialog(const DialogWindow& dialog) override;
  // The following "dialog-*" lines, one input at a time.
  std::optional<DialogInput> next_dialog_input(
      std::optional<std::uint32_t> wait_ms) override;
  // Input left for a dialog that closed is dropped.
  void dialog_closed() override { pending_dialog_.clear(); }

 private:
  // Executes one non-pass command. Mouse commands go to the session (or,
  // inside a modal, are returned as events through pending_).
  void execute(const ScriptCommand& command, bool in_modal);
  void check_expectation(const ScriptCommand& command) const;

  std::vector<ScriptCommand> commands_;
  std::size_t next_{};
  Session& session_;
  std::function<void()> print_digest_;
  DialogHoldWriter hold_dialog_;
  std::deque<InputEvent> pending_{};
  std::deque<DialogInput> pending_dialog_{};
};

}  // namespace simtower::sdl
