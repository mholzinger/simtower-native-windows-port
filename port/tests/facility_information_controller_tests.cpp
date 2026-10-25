// The shared Facility Information controller
// (original_facility_information_controller.hpp) against a frozen copy of
// the Win32 TENANTINFODLOGFILTER branches in native_main.cpp as they were
// before the procedure shared it: WM_INITDIALOG's combo fill and focus
// return, refresh_original_facility_dialog, WM_LBUTTONDOWN's portrait hit,
// WM_MOUSEMOVE's cursor and the whole WM_COMMAND switch. Each Win32 call
// the old code made is one log line; the controller runs over a recording
// OriginalDialogOps and a host that logs what Win32FacilityInformationHost
// does. The logs, results and saved towers must be identical for every
// facility of the "full" staged tower. Also: the portable painter on the
// canvas and the preview raster. Runs natively and under Wine; needs the
// pack (exit 77 without it).
//
// 2026-10-25: the owner chose the original's behaviour on both hosts, so
// the frozen copy now asserts it. Its WM_INITDIALOG / refresh EnableWindow
// and its CBN_SELCHANGE setter come from the shared builder
// (rent_control_enabled, set_original_facility_rent_rate), which now
// disables the combo only for a sold condo (1100:096f-09a5) and has no
// condo test in the setter (1100:0bae-0c2b); before, both were inverted.
// The staged condos are unsold (status 0x18 / 0x20), so their rent picks
// now change the tower on both sides. original_behaviour_tests covers the
// sold and unsold states, Yesterday's Profit for restaurants and fast
// food (1100:1716 -> 2715) and the shop's own picture (1228:02c2).

#include "original_construction.hpp"
#include "original_draw_canvas.hpp"
#include "original_facility_information_controller.hpp"
#include "original_information.hpp"
#include "original_information_paint.hpp"
#include "original_resources.hpp"
#include "original_startup_data.hpp"
#include "original_tdt.hpp"
#include "test_scenario.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

using simtower::OriginalDialogSignal;
using simtower::OriginalTdtDocument;
using Kind = OriginalDialogSignal::Kind;
using Log = std::vector<std::string>;

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

// What the nested modals answer in one run.
struct NestedAnswers {
  bool rename_changes{};
  bool movie_chooses{};
  bool person_changes{};
};

// The fake nested dialogs both sides run: Rename saves a name, New Movie
// runs the classic choice, Person Information changes nothing itself.
bool fake_rename(OriginalTdtDocument& document, std::int16_t floor,
                 std::size_t tenant, const NestedAnswers& answers, Log& log) {
  log.push_back("rename " + std::to_string(floor) + " " +
                std::to_string(tenant));
  if (!answers.rename_changes) return false;
  return simtower::set_original_tenant_name(document, floor, tenant,
                                            "Renamed")
      .changed;
}

bool fake_movie(OriginalTdtDocument& document,
                const simtower::OriginalPartTable& part, std::size_t record,
                const NestedAnswers& answers, Log& log) {
  log.push_back("movie " + std::to_string(record));
  if (!answers.movie_chooses) return false;
  (void)simtower::choose_original_movie(document, part, record,
                                        simtower::OriginalMovieChoice::classic);
  return true;
}

// The combo's CB_GETCURSEL answer (the user's pick before CBN_SELCHANGE).
struct Combo {
  int selection{-1};
};

// --- The frozen Win32 branches (nm 4345-4664 before the controller) ------

struct LegacyDialog {
  const simtower::OriginalResources& resources;
  const simtower::OriginalPartTable& part;
  OriginalTdtDocument* document;  // g_tower_document
  std::int16_t floor;
  std::size_t tenant_index;
  simtower::OriginalFacilityInformation information;
  simtower::OriginalDtmp dtmp;
  NestedAnswers answers;
  Combo* combo;
  Log* log;
  bool changed{};
  std::optional<int> ended{};

  void refresh() {
    if (!document) return;
    information = simtower::original_facility_information(
        resources, *document, part, floor, tenant_index);
    if (information.rent_control_visible) {
      combo->selection = information.selected_rent_rate;
      log->push_back("CB_SETCURSEL 13 " +
                     std::to_string(information.selected_rent_rate));
      log->push_back(std::string("EnableWindow 13 ") +
                     (information.rent_control_enabled ? "1" : "0"));
    }
    log->push_back("InvalidateRect all");
    log->push_back("UpdateWindow");
  }

