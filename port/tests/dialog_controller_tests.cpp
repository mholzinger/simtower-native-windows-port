// The shared dialog controllers (original_dialog_controllers.hpp) against
// frozen copies of the Win32 procedures' transactions in native_main.cpp
// as they were before the procedures shared them: FINDDIALOGFILTER's
// resolve / remove / close (find_dialog_proc now runs the controller), the
// two rename Save branches (now calling original_rename_save), the two
// rename procedures' whole WM_COMMAND and the New Movie procedure's button
// gate and WM_COMMAND (all three now run the controllers) and
// COUNTDLOGMAIN's key-up, mouse and text placement (finance_dialog_proc and
// its painter now run the controller and original_finance_text_ops). The
// frozen copies prove the shared path behaves as Win32 did. Each scenario
// runs on two copies of a staged tower; the event logs and the saved bytes
// must be identical. Runs natively and under Wine; the staged tower needs
// the pack (exit 77 without it).

#include "original_dialog_controllers.hpp"
#include "original_dialog_model.hpp"
#include "original_finance.hpp"
#include "original_find.hpp"
#include "original_information.hpp"
#include "original_resources.hpp"
#include "original_startup_data.hpp"
#include "original_tdt.hpp"
#include "original_ui.hpp"
#include "test_scenario.hpp"

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using simtower::OriginalDialogSignal;
using simtower::OriginalFindMode;
using simtower::OriginalTdtDocument;
using Kind = OriginalDialogSignal::Kind;

