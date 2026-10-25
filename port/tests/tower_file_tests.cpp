// The shared File transactions (original_tower_file.hpp) against a
// recording host and real files in a temporary directory: Save As with a
// cancel, an invalid name retry, the replace prompt (No and Yes), a normal
// Save, the transition confirmation (cancel, proceed, save then proceed),
// Open with a cancel, a successful load and failed loads falling back to
// New. No assets.

#include "original_tower_file.hpp"

#include "original_tdt.hpp"

#include <cassert>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace simtower;

class RecordingHost final : public OriginalTowerFileHost {
 public:
  std::vector<std::string> calls;
  std::optional<OriginalTdtDocument> tower = make_original_new_tdt();
  std::filesystem::path path{};
  std::filesystem::path title{"untitled"};
  std::deque<std::optional<std::filesystem::path>> picks{};
  bool replace_answer{true};
  int transition_answer{2};
  bool file_name_only{false};

  void log(std::string text) { calls.push_back(std::move(text)); }

  OriginalTdtDocument* document() override {
    return tower ? &*tower : nullptr;
  }
  std::filesystem::path tower_path() override { return path; }
  std::filesystem::path tower_title() override { return title; }
  void commit_tower_identity(const std::filesystem::path& new_path,
                             const std::filesystem::path& new_title) override {
    path = new_path;
    title = new_title;
    log("commit " + new_title.string());
  }
  void store_view_in_header() override { log("view"); }
  void begin_file_dialog() override { log("begin"); }
  void end_file_dialog() override { log("end"); }
  std::optional<OriginalPickedTowerFile> pick_open_file() override {
    log("pick open");
    auto next = take();
    if (!next) return std::nullopt;
    return OriginalPickedTowerFile{*next, {}};
  }
  std::optional<std::filesystem::path> pick_save_file() override {
    log("pick save");
    return take();
  }
  [[nodiscard]] std::wstring_view path_separators() const override {
    return L"\\/";
  }
  [[nodiscard]] bool basename_rule_uses_file_name_only() const override {
    return file_name_only;
  }
  bool confirm_replace(const std::filesystem::path&) override {
    log("replace?");
    return replace_answer;
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
  void install_empty_document() override {
    tower = make_original_new_tdt();
    log("empty");
  }
  void install_loaded_document(OriginalTdtDocument document) override {
    tower = std::move(document);
    log("loaded");
  }
  void begin_new_tower() override {
    tower = make_original_new_tdt();
    path.clear();
    title = "untitled";
    log("new");
  }

 private:
  std::optional<std::filesystem::path> take() {
    if (picks.empty()) return std::nullopt;
    auto next = picks.front();
    picks.pop_front();
    return next;
  }
};

int failures = 0;

void check(RecordingHost& host, bool actual, bool expected_result,
           std::initializer_list<const char*> calls, const char* what) {
  const std::vector<std::string> expected(calls.begin(), calls.end());
  if (actual != expected_result || host.calls != expected) {
    ++failures;
    std::fprintf(stderr, "%s: result %d, calls:", what, actual);
    for (const auto& call : host.calls) {
      std::fprintf(stderr, " [%s]", call.c_str());
    }
    std::fprintf(stderr, "\n  expected:");
    for (const auto& call : expected) {
      std::fprintf(stderr, " [%s]", call.c_str());
    }
    std::fprintf(stderr, "\n");
  }
  host.calls.clear();
}

}  // namespace

