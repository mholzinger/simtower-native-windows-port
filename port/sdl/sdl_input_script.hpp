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
//   alert 1|2              answer for the next alert (default 1)
//   expect-type T | expect-mode M | expect-balance B
//                          assertion; exit 3 on mismatch
//   digest                 print a digest line now
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

// A script error (exit 2) or a failed expectation (exit 3).
class ScriptFailure : public std::runtime_error {
 public:
  ScriptFailure(int exit_code, const std::string& message)
      : std::runtime_error(message), exit_code_(exit_code) {}
  [[nodiscard]] int exit_code() const { return exit_code_; }

 private:
  int exit_code_;
};

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
  expect_type,
  expect_mode,
  expect_balance,
  digest,
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
};

// Throws ScriptFailure(2) with the file name and line on a syntax error.
[[nodiscard]] std::vector<ScriptCommand> parse_input_script(
    const std::filesystem::path& path);

// Throws ScriptFailure(2) naming the line and the failed condition unless
// every "pass N" is at least the previous one and at most max_passes. Run
// before any pass so a bad script fails without simulating anything.
void validate_pass_order(const std::vector<ScriptCommand>& commands,
                         int max_passes);

class ScriptPlayer final : public InputSource {
 public:
  // print_digest is called for "digest" with the passes run so far.
  ScriptPlayer(std::vector<ScriptCommand> commands,
               Session& session,
               std::function<void()> print_digest);

  // Executes commands up to the next "pass N" and returns that command,
  // or null when the script has ended.
  const ScriptCommand* run_until_pass();

  std::optional<InputEvent> next_modal_event() override;

 private:
  // Executes one non-pass command. Mouse commands go to the session (or,
  // inside a modal, are returned as events through pending_).
  void execute(const ScriptCommand& command, bool in_modal);
  void check_expectation(const ScriptCommand& command) const;

  std::vector<ScriptCommand> commands_;
  std::size_t next_{};
  Session& session_;
  std::function<void()> print_digest_;
  std::deque<InputEvent> pending_{};
};

}  // namespace simtower::sdl