  bool init() {
    if (information.rent_control_visible) {
      for (const auto& choice : information.rent_choices) {
        log->push_back("CB_ADDSTRING 13 " + choice);
      }
      combo->selection = information.selected_rent_rate;
      log->push_back("CB_SETCURSEL 13 " +
                     std::to_string(information.selected_rent_rate));
      log->push_back(std::string("EnableWindow 13 ") +
                     (information.rent_control_enabled ? "1" : "0"));
    }
    const auto focus_plan =
        simtower::original_painted_dialog_initialization_focus_plan();
    if (focus_plan.set_explicit_focus) log->push_back("SetFocus 1");
    return focus_plan.consume;
  }

  void button_down(int x, int y) {
    std::optional<std::size_t> person{};
    if (document) {
      person = simtower::original_information_person_sprite_hit(
          information.person_sprites, static_cast<std::int16_t>(x),
          static_cast<std::int16_t>(y));
    }
    if (person) {
      log->push_back("person " + std::to_string(*person));
      const bool person_changed = answers.person_changes;
      changed = changed || person_changed;
      refresh();
    }
  }

  void mouse_move(int x, int y) {
    const bool include_item_9 =
        information.dialog_group >= 9U && information.dialog_group <= 11U;
    const bool portrait_panel =
        simtower::original_information_portrait_panel_hit(
            dtmp, static_cast<std::int16_t>(x), static_cast<std::int16_t>(y),
            include_item_9);
    log->push_back("SetCursor " + std::to_string(portrait_panel ? 1003U : 0U));
  }

  bool command(std::uint16_t id, std::uint16_t code) {
    using CommandAction = simtower::OriginalFacilityInformationCommandAction;
    const auto command = simtower::original_facility_information_command_plan(
        id, code, information.dialog_group);
    switch (command.action) {
      case CommandAction::close:
        log->push_back("DS31a4 0");
        ended = 1;
        return true;
      case CommandAction::rename:
        if (document) {
          log->push_back("DS31a4 0");
          const bool renamed =
              fake_rename(*document, floor, tenant_index, answers, *log);
          log->push_back("DS31a4 dialog");
          log->push_back("TOPMOST");
          if (renamed) {
            changed = true;
            log->push_back("dirty");
            log->push_back("title");
          }
          refresh();
        }
        return command.consume;
      case CommandAction::change_rent:
        if (document) {
          log->push_back("CB_GETCURSEL 13");
          const auto selection = combo->selection;
          if (selection >= 0 && selection <= 3 &&
              simtower::set_original_facility_rent_rate(
                  *document, part, floor, tenant_index,
                  static_cast<std::uint8_t>(selection))) {
            changed = true;
            log->push_back("dirty");
            log->push_back("title");
            refresh();
          }
        }
        return command.consume;
      case CommandAction::choose_movie:
        if (document &&
            information.linked_record_index !=
                simtower::OriginalMagnifierTarget::kNoIndex &&
            fake_movie(*document, part, information.linked_record_index,
                       answers, *log)) {
          changed = true;
          log->push_back("dirty");
          log->push_back("title");
          refresh();
        }
        return command.consume;
      case CommandAction::none:
        return command.consume;
    }
    return false;
  }
};

// --- The shared side: Win32DialogOps and Win32FacilityInformationHost as
// log lines ---------------------------------------------------------------

