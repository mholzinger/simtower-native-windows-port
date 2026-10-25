#include "sdl_input_script.hpp"

#include "original_world_input.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
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

// The rest of the line after `verb` (comment stripped, blanks trimmed), so
// a value may hold spaces; empty when there is none.
std::string rest_of_line(const std::string& line, const std::string& verb) {
  const auto code = line.substr(0, line.find('#'));
  const auto start =
      code.find_first_not_of(" \t", code.find(verb) + verb.size());
  if (start == std::string::npos) return {};
  const auto end = code.find_last_not_of(" \t\r");
  return code.substr(start, end + 1 - start);
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

bool dialog_op(ScriptOp op) {
  switch (op) {
    case ScriptOp::dialog_click:
    case ScriptOp::dialog_key:
    case ScriptOp::dialog_type:
    case ScriptOp::dialog_list:
    case ScriptOp::dialog_at:
    case ScriptOp::dialog_wait:
    case ScriptOp::dialog_timer:
    case ScriptOp::dialog_hold:
      return true;
    default:
      return false;
  }
}

std::string op_name(ScriptOp op) {
  switch (op) {
    case ScriptOp::dialog_click: return "dialog-click";
    case ScriptOp::dialog_key: return "dialog-key";
    case ScriptOp::dialog_type: return "dialog-type";
    case ScriptOp::dialog_list: return "dialog-list";
    case ScriptOp::dialog_at: return "dialog-at";
    case ScriptOp::dialog_wait: return "dialog-wait";
    case ScriptOp::dialog_timer: return "dialog-timer";
    case ScriptOp::dialog_hold: return "dialog-hold";
    case ScriptOp::expect_dialog: return "expect-dialog";
    default: return "command";
  }
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
      if (command.value < 1 || command.value > 3) {
        throw fail("alert answer must be 1, 2 or 3");
      }
    } else if (verb == "set-balance") {
      command.op = ScriptOp::set_balance;
      single_value();
      if (command.value < std::numeric_limits<std::int32_t>::min() ||
          command.value > std::numeric_limits<std::int32_t>::max()) {
        throw fail("balance must fit a signed 32-bit value");
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
    } else if (verb == "menu") {
      command.op = ScriptOp::menu;
      single_value();
      if (command.value < 0 || command.value > 0xFFFF) {
        throw fail("menu id must be 0..65535");
      }
    } else if (verb == "key") {
      command.op = ScriptOp::key;
      if (words.size() != 2 || (words[1] != "f1" && words[1] != "F1")) {
        throw fail("key takes f1");
      }
      command.value = kOriginalVirtualKeyF1;
    } else if (verb == "pick-file") {
      command.op = ScriptOp::pick_file;
      command.text = rest_of_line(line, verb);
      if (command.text.empty()) throw fail("pick-file needs PATH or cancel");
      if (command.text == "cancel") command.text.clear();
    } else if (verb == "menu-show") {
      command.op = ScriptOp::menu_show;
      if (words.size() < 2 || words.size() > 4) {
        throw fail("menu-show takes BAR [ITEM [SUB]]");
      }
      for (std::size_t index = 1; index < words.size(); ++index) {
        const auto value = parse_integer(words[index], where);
        if (value < 0 || value > 63) throw fail("menu-show index out of range");
        command.numbers.push_back(static_cast<int>(value));
      }
    } else if (verb == "menu-hide") {
      command.op = ScriptOp::menu_hide;
      if (words.size() != 1) throw fail("menu-hide takes no value");
    } else if (verb == "expect-menu") {
      command.op = ScriptOp::expect_menu;
      if (words.size() != 3) throw fail("expect-menu takes ID STATE");
      command.value = parse_integer(words[1], where);
      command.text = words[2];
      if (command.text != "checked" && command.text != "unchecked" &&
          command.text != "grayed" && command.text != "enabled" &&
          command.text != "absent") {
        throw fail("unknown menu state '" + command.text + "'");
      }
    } else if (verb == "expect-dialog") {
      command.op = ScriptOp::expect_dialog;
      const auto usage = [&]() {
        return fail(
            "expect-dialog takes event ID, alert ID, about, box replace, "
            "box invalid-filename, finance, find person, find tenant, "
            "rename person, rename tenant, movie, elevator popup, "
            "facility ID, person ID or transport ID");
      };
      const std::string rest =
          words.size() == 3 ? words[1] + " " + words[2] : std::string{};
      if (words.size() == 2 && words[1] == "about") {
        command.text = kAboutDialogLabel;
      } else if (const auto kind = words.size() == 3
                                       ? parse_information_dialog_kind(words[1])
                                       : std::nullopt) {
        const auto id = parse_integer(words[2], where);
        if (id < 0 || id > 0xFFFF) throw fail("dialog id must be 0..65535");
        command.text = information_dialog_label(
            *kind, static_cast<std::uint16_t>(id));
      } else if (words.size() == 2 &&
                 (words[1] == kFinanceDialogLabel ||
                  words[1] == kMovieDialogLabel)) {
        command.text = words[1];
      } else if (rest == kFindPersonDialogLabel ||
                 rest == kFindTenantDialogLabel ||
                 rest == kRenamePersonDialogLabel ||
                 rest == kRenameTenantDialogLabel) {
        command.text = rest;
      } else if (words.size() == 3 &&
                 (words[1] == "event" || words[1] == "alert")) {
        const auto id = parse_integer(words[2], where);
        if (id < 0 || id > 0xFFFF) throw fail("dialog id must be 0..65535");
        command.text = words[1] == "event"
                           ? event_dialog_label(static_cast<std::uint16_t>(id))
                           : alert_dialog_label(static_cast<int>(id));
      } else if (rest == kElevatorPopupDialogLabel) {
        command.text = rest;
      } else if (words.size() == 3 && words[1] == "box" &&
                 (words[2] == "replace" || words[2] == "invalid-filename")) {
        command.text = "box " + words[2];
      } else {
        throw usage();
      }
    } else if (verb == "dialog-click") {
      command.op = ScriptOp::dialog_click;
      single_value();
      if (command.value < 0 || command.value > 0xFFFF) {
        throw fail("control id must be 0..65535");
      }
    } else if (verb == "dialog-key" || verb == "panel-key") {
      command.op =
          verb == "dialog-key" ? ScriptOp::dialog_key : ScriptOp::panel_key;
      if (words.size() != 2) throw fail(verb + " needs one key");
      static const std::pair<const char*, DialogKey> kKeys[] = {
          {"enter", DialogKey::enter},     {"esc", DialogKey::escape},
          {"tab", DialogKey::tab},         {"shift-tab", DialogKey::shift_tab},
          {"space", DialogKey::space},     {"left", DialogKey::left},
          {"right", DialogKey::right},     {"up", DialogKey::up},
          {"down", DialogKey::down},       {"home", DialogKey::home},
          {"end", DialogKey::end},         {"pgup", DialogKey::page_up},
          {"pgdn", DialogKey::page_down},  {"backspace", DialogKey::backspace},
          {"delete", DialogKey::delete_key}, {"f4", DialogKey::f4},
          {"alt", DialogKey::menu},        {"other", DialogKey::other},
      };
      auto name = words[1];
      if (name.rfind("shift-", 0) == 0 && name != "shift-tab") {
        command.shift = true;
        name = name.substr(6);
        if (name != "left" && name != "right" && name != "home" &&
            name != "end") {
          throw fail("shift- takes left, right, home or end");
        }
      }
      const auto* found = std::ranges::find_if(
          kKeys, [&](const auto& entry) { return name == entry.first; });
      if (found != std::end(kKeys)) {
        command.dialog_key = found->second;
      } else if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z') {
        command.dialog_key = DialogKey::other;
        command.character = name[0];
      } else {
        throw fail("unknown dialog key '" + words[1] + "'");
      }
    } else if (verb == "dialog-type") {
      command.op = ScriptOp::dialog_type;
      command.text = rest_of_line(line, verb);
      if (command.text.empty()) throw fail("dialog-type needs TEXT");
    } else if (verb == "dialog-list") {
      command.op = ScriptOp::dialog_list;
      if (words.size() != 3 && !(words.size() == 4 && words[3] == "double")) {
        throw fail("dialog-list takes ID INDEX [double]");
      }
      command.value = parse_integer(words[1], where);
      if (command.value < 0 || command.value > 0xFFFF) {
        throw fail("control id must be 0..65535");
      }
      const auto index = parse_integer(words[2], where);
      if (index < 0 || index > 0x7FFF) throw fail("row must be 0..32767");
      command.numbers = {static_cast<int>(index), words.size() == 4 ? 1 : 0};
    } else if (verb == "dialog-at") {
      command.op = ScriptOp::dialog_at;
      if (words.size() != 3 && !(words.size() == 4 && words[3] == "right")) {
        throw fail("dialog-at takes X Y [right]");
      }
      command.x = parse_coordinate(words[1], where);
      command.y = parse_coordinate(words[2], where);
      command.keys = words.size() == 4 ? 1U : 0U;
    } else if (verb == "open-dialog") {
      command.op = ScriptOp::open_dialog;
      const auto usage = [&]() {
        return fail(
            "open-dialog takes rename-person PERSON, rename-tenant FLOOR "
            "TENANT, movie RECORD, facility FLOOR TENANT, person PERSON "
            "[main|transport|facility], transport-car SHAFT CAR or "
            "transport-stair INDEX");
      };
      if (words.size() < 3) throw usage();
      command.text = words[1];
      // person's context: 0 main (DS:b3a6 = 0), 1 transport, 2 facility.
      std::size_t numbers_end = words.size();
      if (command.text == "person" && words.size() == 4) {
        if (words[3] == "main") {
          command.value = 0;
        } else if (words[3] == "transport") {
          command.value = 1;
        } else if (words[3] == "facility") {
          command.value = 2;
        } else {
          throw usage();
        }
        numbers_end = 3U;
      }
      for (std::size_t index = 2; index < numbers_end; ++index) {
        const auto value = parse_integer(words[index], where);
        if (value < -32768 || value > 0x7FFFFFFF) throw usage();
        command.numbers.push_back(static_cast<int>(value));
      }
      if (!((command.text == "rename-person" && command.numbers.size() == 1U &&
             command.numbers[0] >= 0) ||
            (command.text == "rename-tenant" && command.numbers.size() == 2U &&
             command.numbers[1] >= 0) ||
            (command.text == "facility" && command.numbers.size() == 2U &&
             command.numbers[1] >= 0) ||
            (command.text == "movie" && command.numbers.size() == 1U &&
             command.numbers[0] >= 0) ||
            (command.text == "person" && command.numbers.size() == 1U &&
             command.numbers[0] >= 0) ||
            (command.text == "transport-car" && command.numbers.size() == 2U &&
             command.numbers[0] >= 0 && command.numbers[1] >= 0) ||
            (command.text == "transport-stair" &&
             command.numbers.size() == 1U && command.numbers[0] >= 0))) {
        throw usage();
      }
    } else if (verb == "open-elevator-control") {
      command.op = ScriptOp::open_elevator_control;
      single_value();
      if (command.value < 0 || command.value > 23) {
        throw fail("shaft index must be 0..23");
      }
    } else if (verb == "expect-panel") {
      command.op = ScriptOp::expect_panel;
      if (words.size() == 2 && words[1] == "none") {
        command.value = -1;
      } else if (words.size() == 3 && words[1] == "elevator-control") {
        command.value = parse_integer(words[2], where);
      } else {
        throw fail("expect-panel takes elevator-control INDEX or none");
      }
    } else if (verb == "panel-at") {
      command.op = ScriptOp::panel_at;
      if (words.size() != 3 && !(words.size() == 4 && words[3] == "right")) {
        throw fail("panel-at takes X Y [right]");
      }
      command.x = parse_coordinate(words[1], where);
      command.y = parse_coordinate(words[2], where);
      command.keys = words.size() == 4 ? 1U : 0U;
    } else if (verb == "panel-down" || verb == "panel-move" ||
               verb == "panel-up" || verb == "panel-dblclick" ||
               verb == "panel-click") {
      command.op = verb == "panel-down"       ? ScriptOp::panel_down
                   : verb == "panel-move"     ? ScriptOp::panel_move
                   : verb == "panel-up"       ? ScriptOp::panel_up
                   : verb == "panel-click"    ? ScriptOp::panel_click
                                              : ScriptOp::panel_double_click;
      point(false);
    } else if (verb == "expect-served") {
      command.op = ScriptOp::expect_served;
      if (words.size() != 4 || (words[3] != "yes" && words[3] != "no")) {
        throw fail("expect-served takes INDEX FLOOR yes|no");
      }
      command.numbers = {static_cast<int>(parse_integer(words[1], where)),
                         static_cast<int>(parse_integer(words[2], where))};
      command.value = words[3] == "yes" ? 1 : 0;
    } else if (verb == "expect-scroll") {
      command.op = ScriptOp::expect_scroll;
      single_value();
    } else if (verb == "expect-phase") {
      command.op = ScriptOp::expect_phase;
      single_value();
    } else if (verb == "expect-waiting") {
      command.op = ScriptOp::expect_waiting;
      single_value();
    } else if (verb == "expect-isolation") {
      command.op = ScriptOp::expect_isolation;
      if (words.size() != 2 || (words[1] != "on" && words[1] != "off")) {
        throw fail("expect-isolation takes on or off");
      }
      command.value = words[1] == "on" ? 1 : 0;
    } else if (verb == "expect-rent") {
      command.op = ScriptOp::expect_rent;
      if (words.size() != 4) throw fail("expect-rent takes FLOOR TENANT RATE");
      command.numbers = {static_cast<int>(parse_integer(words[1], where)),
                         static_cast<int>(parse_integer(words[2], where))};
      command.value = parse_integer(words[3], where);
      if (command.numbers[0] < 0 || command.numbers[0] >= 120 ||
          command.numbers[1] < 0 || command.value < 0 || command.value > 3) {
        throw fail("expect-rent takes FLOOR 0..119, TENANT >= 0, RATE 0..3");
      }
    } else if (verb == "expect-find-latch") {
      command.op = ScriptOp::expect_find_latch;
      single_value();
      if (command.value != 0 && command.value != 1) {
        throw fail("expect-find-latch takes 0 or 1");
      }
    } else if (verb == "expect-idle-passes") {
      command.op = ScriptOp::expect_idle_passes;
      single_value();
    } else if (verb == "hold") {
      command.op = ScriptOp::hold;
      command.text = rest_of_line(line, verb);
    } else if (verb == "dialog-wait") {
      command.op = ScriptOp::dialog_wait;
      single_value();
      if (command.value < 0 || command.value > 0x7FFFFFFF) {
        throw fail("dialog-wait takes 0..2147483647 milliseconds");
      }
    } else if (verb == "dialog-timer") {
      command.op = ScriptOp::dialog_timer;
      if (words.size() != 1) throw fail("dialog-timer takes no value");
    } else if (verb == "dialog-hold") {
      command.op = ScriptOp::dialog_hold;
      command.text = rest_of_line(line, verb);
    } else if (verb == "expect-help") {
      command.op = ScriptOp::expect_help;
      if (words.size() != 2) throw fail("expect-help takes PAGE, missing or "
                                        "none");
      command.text = words[1];
    } else if (verb == "expect-title" || verb == "expect-saved") {
      command.op = verb == "expect-title" ? ScriptOp::expect_title
                                          : ScriptOp::expect_saved;
      command.text = rest_of_line(line, verb);
      if (command.text.empty()) throw fail(verb + " needs a value");
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
                           std::function<void()> print_digest,
                           DialogHoldWriter hold_dialog)
    : commands_(std::move(commands)),
      session_(session),
      print_digest_(std::move(print_digest)),
      hold_dialog_(std::move(hold_dialog)) {}

const ScriptCommand* ScriptPlayer::run_until_pass() {
  while (next_ < commands_.size()) {
    const auto& command = commands_[next_++];
    if (command.op == ScriptOp::pass) return &command;
    execute(command, false);
    if (session_.quit_requested()) return nullptr;
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

std::optional<std::optional<std::filesystem::path>>
ScriptPlayer::next_file_pick() {
  if (next_ >= commands_.size() ||
      commands_[next_].op != ScriptOp::pick_file) {
    return std::nullopt;
  }
  const auto& command = commands_[next_++];
  if (command.text.empty()) {
    return std::optional<std::optional<std::filesystem::path>>(std::in_place,
                                                               std::nullopt);
  }
  return std::optional<std::optional<std::filesystem::path>>(
      std::in_place, std::filesystem::path(command.text));
}

bool ScriptPlayer::drives_dialog(const DialogWindow& dialog) {
  if (next_ >= commands_.size()) return false;
  const auto& command = commands_[next_];
  if (command.op == ScriptOp::expect_dialog) {
    if (command.text != dialog.label()) {
      throw ScriptFailure(3, command.where + ": expect-dialog " +
                                 command.text + ", but " + dialog.label() +
                                 " opened");
    }
    ++next_;
    return true;
  }
  if (dialog_op(command.op)) {
    throw ScriptFailure(2, command.where + ": " + op_name(command.op) +
                               " without expect-dialog: " + dialog.label() +
                               " opened (add \"expect-dialog " +
                               dialog.label() + "\" before it)");
  }
  return false;
}

std::optional<DialogInput> ScriptPlayer::next_dialog_input(
    std::optional<std::uint32_t> /*wait_ms*/) {
  while (pending_dialog_.empty()) {
    if (next_ >= commands_.size()) return std::nullopt;
    const auto& command = commands_[next_];
    switch (command.op) {
      case ScriptOp::dialog_click: {
        ++next_;
        const auto dialogs = session_.dialogs();
        const auto id = static_cast<std::uint16_t>(command.value);
        const auto point =
            dialogs.empty() ? std::nullopt : dialogs.back()->click_point(id);
        if (!point) {
          const auto label =
              dialogs.empty() ? std::string("none") : dialogs.back()->label();
          throw ScriptFailure(2, command.where + ": dialog-click " +
                                     std::to_string(id) + ": " + label +
                                     " has no visible, enabled push button "
                                     "with that id");
        }
        DialogInput input{};
        input.pointer = {InputKind::down, point->first, point->second, 0U};
        pending_dialog_.push_back(input);
        input.pointer.kind = InputKind::up;
        pending_dialog_.push_back(input);
        break;
      }
      case ScriptOp::dialog_key: {
        // A key stroke: WM_KEYDOWN, the WM_CHAR a letter or Space types,
        // then WM_KEYUP.
        ++next_;
        DialogInput input{};
        input.kind = DialogInput::Kind::key;
        input.key = command.dialog_key;
        input.character = command.character;
        input.shift = command.shift;
        pending_dialog_.push_back(input);
        if (command.character != '\0' ||
            command.dialog_key == DialogKey::space) {
          DialogInput typed{};
          typed.kind = DialogInput::Kind::character;
          typed.character =
              command.character != '\0' ? command.character : ' ';
          pending_dialog_.push_back(typed);
        }
        input.kind = DialogInput::Kind::key_up;
        pending_dialog_.push_back(input);
        break;
      }
      case ScriptOp::dialog_type: {
        ++next_;
        const auto dialogs = session_.dialogs();
        const auto* focused =
            dialogs.empty() ? nullptr : dialogs.back()->focused_control();
        if (!focused || focused->kind != DialogControlKind::edit) {
          const auto label = dialogs.empty() ? std::string("none")
                                             : dialogs.back()->label();
          throw ScriptFailure(2, command.where +
                                     ": dialog-type: the focused control of " +
                                     label + " is not an edit");
        }
        for (const char character : command.text) {
          DialogInput typed{};
          typed.kind = DialogInput::Kind::character;
          typed.character = character;
          pending_dialog_.push_back(typed);
        }
        break;
      }
      case ScriptOp::dialog_list: {
        ++next_;
        const auto dialogs = session_.dialogs();
        const auto id = static_cast<std::uint16_t>(command.value);
        const int row = command.numbers[0];
        const auto* target = dialogs.empty() ? nullptr
                                             : dialogs.back()->control(id);
        if (target && target->kind == DialogControlKind::combo_box) {
          // A drop-down combo: click the field (drops the list), then the
          // row of the dropped list.
          const auto field = dialogs.back()->combo_field_point(id);
          const auto point = dialogs.back()->combo_item_point(id, row);
          if (!field || !point || command.numbers[1] != 0) {
            throw ScriptFailure(2, command.where + ": dialog-list " +
                                       std::to_string(id) + " " +
                                       std::to_string(row) +
                                       ": no such row of a visible, enabled "
                                       "combo box (or \"double\")");
          }
          for (const auto& at : {*field, *point}) {
            DialogInput input{};
            input.pointer = {InputKind::down, at.first, at.second, 0U};
            pending_dialog_.push_back(input);
            input.pointer.kind = InputKind::up;
            pending_dialog_.push_back(input);
          }
          break;
        }
        const auto point = dialogs.empty()
                               ? std::nullopt
                               : dialogs.back()->list_item_point(id, row);
        if (!point) {
          throw ScriptFailure(2, command.where + ": dialog-list " +
                                     std::to_string(id) + " " +
                                     std::to_string(row) +
                                     ": no visible row of a visible, enabled "
                                     "list box");
        }
        const int presses = command.numbers[1] != 0 ? 2 : 1;
        for (int press = 0; press < presses; ++press) {
          DialogInput input{};
          input.pointer = {InputKind::down, point->first, point->second, 0U};
          pending_dialog_.push_back(input);
          input.pointer.kind = InputKind::up;
          pending_dialog_.push_back(input);
        }
        break;
      }
      case ScriptOp::dialog_at: {
        ++next_;
        const auto dialogs = session_.dialogs();
        if (dialogs.empty()) {
          throw ScriptFailure(2, command.where + ": dialog-at: no dialog");
        }
        const int x = dialogs.back()->client_x() + command.x;
        const int y = dialogs.back()->client_y() + command.y;
        const bool right = command.keys != 0U;
        DialogInput input{};
        input.pointer = {right ? InputKind::right_down : InputKind::down, x, y,
                         0U};
        pending_dialog_.push_back(input);
        input.pointer.kind = right ? InputKind::right_up : InputKind::up;
        pending_dialog_.push_back(input);
        break;
      }
      case ScriptOp::dialog_wait: {
        ++next_;
        DialogInput input{};
        input.kind = DialogInput::Kind::wait_elapsed;
        input.elapsed_ms = static_cast<std::uint32_t>(command.value);
        pending_dialog_.push_back(input);
        break;
      }
      case ScriptOp::dialog_timer: {
        ++next_;
        DialogInput input{};
        input.kind = DialogInput::Kind::timer;
        pending_dialog_.push_back(input);
        break;
      }
      case ScriptOp::dialog_hold:
        ++next_;
        if (!hold_dialog_) {
          throw ScriptFailure(2, command.where +
                                     ": dialog-hold cannot write a frame here");
        }
        hold_dialog_(command.text, command.where);
        break;
      default:
        // Any other line (another expect-dialog included): the dialog takes
        // its default answer.
        return std::nullopt;
    }
  }
  const auto input = pending_dialog_.front();
  pending_dialog_.pop_front();
  return input;
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
      session_.set_alert_answer(static_cast<int>(command.value),
                                command.where);
      break;
    case ScriptOp::set_balance:
      session_.set_balance_for_test(static_cast<std::int32_t>(command.value));
      break;
    case ScriptOp::expect_type:
    case ScriptOp::expect_mode:
    case ScriptOp::expect_balance:
      check_expectation(command);
      break;
    case ScriptOp::digest:
      print_digest_();
      break;
    case ScriptOp::menu:
      if (!session_.panel_enabled(Panel::main)) {
        throw ScriptFailure(2, command.where + ": menu " +
                                   std::to_string(command.value) +
                                   ": Main and its menu are disabled while "
                                   "Elevator Control is open");
      }
      if (!find_original_menu_command(session_.menu(),
                                      static_cast<std::uint16_t>(
                                          command.value))) {
        throw ScriptFailure(2, command.where + ": menu " +
                                   std::to_string(command.value) +
                                   " is not in the live menu");
      }
      (void)session_.choose_menu_command(
          static_cast<std::uint16_t>(command.value));
      break;
    case ScriptOp::key:
      (void)session_.accelerator(static_cast<std::uint16_t>(command.value));
      break;
    case ScriptOp::pick_file:
      if (command.text.empty()) {
        session_.queue_picked_file(std::nullopt);
      } else {
        session_.queue_picked_file(std::filesystem::path(command.text));
      }
      break;
    case ScriptOp::menu_show: {
      MenuView view{};
      view.bar = command.numbers[0];
      if (command.numbers.size() > 1) view.item = command.numbers[1];
      if (command.numbers.size() > 2) {
        view.cascade_open = true;
        view.cascade_item = command.numbers[2];
      }
      const auto* popup = original_sub_menu(
          session_.menu(), static_cast<std::size_t>(view.bar));
      const bool item_ok =
          popup && view.item < static_cast<int>(popup->size());
      const OriginalMenuItems* cascade =
          item_ok && view.item >= 0
              ? original_sub_menu(*popup, static_cast<std::size_t>(view.item))
              : nullptr;
      if (!item_ok || (view.cascade_open &&
                       (!cascade || view.cascade_item >=
                                        static_cast<int>(cascade->size())))) {
        throw ScriptFailure(2, command.where +
                                   ": menu-show indexes are outside the menu");
      }
      // A highlighted cascade row opens its drop-down, as USER does.
      if (cascade) view.cascade_open = true;
      session_.set_menu_view(view);
      break;
    }
    case ScriptOp::menu_hide:
      session_.set_menu_view({});
      break;
    case ScriptOp::expect_menu:
    case ScriptOp::expect_title:
    case ScriptOp::expect_saved:
      check_expectation(command);
      break;
    case ScriptOp::open_dialog:
      // Test shortcut: the child dialogs Person / Facility Information open,
      // or Facility / Person Information as a Magnifier hit opens them.
      if (command.text == "rename-person") {
        OriginalRenameTarget target{};
        target.person = true;
        target.person_index = static_cast<std::size_t>(command.numbers[0]);
        (void)session_.run_rename_dialog(target);
      } else if (command.text == "rename-tenant") {
        OriginalRenameTarget target{};
        target.person = false;
        target.floor = static_cast<std::int16_t>(command.numbers[0]);
        target.tenant_index = static_cast<std::size_t>(command.numbers[1]);
        (void)session_.run_rename_dialog(target);
      } else if (command.text == "facility") {
        session_.open_facility_information(
            static_cast<std::int16_t>(command.numbers[0]),
            static_cast<std::size_t>(command.numbers[1]));
      } else if (command.text == "person") {
        (void)session_.open_person_information(
            static_cast<std::size_t>(command.numbers[0]),
            static_cast<OriginalPersonInformationContext>(command.value));
      } else if (command.text == "transport-car" ||
                 command.text == "transport-stair") {
        // The target a Magnifier hit on that car or stair / escalator
        // builds (select_original_magnifier_target).
        const auto* document = session_.document();
        const bool car = command.text == "transport-car";
        const auto target =
            !document ? std::nullopt
            : car     ? original_elevator_car_information_target(
                            *document,
                            static_cast<std::size_t>(command.numbers[0]),
                            static_cast<std::size_t>(command.numbers[1]))
                      : original_vertical_transport_information_target(
                            *document,
                            static_cast<std::size_t>(command.numbers[0]));
        if (!target) {
          throw ScriptFailure(2, command.where +
                                     (car ? ": no such car in service"
                                          : ": no such stair / escalator"));
        }
        session_.open_transport_information(*target);
      } else {
        (void)session_.run_movie_dialog(
            static_cast<std::size_t>(command.numbers[0]));
      }
      break;
    case ScriptOp::open_elevator_control:
      session_.open_elevator_control_for_test(
          static_cast<std::size_t>(command.value));
      break;
    case ScriptOp::panel_at:
    case ScriptOp::panel_down:
    case ScriptOp::panel_move:
    case ScriptOp::panel_up:
    case ScriptOp::panel_double_click:
    case ScriptOp::panel_click: {
      const auto* panel = session_.elevator_control();
      if (!panel) {
        throw ScriptFailure(2, command.where +
                                   ": no Elevator Control is open");
      }
      const int x = panel->client_x() + command.x;
      const int y = panel->client_y() + command.y;
      const auto deliver = [&](InputKind kind) {
        session_.pointer_event({kind, x, y, 0U});
        while (!pending_.empty()) {
          const auto queued = pending_.front();
          pending_.pop_front();
          session_.pointer_event(queued);
        }
      };
      if (command.op == ScriptOp::panel_click) {
        // The release is queued behind the press: the press's
        // GetAsyncKeyState answer is "up" (primary_button_held), then the
        // release is routed. A modal the press opened anyway would take
        // the release, as for panel-at.
        const auto dialogs = session_.host_requests().dialogs;
        release_queued_ = true;
        try {
          deliver(InputKind::down);
        } catch (...) {
          release_queued_ = false;
          throw;
        }
        release_queued_ = false;
        if (session_.host_requests().dialogs == dialogs) {
          deliver(InputKind::up);
        }
      } else if (command.op == ScriptOp::panel_at) {
        const bool right = command.keys != 0U;
        // A press that ran a modal (a phase button's ELVPOPUP) gave its
        // release to that modal: under USER's capture the popup ends on
        // it, so ELVDLOGMAIN never sees it. Otherwise the release follows.
        const auto dialogs = session_.host_requests().dialogs;
        deliver(right ? InputKind::right_down : InputKind::down);
        if (session_.host_requests().dialogs == dialogs) {
          deliver(right ? InputKind::right_up : InputKind::up);
        }
      } else {
        deliver(command.op == ScriptOp::panel_down   ? InputKind::down
                : command.op == ScriptOp::panel_move ? InputKind::move
                : command.op == ScriptOp::panel_up   ? InputKind::up
                                                     : InputKind::double_click);
      }
      break;
    }
    case ScriptOp::panel_key: {
      if (!session_.elevator_control()) {
        throw ScriptFailure(2, command.where +
                                   ": no Elevator Control is open");
      }
      DialogInput input{};
      input.kind = DialogInput::Kind::key;
      input.key = command.dialog_key;
      input.character = command.character;
      input.shift = command.shift;
      session_.elevator_control_input(input);
      if (command.character != '\0' ||
          command.dialog_key == DialogKey::space) {
        DialogInput typed{};
        typed.kind = DialogInput::Kind::character;
        typed.character = command.character != '\0' ? command.character : ' ';
        session_.elevator_control_input(typed);
      }
      input.kind = DialogInput::Kind::key_up;
      session_.elevator_control_input(input);
      break;
    }
    case ScriptOp::expect_panel:
    case ScriptOp::expect_served:
    case ScriptOp::expect_scroll:
    case ScriptOp::expect_phase:
    case ScriptOp::expect_waiting:
    case ScriptOp::expect_isolation:
    case ScriptOp::expect_idle_passes:
    case ScriptOp::expect_rent:
    case ScriptOp::expect_find_latch:
    case ScriptOp::expect_help:
      check_expectation(command);
      break;
    case ScriptOp::hold:
      if (!hold_dialog_) {
        throw ScriptFailure(2, command.where + ": hold cannot write a frame "
                                               "here");
      }
      hold_dialog_(command.text, command.where);
      break;
    case ScriptOp::expect_dialog:
      // Read by drives_dialog when a dialog opens.
      throw ScriptFailure(3, command.where + ": expect-dialog " +
                                 command.text + ", but no dialog opened");
    case ScriptOp::dialog_click:
    case ScriptOp::dialog_key:
    case ScriptOp::dialog_type:
    case ScriptOp::dialog_list:
    case ScriptOp::dialog_at:
    case ScriptOp::dialog_wait:
    case ScriptOp::dialog_timer:
    case ScriptOp::dialog_hold:
      // Dialog lines are read by next_dialog_input while a dialog is open.
      throw ScriptFailure(2, command.where + ": " + op_name(command.op) +
                                 " with no dialog open");
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
    case ScriptOp::expect_menu: {
      const auto* item = find_original_menu_command(
          session_.menu(), static_cast<std::uint16_t>(command.value));
      std::string state = "absent";
      if (item) {
        if (command.text == "checked" || command.text == "unchecked") {
          state = item->checked() ? "checked" : "unchecked";
        } else {
          state = item->grayed() ? "grayed" : "enabled";
        }
      }
      if (state != command.text) {
        throw ScriptFailure(3, command.where + ": expected menu " +
                                   std::to_string(command.value) + " " +
                                   command.text + ", got " + state);
      }
      return;
    }
    case ScriptOp::expect_saved: {
      std::ifstream stream(command.text, std::ios::binary);
      const std::vector<char> chars((std::istreambuf_iterator<char>(stream)),
                                    std::istreambuf_iterator<char>());
      const auto* document = session_.document();
      const auto expected =
          document ? serialize_original_tdt_game_save(*document)
                   : std::vector<std::byte>{};
      const bool same =
          stream.good() || stream.eof()
              ? chars.size() == expected.size() &&
                    std::equal(chars.begin(), chars.end(), expected.begin(),
                               [](char a, std::byte b) {
                                 return static_cast<unsigned char>(a) ==
                                        static_cast<unsigned char>(b);
                               })
              : false;
      if (!document || !same) {
        throw ScriptFailure(3, command.where + ": " + command.text +
                                   " does not hold the live tower (" +
                                   std::to_string(chars.size()) + " bytes, " +
                                   std::to_string(expected.size()) +
                                   " expected)");
      }
      return;
    }
    case ScriptOp::expect_panel: {
      const auto* controller = session_.elevator_controller();
      const std::int64_t open =
          controller ? static_cast<std::int64_t>(
                           controller->state().elevator_index)
                     : -1;
      if (open != command.value) {
        throw ScriptFailure(
            3, command.where + ": expected " +
                   (command.value < 0 ? std::string("no Elevator Control")
                                      : "Elevator Control on shaft " +
                                            std::to_string(command.value)) +
                   ", got " +
                   (open < 0 ? std::string("none")
                             : "shaft " + std::to_string(open)));
      }
      return;
    }
    case ScriptOp::expect_served: {
      const auto* document = session_.document();
      const auto index = static_cast<std::size_t>(command.numbers[0]);
      const int floor = command.numbers[1];
      if (!document || index >= document->elevators.size() || floor < 0 ||
          floor >= 120) {
        throw ScriptFailure(2, command.where + ": no such shaft or floor");
      }
      actual = document->elevators[index]
                           .serviced_floors[static_cast<std::size_t>(floor)] !=
                       std::byte{0}
                   ? 1
                   : 0;
      what = "served";
      break;
    }
    case ScriptOp::expect_scroll:
    case ScriptOp::expect_phase:
    case ScriptOp::expect_waiting:
    case ScriptOp::expect_isolation: {
      const auto* controller = session_.elevator_controller();
      if (!controller) {
        throw ScriptFailure(3, command.where +
                                   ": no Elevator Control is open");
      }
      if (command.op == ScriptOp::expect_scroll) {
        actual = controller->state().scroll_position;
        what = "scroll position";
      } else if (command.op == ScriptOp::expect_phase) {
        actual = controller->state().day_phase;
        what = "day phase";
      } else if (command.op == ScriptOp::expect_waiting) {
        const auto* document = session_.document();
        const auto& state = controller->state();
        if (!document || state.elevator_index >= document->elevators.size()) {
          throw ScriptFailure(2, command.where + ": no such shaft");
        }
        actual = original_elevator_control_waiting_value(
            document->elevators[state.elevator_index], state);
        what = "waiting count";
      } else {
        actual = controller->state().isolation_active ? 1 : 0;
        what = "isolation";
      }
      break;
    }
    case ScriptOp::expect_idle_passes:
      actual = static_cast<std::int64_t>(session_.idle_passes());
      what = "idle passes";
      break;
    case ScriptOp::expect_find_latch:
      actual = session_.find_marker().phase != 0U ? 1 : 0;
      what = "find latch";
      break;
    case ScriptOp::expect_rent: {
      const auto* document = session_.document();
      const auto rate =
          document ? get_original_facility_rent_rate(
                         *document, static_cast<std::int16_t>(command.numbers[0]),
                         static_cast<std::size_t>(command.numbers[1]))
                   : std::nullopt;
      if (!rate) throw ScriptFailure(2, command.where + ": no such tenant");
      actual = *rate;
      what = "rent rate";
      break;
    }
    case ScriptOp::expect_help: {
      const auto& help = session_.last_help();
      const std::string actual_help =
          !help ? "none" : help->page.empty() ? "missing" : help->page;
      if (actual_help != command.text) {
        throw ScriptFailure(3, command.where + ": expected help " +
                                   command.text + ", got " + actual_help);
      }
      return;
    }
    case ScriptOp::expect_title:
      if (session_.window_title() != command.text) {
        throw ScriptFailure(3, command.where + ": expected title '" +
                                   command.text + "', got '" +
                                   session_.window_title() + "'");
      }
      return;
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