std::optional<std::vector<std::byte>> read_file(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::nullopt;
  const std::vector<char> chars((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(chars.size());
  for (std::size_t i = 0; i < chars.size(); ++i) {
    bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(chars[i]));
  }
  return bytes;
}

OriginalDialogSignal command(std::uint16_t id, std::uint16_t code = 0U) {
  return {Kind::command, id, code};
}

std::string enable_event(std::uint16_t id, bool on) {
  return "enable " + std::to_string(id) + (on ? " 1" : " 0");
}

// A dialog's controls as the procedures see them: the list box, the
// enabled flags, item texts and focus.
struct FakeOps final : simtower::OriginalDialogOps {
  std::vector<std::string> list{};
  int selection{-1};
  std::map<std::uint16_t, bool> enabled{};
  std::map<std::uint16_t, std::string> texts{};
  std::uint16_t focus{};
  int invalidations{};
  // UpdateWindow calls, and the invalidations each one followed.
  std::vector<int> updates{};
  std::vector<simtower::OriginalRect> rects{};
  // When set, every EnableWindow is logged in call order.
  std::vector<std::string>* log{};

  void show_item(std::uint16_t, bool) override {}
  void invalidate() override { ++invalidations; }
  void invalidate_rect(const simtower::OriginalRect& rect) override {
    rects.push_back(rect);
    ++invalidations;
  }
  void update_window() override { updates.push_back(invalidations); }
  void resize(int, int) override {}
  void enable_item(std::uint16_t id, bool on) override {
    enabled[id] = on;
    if (log) log->push_back(enable_event(id, on));
  }
  void focus_item(std::uint16_t id) override { focus = id; }
  void set_item_text(std::uint16_t id, std::string_view text) override {
    texts[id] = std::string(text);
  }
  [[nodiscard]] std::string item_text(std::uint16_t id) const override {
    const auto it = texts.find(id);
    return it == texts.end() ? std::string{} : it->second;
  }
  void list_reset(std::uint16_t) override {
    list.clear();
    selection = -1;
  }
  void list_add(std::uint16_t, std::string_view text) override {
    list.emplace_back(text);
  }
  void list_delete(std::uint16_t, int index) override {
    list.erase(list.begin() + index);
    if (selection == index) {
      selection = -1;
    } else if (selection > index) {
      --selection;
    }
  }
  [[nodiscard]] int list_selection(std::uint16_t) const override {
    return selection;
  }
  void list_set_selection(std::uint16_t, int index) override {
    selection = index;
  }
};

// --- Find ---------------------------------------------------------------

struct FindLog {
  std::vector<std::string> events{};
  std::uint32_t tick{1000U};
};

class RecordingFindHost final : public simtower::OriginalFindDialogHost {
 public:
  RecordingFindHost(OriginalTdtDocument& document, FindLog& log)
      : document_(document), log_(log) {}
  OriginalTdtDocument* document() override { return &document_; }
  int main_client_width() override { return 640; }
  int main_client_height() override { return 400; }
  void reset_marker() override { log_.events.push_back("reset"); }
  void start_marker(const simtower::OriginalFindResolution& resolution,
                    std::uint32_t selected) override {
    log_.events.push_back("start " + std::to_string(resolution.floor) + " " +
                          std::to_string(resolution.x) + " " +
                          std::to_string(resolution.view.x) + " " +
                          std::to_string(resolution.view.y) + " " +
                          std::to_string(selected));
  }
  void alert(int id, const std::array<std::string, 4>& subs) override {
    log_.events.push_back("alert " + std::to_string(id) + " " + subs[0] +
                          "|" + subs[1]);
  }
  void entry_removed() override { log_.events.push_back("dirty"); }
  void invalidate_main() override { log_.events.push_back("invalidate"); }
  void set_arrow_cursor() override { log_.events.push_back("cursor"); }

 private:
  OriginalTdtDocument& document_;
  FindLog& log_;
};

// Frozen copy of the Win32 FINDDIALOGFILTER transactions (native_main.cpp
// before this change), with the HWND calls replaced by the same log and the
// list box by a vector.
struct LegacyFind {
  OriginalFindMode mode{};
  std::vector<simtower::OriginalFindEntry> entries{};
  std::vector<std::string> list{};
  int selection{-1};
  std::map<std::uint16_t, bool> enabled{};
  simtower::OriginalFindResolution resolution{};
  bool changed{};
  FindLog* log{};
  std::optional<int> ended{};

  void init() {
    list.clear();
    for (const auto& entry : entries) list.push_back(entry.name);
    enabled[3] = false;
    enabled[4] = false;
  }
  bool resolve(OriginalTdtDocument& document) {
    const int selected = selection;
    if (selected < 0 || static_cast<std::size_t>(selected) >= entries.size()) {
      return false;
    }
    const auto& entry = entries[static_cast<std::size_t>(selected)];
    log->events.push_back("reset");
    if (mode == OriginalFindMode::person) {
      resolution = simtower::resolve_original_find_person(document, entry.link,
                                                          640, 400);
    } else {
      resolution = simtower::resolve_original_find_tenant(
          document, static_cast<std::uint16_t>(entry.link), 640, 400);
    }
    if (mode == OriginalFindMode::person && resolution.alerts()) {
      log->events.push_back("cursor");
      if (resolution.kind ==
          simtower::OriginalFindResolutionKind::not_in_tower_alert) {
        log->events.push_back("alert 1002 " + entry.name + "|");
      } else {
        log->events.push_back(
            "alert 1003 " + entry.name + "|" +
            simtower::original_find_floor_text(resolution.floor));
      }
      resolution = {};
      log->events.push_back("invalidate");
      return false;
    }
    if (!resolution.focused()) return false;
    log->events.push_back(
        "start " + std::to_string(resolution.floor) + " " +
        std::to_string(resolution.x) + " " +
        std::to_string(resolution.view.x) + " " +
        std::to_string(resolution.view.y) + " " +
        std::to_string(mode == OriginalFindMode::person ? entry.link
                                                        : 0xffffffffU));
    ended = 1;
    return true;
  }
  void command(OriginalTdtDocument& document, std::uint16_t id,
               std::uint16_t code) {
    using Action = simtower::OriginalFindDialogCommandAction;
    const auto plan = simtower::original_find_dialog_command_plan(id, code);
    switch (plan.action) {
      case Action::close:
        log->events.push_back("reset");
        log->events.push_back("invalidate");
        ended = 1;
        break;
      case Action::remove: {
        const int selected = selection;
        if (selected >= 0 &&
            simtower::remove_original_find_entry(
                document, mode, static_cast<std::size_t>(selected))) {
          list.erase(list.begin() + selected);
          if (selection == selected) selection = -1;
          entries.erase(entries.begin() + selected);
          changed = true;
          log->events.push_back("dirty");
        }
        break;
      }
      case Action::resolve:
        (void)resolve(document);
        break;
      case Action::enable_actions:
        enabled[3] = enabled[4] = true;
        break;
      case Action::disable_actions:
        enabled[3] = enabled[4] = false;
        break;
      case Action::none:
        break;
    }
  }
};

struct FindStep {
  int select{-2};  // -2: leave the selection
  std::uint16_t id{};
  std::uint16_t code{};
};

void find_parity(const OriginalTdtDocument& base, OriginalFindMode mode,
                 const std::vector<FindStep>& steps) {
  auto legacy_document = base;
  auto shared_document = base;
  FindLog legacy_log;
  FindLog shared_log;
  LegacyFind legacy{};
  legacy.mode = mode;
  legacy.entries = simtower::original_find_entries(legacy_document, mode);
  legacy.log = &legacy_log;
  legacy.init();
  simtower::OriginalFindController controller(
      mode, simtower::original_find_entries(shared_document, mode));
  FakeOps ops;
  RecordingFindHost host(shared_document, shared_log);
  controller.initialize(ops);
  assert(ops.list == legacy.list && ops.enabled == legacy.enabled);
  // The Win32 tail: original_find_initialization_focus_plan (no SetFocus,
  // TRUE so the dialog manager focuses OK).
  assert(controller.initial_focus(ops) && ops.focus == 0U);
  std::optional<int> ended{};
  for (const auto& step : steps) {
    if (step.select != -2) {
      ops.selection = step.select;
      legacy.selection = step.select;
    }
    const auto outcome =
        controller.signal(command(step.id, step.code), ops, host);
    legacy.command(legacy_document, step.id, step.code);
    if (outcome.result) ended = outcome.result;
    assert(ops.list == legacy.list);
    assert(ops.selection == legacy.selection);
    assert(ops.enabled == legacy.enabled);
    assert(shared_log.events == legacy_log.events);
    assert(ended == legacy.ended);
    if (ended) break;
  }
  assert(controller.changed() == legacy.changed);
  assert(controller.resolution() == legacy.resolution);
  assert(simtower::serialize_original_tdt_game_save(shared_document) ==
         simtower::serialize_original_tdt_game_save(legacy_document));
}

// --- Rename -------------------------------------------------------------

// Frozen copy of the Win32 Save branches before original_rename_save.
simtower::OriginalRenameSaveResult legacy_rename_save(
    OriginalTdtDocument& document, const simtower::OriginalRenameTarget& t,
    std::string_view name) {
  simtower::OriginalRenameSaveResult r{};
  if (t.person) {
    const auto result =
        simtower::set_original_person_name(document, t.person_index, name);
    if (result.status == simtower::OriginalPersonNameStatus::too_long) {
      r.alert_strl_index = 4U;
      return r;  // return TRUE: stays open, changed untouched
    }
    if (result.status == simtower::OriginalPersonNameStatus::full) {
      r.alert_strl_index = 6U;
    }
    r.changed = result.changed;
  } else {
    const auto result = simtower::set_original_tenant_name(
        document, t.floor, t.tenant_index, name);
    if (result.status == simtower::OriginalTenantNameStatus::too_long) {
      r.alert_strl_index = 4U;
      return r;
    }
    if (result.status == simtower::OriginalTenantNameStatus::full) {
      r.alert_strl_index = 7U;
    }
    r.changed = result.changed;
  }
  r.close = true;
  return r;
}

void rename_parity(const OriginalTdtDocument& base,
                   const std::vector<simtower::OriginalRenameTarget>& targets) {
  auto legacy_document = base;
  auto shared_document = base;
  const std::vector<std::string> names = {
      "A", "Bob", "Fifteen chars!!", "Sixteen chars!!!", "Bob", "Zoe",
      std::string(40, 'x')};
  int full = 0;
  int too_long = 0;
  for (std::size_t round = 0; round < 25U; ++round) {
    for (const auto& target : targets) {
      for (const auto& name : names) {
        auto label = name;
        if (name == "Zoe") label += std::to_string(round);
        const auto legacy = legacy_rename_save(legacy_document, target, label);
        const auto shared =
            simtower::original_rename_save(shared_document, target, label);
        assert(legacy == shared);
        if (shared.alert_strl_index == 4U) ++too_long;
        if (shared.alert_strl_index == 6U || shared.alert_strl_index == 7U) {
          ++full;
        }
      }
    }
  }
  // Distinct people and tenants until both name tables are full.
  for (std::size_t index = 0; index < legacy_document.people.size() &&
                              index < 400U;
       ++index) {
    const simtower::OriginalRenameTarget target{true, index, 0, 0U};
    const auto name = "P" + std::to_string(index);
    const auto legacy = legacy_rename_save(legacy_document, target, name);
    const auto shared =
        simtower::original_rename_save(shared_document, target, name);
    assert(legacy == shared);
    if (shared.alert_strl_index == 6U) ++full;
  }
  for (std::int16_t floor = 0; floor < 120; ++floor) {
    for (std::size_t index = 0; index < 12U; ++index) {
      const simtower::OriginalRenameTarget target{false, 0U, floor, index};
      const auto name = "T" + std::to_string(floor) + "." +
                        std::to_string(index);
      const auto legacy = legacy_rename_save(legacy_document, target, name);
      const auto shared =
          simtower::original_rename_save(shared_document, target, name);
      assert(legacy == shared);
      if (shared.alert_strl_index == 7U) ++full;
    }
  }
  assert(too_long > 0 && full > 0);
  assert(simtower::serialize_original_tdt_game_save(shared_document) ==
         simtower::serialize_original_tdt_game_save(legacy_document));
  std::printf("rename parity: %d too-long, %d full alerts\n", too_long, full);
}

class RecordingRenameHost final : public simtower::OriginalRenameDialogHost {
 public:
  explicit RecordingRenameHost(OriginalTdtDocument& document)
      : document_(document) {}
  OriginalTdtDocument* document() override { return &document_; }
  void alert_strl(std::uint16_t index) override { alerts.push_back(index); }
  std::vector<std::uint16_t> alerts{};

 private:
  OriginalTdtDocument& document_;
};

void rename_controller_tests(OriginalTdtDocument document,
                             const simtower::OriginalRenameTarget& target) {
  simtower::OriginalRenameController controller(target);
  FakeOps ops;
  RecordingRenameHost host(document);
  controller.initialize(ops, document);
  // An unnamed target: empty text, Delete and Rename disabled.
  assert(ops.texts[4].empty() && !ops.enabled[3] && !ops.enabled[1]);
  // The WM_INITDIALOG tail: no SetFocus, TRUE (the dialog manager focuses
  // the first tab stop); edit 4 waits for the WM_PAINT tail.
  assert(controller.initial_focus(ops) && ops.focus == 0U);
  // WM_PAINT: the edit is enabled and focused, nothing else.
  assert(!controller.signal({Kind::paint, 0U}, ops, host).result);
  assert(ops.focus == 4U && ops.enabled[4]);
  // EN_CHANGE (any code) re-runs the gate.
  ops.texts[4] = "Al";
  assert(!controller.signal(command(4, simtower::kOriginalEditChange), ops,
                            host)
              .result);
  assert(ops.enabled[1]);
  // Save with empty text stays open.
  ops.texts[4].clear();
  assert(!controller.signal(command(1), ops, host).result);
  // Too long: STRL/1005[4], stays open, nothing changed.
  ops.texts[4] = std::string(20, 'q');
  assert(!controller.signal(command(1), ops, host).result);
  assert(host.alerts == std::vector<std::uint16_t>{4U} &&
         !controller.changed());
  // Saved: EndDialog(1), changed, and the saved name reads back.
  ops.texts[4] = "Al Tower";
  assert(controller.signal(command(1), ops, host).result == 1);
  assert(controller.changed());
  assert(simtower::original_rename_initial_text(document, target) ==
         "Al Tower");
  // A new dialog: Delete enabled for the saved name; Delete removes it.
  simtower::OriginalRenameController again(target);
  FakeOps second;
  again.initialize(second, document);
  assert(second.texts[4] == "Al Tower" && second.enabled[3] &&
         second.enabled[1]);
  assert(again.signal(command(3), second, host).result == 1);
  assert(again.changed());
  assert(simtower::original_rename_initial_text(document, target).empty());
  // Cancel closes with 1 and changes nothing; IDCANCEL from Esc is 2 too.
  simtower::OriginalRenameController cancelled(target);
  assert(cancelled.signal(command(2), second, host).result == 1);
  assert(!cancelled.changed());
  // Other ids fall through.
  assert(!cancelled.signal(command(9), second, host).result);
}

// The rename procedures' WM_COMMAND, end to end. Both sides log EnableWindow,
// the ALRT/1000 index and each close tail step in call order.
class LoggingRenameHost final : public simtower::OriginalRenameDialogHost {
 public:
  LoggingRenameHost(OriginalTdtDocument* document,
                    std::vector<std::string>& log)
      : document_(document), log_(log) {}
  OriginalTdtDocument* document() override { return document_; }
  void alert_strl(std::uint16_t index) override {
    log_.push_back("alert " + std::to_string(index));
  }

 private:
  OriginalTdtDocument* document_{};
  std::vector<std::string>& log_;
};

// The close tails both procedures keep: NAMEPEPLEDIALOGFILTER 1100:3ce8-3d0f
// (DTMP, brush, DS:31a4, ReleaseCapture, EndDialog) and
// NAMETENANTDIALOGFILTER 1100:4069-4082 (DTMP, DS:31a4, EndDialog).
void log_rename_close(bool person_proc, int result,
                      std::vector<std::string>& log) {
  log.push_back("dtmp");
  if (person_proc) log.push_back("brush");
  log.push_back("modal");
  if (person_proc) log.push_back("capture");
  log.push_back("end " + std::to_string(result));
}

// Frozen copy of person_rename_dialog_proc / tenant_rename_dialog_proc's
// WM_COMMAND (native_main.cpp before they ran the controller), with the
// HWND calls replaced by the log and edit 4 by a string.
struct LegacyRenameDialog {
  simtower::OriginalRenameTarget target{};
  OriginalTdtDocument* document{};  // g_tower_document
  std::string edit{};
  bool changed{};
  std::vector<std::string>* log{};
  std::optional<int> ended{};

  bool command(std::uint16_t id) {
    using CommandAction = simtower::OriginalRenameDialogCommandAction;
    const auto plan = simtower::original_rename_dialog_command_plan(id);
    const auto close_dialog = [&]() {
      log_rename_close(target.person, plan.close_result, *log);
      ended = plan.close_result;
    };
    switch (plan.action) {
      case CommandAction::refresh_edit_gate:
        // enable_original_person_rename_button (GetWindowTextLengthW
        // through original_rename_ok_enabled) and
        // enable_original_tenant_rename_button (length != 0).
        log->push_back(enable_event(
            1U, target.person
                    ? simtower::original_rename_ok_enabled(edit.size())
                    : !edit.empty()));
        return true;
      case CommandAction::save:
        if (!document) return plan.consume;
        {
          if (edit.empty()) return true;
          const auto result =
              simtower::original_rename_save(*document, target, edit);
          if (result.alert_strl_index) {
            log->push_back("alert " +
                           std::to_string(*result.alert_strl_index));
          }
          if (!result.close) return true;
          changed = changed || result.changed;
        }
        close_dialog();
        return true;
      case CommandAction::cancel:
        close_dialog();
        return true;
      case CommandAction::remove:
        if (document) {
          const bool removed =
              target.person
                  ? simtower::remove_original_person_name(
                        *document, target.person_index)
                        .changed
                  : simtower::remove_original_tenant_name(
                        *document, target.floor, target.tenant_index)
                        .changed;
          changed = changed || removed;
        }
        close_dialog();
        return true;
      case CommandAction::none:
        return plan.consume;
    }
    return false;
  }
};

// The procedures as they are now: signal_original_rename_dialog (a
// controller per message over the dialog's ops and Win32RenameDialogHost)
// and the proc's close tail when it returns a result.
struct SharedRenameDialog {
  simtower::OriginalRenameTarget target{};
  OriginalTdtDocument* document{};
  FakeOps ops{};
  bool changed{};
  std::vector<std::string>* log{};
  std::optional<int> ended{};

  bool command(std::uint16_t id, std::uint16_t code) {
    const auto plan = simtower::original_rename_dialog_command_plan(id);
    ops.log = log;
    LoggingRenameHost host(document, *log);
    simtower::OriginalRenameController controller(target);
    const auto outcome = controller.signal(::command(id, code), ops, host);
    changed = changed || controller.changed();
    if (outcome.result) {
      log_rename_close(target.person, *outcome.result, *log);
      ended = outcome.result;
    }
    return plan.consume;
  }
};

struct RenameStep {
  std::string text{};
  std::uint16_t id{};
  std::uint16_t code{};
};

// Runs the steps on both copies until the dialog closes; returns the number
// of steps run.
std::size_t rename_command_parity(
    const OriginalTdtDocument& base,
    const simtower::OriginalRenameTarget& target, bool with_document,
    const std::vector<RenameStep>& steps) {
  auto legacy_document = base;
  auto shared_document = base;
  std::vector<std::string> legacy_log;
  std::vector<std::string> shared_log;
  LegacyRenameDialog legacy{target,
                            with_document ? &legacy_document : nullptr};
  legacy.log = &legacy_log;
  SharedRenameDialog shared{target,
                            with_document ? &shared_document : nullptr};
  shared.log = &shared_log;
  std::size_t run = 0U;
  for (const auto& step : steps) {
    legacy.edit = step.text;
    shared.ops.texts[simtower::kOriginalRenameEditId] = step.text;
    const bool legacy_return = legacy.command(step.id);
    const bool shared_return = shared.command(step.id, step.code);
    ++run;
    assert(legacy_return == shared_return);
    assert(shared_log == legacy_log);
    assert(shared.ended == legacy.ended);
    assert(shared.changed == legacy.changed);
    if (shared.ended) break;
  }
  assert(simtower::serialize_original_tdt_game_save(shared_document) ==
         simtower::serialize_original_tdt_game_save(legacy_document));
  return run;
}

// A copy of `base` whose person (or tenant) name table is full, and an
// unnamed target whose Save then reports the full table.
std::pair<OriginalTdtDocument, simtower::OriginalRenameTarget>
full_name_table(const OriginalTdtDocument& base, bool person) {
  auto document = base;
  if (person) {
    for (std::size_t index = 0; index < document.people.size(); ++index) {
      // A full table returns before touching the document.
      const auto result = simtower::set_original_person_name(
          document, index, "P" + std::to_string(index));
      if (result.status == simtower::OriginalPersonNameStatus::full) {
        return {document, {true, index, 0, 0U}};
      }
    }
  } else {
    for (std::int16_t floor = 0; floor < 120; ++floor) {
      for (std::size_t index = 0; index < 12U; ++index) {
        const auto result = simtower::set_original_tenant_name(
            document, floor, index,
            "T" + std::to_string(floor) + "." + std::to_string(index));
        if (result.status == simtower::OriginalTenantNameStatus::full) {
          return {document, {false, 0U, floor, index}};
        }
      }
    }
  }
  assert(false && "name table never filled");
  return {document, {}};
}

void rename_command_tests(
    const OriginalTdtDocument& base,
    const std::vector<simtower::OriginalRenameTarget>& targets) {
  constexpr std::uint16_t change = simtower::kOriginalEditChange;
  const std::string too_long(20, 'q');
  // Edit gate both ways (any notification), an unknown id and id 0, Save
  // with empty text, Save too long (alert, stays open), then a saved name.
  const std::vector<RenameStep> edit_then_save{
      {"", 4, change}, {"Al", 4, change}, {"Al", 4, 0x0100U},
      {"Al", 9, 0},    {"Al", 0, 0},      {"", 1, 0},
      {too_long, 1, 0}, {"Al Tower", 1, 0}};
  const std::vector<RenameStep> remove{{"x", 4, change}, {"", 3, 0}};
  const std::vector<RenameStep> cancel{{"Zed", 4, change}, {"Zed", 2, 0}};
  const std::vector<RenameStep> no_document{
      {"Al", 4, change}, {"Al", 1, 0}, {"Al", 3, 0}};
  int closes = 0;
  for (const auto& target : targets) {
    // Named (the staged names) and unnamed targets.
    auto unnamed = base;
    (void)simtower::original_rename_remove(unnamed, target);
    const OriginalTdtDocument& unnamed_view = unnamed;
    for (const auto* document : {&base, &unnamed_view}) {
      assert(rename_command_parity(*document, target, true, edit_then_save) ==
             edit_then_save.size());
      assert(rename_command_parity(*document, target, true, remove) == 2U);
      assert(rename_command_parity(*document, target, true, cancel) == 2U);
      assert(rename_command_parity(*document, target, false, no_document) ==
             3U);
      closes += 4;
    }
  }
  // A full table: STRL/1005[6] or [7], and the dialog still closes.
  for (const bool person : {true, false}) {
    const auto [full, target] = full_name_table(base, person);
    assert(rename_command_parity(full, target, true,
                                 {{"New Name", 1, 0}}) == 1U);
    std::vector<std::string> log;
    SharedRenameDialog shared{target, nullptr};
    auto copy = full;
    shared.document = &copy;
    shared.log = &log;
    shared.ops.texts[simtower::kOriginalRenameEditId] = "New Name";
    (void)shared.command(1, 0);
    assert(!log.empty() && log.front() == (person ? "alert 6" : "alert 7"));
    assert(shared.ended == 1);
    ++closes;
  }
  std::printf("rename command parity: %zu targets, %d closing runs\n",
              targets.size(), closes);
}

// --- New Movie ------------------------------------------------------------

void movie_tests(OriginalTdtDocument document,
                 const simtower::OriginalPartTable& part) {
  const auto new_cost = std::bit_cast<std::int16_t>(part.words_52_to_ac[34]);
  const auto classic_cost =
      std::bit_cast<std::int16_t>(part.words_52_to_ac[35]);
  assert(new_cost > classic_cost && classic_cost > 0);
  for (const std::int32_t balance :
       {0, classic_cost - 1, static_cast<int>(classic_cost),
        new_cost - 1, static_cast<int>(new_cost), 100000000}) {
    document.header.balance = balance;
    // Frozen copy of enable_original_movie_choice_buttons.
    const bool legacy_new = document.header.balance >= new_cost;
    const bool legacy_classic = document.header.balance >= classic_cost;
    const auto enabled =
        simtower::original_movie_choice_enabled(document, part);
    assert(enabled.new_release == legacy_new &&
           enabled.classic == legacy_classic);
    simtower::OriginalMovieChoiceController controller(0U);
    FakeOps ops;
    controller.initialize(ops, document, part);
    assert(ops.enabled[1] == legacy_new && ops.enabled[3] == legacy_classic);
  }
  simtower::OriginalMovieChoiceController controller(0U);
  assert(controller.signal(command(2), &document, part).result == 0);
  assert(!controller.signal(command(7), &document, part).result);
  assert(controller.signal(command(1), nullptr, part).result == 1);
  assert(!controller.changed());
}

// Frozen copy of movie_choice_dialog_proc's WM_INITDIALOG button gate and
// WM_COMMAND (native_main.cpp before it ran the controller), the HWND calls
// replaced by a log.
struct LegacyMovieDialog {
  std::size_t record{};
  OriginalTdtDocument* document{};  // g_tower_document
  const simtower::OriginalPartTable* part{};
  bool changed{};
  std::vector<std::string>* log{};
  std::optional<int> ended{};

  // enable_original_movie_choice_buttons.
  void init() {
    if (!document) return;
    const auto enabled =
        simtower::original_movie_choice_enabled(*document, *part);
    log->push_back(enable_event(1U, enabled.new_release));
    log->push_back(enable_event(3U, enabled.classic));
  }
  bool command(std::uint16_t id) {
    using CommandAction = simtower::OriginalMovieChoiceDialogCommandAction;
    const auto plan = simtower::original_movie_choice_dialog_command_plan(id);
    switch (plan.action) {
      case CommandAction::cancel:
        log->push_back("dtmp");
        log->push_back("end 0");
        ended = 0;
        return true;
      case CommandAction::new_release:
      case CommandAction::classic:
        if (document) {
          const auto choice = plan.action == CommandAction::new_release
                                  ? simtower::OriginalMovieChoice::new_release
                                  : simtower::OriginalMovieChoice::classic;
          const auto result =
              simtower::choose_original_movie(*document, *part, record, choice);
          changed = changed || result.changed;
        }
        log->push_back("dtmp");
        log->push_back("end 1");
        ended = 1;
        return true;
      case CommandAction::none:
        return plan.consume;
    }
    return false;
  }
};

// movie_choice_dialog_proc as it is now: the controller's initialize (with
// the proc's document guard) and a controller per WM_COMMAND, then the
// proc's DTMP release and EndDialog when it returns a result.
struct SharedMovieDialog {
  std::size_t record{};
  OriginalTdtDocument* document{};
  const simtower::OriginalPartTable* part{};
  FakeOps ops{};
  bool changed{};
  std::vector<std::string>* log{};
  std::optional<int> ended{};

  void init() {
    ops.log = log;
    if (!document) return;
    simtower::OriginalMovieChoiceController(record).initialize(ops, *document,
                                                               *part);
  }
  bool command(std::uint16_t id, std::uint16_t code) {
    const auto plan = simtower::original_movie_choice_dialog_command_plan(id);
    simtower::OriginalMovieChoiceController controller(record);
    const auto outcome =
        controller.signal(::command(id, code), document, *part);
    changed = changed || controller.changed();
    if (outcome.result) {
      log->push_back("dtmp");
      log->push_back("end " + std::to_string(*outcome.result));
      ended = outcome.result;
    }
    return plan.consume;
  }
};

void movie_command_tests(const OriginalTdtDocument& base,
                         const simtower::OriginalPartTable& part) {
  const auto new_cost = std::bit_cast<std::int16_t>(part.words_52_to_ac[34]);
  const auto classic_cost =
      std::bit_cast<std::int16_t>(part.words_52_to_ac[35]);
  const std::size_t records = base.post_elevator.dc24_records.size();
  const std::vector<std::vector<std::uint16_t>> scripts{
      {7, 0, 4, 1}, {3}, {2}, {0xffffU, 2}};
  int runs = 0;
  int changed = 0;
  for (const std::int32_t balance :
       {0, static_cast<int>(classic_cost), static_cast<int>(new_cost),
        100000000}) {
    for (const std::size_t record :
         {std::size_t{0}, records == 0U ? std::size_t{0} : records - 1U,
          records + 3U}) {
      for (const bool with_document : {true, false}) {
        for (const auto& script : scripts) {
          auto legacy_document = base;
          auto shared_document = base;
          legacy_document.header.balance = balance;
          shared_document.header.balance = balance;
          std::vector<std::string> legacy_log;
          std::vector<std::string> shared_log;
          LegacyMovieDialog legacy{
              record, with_document ? &legacy_document : nullptr, &part};
          legacy.log = &legacy_log;
          SharedMovieDialog shared{
              record, with_document ? &shared_document : nullptr, &part};
          shared.log = &shared_log;
          legacy.init();
          shared.init();
          assert(shared_log == legacy_log);
          for (const auto id : script) {
            const bool legacy_return = legacy.command(id);
            const bool shared_return = shared.command(id, 0U);
            assert(legacy_return == shared_return);
            assert(shared_log == legacy_log);
            assert(shared.ended == legacy.ended);
            assert(shared.changed == legacy.changed);
            if (shared.ended) break;
          }
          assert(shared.ended.has_value());
          assert(simtower::serialize_original_tdt_game_save(
                     shared_document) ==
                 simtower::serialize_original_tdt_game_save(
                     legacy_document));
          ++runs;
          if (shared.changed) ++changed;
        }
      }
    }
  }
  std::printf("movie command parity: %d runs, %d changed\n", runs, changed);
}

// --- Finance ------------------------------------------------------------

void finance_tests(const simtower::OriginalResources& resources,
                   const OriginalTdtDocument& document) {
  const auto dtmp = simtower::parse_original_dtmp(resources.find("DTMP", 500));
  const auto& r = dtmp.rectangles[0];
  const simtower::OriginalRect button{r.left, r.top, r.right, r.bottom};
  simtower::OriginalFinanceController controller(button);
  FakeOps ops;
  // WM_KEYUP: Return and Space present pressed then released and close
  // with 0; others do nothing (original_finance_key_presentation).
  assert(!controller.signal({Kind::key_up, 0x41U}, ops).result);
  assert(ops.invalidations == 0);
  assert(controller.signal({Kind::key_up, 0x0DU}, ops).result == 0);
  assert(ops.invalidations == 2 && !controller.pressed());
  // Each surface is InvalidateRect(button) then UpdateWindow (nm 5160-5170).
  assert(ops.updates == std::vector<int>({1, 2}));
  assert(ops.rects.size() == 2U && ops.rects[0] == button &&
         ops.rects[1] == button);
  assert(controller.signal({Kind::key_up, 0x20U}, ops).result == 0);
  // WM_COMMAND (Enter's IDOK, Esc's IDCANCEL) is ignored.
  assert(!controller.signal(command(1), ops).result);
  assert(!controller.signal(command(2), ops).result);
  // A press in the button draws it pressed; a release inside closes
  // wherever the press was; a release outside only redraws it.
  OriginalDialogSignal down{Kind::button_down};
  down.x = button.left + 2;
  down.y = button.top + 2;
  assert(!controller.signal(down, ops).result && controller.pressed());
  OriginalDialogSignal up{Kind::button_up};
  up.x = button.left - 5;
  up.y = button.top;
  assert(!controller.signal(up, ops).result && !controller.pressed());
  down.x = 0;
  down.y = 0;
  assert(!controller.signal(down, ops).result && !controller.pressed());
  up.x = button.right - 1;
  up.y = button.bottom - 1;
  assert(controller.signal(up, ops).result == 0);

  // Text placement: the Win32 painter's order, fonts and anchors.
  const auto view = simtower::derive_original_finance_view(document);
  const auto measure = [](int pixels, std::string_view text) {
    return static_cast<int>(text.size()) * pixels;
  };
  const auto ops_list = simtower::original_finance_text_ops(view, dtmp,
                                                            measure);
  assert(ops_list.size() == 9U + 30U);
  const std::array<std::int32_t, 9> summary{
      view.total_income, view.total_maintenance, view.year, view.quarter,
      view.net_revenues, view.other_income, view.construction_costs,
      view.last_quarter_balance, view.total_balance};
  for (std::size_t i = 0; i < 9U; ++i) {
    const auto& op = ops_list[i];
    const std::size_t index = 6U + i;
    assert(op.rectangle == index && !op.right_baseline);
    assert(op.font_pixels == (index <= 7U ? 14 : 12));
    assert(op.text == std::to_string(summary[i]));
    const auto& rect = dtmp.rectangles[index - 1U];
    const auto position = simtower::original_finance_value_position(
        index, rect.right, rect.top, measure(op.font_pixels, op.text));
    assert(op.x == position.x && op.y == position.y);
  }
  for (std::size_t row = 0; row < 10U; ++row) {
    for (std::size_t column = 0; column < 3U; ++column) {
      const auto& op = ops_list[9U + row * 3U + column];
      const auto& rect = dtmp.rectangles[2U + column];
      assert(op.rectangle == 3U + column && op.right_baseline &&
             op.font_pixels == 12);
      assert(op.x == rect.right - 1 &&
             op.y == rect.top + static_cast<int>(row + 1U) * 13 - 2);
      const std::array values{view.population[row], view.income[row],
                              view.maintenance[row]};
      assert(op.text == std::to_string(values[column]));
    }
  }
}

// Names three people and three tenants of the staged tower (whatever the
// first ones the name tables accept are).
void name_entries(OriginalTdtDocument& document,
                  std::vector<simtower::OriginalRenameTarget>& targets) {
  int people = 0;
  for (std::size_t index = 0; index < document.people.size() && people < 3;
       ++index) {
    const auto result = simtower::set_original_person_name(
        document, index, "Person " + std::to_string(index));
    if (result.changed) {
      targets.push_back({true, index, 0, 0U});
      ++people;
    }
  }
  int tenants = 0;
  for (std::int16_t floor = 0; floor < 120 && tenants < 3; ++floor) {
    for (std::size_t index = 0; index < 8U && tenants < 3; ++index) {
      const auto result = simtower::set_original_tenant_name(
          document, floor, index, "Shop " + std::to_string(tenants));
      if (result.changed) {
        targets.push_back({false, 0U, floor, index});
        ++tenants;
      }
    }
  }
  assert(people == 3 && tenants == 3);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 77;
  const auto pack = read_file(argv[1]);
  if (!pack) {
    std::printf("skipped: no resource pack at %s\n", argv[1]);
    return 77;
  }
  const simtower::OriginalResources resources{
      std::span<const std::byte>(*pack)};
  const auto startup = simtower::load_original_startup_data(resources);
  auto document = simtower::make_original_new_tdt();
  (void)simtower::build_scenario("busy", document, startup.part,
                                 startup.construction_costs);
  std::vector<simtower::OriginalRenameTarget> targets;
  name_entries(document, targets);

  // Find: person alerts (the staged people are not placed yet), tenant
  // focus, Remove, selection change / cancel, OK, Esc's IDCANCEL (none).
  find_parity(document, OriginalFindMode::person,
              {{0, 5, 1}, {-2, 4, 0}, {1, 5, 2}, {-1, 4, 0}, {-2, 5, 3},
               {2, 3, 0}, {-2, 3, 0}, {0, 5, 7}, {-2, 2, 0}, {-2, 1, 0}});
  find_parity(document, OriginalFindMode::tenant,
              {{1, 5, 1}, {-2, 3, 0}, {0, 5, 2}});
  find_parity(document, OriginalFindMode::tenant,
              {{0, 5, 1}, {-2, 4, 0}});
  find_parity(document, OriginalFindMode::tenant, {{-1, 4, 0}, {-2, 1, 0}});

  rename_parity(document, targets);
  for (const auto& target : targets) {
    auto fresh = document;
    (void)simtower::original_rename_remove(fresh, target);
    rename_controller_tests(fresh, target);
  }
  rename_command_tests(document, targets);
  movie_tests(document, startup.part);
  movie_command_tests(document, startup.part);
  finance_tests(resources, document);
  std::printf("dialog controller tests passed\n");
  return 0;
}