class RecordingOps final : public simtower::OriginalDialogOps {
 public:
  RecordingOps(Combo& combo, Log& log) : combo_(combo), log_(log) {}
  void show_item(std::uint16_t, bool) override { log_.push_back("show"); }
  void invalidate() override { log_.push_back("InvalidateRect all"); }
  void invalidate_rect(const simtower::OriginalRect&) override {
    log_.push_back("InvalidateRect part");
  }
  void update_window() override { log_.push_back("UpdateWindow"); }
  void resize(int, int) override { log_.push_back("resize"); }
  void enable_item(std::uint16_t id, bool on) override {
    log_.push_back("EnableWindow " + std::to_string(id) + (on ? " 1" : " 0"));
  }
  void focus_item(std::uint16_t id) override {
    log_.push_back("SetFocus " + std::to_string(id));
  }
  void list_reset(std::uint16_t id) override {
    log_.push_back("CB_RESETCONTENT " + std::to_string(id));
  }
  void list_add(std::uint16_t id, std::string_view text) override {
    log_.push_back("CB_ADDSTRING " + std::to_string(id) + " " +
                   std::string(text));
  }
  void list_delete(std::uint16_t id, int) override {
    log_.push_back("CB_DELETESTRING " + std::to_string(id));
  }
  [[nodiscard]] int list_selection(std::uint16_t id) const override {
    log_.push_back("CB_GETCURSEL " + std::to_string(id));
    return combo_.selection;
  }
  void list_set_selection(std::uint16_t id, int index) override {
    combo_.selection = index;
    log_.push_back("CB_SETCURSEL " + std::to_string(id) + " " +
                   std::to_string(index));
  }

 private:
  Combo& combo_;
  Log& log_;
};

class RecordingHost final : public simtower::OriginalFacilityInformationHost {
 public:
  RecordingHost(const simtower::OriginalResources& resources,
                const simtower::OriginalPartTable& part,
                OriginalTdtDocument* document, NestedAnswers answers,
                Log& log)
      : resources_(resources),
        part_(part),
        document_(document),
        answers_(answers),
        log_(log) {}
  OriginalTdtDocument* document() override { return document_; }
  const simtower::OriginalPartTable& part() override { return part_; }
  const simtower::OriginalResources& resources() override {
    return resources_;
  }
  bool rename_tenant(std::int16_t floor, std::size_t tenant) override {
    log_.push_back("DS31a4 0");
    const bool renamed = fake_rename(*document_, floor, tenant, answers_, log_);
    log_.push_back("DS31a4 dialog");
    log_.push_back("TOPMOST");
    return renamed;
  }
  bool choose_movie(std::size_t record) override {
    return fake_movie(*document_, part_, record, answers_, log_);
  }
  bool person_information(std::size_t person) override {
    log_.push_back("person " + std::to_string(person));
    return answers_.person_changes;
  }
  void mark_document_dirty() override {
    log_.push_back("dirty");
    log_.push_back("title");
  }
  void set_cursor(std::uint16_t id) override {
    log_.push_back("SetCursor " + std::to_string(id));
  }

 private:
  const simtower::OriginalResources& resources_;
  const simtower::OriginalPartTable& part_;
  OriginalTdtDocument* document_;
  NestedAnswers answers_;
  Log& log_;
};

// One input of a run.
struct Step {
  Kind kind{Kind::command};
  std::uint16_t id{};
  std::uint16_t code{};
  int x{};
  int y{};
  // Command 13: the combo selection the user picked first (-2: keep).
  int pick{-2};
};

std::vector<Step> script_for(const simtower::OriginalFacilityInformation& info,
                             const simtower::OriginalDtmp& dtmp) {
  std::vector<Step> steps;
  // Moves over item 4, item 9 and nowhere.
  for (const std::size_t item : {4U, 9U}) {
    if (item <= dtmp.rectangles.size()) {
      const auto& rect = dtmp.rectangles[item - 1U];
      steps.push_back({Kind::mouse_move, 0, 0, (rect.left + rect.right) / 2,
                       (rect.top + rect.bottom) / 2});
    }
  }
  steps.push_back({Kind::mouse_move, 0, 0, 1, 1});
  // Presses on each portrait and on nothing.
  for (const auto& sprite : info.person_sprites) {
    steps.push_back({Kind::button_down, 0, 0, sprite.destination_x + 1,
                     sprite.destination_y + 1});
  }
  steps.push_back({Kind::button_down, 0, 0, 2, 2});
  // The combo: every rate, CB_ERR, the drop-down and close-up notices,
  // notification 0.
  for (const int pick : {3, 3, 0, -1, 2, 1}) {
    steps.push_back({Kind::command, 13, 1, 0, 0, pick});
  }
  steps.push_back({Kind::command, 13, 7});
  steps.push_back({Kind::command, 13, 8, 0, 0, 0});
  steps.push_back({Kind::command, 13, 0, 0, 0, 2});
  // Rename, unknown ids, Esc's IDCANCEL, then OK.
  steps.push_back({Kind::command, 7, 0});
  steps.push_back({Kind::command, 2, 0});
  steps.push_back({Kind::command, 99, 0});
  steps.push_back({Kind::key_up, 13});
  steps.push_back({Kind::command, 1, 0});
  return steps;
}