int main() {
  const auto directory = std::filesystem::temp_directory_path() /
                         "simtower_tower_file_tests";
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  const auto saved = directory / "TOWER.TDT";

  RecordingHost host;
  // Save on an untitled tower is Save As; a cancel restores the palettes.
  host.picks = {std::nullopt};
  check(host, save_original_tower(host), false,
        {"begin", "pick save", "end"}, "save as cancel");
  // A basename over eight characters reopens the sheet (begin again,
  // without end); the extension is replaced by .TDT before the check.
  host.picks = {directory / "LONGERNAME.txt", directory / "TOWER.dat"};
  check(host, save_original_tower_as(host), true,
        {"begin", "pick save", "invalid", "begin", "pick save", "view",
         "commit TOWER.TDT", "end"},
        "save as retry");
  assert(std::filesystem::exists(saved));
  assert(host.path == saved);
  // The original measures to the first dot in the whole path, so under a
  // dotted directory a nine-character name passes; the file-name-only rule
  // used off Windows rejects it and reopens the sheet.
  const auto dotted = directory / "my.dir";
  std::filesystem::create_directories(dotted);
  host.picks = {dotted / "LONGERNAME.TDT"};
  check(host, save_original_tower_as(host), true,
        {"begin", "pick save", "view", "commit LONGERNAME.TDT", "end"},
        "dotted directory, whole-path rule");
  host.file_name_only = true;
  host.picks = {dotted / "LONGERNAME.TDT", dotted / "TOWER.TDT"};
  check(host, save_original_tower_as(host), true,
        {"begin", "pick save", "invalid", "begin", "pick save", "view",
         "commit TOWER.TDT", "end"},
        "dotted directory, file-name-only rule");
  host.file_name_only = false;
  host.picks = {saved};
  host.replace_answer = true;
  check(host, save_original_tower_as(host), true,
        {"begin", "pick save", "replace?", "view", "commit TOWER.TDT", "end"},
        "back to the main save");
  // Save As onto an existing file asks; No keeps it and writes nothing.
  host.picks = {saved};
  host.replace_answer = false;
  check(host, save_original_tower_as(host), false,
        {"begin", "pick save", "replace?", "end"}, "replace no");
  host.picks = {saved};
  host.replace_answer = true;
  check(host, save_original_tower_as(host), true,
        {"begin", "pick save", "replace?", "view", "commit TOWER.TDT", "end"},
        "replace yes");
  // A titled Save writes straight to the path, without any prompt.
  check(host, save_original_tower(host), true, {"view", "commit TOWER.TDT"},
        "save");

  // A new tower needs no confirmation.
  check(host, confirm_original_tower_transition(host, 1), true, {},
        "confirm empty tower");

  // Open: cancel, then a good file, then a broken one (alert 2 and New).
  host.picks = {std::nullopt};
  check(host, open_original_tower(host), false, {"begin", "pick open", "end"},
        "open cancel");
  host.picks = {saved};
  check(host, open_original_tower(host), true,
        {"begin", "pick open", "recent", "empty", "loaded", "commit TOWER.TDT",
         "view", "end"},
        "open");
  const auto broken = directory / "BROKEN.TDT";
  {
    std::ofstream stream(broken, std::ios::binary);
    stream << "not a tower";
  }
  // Its first word ("no", 0x6f6e) reads as a version newer than 0x24: the
  // too-new alert, STRL/1002 entry 4.
  host.picks = {broken};
  check(host, open_original_tower(host), false,
        {"begin", "pick open", "recent", "empty", "error 4", "new", "end"},
        "open broken");
  check(host, load_original_tower_from_path(host, directory / "MISSING.TDT",
                                            {}),
        false, {"recent", "empty", "error 2", "new"}, "open missing");

  // No document: Save does nothing after the sheet.
  host.tower.reset();
  host.path = saved;
  host.title = "TOWER.TDT";
  check(host, save_original_tower(host), false, {}, "save without tower");

  assert(original_tdt_load_error_string(OriginalTdtStatus::version_too_new) ==
         4U);
  assert(original_tdt_load_error_string(OriginalTdtStatus::version_too_old) ==
         5U);
  assert(original_tdt_load_error_string(OriginalTdtStatus::malformed) == 2U);
  // The basename rule: the original's backslash by default, a host's
  // separators otherwise.
  assert(!original_tdt_basename_is_valid("/dir/ABCDEFGHI.TDT", L"/"));
  assert(original_tdt_basename_is_valid("/dir/ABCDEFGH.TDT", L"/"));
  assert(!original_tdt_basename_is_valid("/dir/ABCDEFGH.TDT"));
  assert(original_tdt_basename_is_valid("C:\\dir\\ABCDEFGH.TDT"));

  std::filesystem::remove_all(directory);
  if (failures != 0) {
    std::fprintf(stderr, "tower_file_tests: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("tower_file_tests: all transactions ok\n");
  return 0;
}
