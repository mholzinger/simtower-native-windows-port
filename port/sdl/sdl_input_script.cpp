#include "sdl_input_script.hpp"

#include "original_world_input.hpp"

#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace simtower::sdl {
namespace {

std::vector<std::string> split_words(const std::string& line) {
  std::vector<std::string> words;
  std::istringstream stream(line.substr(0, line.find('#')));
  for (std::string word; stream >> word;) words.push_back(word);
  return words;
}

std::int64_t parse_integer(const std::string& text, const std::string& where) {
  try {
    std::size_t used = 0U;
    const auto value = std::stoll(text, &used);
    if (used == text.size()) return value;
  } catch (const std::exception&) {
  }
  throw ScriptFailure(2, where + ": bad number '" + text + "'");
}

// A Win16 mouse LPARAM carries signed 16-bit client coordinates.
int parse_coordinate(const std::string& text, const std::string& where) {
  const auto value = parse_integer(text, where);
  if (value < std::numeric_limits<std::int16_t>::min() ||
      value > std::numeric_limits<std::int16_t>::max()) {
    throw ScriptFailure(2, where + ": coordinate " + text +
                               " is outside -32768..32767");
  }
  return static_cast<int>(value);
}

}  // namespace

std::vector<ScriptCommand> parse_input_script(
    const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream) {
    throw ScriptFailure(2, "cannot open input script " + path.string());
  }
  std::vector<ScriptCommand> commands;
  std::string line;
  int number = 0;
  while (std::getline(stream, line)) {
    ++number;
    const auto words = split_words(line);
    if (words.empty()) continue;
    const std::string where = path.string() + ":" + std::to_string(number);
    const auto fail = [&](const std::string& message) -> ScriptFailure {
      return ScriptFailure(2, where + ": " + message);
    };
    ScriptCommand command{};
    command.where = where;
    const auto& verb = words[0];
    // X Y, then "shift" and/or "ctrl" when modifiers are allowed.
    const auto point = [&](bool modifiers) {
      if (words.size() < 3) throw fail(verb + " needs X Y");
      if (!modifiers && words.size() != 3) {
        throw fail(verb + " takes X Y and no modifiers");
      }
      command.x = parse_coordinate(words[1], where);
      command.y = parse_coordinate(words[2], where);
      for (std::size_t index = 3; index < words.size(); ++index) {
        if (words[index] == "shift") {
          command.keys |= kOriginalKeyShift;
        } else if (words[index] == "ctrl") {
          command.keys |= kOriginalKeyControl;
        } else {
          throw fail("unknown modifier '" + words[index] + "'");
        }
      }
    };
    const auto single_value = [&]() {
      if (words.size() != 2) throw fail(verb + " needs one value");
      command.value = parse_integer(words[1], where);
    };
    if (verb == "pass") {
      command.op = ScriptOp::pass;
      single_value();
      if (command.value < 0 ||
          command.value > std::numeric_limits<int>::max()) {
        throw fail("pass count must be 0.." +
                   std::to_string(std::numeric_limits<int>::max()));
      }
    } else if (verb == "down") {
      command.op = ScriptOp::down;
      point(true);
    } else if (verb == "move") {
      command.op = ScriptOp::move;
      point(true);
    } else if (verb == "up") {
      command.op = ScriptOp::up;
      point(true);
    } else if (verb == "dblclick") {
      command.op = ScriptOp::double_click;
      point(true);
    } else if (verb == "click") {
      command.op = ScriptOp::click;
      point(true);
    } else if (verb == "click-command") {
      command.op = ScriptOp::click_command;
      point(false);
    } else if (verb == "rdown") {
      command.op = ScriptOp::right_down;
      point(false);
    } else if (verb == "tool") {
      command.op = ScriptOp::tool;
      if (words.size() != 2) throw fail("tool needs one name or type");
      if (words[1] == "bulldozer") {
        command.edit_mode = 0U;
      } else if (words[1] == "finger") {
        command.edit_mode = 1U;
      } else if (words[1] == "inspect") {
        command.edit_mode = 2U;
      } else {
        const auto type = parse_integer(words[1], where);
        if (type < 0 || type > 44) throw fail("type must be 0..44");
        command.build_type = static_cast<std::uint16_t>(type);
      }
    } else if (verb == "alert") {
      command.op = ScriptOp::alert;
      single_value();
      if (command.value != 1 && command.value != 2) {
        throw fail("alert answer must be 1 or 2");
      }
    } else if (verb == "expect-type") {
      command.op = ScriptOp::expect_type;
      single_value();
    } else if (verb == "expect-mode") {
      command.op = ScriptOp::expect_mode;
      single_value();
    } else if (verb == "expect-balance") {
      command.op = ScriptOp::expect_balance;
      single_value();
    } else if (verb == "digest") {
      command.op = ScriptOp::digest;
      if (words.size() != 1) throw fail("digest takes no value");
    } else {
      throw fail("unknown command '" + verb + "'");
    }
    commands.push_back(command);
  }
  return commands;
}