struct ParityCounts {
  int runs{};
  int changed{};
  int rent_changes{};
  int portraits{};
  std::set<int> groups{};
};

void run_parity(const simtower::OriginalResources& resources,
                const simtower::OriginalPartTable& part,
                const OriginalTdtDocument& base, std::int16_t floor,
                std::size_t tenant, const NestedAnswers& answers,
                bool with_document, bool synthetic_sprite,
                ParityCounts& counts) {
  auto legacy_document = std::make_unique<OriginalTdtDocument>(base);
  auto shared_document = std::make_unique<OriginalTdtDocument>(base);
  auto information = simtower::original_facility_information(
      resources, base, part, floor, tenant);
  assert(information.valid);
  if (synthetic_sprite) {
    // A portrait the staged tower has no person for yet: both sides hit it
    // once, then refresh drops it.
    simtower::OriginalFacilityPersonSprite sprite{};
    sprite.person_index = 5U;
    sprite.frame = 0;
    sprite.destination_x = 40;
    sprite.destination_y = 130;
    sprite.width = 16;
    information.person_sprites.push_back(sprite);
  }
  const auto dtmp = simtower::parse_original_dtmp(
      resources.find("DTMP", information.dialog_id));
  Log legacy_log;
  Log shared_log;
  Combo legacy_combo;
  Combo shared_combo;
  LegacyDialog legacy{resources,
                      part,
                      with_document ? legacy_document.get() : nullptr,
                      floor,
                      tenant,
                      information,
                      dtmp,
                      answers,
                      &legacy_combo,
                      &legacy_log};
  simtower::OriginalFacilityInformationController controller(
      floor, tenant, information, dtmp);
  RecordingOps ops(shared_combo, shared_log);
  RecordingHost host(resources, part,
                     with_document ? shared_document.get() : nullptr, answers,
                     shared_log);
  const bool legacy_focus = legacy.init();
  controller.initialize(ops);
  const bool shared_focus = controller.initial_focus(ops);
  assert(legacy_focus == shared_focus);
  assert(legacy_log == shared_log);
  std::optional<int> shared_ended{};
  for (const auto& step : script_for(information, dtmp)) {
    if (step.pick != -2) {
      legacy_combo.selection = step.pick;
      shared_combo.selection = step.pick;
    }
    OriginalDialogSignal signal{step.kind, step.id, step.code};
    signal.x = step.x;
    signal.y = step.y;
    // The old procedure returned TRUE for every WM_COMMAND, WM_LBUTTONDOWN
    // and WM_MOUSEMOVE, FALSE for anything else (the key_up step).
    bool legacy_handled = step.kind == Kind::button_down ||
                          step.kind == Kind::mouse_move;
    if (step.kind == Kind::command) {
      legacy_handled = legacy.command(step.id, step.code);
      assert(legacy_handled);
    } else if (step.kind == Kind::button_down) {
      const auto before = legacy_log.size();
      legacy.button_down(step.x, step.y);
      if (legacy_log.size() != before) ++counts.portraits;
    } else if (step.kind == Kind::mouse_move) {
      legacy.mouse_move(step.x, step.y);
    }
    const auto rate_before =
        with_document
            ? shared_document->floors[static_cast<std::size_t>(floor)]
                  .tenants[tenant]
                  .exact_bytes[16]
            : std::byte{0};
    bool handled = !legacy_handled;
    const auto outcome = controller.signal(signal, ops, host, &handled);
    assert(handled == legacy_handled);
    // The Win32 procedure's close tail (DS:31a4 cleared, EndDialog).
    if (outcome.result) shared_log.push_back("DS31a4 0");
    if (with_document &&
        shared_document->floors[static_cast<std::size_t>(floor)]
                .tenants[tenant]
                .exact_bytes[16] != rate_before) {
      ++counts.rent_changes;
    }
    assert(shared_log == legacy_log);
    shared_ended = outcome.result;
    assert(shared_ended == legacy.ended);
    assert(controller.changed() == legacy.changed);
    if (shared_ended) break;
  }
  assert(shared_ended == std::optional<int>(1));
  assert(simtower::serialize_original_tdt_game_save(*shared_document) ==
         simtower::serialize_original_tdt_game_save(*legacy_document));
  ++counts.runs;
  if (controller.changed()) ++counts.changed;
  counts.groups.insert(information.dialog_group);
}

