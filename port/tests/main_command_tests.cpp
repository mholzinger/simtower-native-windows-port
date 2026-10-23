// The shared WM_COMMAND dispatcher (original_main_command.hpp) against a
// recording host: every id of the 27-entry table, the 3000..4001 generic
// route, the DefWindowProc fallthrough, and the DS:24b8 modal lock. Each
// case checks the exact ordered hook calls the Win32 switch made inline.
// No assets: the document is a fresh make_original_new_tdt tower and the
// PART table is default-constructed.

#include "original_main_command.hpp"

#include "original_tdt.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace simtower;

// The File commands' tower-file host: records into the command host's
// list. Every sheet is cancelled; the full transactions are covered by
// tower_file_tests.
class RecordingFileHost final : public OriginalTowerFileHost {
 public:
  explicit RecordingFileHost(std::vector<std::string>& calls)
      : calls_(calls) {}

  OriginalTdtDocument tower = make_original_new_tdt();
  int transition_answer{2};

  OriginalTdtDocument* document() override { return &tower; }
  std::filesystem::path tower_path() override { return {}; }
  std::filesystem::path tower_title() override { return "untitled"; }
  void commit_tower_identity(const std::filesystem::path&,
                             const std::filesystem::path&) override {
    log("commit");
  }
  void store_view_in_header() override { log("view"); }
  void begin_file_dialog() override { log("begin"); }
  void end_file_dialog() override { log("end"); }
  std::optional<OriginalPickedTowerFile> pick_open_file() override {
    log("pick open");
    return std::nullopt;
  }
  std::optional<std::filesystem::path> pick_save_file() override {
    log("pick save");
    return std::nullopt;
  }
  bool confirm_replace(const std::filesystem::path&) override {
    log("replace?");
    return false;
  }
  void invalid_filename() override { log("invalid"); }
  void file_error(std::uint16_t index) override {
    log("error " + std::to_string(index));
  }
  int transition_alert(std::uint16_t action) override {
    log("alert " + std::to_string(action));
    return transition_answer;
  }
  void remove_recent_file_items() override { log("recent"); }
  void install_empty_document() override { log("empty"); }
  void install_loaded_document(OriginalTdtDocument) override {
    log("loaded");
  }
  void begin_new_tower() override { log("new"); }

 private:
  void log(std::string text) { calls_.push_back(std::move(text)); }

  std::vector<std::string>& calls_;
};

class RecordingHost final : public OriginalMainCommandHost {
 public:
  std::vector<std::string> calls;
  RecordingFileHost file{calls};
  bool locked{};
  bool audio{true};
  std::array<bool, 3> categories{true, true, true};
  bool fast{true}, people{true}, effects{true};
  bool command_visible{true}, info_visible{true}, map_visible{true};
  int dialog_result{1};
  bool offer_result{true};
  bool fire_crew_changed{true};
  OriginalTdtDocument tower = make_original_new_tdt();
  bool has_document{true};
  OriginalPartTable part_table{};

  void log(std::string text) { calls.push_back(std::move(text)); }

