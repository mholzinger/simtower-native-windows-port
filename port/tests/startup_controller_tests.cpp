// The shared startup pieces against frozen copies of the Win32 code in
// native_main.cpp as it was before it shared them:
// - NEWORLOADDLOGFILTER's WM_INITDIALOG focus tail and WM_COMMAND
//   (startup_dialog_proc now runs OriginalNewLoadDialogController);
// - WinMain's New / Load result handling, 1128:01a5-01d3 (it now runs
//   apply_original_startup_choice).
// Each frozen copy logs its HWND calls and host calls; the shared path
// must log the same, in the same order, and return the same values. No
// assets: runs natively and under Wine.

#include "original_dialog_controllers.hpp"
#include "original_dialog_model.hpp"
#include "original_startup.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace {

using simtower::OriginalDialogSignal;
using Log = std::vector<std::string>;

// The procedure's own dialog, as far as the controller can reach it.
struct LogOps final : simtower::OriginalDialogOps {
  Log* log{};
  void show_item(std::uint16_t, bool) override { log->push_back("show"); }
  void invalidate() override { log->push_back("invalidate"); }
  void resize(int, int) override { log->push_back("resize"); }
  void enable_item(std::uint16_t, bool) override { log->push_back("enable"); }
  void focus_item(std::uint16_t id) override {
    log->push_back("focus " + std::to_string(id));
  }
};

// --- NEWORLOADDLOGFILTER --------------------------------------------------

// Frozen: WM_INITDIALOG's last two statements.
bool legacy_initial_focus(Log& log) {
  log.push_back("focus 1");  // SetFocus(GetDlgItem(dialog, 1))
  return true;               // return TRUE
}

// Frozen: the WM_COMMAND case.
bool legacy_command(std::uint16_t id, Log& log) {
  const auto plan = simtower::original_new_load_dialog_command_plan(id);
  if (plan.release_dtmp_before_end) {
    log.push_back("dtmp");  // g_startup_dtmp = {}
  }
  if (plan.end_dialog) {
    log.push_back("end " + std::to_string(plan.dialog_result));
  }
  return plan.consume;
}

// startup_dialog_proc now.
bool shared_initial_focus(Log& log) {
  LogOps ops;
  ops.log = &log;
  return simtower::OriginalNewLoadDialogController{}.initial_focus(ops);
}

bool shared_command(std::uint16_t id, std::uint16_t code, Log& log) {
  const auto outcome = simtower::OriginalNewLoadDialogController{}.signal(
      {OriginalDialogSignal::Kind::command, id, code});
  if (outcome.result) {
    log.push_back("dtmp");
    log.push_back("end " + std::to_string(*outcome.result));
  }
  return outcome.result.has_value();
}

void new_load_dialog_tests() {
  Log legacy_log;
  Log shared_log;
  assert(legacy_initial_focus(legacy_log) == shared_initial_focus(shared_log));
  assert(legacy_log == shared_log);
  int closing = 0;
  for (std::uint32_t id = 0U; id <= 0xFFFFU; ++id) {
    for (const std::uint16_t code : {0U, 1U, 5U, 0x0300U, 0xFFFFU}) {
      Log legacy;
      Log shared;
      const bool legacy_return =
          legacy_command(static_cast<std::uint16_t>(id), legacy);
      const bool shared_return =
          shared_command(static_cast<std::uint16_t>(id), code, shared);
      assert(legacy_return == shared_return);
      assert(legacy == shared);
      if (legacy_return && code == 0U) ++closing;
    }
  }
  assert(closing == 3);
  // Only WM_COMMAND reaches the result (the other messages have their own
  // branches or none).
  const simtower::OriginalNewLoadDialogController controller;
  for (const auto kind :
       {OriginalDialogSignal::Kind::timer, OriginalDialogSignal::Kind::close,
        OriginalDialogSignal::Kind::paint,
        OriginalDialogSignal::Kind::button_down,
        OriginalDialogSignal::Kind::key_up}) {
    assert(!controller.signal({kind, 1U}).result);
  }
  std::printf("new/load dialog parity: %d closing ids\n", closing);
}

// --- WinMain's startup choice ---------------------------------------------

struct Host final : simtower::OriginalStartupChoiceHost {
  Log* log{};
  bool open_result{};
  bool document_after_open{};
  bool document{};

  bool open_tower() override {
    log->push_back("open");
    document = document || document_after_open;
    return open_result;
  }
  bool has_document() override {
    log->push_back("has_document");
    return document;
  }
  void begin_new_tower() override {
    log->push_back("new");
    document = true;
  }
};

// Frozen: WinMain after destroy_original_startup_splash, with the Main
// handle, g_tower_document and the teardown replaced by the host and log.
std::optional<int> legacy_choice(int startup_choice, Host& host) {
  if (startup_choice == 2 || startup_choice == -1) {
    host.log->push_back("teardown");
    return startup_choice == 2 ? 0 : 1;
  }
  if (startup_choice == 3) {
    if (!host.open_tower() && !host.has_document()) {
      host.begin_new_tower();
    }
  } else if (startup_choice == 1) {
    host.begin_new_tower();
  }
  return std::nullopt;
}

// WinMain now.
std::optional<int> shared_choice(int startup_choice, Host& host) {
  if (const auto exit_code =
          simtower::apply_original_startup_choice(startup_choice, host)) {
    host.log->push_back("teardown");
    return *exit_code;
  }
  return std::nullopt;
}

void startup_choice_tests() {
  int runs = 0;
  for (const int choice : {-2, -1, 0, 1, 2, 3, 4, 0x7FFF, 0xFFFF}) {
    for (const bool open_result : {false, true}) {
      for (const bool document_after_open : {false, true}) {
        Log legacy_log;
        Log shared_log;
        Host legacy{};
        legacy.log = &legacy_log;
        legacy.open_result = open_result;
        legacy.document_after_open = document_after_open;
        Host shared = legacy;
        shared.log = &shared_log;
        const auto legacy_exit = legacy_choice(choice, legacy);
        const auto shared_exit = shared_choice(choice, shared);
        assert(legacy_exit == shared_exit);
        assert(legacy_log == shared_log);
        assert(legacy.document == shared.document);
        ++runs;
      }
    }
  }
  // The recovered cases, spelled out.
  Log log;
  Host host{};
  host.log = &log;
  assert(simtower::apply_original_startup_choice(2, host) == 0);
  assert(simtower::apply_original_startup_choice(-1, host) == 1);
  assert(log.empty());
  assert(!simtower::apply_original_startup_choice(1, host));
  assert(log == Log({"new"}));
  log.clear();
  host.document = false;
  // A cancelled Open sheet: nothing loaded, no tower, so New.
  assert(!simtower::apply_original_startup_choice(3, host));
  assert(log == Log({"open", "has_document", "new"}));
  log.clear();
  host.document = false;
  // A failed load already installed New (10d0:0225).
  host.document_after_open = true;
  assert(!simtower::apply_original_startup_choice(3, host));
  assert(log == Log({"open", "has_document"}));
  log.clear();
  // 1128:01b7: any other result shows the windows with no call.
  assert(!simtower::apply_original_startup_choice(0, host));
  assert(!simtower::apply_original_startup_choice(4, host));
  assert(log.empty());
  std::printf("startup choice parity: %d runs\n", runs);
}

}  // namespace

int main() {
  new_load_dialog_tests();
  startup_choice_tests();
  std::printf("startup controller tests passed\n");
  return 0;
}