void parity_tests(const simtower::OriginalResources& resources,
                  const simtower::OriginalPartTable& part,
                  const OriginalTdtDocument& document) {
  ParityCounts counts{};
  std::set<std::pair<int, int>> seen_types{};
  for (std::size_t floor = 0; floor < document.floors.size(); ++floor) {
    const auto& tenants = document.floors[floor].tenants;
    for (std::size_t tenant = 0; tenant < tenants.size(); ++tenant) {
      const auto information = simtower::original_facility_information(
          resources, document, part, static_cast<std::int16_t>(floor),
          tenant);
      if (!information.valid) continue;
      // render_original_facility_preview takes the backing counts' floor
      // and tenant from the information where the old inline Win32 code
      // used the runner's arguments: the same whenever it is valid.
      assert(information.floor == static_cast<std::int16_t>(floor) &&
             information.tenant_index == tenant);
      // Every type once (plus a second tenant of it), every answer mix.
      const int type = information.type;
      const auto same_type = [&](const auto& entry) {
        return entry.first == type;
      };
      const int seen = static_cast<int>(
          std::count_if(seen_types.begin(), seen_types.end(), same_type));
      if (seen >= 2) continue;
      seen_types.insert({type, static_cast<int>(floor * 100U + tenant)});
      for (const bool all : {false, true}) {
        const NestedAnswers answers{all, all, all};
        run_parity(resources, part, document, static_cast<std::int16_t>(floor),
                   tenant, answers, true, all, counts);
      }
      run_parity(resources, part, document, static_cast<std::int16_t>(floor),
                 tenant, {true, true, true}, false, true, counts);
    }
  }
  std::printf("facility parity: %d runs, %d changed, %d rent changes, %d "
              "portrait hits, %zu groups\n",
              counts.runs, counts.changed, counts.rent_changes,
              counts.portraits, counts.groups.size());
  assert(counts.runs > 0 && counts.rent_changes > 0 && counts.portraits > 0);
  // The staged "full" tower with its pending construction activated has
  // rent groups 0..5 and the Rename-only (9 medical, 11 party hall),
  // OK-only (7, 8) and New Movie (10) groups.
  for (const int group : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}) {
    if (!counts.groups.contains(group)) {
      std::printf("facility parity: group %d not staged\n", group);
    }
  }
  assert(counts.groups.contains(0) && counts.groups.contains(4) &&
         counts.groups.contains(5) && counts.groups.contains(8) &&
         counts.groups.contains(9) && counts.groups.contains(10) &&
         counts.groups.contains(11));
}