  bool modal_input_locked() override { return locked; }
  int event_dialog(const OriginalEventDialogRequest& request) override {
    log("dialog " + std::to_string(request.dialog_id) + " " +
        std::to_string(request.argument) + " " +
        std::to_string(request.wave_resource));
    return dialog_result;
  }
  void exit_command() override { log("exit"); }
  OriginalTowerFileHost& tower_file() override { return file; }
  bool& command_flag(OriginalMainCommandFlag flag) override {
    switch (flag) {
      case OriginalMainCommandFlag::fast_mode: log("flag fast"); return fast;
      case OriginalMainCommandFlag::people_animation:
        log("flag people");
        return people;
      case OriginalMainCommandFlag::effects_animation:
        log("flag effects");
        return effects;
    }
    return fast;
  }
  void apply_menu_op(const OriginalMenuOp& op) override {
    assert(op.depth == 0U && !op.by_position);
    log(std::string(op.kind == OriginalMenuOpKind::check ? "check "
                                                          : "enable ") +
        std::to_string(op.target) + " " + std::to_string(op.flags));
  }
  OriginalTdtDocument* document() override {
    log("document");
    return has_document ? &tower : nullptr;
  }
  const OriginalPartTable& part() override { return part_table; }
  bool consume_fire_crew_resolution(
      const OriginalFireCrewResolution&) override {
    log("consume_fire_crew");
    return fire_crew_changed;
  }
  void fire_crew_hired() override { log("fire_crew_hired"); }
  bool audio_available() override { return audio; }
  bool audio_category_enabled(std::size_t category) override {
    log("category? " + std::to_string(category));
    return categories[category];
  }
  void set_audio_category_enabled(std::size_t category,
                                  bool enabled) override {
    log("category " + std::to_string(category) + " " +
        std::to_string(enabled));
    categories[category] = enabled;
  }
  void stop_all_audio(bool force) override {
    log("stop_all " + std::to_string(force));
  }
  bool& auxiliary_visible(OriginalAuxiliaryWindow window) override {
    switch (window) {
      case OriginalAuxiliaryWindow::command: return command_visible;
      case OriginalAuxiliaryWindow::info: return info_visible;
      case OriginalAuxiliaryWindow::map: break;
    }
    return map_visible;
  }
  void show_auxiliary(OriginalAuxiliaryWindow window,
                      OriginalAuxiliaryInsertAfter after) override {
    log("show " + std::to_string(static_cast<int>(window)) + " " +
        (after == OriginalAuxiliaryInsertAfter::topmost ? "topmost" : "top"));
  }
  void hide_auxiliary(OriginalAuxiliaryWindow window) override {
    log("hide " + std::to_string(static_cast<int>(window)));
  }
  void finance_dialog() override { log("finance"); }
  void about_dialog() override { log("about"); }
  void find_dialog(OriginalFindMode mode) override {
    log(mode == OriginalFindMode::person ? "find person" : "find tenant");
  }
  void help_contents() override { log("help"); }
  bool bomb_offer() override {
    log("bomb");
    return offer_result;
  }
  bool fire_offer() override {
    log("fire");
    return offer_result;
  }
  void world_interaction_changed() override { log("world_changed"); }
  void mark_document_dirty() override { log("dirty"); }
  void present_info_status() override { log("info"); }
  void present_main_direct() override { log("present_main"); }
};

int failures = 0;

void expect(RecordingHost& host, std::uint16_t command,
            OriginalMainCommandResult result,
            std::initializer_list<const char*> calls) {
  host.calls.clear();
  const auto actual = dispatch_original_main_command(host, command);
  const std::vector<std::string> expected(calls.begin(), calls.end());
  if (actual != result || host.calls != expected) {
    ++failures;
    std::fprintf(stderr, "command %u: result %d, calls:", command,
                 static_cast<int>(actual));
    for (const auto& call : host.calls) {
      std::fprintf(stderr, " [%s]", call.c_str());
    }
    std::fprintf(stderr, "\n  expected:");
    for (const auto& call : expected) {
      std::fprintf(stderr, " [%s]", call.c_str());
    }
    std::fprintf(stderr, "\n");
  }
}

constexpr auto handled = OriginalMainCommandResult::handled;
constexpr auto def = OriginalMainCommandResult::def_window_proc;

}  // namespace