void validate_pass_order(const std::vector<ScriptCommand>& commands,
                         int max_passes) {
  std::int64_t previous = 0;
  const ScriptCommand* previous_command = nullptr;
  for (const auto& command : commands) {
    if (command.op != ScriptOp::pass) continue;
    if (command.value > max_passes) {
      throw ScriptFailure(2, command.where + ": pass " +
                                 std::to_string(command.value) +
                                 " is beyond --passes " +
                                 std::to_string(max_passes));
    }
    if (command.value < previous) {
      throw ScriptFailure(2, command.where + ": pass " +
                                 std::to_string(command.value) +
                                 " is before pass " +
                                 std::to_string(previous) + " at " +
                                 previous_command->where);
    }
    previous = command.value;
    previous_command = &command;
  }
}

ScriptPlayer::ScriptPlayer(std::vector<ScriptCommand> commands,
                           Session& session,
                           std::function<void()> print_digest)
    : commands_(std::move(commands)),
      session_(session),
      print_digest_(std::move(print_digest)) {}

const ScriptCommand* ScriptPlayer::run_until_pass() {
  while (next_ < commands_.size()) {
    const auto& command = commands_[next_++];
    if (command.op == ScriptOp::pass) return &command;
    execute(command, false);
  }
  return nullptr;
}

std::optional<InputEvent> ScriptPlayer::next_modal_event() {
  while (pending_.empty()) {
    if (next_ >= commands_.size()) return std::nullopt;
    const auto& command = commands_[next_++];
    if (command.op == ScriptOp::pass) {
      throw ScriptFailure(2, command.where + ": pass inside a modal selector");
    }
    execute(command, true);
  }
  const auto event = pending_.front();
  pending_.pop_front();
  return event;
}

void ScriptPlayer::execute(const ScriptCommand& command, bool in_modal) {
  // Inside a modal every event is queued for next_modal_event. Outside, the
  // event goes to the session; a modal it opened may stop reading before
  // the queue is empty (a selector closes on the press of a click whose
  // release is still queued), so the rest is delivered in FIFO order
  // before the next script line, exactly as separate down/up lines would.
  const auto send = [&](InputKind kind) {
    const InputEvent event{kind, command.x, command.y, command.keys};
    if (in_modal) {
      pending_.push_back(event);
      return;
    }
    session_.pointer_event(event);
    while (!pending_.empty()) {
      const auto queued = pending_.front();
      pending_.pop_front();
      session_.pointer_event(queued);
    }
  };
  switch (command.op) {
    case ScriptOp::pass:
      break;
    case ScriptOp::down: send(InputKind::down); break;
    case ScriptOp::move: send(InputKind::move); break;
    case ScriptOp::up: send(InputKind::up); break;
    case ScriptOp::double_click: send(InputKind::double_click); break;
    case ScriptOp::right_down: send(InputKind::right_down); break;
    case ScriptOp::click_command:
      if (!session_.layout().command_client.contains(command.x, command.y)) {
        throw ScriptFailure(2, command.where +
                                   ": click-command point is outside the "
                                   "Command client");
      }
      [[fallthrough]];
    case ScriptOp::click:
      if (in_modal) {
        send(InputKind::down);
        send(InputKind::up);
      } else {
        // A modal opened by the down (the grouped selector) receives the
        // release first, as the physical button-up reaches the captured
        // popup; otherwise send's drain gives the up to the panel under
        // the point.
        pending_.push_back({InputKind::up, command.x, command.y, command.keys});
        send(InputKind::down);
      }
      break;
    case ScriptOp::tool:
      session_.select_tool(command.edit_mode, command.build_type);
      break;
    case ScriptOp::alert:
      session_.set_alert_answer(static_cast<int>(command.value));
      break;
    case ScriptOp::expect_type:
    case ScriptOp::expect_mode:
    case ScriptOp::expect_balance:
      check_expectation(command);
      break;
    case ScriptOp::digest:
      print_digest_();
      break;
  }
}

void ScriptPlayer::check_expectation(const ScriptCommand& command) const {
  std::int64_t actual = 0;
  const char* what = "";
  switch (command.op) {
    case ScriptOp::expect_type:
      actual = session_.world_input().selected_build_type;
      what = "type";
      break;
    case ScriptOp::expect_mode:
      actual = session_.world_input().command_mode;
      what = "mode";
      break;
    case ScriptOp::expect_balance:
      actual = session_.document() ? session_.document()->header.balance : 0;
      what = "balance";
      break;
    default:
      return;
  }
  if (actual != command.value) {
    throw ScriptFailure(3, command.where + ": expected " + what + " " +
                               std::to_string(command.value) + ", got " +
                               std::to_string(actual));
  }
}

}  // namespace simtower::sdl