// Controller-only cases: the call order of each action, the Esc no-op, the
// group-10 New Movie button and the cursor zones.
void controller_tests(const simtower::OriginalResources& resources,
                      const simtower::OriginalPartTable& part,
                      const OriginalTdtDocument& base) {
  // An office: rent changes call CB_GETCURSEL, then refresh.
  for (std::size_t floor = 0; floor < base.floors.size(); ++floor) {
    const auto& tenants = base.floors[floor].tenants;
    for (std::size_t tenant = 0; tenant < tenants.size(); ++tenant) {
      const auto information = simtower::original_facility_information(
          resources, base, part, static_cast<std::int16_t>(floor), tenant);
      if (!information.valid || information.dialog_group != 0U) continue;
      auto document = std::make_unique<OriginalTdtDocument>(base);
      Log log;
      Combo combo;
      RecordingOps ops(combo, log);
      RecordingHost host(resources, part, document.get(), {}, log);
      simtower::OriginalFacilityInformationController controller(
          static_cast<std::int16_t>(floor), tenant, information,
          simtower::parse_original_dtmp(resources.find("DTMP", 748)));
      controller.initialize(ops);
      assert(log.size() == 6U && log[0].rfind("CB_ADDSTRING 13 ", 0) == 0 &&
             log[4] == "CB_SETCURSEL 13 " +
                           std::to_string(information.selected_rent_rate) &&
             log[5] == "EnableWindow 13 1");
      log.clear();
      const int target = information.selected_rent_rate == 3U ? 0 : 3;
      combo.selection = target;
      assert(!controller.signal({Kind::command, 13, 1}, ops, host).result);
      assert((log == Log{"CB_GETCURSEL 13", "dirty", "title",
                         "CB_SETCURSEL 13 " + std::to_string(target),
                         "EnableWindow 13 1", "InvalidateRect all",
                         "UpdateWindow"}));
      assert(std::to_integer<int>(
                 document->floors[floor].tenants[tenant].exact_bytes[16]) ==
             target);
      assert(controller.changed());
      log.clear();
      // The same pick again changes nothing.
      assert(!controller.signal({Kind::command, 13, 1}, ops, host).result);
      assert((log == Log{"CB_GETCURSEL 13"}));
      log.clear();
      // Esc (IDCANCEL) does nothing; Enter / OK closes with 1.
      assert(!controller.signal({Kind::command, 2, 0}, ops, host).result);
      assert(log.empty());
      assert(controller.signal({Kind::command, 1, 0}, ops, host).result ==
             std::optional<int>(1));
      assert(log.empty());
      // The portrait cursor zone: item 4 only for group 0.
      const auto& dtmp = controller.dtmp();
      const auto& item4 = dtmp.rectangles[3];
      const auto& item9 = dtmp.rectangles[8];
      OriginalDialogSignal move{Kind::mouse_move};
      move.x = item4.left;
      move.y = item4.top;
      (void)controller.signal(move, ops, host);
      move.x = item9.left;
      move.y = item9.top;
      (void)controller.signal(move, ops, host);
      move.x = item4.right;
      move.y = item4.top;
      (void)controller.signal(move, ops, host);
      assert((log == Log{"SetCursor 1003", "SetCursor 0", "SetCursor 0"}));
      std::printf("facility controller: office floor %zu tenant %zu\n",
                  floor, tenant);
      return;
    }
  }
  assert(false && "no office staged");
}