int main() {
  RecordingHost host;

  // DS:24b8 consumes every command, table or not, without any effect.
  host.locked = true;
  for (const auto id : kOriginalMainCommandIds) expect(host, id, handled, {});
  expect(host, 3500, handled, {});
  expect(host, 1, handled, {});
  host.locked = false;

  expect(host, 3000, handled, {"dialog 3000 0 10000"});
  expect(host, 3001, handled, {"dialog 3001 0 10000"});
  expect(host, 3002, handled, {"dialog 3002 0 10000"});
  expect(host, 40001, handled, {"exit"});
  // File commands run the shared transactions on tower_file(). A fresh
  // tower has no floor-10 tenants, so New and Open ask nothing.
  expect(host, 40002, handled, {"begin", "pick save", "end"});
  // Save on the untitled tower is Save As.
  expect(host, 40003, handled, {"begin", "pick save", "end"});
  expect(host, 40004, handled, {"begin", "pick open", "end"});
  expect(host, 40005, handled, {"new"});
  // With floor-10 tenants New asks first (STRL/1001 action 1): 2 proceeds
  // without saving, anything else cancels.
  host.file.tower.floors[10U].tenants.emplace_back();
  expect(host, 40005, handled, {"alert 1", "new"});
  expect(host, 40004, handled, {"alert 2", "begin", "pick open", "end"});
  host.file.transition_answer = 3;
  expect(host, 40005, handled, {"alert 1"});
  expect(host, 40004, handled, {"alert 2"});
  host.file.tower.floors[10U].tenants.clear();

  // Toggles: flip the word, then CheckMenuItem by command with the new
  // state (MF_CHECKED = 8).
  expect(host, 40007, handled, {"flag fast", "check 40007 0"});
  assert(!host.fast);
  expect(host, 40007, handled, {"flag fast", "check 40007 8"});
  expect(host, 40009, handled, {"flag people", "check 40009 0"});
  expect(host, 40010, handled, {"flag effects", "check 40010 0"});
  assert(host.fast && !host.people && !host.effects);

  // Fire rescue: offer available (crew x word zero on a new tower), the
  // offer DIALOG/3012 (cost -PART word 36, zero by default), the
  // resolution, then Main/Info invalidation.
  expect(host, 40008, handled,
         {"document", "dialog 3012 0 10000", "consume_fire_crew",
          "fire_crew_hired"});
  host.fire_crew_changed = false;
  expect(host, 40008, handled,
         {"document", "dialog 3012 0 10000", "consume_fire_crew"});
  host.has_document = false;
  expect(host, 40008, handled, {"document"});
  expect(host, 9001, handled, {"document"});
  host.has_document = true;

  // Sound: flush every channel before disabling, never before enabling;
  // 40012 drives category 1 and 40013 category 2.
  expect(host, 40011, handled,
         {"category? 0", "stop_all 1", "category 0 0", "check 40011 0"});
  expect(host, 40011, handled,
         {"category? 0", "category 0 1", "check 40011 8"});
  expect(host, 40012, handled,
         {"category? 1", "stop_all 1", "category 1 0", "check 40012 0"});
  expect(host, 40013, handled,
         {"category? 2", "stop_all 1", "category 2 0", "check 40013 0"});
  host.audio = false;
  expect(host, 40011, handled, {});
  host.audio = true;

  // Windows: flip the visibility word; Tool Bar is raised TOPMOST, the
  // others TOP; no CheckMenuItem (1158:0886/08a6/08c4).
  expect(host, 40014, handled, {"hide 0"});
  expect(host, 40014, handled, {"show 0 topmost"});
  expect(host, 40015, handled, {"hide 1"});
  expect(host, 40015, handled, {"show 1 top"});
  expect(host, 40016, handled, {"hide 2"});
  expect(host, 40016, handled, {"show 2 top"});
  assert(host.command_visible && host.info_visible && host.map_visible);

  expect(host, 40017, handled, {"finance"});
  expect(host, 40018, handled, {"about"});
  expect(host, 40019, handled, {"find person"});
  expect(host, 40020, handled, {"find tenant"});
  expect(host, 40021, handled, {"help"});

  expect(host, 9000, handled, {"bomb", "world_changed"});
  expect(host, 9002, handled, {"fire", "world_changed"});
  host.offer_result = false;
  expect(host, 9000, handled, {"bomb"});
  expect(host, 9002, handled, {"fire"});
  // 9001 on a rating-1 tower: DIALOG/3040 with the (zero, default PART)
  // treasure, then dirty and the Info repaint.
  expect(host, 9001, handled,
         {"document", "dialog 3040 0 10001", "dirty", "info"});
  assert(host.tower.post_elevator.b922_flag == 1U);
  expect(host, 9003, handled, {"present_main"});

  // Fallthrough: 3000..4001 outside the table run the generic dialog and
  // still reach DefWindowProc; everything else goes there directly.
  expect(host, 3003, def, {"dialog 3003 0 10000"});
  expect(host, 4001, def, {"dialog 4001 0 10000"});
  expect(host, 2999, def, {});
  expect(host, 4002, def, {});
  expect(host, 40006, def, {});
  expect(host, 9004, def, {});

  // Every table id is handled by its own case (none falls through).
  for (const auto id : kOriginalMainCommandIds) {
    host.calls.clear();
    assert(dispatch_original_main_command(host, id) == handled);
  }

  if (failures != 0) {
    std::fprintf(stderr, "main_command_tests: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("main_command_tests: all commands ok\n");
  return 0;
}
