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
            "expect-dialog takes event ID, alert ID, about, box replace "
            "or box invalid-filename");
      };
      if (words.size() == 2 && words[1] == "about") {
        command.text = kAboutDialogLabel;
      } else if (words.size() == 3 &&
                 (words[1] == "event" || words[1] == "alert")) {
        const auto id = parse_integer(words[2], where);
        if (id < 0 || id > 0xFFFF) throw fail("dialog id must be 0..65535");
        command.text = words[1] == "event"
                           ? event_dialog_label(static_cast<std::uint16_t>(id))
                           : alert_dialog_label(static_cast<int>(id));
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
    } else if (verb == "dialog-key") {
      command.op = ScriptOp::dialog_key;
      if (words.size() != 2) throw fail("dialog-key needs one key");
      static const std::pair<const char*, DialogKey> kKeys[] = {
          {"enter", DialogKey::enter}, {"esc", DialogKey::escape},
          {"tab", DialogKey::tab},     {"shift-tab", DialogKey::shift_tab},
          {"space", DialogKey::space}, {"arrow", DialogKey::arrow},
          {"other", DialogKey::other},
      };
      const auto* found = std::ranges::find_if(
          kKeys, [&](const auto& entry) { return words[1] == entry.first; });
      if (found != std::end(kKeys)) {
        command.dialog_key = found->second;
      } else if (words[1].size() == 1 && words[1][0] >= 'a' &&
                 words[1][0] <= 'z') {
        command.dialog_key = DialogKey::other;
        command.character = words[1][0];
      } else {
        throw fail("unknown dialog key '" + words[1] + "'");
      }
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

bool ScriptPlayer::drives_dialog(const ModalDialog& dialog) {
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
        ++next_;
        DialogInput input{};
        input.kind = DialogInput::Kind::key;
        input.key = command.dialog_key;
        input.character = command.character;
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
      session_.set_alert_answer(static_cast<int>(command.value));
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
    case ScriptOp::expect_dialog:
      // Read by drives_dialog when a dialog opens.
      throw ScriptFailure(3, command.where + ": expect-dialog " +
                                 command.text + ", but no dialog opened");
    case ScriptOp::dialog_click:
    case ScriptOp::dialog_key:
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