// The four Facility Information behaviours matched to the original on
// 2026-10-25 (see original_facility_information_controller.hpp).
void original_behaviour_tests(const simtower::OriginalResources& resources,
                              const simtower::OriginalPartTable& part,
                              const OriginalTdtDocument& base) {
  struct Found {
    std::int16_t floor{-1};
    std::size_t tenant{};
  };
  const auto find_type = [&](int type) {
    for (std::size_t floor = 0; floor < base.floors.size(); ++floor) {
      const auto& tenants = base.floors[floor].tenants;
      for (std::size_t tenant = 0; tenant < tenants.size(); ++tenant) {
        if (std::to_integer<std::int8_t>(tenants[tenant].exact_bytes[4]) ==
            type) {
          return Found{static_cast<std::int16_t>(floor), tenant};
        }
      }
    }
    return Found{};
  };

  // 1. Condo (group 4): 1100:096f-09a5 disables combo 13 for a sold condo
  // (byte 5 < 24) only; 1100:0bae-0c2b sets the price in both states.
  const auto condo = find_type(9);
  assert(condo.floor >= 0);
  for (const int status : {0x10, 0x17, 0x18, 0x20}) {
    auto document = std::make_unique<OriginalTdtDocument>(base);
    auto& tenant = document->floors[static_cast<std::size_t>(condo.floor)]
                       .tenants[condo.tenant];
    tenant.exact_bytes[5] = static_cast<std::byte>(status);
    tenant.status = static_cast<std::uint8_t>(status);
    const auto information = simtower::original_facility_information(
        resources, *document, part, condo.floor, condo.tenant);
    assert(information.dialog_group == 4U &&
           information.rent_control_visible);
    const bool sold = status < 24;
    assert(information.occupancy_text == (sold ? "Occupied" : "For Sale"));
    assert(information.rent_control_enabled == !sold);
    Log log;
    Combo combo;
    RecordingOps ops(combo, log);
    simtower::OriginalFacilityInformationController controller(
        condo.floor, condo.tenant, information,
        simtower::parse_original_dtmp(resources.find("DTMP", 752)));
    controller.initialize(ops);
    assert(log.back() == (sold ? "EnableWindow 13 0" : "EnableWindow 13 1"));
    const auto rate = information.selected_rent_rate == 3U ? 0U : 3U;
    assert(simtower::set_original_facility_rent_rate(
        *document, part, condo.floor, condo.tenant,
        static_cast<std::uint8_t>(rate)));
    assert(tenant.exact_bytes[16] == static_cast<std::byte>(rate));
  }

  // 3. Yesterday's Profit: group 12 (types 6 and 12) through 1100:2715;
  // a shop (group 5) has none.
  const auto suffix = simtower::original_strl_entry(
      resources.find("STRL", 713), 7U);
  for (const int type : {6, 12}) {
    const auto found = find_type(type);
    assert(found.floor >= 0);
    auto document = std::make_unique<OriginalTdtDocument>(base);
    const auto& tenant = document->floors[static_cast<std::size_t>(
        found.floor)].tenants[found.tenant];
    const auto linked = std::to_integer<std::size_t>(tenant.exact_bytes[6]) |
                        (std::to_integer<std::size_t>(tenant.exact_bytes[7])
                         << 8U);
    assert(linked < document->retail.size());
    for (const int profit : {7, -3}) {
      document->retail[linked].exact_bytes[10] =
          static_cast<std::byte>(profit);
      const auto information = simtower::original_facility_information(
          resources, *document, part, found.floor, found.tenant);
      assert(information.dialog_id == 760U);
      assert(information.yesterday_profit_text ==
             std::to_string(profit) + suffix);
    }
  }
  const auto shop = find_type(10);
  assert(shop.floor >= 0);
  const auto shop_information = simtower::original_facility_information(
      resources, base, part, shop.floor, shop.tenant);
  assert(shop_information.dialog_id == 753U &&
         shop_information.yesterday_profit_text.empty());

  // 4. The shop picture: activation (1228:02c2 -> 0354 -> 11a8:07d3) gives
  // every shop its own Retail record, closed (byte 2 = 0xff), so 1100:465a
  // crops the shop itself. Before, every shop kept word +0xc = 0 and showed
  // record 0's Fast Food (the Soba picture).
  std::set<std::size_t> shop_records{};
  std::size_t shops = 0U;
  for (std::size_t floor = 0; floor < base.floors.size(); ++floor) {
    const auto& tenants = base.floors[floor].tenants;
    for (std::size_t tenant = 0; tenant < tenants.size(); ++tenant) {
      const auto& record = tenants[tenant];
      if (std::to_integer<std::int8_t>(record.exact_bytes[4]) != 10) {
        continue;
      }
      const auto information = simtower::original_facility_information(
          resources, base, part, static_cast<std::int16_t>(floor), tenant);
      const auto linked = information.linked_record_index;
      assert(linked < base.retail.size());
      const auto& retail = base.retail[linked].exact_bytes;
      assert(std::to_integer<std::size_t>(retail[0]) == floor);
      assert(base.floors[floor].tenant_index[std::to_integer<std::size_t>(
                 retail[1])] == tenant);
      assert(information.preview.view_x == record.left * 8 &&
             information.preview.width == 12 * 8);
      shop_records.insert(linked);
      ++shops;
    }
  }
  assert(shops > 1U && shop_records.size() == shops);
  std::printf("facility original behaviour: %zu shops, own records\n",
              shops);
  std::printf("facility original behaviour: condo floor %d tenant %zu\n",
              condo.floor, condo.tenant);
}

// The portable painter on the canvas for every staged dialog: the chrome
// fills the client, the preview is stretched into item 2, the name is drawn
// in item 3.
void painter_tests(const simtower::OriginalResources& resources,
                   const simtower::OriginalPartTable& part,
                   const OriginalTdtDocument& document) {
  auto& font = simtower::OriginalCanvasFont::bundled();
  std::set<std::uint16_t> painted{};
  for (std::size_t floor = 0; floor < document.floors.size(); ++floor) {
    const auto& tenants = document.floors[floor].tenants;
    for (std::size_t tenant = 0; tenant < tenants.size(); ++tenant) {
      const auto information = simtower::original_facility_information(
          resources, document, part, static_cast<std::int16_t>(floor),
          tenant);
      if (!information.valid || painted.contains(information.dialog_id)) {
        continue;
      }
      painted.insert(information.dialog_id);
      const auto dtmp = simtower::parse_original_dtmp(
          resources.find("DTMP", information.dialog_id));
      const auto preview = simtower::render_original_facility_preview(
          resources, document, information, 640, 480, nullptr);
      assert(information.preview.valid() ==
             (preview.width > 0 && preview.height > 0));
      simtower::OriginalWorldRaster raster{};
      raster.width = dtmp.width_or_header;
      raster.height = dtmp.height_or_header;
      raster.pixels.assign(
          static_cast<std::size_t>(raster.width) * raster.height,
          0x123456U);
      simtower::OriginalCanvasDrawContext context(raster, font);
      simtower::paint_original_facility_information(
          context, resources, dtmp, information, preview,
          {0, 0, raster.width, raster.height},
          [](std::uint16_t id) { return id == 1U; });
      // Inside the frame's bevels nothing is left unpainted.
      for (int y = 6; y < raster.height - 6; ++y) {
        for (int x = 6; x < raster.width - 6; ++x) {
          assert(raster.at(x, y) != 0x123456U);
        }
      }
      // Black text inside item 3 (the name).
      const auto& name = dtmp.rectangles[2];
      int black = 0;
      for (int y = name.top; y < name.bottom; ++y) {
        for (int x = name.left; x < name.right; ++x) {
          if (raster.at(x, y) == 0U) ++black;
        }
      }
      assert(information.display_name.empty() || black > 0);
      // The preview: not one flat colour inside item 2.
      if (information.preview.valid()) {
        const auto& item2 = dtmp.rectangles[1];
        std::set<std::uint32_t> colours{};
        for (int y = item2.top + 2; y < item2.bottom - 2; ++y) {
          for (int x = item2.left + 2; x < item2.right - 2; ++x) {
            colours.insert(raster.at(x, y));
          }
        }
        assert(colours.size() > 2U);
      }
    }
  }
  std::printf("facility painter: %zu dialogs painted\n", painted.size());
  assert(painted.size() >= 8U);
}

// The canvas stretch: nearest-neighbour COLORONCOLOR sampling.
void stretch_tests() {
  auto& font = simtower::OriginalCanvasFont::bundled();
  simtower::OriginalWorldRaster raster{};
  raster.width = 8;
  raster.height = 4;
  raster.pixels.assign(32U, 0U);
  simtower::OriginalCanvasDrawContext context(raster, font);
  const std::vector<std::uint32_t> source{1U, 2U, 3U, 4U};  // 2 x 2
  context.stretch_pixels(source, 2, 2, {0, 0, 2, 2}, {0, 0, 4, 4});
  assert(raster.at(0, 0) == 1U && raster.at(1, 0) == 1U &&
         raster.at(2, 0) == 2U && raster.at(3, 3) == 4U &&
         raster.at(0, 2) == 3U && raster.at(4, 0) == 0U);
  // Shrinking and a source offset.
  context.stretch_pixels(source, 2, 2, {1, 0, 2, 2}, {6, 0, 7, 1});
  assert(raster.at(6, 0) == 2U);
}

}  // namespace

int main(int argc, char** argv) {
  stretch_tests();
  if (argc < 2) return 77;
  const auto pack = read_file(argv[1]);
  if (!pack) {
    std::printf("skipped: no resource pack at %s\n", argv[1]);
    return 77;
  }
  const simtower::OriginalResources resources{
      std::span<const std::byte>(*pack)};
  const auto startup = simtower::load_original_startup_data(resources);
  auto document =
      std::make_unique<OriginalTdtDocument>(simtower::make_original_new_tdt());
  (void)simtower::build_scenario("full", *document, startup.part,
                                 startup.construction_costs);
  // The basement facilities (medical, party hall, recycling, ...) are
  // still pending (negative type bytes) right after staging; 11f0:0016's
  // flush activates them, as the simulation does by about pass 300.
  auto active = std::make_unique<OriginalTdtDocument>(*document);
  simtower::activate_all_original_pending_facilities_for_schedule(*active);
  parity_tests(resources, startup.part, *active);
  original_behaviour_tests(resources, startup.part, *active);
  controller_tests(resources, startup.part, *document);
  painter_tests(resources, startup.part, *document);
  std::printf("facility information controller tests passed\n");
  return 0;
}
