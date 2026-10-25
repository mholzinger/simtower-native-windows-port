// Checks the shared world input module (original_world_input.hpp) against
// direct constructor calls. Both hosts (native_main.cpp and the SDL session)
// run this same module, so a placement made through the pointer entry points
// must leave exactly the tower bytes the portable constructors plus the
// host's common success tail (1148:0163 rating callback, then the literal
// 0ed8 route rebuild) leave, and the hook trace must follow the original
// status/sound/dialog/repaint order.
//
// A recording host stands in for the window system: it logs every hook with
// its arguments, keeps a plain scroll position, and answers dialogs with
// fixed values. No SDL and no Win32, so this runs natively and under Wine.
//
// Usage: simtower_world_input_tests <original_resources.pack>

#include "test_scenario.hpp"

#include "original_construction.hpp"
#include "original_resources.hpp"
#include "original_simulation.hpp"
#include "original_startup_data.hpp"
#include "original_tdt.hpp"
#include "original_ui.hpp"
#include "original_world.hpp"
#include "original_world_input.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace simtower;

constexpr int kClientWidth = 640;
constexpr int kClientHeight = 400;

std::vector<std::byte> read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  assert(stream);
  const std::vector<char> chars((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(chars.size());
  for (std::size_t index = 0; index < chars.size(); ++index) {
    bytes[index] =
        static_cast<std::byte>(static_cast<unsigned char>(chars[index]));
  }
  return bytes;
}

struct Tables {
  OriginalResources resources;
  OriginalPartTable part;
  OriginalYenTable costs;
  OriginalYenTable rent;
  std::array<OriginalCommandRatingState, 6> ratings;
};

// Logs every hook as one token. Scroll positions are clamped like
// SetScrollInfo (the SDL viewport uses the same helper). It is its own
// OriginalHostServices, as a frontend's idle-pass host would be.
class RecordingHost final : public OriginalWorldInputHost,
                            public OriginalHostServices {
 public:
  std::vector<std::string> trace;
  OriginalWorldPoint view{};
  int alert_answer{1};
  int event_dialog_answer{1};
  bool primary_down{true};
  std::optional<std::uint16_t> selector_row{};  // scripted selector choice
  bool main_available{true};  // main_client() fails when false

  void log(std::string token) { trace.push_back(std::move(token)); }
  void clear() { trace.clear(); }

  OriginalHostServices& services() override { return *this; }
  std::uint32_t now_ms() override { return 1000U; }
  bool beep_only() override { return false; }
  bool elevator_isolation_active() override { return false; }
  bool modal_input_locked() override { return false; }
  bool find_exit_latch_set() override { return false; }
  void reset_find_marker() override { log("find_reset"); }
  bool primary_button_down() override { return primary_down; }

  void set_capture(OriginalInputSurface surface) override {
    log("capture " + std::to_string(static_cast<int>(surface)));
  }
  void release_capture() override { log("release"); }
  void set_cursor(std::uint16_t id) override {
    log("cursor " + std::to_string(id));
  }

  OriginalWorldPoint main_view() override { return view; }
  std::optional<OriginalMapRect> main_client() override {
    if (!main_available) return std::nullopt;
    return OriginalMapRect{0, 0, kClientWidth, kClientHeight};
  }
  void set_main_view(int x, int y) override {
    clamp_to(x, y);
    log("view " + std::to_string(view.x) + " " + std::to_string(view.y));
  }
  void set_main_scroll_position(int x, int y) override {
    clamp_to(x, y);
    log("scroll " + std::to_string(view.x) + " " + std::to_string(view.y));
  }

  void mark_document_dirty() override { log("dirty"); }
  void invalidate_main() override { log("main"); }
  void mark_main_backing_dirty() override { log("backing"); }
  void invalidate_map() override { log("map"); }
  void present_info_status() override { log("info"); }
  void invalidate_command() override { log("command"); }
  void present_command_now() override { log("command_now"); }
  void resize_command(int height) override {
    log("resize " + std::to_string(height));
  }
  void request_main_pass(OriginalMainSurfacePass pass, bool sync) override {
    log("pass " + std::to_string(static_cast<int>(pass)) +
        (sync ? " sync" : ""));
  }
  void begin_map_focus() override { log("map_dc"); }
  void end_map_focus() override { log("map_dc_release"); }
  void xor_map_focus(const OriginalMapRect& focus) override {
    log("focus " + std::to_string(focus.left) + " " +
        std::to_string(focus.top));
  }
  void hide_palette(OriginalAuxiliaryWindow kind) override {
    log("hide " + std::to_string(static_cast<int>(kind)));
  }

  void construction_status(std::uint16_t code) override {
    log("status " + std::to_string(code));
  }
  void notification_status(std::uint16_t code) override {
    log("notify " + std::to_string(code));
  }
  void command_status(std::uint16_t code) override {
    log("command_status " + std::to_string(code));
  }
  void play_sound(std::int32_t id, std::uint16_t repeat,
                  std::uint16_t priority) override {
    log("sound " + std::to_string(id) + " " + std::to_string(repeat) + " " +
        std::to_string(priority));
  }
  void play_reserved_if_idle(std::int32_t id, std::uint16_t,
                             std::uint32_t) override {
    log("reserved " + std::to_string(id));
  }
  void play_event_sounds(
      std::span<const OriginalEventSoundRequest> requests) override {
    for (const auto& request : requests) {
      log("event_sound " + std::to_string(request.resource));
    }
  }
  void beep() override { log("beep"); }
  void audio_pump() override { log("pump"); }
  void audio_transition(OriginalIdleAudioTransition transition) override {
    if (transition != OriginalIdleAudioTransition::none) {
      log("audio " + std::to_string(static_cast<int>(transition)));
    }
  }
  void sample_auxiliary_pointer_tick() override { log("tick"); }

  int alert(int resource_id, std::string_view) override {
    log("alert " + std::to_string(resource_id));
    return alert_answer;
  }
  int event_dialog(const OriginalEventDialogRequest& request) override {
    log("dialog " + std::to_string(request.dialog_id));
    return event_dialog_answer;
  }
  void open_elevator_control(std::size_t elevator, std::int16_t,
                             std::int16_t) override {
    log("elevator_control " + std::to_string(elevator));
  }
  void transport_information(const OriginalMagnifierTarget& target) override {
    log("transport_info " + std::to_string(static_cast<int>(target.kind)));
  }
  void facility_information(std::int16_t floor, std::size_t tenant) override {
    log("facility_info " + std::to_string(floor) + " " +
        std::to_string(tenant));
  }
  void person_information(std::size_t person) override {
    log("person_info " + std::to_string(person));
  }
  OriginalWorldPoint command_outer_origin() override { return {134, 178}; }
  bool run_command_selector(OriginalCommandSelectorSession& session) override {
    log("selector " + std::to_string(session.group().catalog_icons.size()));
    if (!selector_row) return false;
    (void)session.on_move(*selector_row);
    return session.on_lbutton_up(*selector_row).accepted;
  }
  void consume_person_family_dispatches(
      std::span<const OriginalPersonFamilyDispatchResult> dispatches)
      override {
    log("dispatches " + std::to_string(dispatches.size()));
  }

 private:
  void clamp_to(int x, int y) {
    view.x = original_main_scrollbar_resize_state(x, kClientWidth,
                                                  kOriginalWorldWidth)
                 .position;
    view.y = original_main_scrollbar_resize_state(y, kClientHeight,
                                                  kOriginalWorldHeight)
                 .position;
  }
};

OriginalLobbyPlacement placement_for(std::uint16_t type, int x, int y,
                                     OriginalWorldPoint view) {
  switch (type) {
    case 0: return original_floor_placement_from_client(x, y, view.x, view.y);
    case 7: return original_office_placement_from_client(x, y, view.x, view.y);
    case 24: return original_lobby_placement_from_client(x, y, view.x, view.y);
    default:
      return original_facility_placement_from_client(type, x, y, view.x,
                                                     view.y);
  }
}

// The constructor 11f8:07d8 reaches for a single placement, written
// independently of the module from the constructor table.
OriginalConstructionResult direct_build(OriginalTdtDocument& document,
                                        const Tables& tables,
                                        std::uint16_t type,
                                        const OriginalLobbyPlacement& p,
                                        std::uint8_t variant,
                                        std::uint16_t keys = 0U) {
  const auto left = static_cast<std::uint16_t>(p.left);
  const auto right = static_cast<std::uint16_t>(p.right);
  const auto& costs = tables.costs;
  switch (type) {
    case 0: return build_original_floor(document, p.floor, left, right, costs);
    case 24:
      if (p.floor == 10 && document.header.lobby_height == 0U) {
        std::uint16_t height = 1U;
        if ((keys & kOriginalKeyControl) != 0U) {
          height = (keys & kOriginalKeyShift) != 0U ? 3U : 2U;
        }
        return build_original_initial_lobby(document, left, right, height,
                                            costs);
      }
      if (p.floor == 10) {
        return extend_original_lobby(document, left, right, costs);
      }
      return build_original_sky_lobby(document, p.floor, left, right, costs);
    case 1: case 42: case 43:
      return build_original_elevator(document, type, p.floor, left, costs,
                                     tables.part);
    case 22: case 27:
      return build_original_vertical_transport(
          document, static_cast<std::uint8_t>(type), p.floor, left, costs);
    case 3: case 4: case 5:
      return build_original_hotel_room(
          document, static_cast<std::uint8_t>(type), p.floor, left, variant,
          costs);
    case 6: return build_original_restaurant(document, p.floor, left, costs);
    case 7:
      return build_original_office(document, p.floor, left, variant, costs);
    case 9:
      return build_original_condo(document, p.floor, left, variant, costs);
    case 10: return build_original_retail_shop(document, p.floor, left, costs);
    case 12: return build_original_fast_food(document, p.floor, left, costs);
    case 13:
      return build_original_medical_center(document, p.floor, left, variant,
                                           costs);
    case 14: return build_original_security(document, p.floor, left, costs);
    case 15: return build_original_housekeeping(document, p.floor, left, costs);
    case 17: return build_original_secom_center(document, p.floor, left, costs);
    case 18:
      return build_original_movie_theater(document, p.floor, left, costs);
    case 20:
      return build_original_recycling_center(document, p.floor, left, costs);
    case 29: return build_original_party_hall(document, p.floor, left, costs);
    case 31:
      return build_original_metro_station(document, p.floor, left, costs);
    case 36: return build_original_cathedral(document, p.floor, left, costs);
    default: break;
  }
  assert(false && "no direct constructor");
  return {};
}

// The host's common success tail after a click (11f8:0e21 -> 1148:0163 ->
// 0ed8), as test_scenario.cpp runs it.
void direct_tail(OriginalTdtDocument& document, const Tables& tables,
                 std::uint16_t type) {
  (void)complete_original_rating_construction(document, tables.part, type);
  if (original_construction_rebuilds_routes(type)) {
    rebuild_original_transport_route_graphs(document);
  }
}

struct Target {
  OriginalWorldPoint view{};
  int x{};
  int y{};
  OriginalLobbyPlacement placement{};
};

// Finds a client point whose placement the direct constructor accepts (and
// pays for) on a copy, by brute force over a few views and the client.
std::optional<Target> find_target(const OriginalTdtDocument& base,
                                  const Tables& tables,
                                  std::uint16_t type,
                                  std::uint8_t variant,
                                  std::span<const int> floors) {
  std::set<std::pair<int, int>> tried;
  for (const int floor : floors) {
    for (const int cell : {240, 140, 340}) {
      const auto view = original_facility_focus_view(cell, floor, kClientWidth,
                                                     kClientHeight);
      for (int y = 2; y < kClientHeight; y += 6) {
        for (int x = 4; x < kClientWidth; x += 8) {
          const auto p = placement_for(type, x, y, view);
          if (p.floor != floor) continue;
          if (!tried.insert({p.floor, p.left}).second) continue;
          auto copy = base;
          const auto result = direct_build(copy, tables, type, p, variant);
          if (result.succeeded() &&
              copy.header.balance < base.header.balance) {
            return Target{view, x, y, p};
          }
        }
      }
    }
  }
  return std::nullopt;
}

OriginalWorldInputContext context(OriginalTdtDocument& document,
                                  const Tables& tables,
                                  OriginalWorldInputState& state) {
  return {&document, tables.resources, tables.part, tables.costs, tables.rent,
          state};
}

OriginalWorldInputState construction_state(const Tables& tables,
                                           std::uint16_t type) {
  OriginalWorldInputState state{};
  state.command_ratings = tables.ratings;
  state.command_mode = 3U;
  state.selected_build_type = type;
  return state;
}

void click(const OriginalWorldInputContext& ctx, RecordingHost& host, int x,
           int y, std::uint16_t keys = 0U) {
  original_main_pointer_down(ctx, host, {x, y, static_cast<std::uint16_t>(
                                                   keys | kOriginalKeyLButton)},
                             false);
  original_main_pointer_up(ctx, host, {x, y, keys});
}

bool same_tower(const OriginalTdtDocument& a, const OriginalTdtDocument& b) {
  return serialize_original_tdt(a) == serialize_original_tdt(b);
}

// Hook tokens that carry the construction result, in order.
std::vector<std::string> result_tokens(const std::vector<std::string>& trace) {
  std::vector<std::string> tokens;
  for (const auto& token : trace) {
    if (token.rfind("status", 0) == 0 || token.rfind("sound", 0) == 0 ||
        token.rfind("dialog", 0) == 0 || token == "main" || token == "map" ||
        token == "info" || token == "dirty" || token == "command_now") {
      tokens.push_back(token);
    }
  }
  return tokens;
}

void print_trace(const char* label, const std::vector<std::string>& trace) {
  std::fprintf(stderr, "%s:", label);
  for (const auto& token : trace) std::fprintf(stderr, " [%s]", token.c_str());
  std::fprintf(stderr, "\n");
}

int g_failures = 0;
void expect(bool ok, const std::string& what) {
  if (!ok) {
    ++g_failures;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
  }
}

OriginalTdtDocument staged_tower(const Tables& tables, const char* scenario) {
  auto document = make_original_new_tdt();
  (void)build_scenario(scenario, document, tables.part, tables.costs);
  // Empty floors above the busy tower (25..34) and below it (9..0) for
  // facility, Metro and Parking placements.
  const auto add_floor = [&](std::int16_t floor) {
    const auto result =
        build_original_floor(document, floor, 100U, 380U, tables.costs);
    assert(result.succeeded());
    direct_tail(document, tables, 0U);
  };
  for (std::int16_t floor = 25; floor <= 34; ++floor) add_floor(floor);
  if (std::string_view(scenario) == "busy") {
    for (std::int16_t floor = 9; floor >= 0; --floor) add_floor(floor);
  }
  return document;
}

// One click per facility type through the module versus the direct path.
void test_single_placements(const Tables& tables) {
  const auto staged = staged_tower(tables, "busy");
  // The Cathedral (36) is left out: 11f8:3204 accepts it only on runtime
  // floor 113, which the staged tower does not reach.
  const std::vector<std::uint16_t> types{0,  1,  3,  4,  5,  6,  7,  9,
                                         10, 12, 13, 14, 15, 17, 18, 20,
                                         22, 24, 27, 29, 31, 42, 43};
  const std::vector<int> floors{35, 26, 27, 28, 30, 2, 9, 11, 24};
  std::size_t checked = 0U;
  for (const auto type : types) {
    const auto& base = staged;
    const auto target = find_target(base, tables, type, 0U, floors);
    if (!target) {
      std::fprintf(stderr, "world_input: type %u has no accepted target\n",
                   type);
      expect(false, "target for type " + std::to_string(type));
      continue;
    }
    auto a = base;
    auto state = construction_state(tables, type);
    RecordingHost host;
    host.view = target->view;
    click(context(a, tables, state), host, target->x, target->y);

    auto b = base;
    const auto result = direct_build(b, tables, type, target->placement, 0U);
    assert(result.succeeded());
    const auto balance_before_tail = b.header.balance;
    direct_tail(b, tables, type);
    expect(same_tower(a, b), "tower bytes for type " + std::to_string(type));

    // Single clicks: 11f8:0e21 status clear, 0e58's type-filtered
    // WAVE/7000, the 1148:0163 dialog (if any), then the mutation
    // presentation, whose Info repaint follows the construction debit.
    // Captured Floor/Lobby: the same tail runs once on button-up, after
    // 0839's drag WAVE/7000 and RELEASECAPTURE; its Info repaint follows
    // only a balance change inside the tail.
    const bool continuous = type == 0U || type == 24U;
    std::vector<std::string> trace = host.trace;
    if (continuous) {
      const auto release =
          std::find(trace.begin(), trace.end(), "release");
      expect(release != trace.end(), "captured click releases");
      expect(release != trace.begin() && *(release - 1) == "sound 7000 0 4",
             "drag WAVE/7000 precedes RELEASECAPTURE");
      trace.erase(trace.begin(), release + 1);
    }
    std::vector<std::string> core;
    for (const auto& token : result_tokens(trace)) {
      if (token.rfind("dialog", 0) == 0 || token == "sound 7001 0 5") continue;
      core.push_back(token);
    }
    std::vector<std::string> expected{"status 0"};
    if (type != 0U && type != 2U && type != 11U && type != 24U &&
        type != 44U) {
      expected.push_back("sound 7000 0 4");
    }
    const auto presentation = original_world_mutation_presentation_plan(
        true, !continuous || b.header.balance != balance_before_tail);
    if (presentation.mark_document_dirty) expected.push_back("dirty");
    if (presentation.invalidate_main_surface) expected.push_back("main");
    if (presentation.repaint_info_balance_synchronously) {
      expected.push_back("info");
    }
    if (presentation.invalidate_map_surface) expected.push_back("map");
    const bool new_shaft =
        (type == 1U || type == 42U || type == 43U) &&
        std::find(core.begin(), core.end(), "command_now") != core.end();
    if (new_shaft) {
      expected.push_back("command_now");
      expect(state.command_mode == 1U,
             "new shaft selects the Finger for type " + std::to_string(type));
    }
    if (core != expected) {
      print_trace(("trace type " + std::to_string(type)).c_str(), host.trace);
      expect(false, "hook order for type " + std::to_string(type));
    }
    expect(!state.pointer_armed, "up clears DS:0242");
    ++checked;
  }
  std::fprintf(stderr, "world_input: %zu single placements match\n", checked);
}

// Initial Lobby on a fresh tower: Control selects two stories,
// Control+Shift three, Shift alone one.
void test_initial_lobby(const Tables& tables) {
  const std::uint16_t key_sets[] = {0U, kOriginalKeyShift, kOriginalKeyControl,
                                    kOriginalKeyControl | kOriginalKeyShift};
  const std::uint16_t heights[] = {1U, 1U, 2U, 3U};
  for (std::size_t index = 0; index < 4U; ++index) {
    auto base = make_original_new_tdt();
    const auto view = original_initial_view(kClientWidth, kClientHeight);
    std::optional<Target> target;
    for (int y = 2; y < kClientHeight && !target; y += 4) {
      const auto p = original_lobby_placement_from_client(320, y, view.x,
                                                          view.y);
      if (p.floor == 10) target = Target{view, 320, y, p};
    }
    assert(target);
    auto a = base;
    auto state = construction_state(tables, 24U);
    RecordingHost host;
    host.view = view;
    click(context(a, tables, state), host, target->x, target->y,
          key_sets[index]);
    auto b = base;
    const auto result =
        direct_build(b, tables, 24U, target->placement, 0U, key_sets[index]);
    expect(result.succeeded(), "initial lobby builds");
    direct_tail(b, tables, 24U);
    expect(same_tower(a, b), "initial lobby bytes " + std::to_string(index));
    expect(a.header.lobby_height == heights[index],
           "lobby height " + std::to_string(index));
  }
}

// Two placements per variant type: the module advances DS:795c/7954.
void test_variant_cycling(const Tables& tables) {
  const auto base = staged_tower(tables, "busy");
  const std::vector<int> floors{27, 28, 29, 30};
  for (const std::uint16_t type : {3U, 4U, 5U, 7U, 9U, 13U}) {
    auto a = base;
    auto b = base;
    auto state = construction_state(tables, type);
    RecordingHost host;
    for (std::uint8_t round = 0; round < 2U; ++round) {
      const auto target = find_target(b, tables, type, round, floors);
      assert(target);
      host.view = target->view;
      click(context(a, tables, state), host, target->x, target->y);
      const auto result =
          direct_build(b, tables, type, target->placement, round);
      assert(result.succeeded());
      direct_tail(b, tables, type);
    }
    expect(same_tower(a, b), "variant cycling for type " +
                                 std::to_string(type));
    if (type == 7U) {
      expect(state.office_variant == 2U, "office variant advanced twice");
    } else {
      expect(state.facility_variants[type] != 0U || type == 5U ||
                 type == 3U,
             "variant advanced for type " + std::to_string(type));
    }
  }
}

// A rejected click: one status code and one WAVE/7002, no mutation.
void test_reject(const Tables& tables) {
  const auto base = staged_tower(tables, "busy");
  auto a = base;
  auto state = construction_state(tables, 10U);  // retail
  RecordingHost host;
  // Floor 13 is fully occupied by offices in the busy tower.
  host.view = original_facility_focus_view(240, 13, kClientWidth,
                                           kClientHeight);
  std::optional<Target> target;
  for (int y = 2; y < kClientHeight && !target; y += 4) {
    const auto p = placement_for(10U, 320, y, host.view);
    if (p.floor == 13) target = Target{host.view, 320, y, p};
  }
  assert(target);
  auto probe = base;
  const auto rejected = direct_build(probe, tables, 10U, target->placement, 0U);
  assert(!rejected.succeeded());
  click(context(a, tables, state), host, target->x, target->y);
  expect(same_tower(a, base), "reject leaves the tower unchanged");
  std::size_t failure_waves = 0U;
  std::size_t statuses = 0U;
  for (const auto& token : host.trace) {
    if (token == "sound 7002 0 4") ++failure_waves;
    if (token.rfind("status ", 0) == 0) ++statuses;
  }
  expect(failure_waves == 1U, "single WAVE/7002 on reject");
  expect(statuses == (rejected.construction_status_code != 0U ? 1U : 0U),
         "status on reject");
}

// Shift replacement: 11f8:09c1 demolishes under the new footprint first.
void test_shift_replacement(const Tables& tables) {
  const auto base = staged_tower(tables, "busy");
  // Replace an office on floor 14 with a condo... use retail on 11 instead:
  // floor 11 holds retail and food, so a retail placement overlaps them.
  const auto view =
      original_facility_focus_view(240, 11, kClientWidth, kClientHeight);
  for (int y = 2; y < kClientHeight; y += 4) {
    const auto p = placement_for(10U, 320, y, view);
    if (p.floor != 11) continue;
    auto a = base;
    auto state = construction_state(tables, 10U);
    RecordingHost host;
    host.view = view;
    click(context(a, tables, state), host, 320, y, kOriginalKeyShift);
    auto b = base;
    const auto demolition = apply_original_replacement_demolition(
        b, tables.rent, 10U, p.floor, static_cast<std::int16_t>(p.left),
        static_cast<std::int16_t>(p.right));
    if (demolition.completed) {
      const auto result = direct_build(b, tables, 10U, p, 0U);
      if (result.succeeded()) direct_tail(b, tables, 10U);
    }
    expect(same_tower(a, b), "shift replacement bytes");
    expect(serialize_original_tdt(a) != serialize_original_tdt(base),
           "shift replacement changed the tower");
    return;
  }
  assert(false);
}

// Floor and Lobby drags: down, k moves, up. The direct path rebuilds the
// span between the press anchor and each move, as 11f8:240d/0e09 do.
void test_span_drag(const Tables& tables, std::uint16_t type, int floor,
                    int center_cell) {
  const auto base = staged_tower(tables, "busy");
  const auto view = original_facility_focus_view(center_cell, floor,
                                                 kClientWidth, kClientHeight);
  int y = -1;
  for (int probe = 2; probe < kClientHeight; probe += 2) {
    if (placement_for(type, 320, probe, view).floor == floor) {
      y = probe;
      break;
    }
  }
  assert(y >= 0);
  auto a = base;
  auto b = base;
  auto state = construction_state(tables, type);
  RecordingHost host;
  host.view = view;
  const auto ctx = context(a, tables, state);
  const int xs[] = {320, 360, 400, 280, 200};
  original_main_pointer_down(ctx, host, {xs[0], y, kOriginalKeyLButton},
                             false);
  const auto anchor = placement_for(type, xs[0], y, view);
  const auto build_span = [&](std::int32_t left, std::int32_t right) {
    OriginalLobbyPlacement span{anchor.floor, left, right};
    if (type == 0U) {
      return build_original_floor(b, span.floor,
                                  static_cast<std::uint16_t>(left),
                                  static_cast<std::uint16_t>(right),
                                  tables.costs);
    }
    return direct_build(b, tables, type, span, 0U);
  };
  std::uint16_t successes = 0U;
  if (build_span(anchor.left, anchor.right).succeeded()) ++successes;
  for (std::size_t index = 1; index < std::size(xs); ++index) {
    original_main_pointer_move(ctx, host,
                               {xs[index], y, kOriginalKeyLButton});
    const auto p = placement_for(type, xs[index], y, host.view);
    if (build_span(std::min(anchor.left, p.left),
                   std::max(anchor.right, p.right))
            .succeeded()) {
      ++successes;
    }
  }
  expect(state.completion.successful_steps == successes,
         "drag step count for type " + std::to_string(type));
  original_main_pointer_up(ctx, host, {xs[std::size(xs) - 1], y, 0U});
  if (successes != 0U && b.header.balance != base.header.balance) {
    direct_tail(b, tables, type);
  }
  expect(same_tower(a, b), "span drag bytes for type " + std::to_string(type));
  const bool captured = std::find(host.trace.begin(), host.trace.end(),
                                  "capture 0") != host.trace.end();
  const bool released = std::find(host.trace.begin(), host.trace.end(),
                                  "release") != host.trace.end();
  expect(captured && released, "drag captures and releases Main");
  expect(!state.capture_held && !state.completion.active,
         "drag state cleared on up");
}

// Parking (11) and Ramp (44) drags on the full tower's basement, against
// the 11f8:240d/25a2 run plans and per-unit constructors.
void test_parking_drag(const Tables& tables, std::uint16_t type) {
  auto base = staged_tower(tables, "busy");
  if (type == 11U) {
    // Parking needs a Ramp on its floor (11f8:2f2b status 34).
    assert(build_original_parking_ramp(base, 9, 360U, tables.costs)
               .succeeded());
    direct_tail(base, tables, 44U);
  }
  const int floor = 9;
  const auto view =
      original_facility_focus_view(240, floor, kClientWidth, kClientHeight);
  int y = -1;
  for (int probe = 2; probe < kClientHeight; probe += 2) {
    if (placement_for(type, 300, probe, view).floor == floor) {
      y = probe;
      break;
    }
  }
  assert(y >= 0);
  auto a = base;
  auto b = base;
  auto state = construction_state(tables, type);
  RecordingHost host;
  host.view = view;
  const auto ctx = context(a, tables, state);
  // Parking runs horizontally; the Ramp vertically (one floor per 36 px).
  std::vector<std::pair<int, int>> points;
  if (type == 11U) {
    points = {{300, y}, {332, y}, {380, y}, {420, y}};
  } else {
    points = {{300, y}, {300, y + 36}, {300, y + 72}};
  }
  original_main_pointer_down(
      ctx, host, {points[0].first, points[0].second, kOriginalKeyLButton},
      false);
  for (std::size_t index = 1; index < points.size(); ++index) {
    original_main_pointer_move(
        ctx, host,
        {points[index].first, points[index].second, kOriginalKeyLButton});
  }
  original_main_pointer_up(ctx, host,
                           {points.back().first, points.back().second, 0U});

  // Direct: replay the run plans with the same snaps (no auto-scroll here:
  // every point stays inside the client).
  const auto press = placement_for(type, points[0].first, points[0].second,
                                   view);
  OriginalParkingDragRunState parking{};
  OriginalParkingRampDragRunState ramp{};
  std::uint16_t successes = 0U;
  const auto balance_at_press = b.header.balance;
  for (const auto& point : points) {
    const auto p = placement_for(type, point.first, point.second, view);
    bool attempted = false;
    bool final_ok = false;
    if (type == 11U) {
      auto plan = original_parking_drag_run_plan(parking, p.left, p.right,
                                                 p.left, p.right);
      for (const auto left : plan.unit_lefts) {
        attempted = true;
        final_ok = build_original_parking(b, press.floor,
                                          static_cast<std::uint16_t>(left),
                                          tables.costs)
                       .succeeded();
      }
      parking = plan.next_state;
      parking.retained_left = p.left;
      parking.retained_right = p.right;
    } else {
      auto plan = original_parking_ramp_drag_run_plan(ramp, press.floor,
                                                      p.floor, p.left);
      for (const auto& attempt : plan.attempts) {
        attempted = true;
        final_ok = build_original_parking_ramp(
                       b, attempt.floor,
                       static_cast<std::uint16_t>(attempt.left), tables.costs)
                       .succeeded();
      }
      ramp = plan.next_state;
      ramp.retained_floor = p.floor;
      ramp.retained_upper_exclusive = static_cast<std::int16_t>(p.floor + 1);
    }
    if (attempted && final_ok) {
      ++successes;
    } else {
      // Development PR #11: 11f8:07d8 clears DS:3216 when 240d/25a2
      // return zero (0de4-0e09), so the next step re-anchors at the
      // pointer (240d 242a, 25a2 25b9).
      parking.initialized = false;
      ramp.initialized = false;
    }
  }
  const auto release = original_construction_release_plan(
      type, successes, balance_at_press, b.header.balance);
  if (release.complete_success_tail) direct_tail(b, tables, type);
  expect(same_tower(a, b), "parking drag bytes for type " +
                               std::to_string(type));
  expect(b.header.balance < base.header.balance,
         "parking drag built something for type " + std::to_string(type));
  // Parking/Ramp never play WAVE/7000 (0839 and 0e58 both exclude them).
  expect(std::find(host.trace.begin(), host.trace.end(), "sound 7000 0 4") ==
             host.trace.end(),
         "no WAVE/7000 for type " + std::to_string(type));
}

// Edge of view: a Floor drag past the client edge scrolls through
// set_original_view (1080:0054), and later moves use the new view.
void test_edge_scroll(const Tables& tables) {
  const auto base = staged_tower(tables, "busy");
  const int floor = 35;
  const auto view =
      original_facility_focus_view(200, floor, kClientWidth, kClientHeight);
  int y = -1;
  for (int probe = 2; probe < kClientHeight; probe += 2) {
    if (placement_for(0U, 320, probe, view).floor == floor) {
      y = probe;
      break;
    }
  }
  assert(y >= 0);
  auto a = base;
  auto b = base;
  auto state = construction_state(tables, 0U);
  RecordingHost host;
  host.view = view;
  const auto ctx = context(a, tables, state);
  original_main_pointer_down(ctx, host, {320, y, kOriginalKeyLButton}, false);
  const auto anchor = placement_for(0U, 320, y, view);
  (void)build_original_floor(b, anchor.floor,
                             static_cast<std::uint16_t>(anchor.left),
                             static_cast<std::uint16_t>(anchor.right),
                             tables.costs);
  for (const int x : {kClientWidth + 24, kClientWidth + 24}) {
    const auto before = host.view;
    original_main_pointer_move(ctx, host, {x, y, kOriginalKeyLButton});
    const auto p = placement_for(0U, x, y, before);
    (void)build_original_floor(
        b, anchor.floor,
        static_cast<std::uint16_t>(std::min(anchor.left, p.left)),
        static_cast<std::uint16_t>(std::max(anchor.right, p.right)),
        tables.costs);
  }
  original_main_pointer_up(ctx, host, {kClientWidth + 24, y, 0U});
  direct_tail(b, tables, 0U);
  expect(host.view.x > view.x, "drag past the edge scrolled right");
  expect(same_tower(a, b), "edge-scroll drag bytes");
}

// Bulldozer, Finger and Magnifier route to their 1058 legs.
void test_edit_tools(const Tables& tables) {
  const auto base = staged_tower(tables, "busy");
  const auto& shaft = [&]() -> const OriginalTdtElevator& {
    for (const auto& elevator : base.elevators) {
      if (elevator.used != 0U) return elevator;
    }
    assert(false);
    return base.elevators.front();
  }();
  // Magnifier on an office: one Facility Information request.
  {
    auto a = base;
    auto state = construction_state(tables, 7U);
    state.command_mode = 2U;
    RecordingHost host;
    host.view = original_facility_focus_view(240, 14, kClientWidth,
                                             kClientHeight);
    std::size_t opened = 0U;
    for (int y = 2; y < kClientHeight && opened == 0U; y += 6) {
      host.clear();
      click(context(a, tables, state), host, 320, y);
      for (const auto& token : host.trace) {
        if (token.rfind("facility_info", 0) == 0) ++opened;
      }
    }
    expect(opened == 1U, "magnifier opens Facility Information");
    expect(same_tower(a, base), "magnifier does not mutate");
  }
  // Bulldozer on an office: the facility-click damage path mutates.
  {
    auto a = base;
    auto state = construction_state(tables, 7U);
    state.command_mode = 0U;
    RecordingHost host;
    host.view = original_facility_focus_view(240, 15, kClientWidth,
                                             kClientHeight);
    for (int y = 2; y < kClientHeight; y += 6) {
      click(context(a, tables, state), host, 320, y);
      if (!same_tower(a, base)) break;
    }
    expect(!same_tower(a, base), "bulldozer removes a facility");
    expect(std::find(host.trace.begin(), host.trace.end(), "main") !=
               host.trace.end(),
           "bulldozer invalidates Main");
  }
  // Finger: a press on a shaft cap captures and sets the resize cursor; the
  // button-up releases and restores the Finger cursor (10a0:05a2).
  {
    auto a = base;
    auto state = construction_state(tables, 1U);
    state.command_mode = 1U;
    RecordingHost host;
    host.view = original_facility_focus_view(shaft.x, shaft.top_floor,
                                             kClientWidth, kClientHeight);
    bool cap = false;
    for (int y = 2; y < kClientHeight && !cap; y += 2) {
      for (int x = 2; x < kClientWidth && !cap; x += 2) {
        const auto hit = original_elevator_hit_from_client(
            a, x, y, host.view.x, host.view.y);
        if (!hit.hit) continue;
        const auto& elevator = a.elevators[hit.elevator_index];
        const auto path = original_elevator_finger_press_path(
            true, elevator.word_3c != 0U, hit.floor, elevator.bottom_floor,
            elevator.top_floor);
        if (path != OriginalElevatorFingerPressPath::capture_upper_cap) {
          continue;
        }
        cap = true;
        host.clear();
        original_main_pointer_down(context(a, tables, state), host,
                                   {x, y, kOriginalKeyLButton}, false);
        expect(state.shaft_drag.capture_active &&
                   state.shaft_drag.direction ==
                       OriginalElevatorShaftDragDirection::upper,
               "finger captures the upper cap");
        expect(host.trace == std::vector<std::string>{"capture 0",
                                                      "cursor 1000"},
               "finger cap press hooks");
        original_main_pointer_up(context(a, tables, state), host, {x, y, 0U});
        expect(!state.shaft_drag.capture_active, "finger up clears DS:02a6");
        expect(host.trace.back() == "cursor 1004", "finger cursor restored");
      }
    }
    expect(cap, "found an upper cap");
  }
}

// Command palette: the edit-mode cells and a grouped facility selector.
void test_command(const Tables& tables) {
  auto document = staged_tower(tables, "busy");
  auto state = construction_state(tables, 24U);
  RecordingHost host;
  const auto ctx = context(document, tables, state);
  // The hit rectangles (original_command_mode_rect and friends) already
  // include the 8-pixel frame bar, so they take frame coordinates. Edit
  // mode 0 (Bulldozer) is x 0..20, y 40..60.
  original_command_pointer_down(ctx, host, 5, 45);
  original_command_pointer_up(ctx, host, 5, 45);
  expect(state.command_mode == 0U, "Bulldozer cell selects mode 0");
  // Close box.
  host.clear();
  original_command_pointer_down(ctx, host, 5, 3);
  expect(host.trace == std::vector<std::string>{"hide 0"}, "close box hides");
  // First facility cell (catalog 0: the grouped Lobby/Floor command). Each
  // selector row writes its choice back into the rating TABL copy and the
  // command then resolves TABL/1000's type for it.
  const auto group = original_command_group(
      tables.resources, active_original_command_rating(ctx, 1U), 0U);
  assert(group && group->catalog_icons.size() >= 2U);
  std::set<std::uint16_t> types;
  for (int row = 0; row < static_cast<int>(group->catalog_icons.size());
       ++row) {
    host.clear();
    host.selector_row = row;
    original_command_pointer_down(ctx, host, 5, 70);
    original_command_pointer_up(ctx, host, 5, 70);
    expect(state.command_mode == 3U, "facility cell selects mode 3");
    expect(host.trace.front() == "selector " +
               std::to_string(group->catalog_icons.size()),
           "grouped cell opens the selector");
    expect(host.trace.back() == "map", "activation repaints Map last");
    types.insert(state.selected_build_type);
  }
  expect(types.count(0U) == 1U && types.count(24U) == 1U,
         "the Lobby group offers Floor and Lobby");
  // Released without the primary button: 1058:053d keeps the choice.
  host.clear();
  host.primary_down = false;
  const auto before = state.selected_build_type;
  original_command_pointer_down(ctx, host, 5, 70);
  expect(state.selected_build_type == before &&
             std::none_of(host.trace.begin(), host.trace.end(),
                          [](const std::string& token) {
                            return token.rfind("selector", 0) == 0;
                          }),
         "no selector without the primary button");
}

// The selector state machine replays 1050:05a7's per-message branches.
void test_selector_session() {
  OriginalCommandGroup group{1000U, 2U, {5U, 7U, 9U}};
  OriginalCommandSelectorSession session(group, 140, 240);
  expect(session.highlight() == 2U, "initial highlight is the choice index");
  expect(session.height() == 96, "selector height");
  const auto move = session.on_move(1);
  expect(move.redraw && !move.end && session.highlight() == 8U,
         "move highlights TABM(icon)+1");
  expect(!session.on_move(1).redraw, "same row does not redraw");
  const auto outside = session.on_move(std::nullopt);
  expect(outside.end && !outside.accepted && outside.unclip_and_release &&
             !outside.handled,
         "move outside cancels with FALSE");
  OriginalCommandSelectorSession down(group, 0, 0);
  const auto pressed = down.on_lbutton_down(2);
  expect(pressed.end && pressed.accepted && !pressed.unclip_and_release &&
             down.choice() == 3U,
         "down commits without unclipping");
  OriginalCommandSelectorSession up(group, 0, 0);
  const auto released = up.on_lbutton_up(0);
  expect(released.end && released.accepted && released.unclip_and_release &&
             up.choice() == 1U,
         "up commits and unclips");
  expect(!up.on_rbutton_down().accepted, "right button cancels");
  expect(OriginalCommandSelectorSession::row_at(31, 95, 32, 96) == 2 &&
             !OriginalCommandSelectorSession::row_at(32, 0, 32, 96) &&
             !OriginalCommandSelectorSession::row_at(0, -1, 32, 96),
         "row_at matches PtInRect");
}

// Map: toolbar selects an overlay; content drags the Main view.
void test_map(const Tables& tables) {
  auto document = staged_tower(tables, "busy");
  auto state = construction_state(tables, 24U);
  RecordingHost host;
  host.view = original_initial_view(kClientWidth, kClientHeight);
  const auto ctx = context(document, tables, state);
  // Content point: frame y 8 + toolbar 18 + 100.
  original_map_pointer_down(ctx, host, 100, kOriginalMapClientTop + 118);
  expect(state.map_drag_active && state.map_pointer_down,
         "content press starts the drag");
  expect(state.map_focus.has_value(), "drag published a focus rectangle");
  original_map_pointer_move(ctx, host, 160, kOriginalMapClientTop + 200,
                            kOriginalKeyLButton);
  expect(original_map_pointer_up(ctx, host), "up consumed");
  expect(!state.map_drag_active && !state.map_pointer_down,
         "up clears the drag words");
  std::size_t scrolls = 0U;
  for (const auto& token : host.trace) {
    if (token.rfind("scroll", 0) == 0) ++scrolls;
  }
  expect(scrolls == 2U, "two drag commits");

  // 1058:02d7-032e: one Map DC from after the first pump until after the
  // synchronous Main present, around both XOR focus draws.
  host.clear();
  original_map_pointer_down(ctx, host, 40, kOriginalMapClientTop + 60);
  std::vector<std::string> order;
  for (const auto& token : host.trace) {
    if (token == "pump" || token == "map_dc" || token == "map_dc_release" ||
        token.rfind("focus", 0) == 0 || token.rfind("scroll", 0) == 0 ||
        token.rfind("pass", 0) == 0) {
      order.push_back(token.rfind("focus", 0) == 0    ? "focus"
                      : token.rfind("scroll", 0) == 0 ? "scroll"
                                                      : token);
    }
  }
  expect(order == std::vector<std::string>{"pump", "map_dc", "focus",
                                           "scroll", "focus", "pump",
                                           "pass 4 sync", "map_dc_release",
                                           "pump"},
         "map drag keeps one DC across 02d7-032e");
  (void)original_map_pointer_up(ctx, host);

  // Without a Main client the drag publishes nothing.
  host.clear();
  host.main_available = false;
  const auto focus_before = state.map_focus;
  original_map_pointer_down(ctx, host, 150, kOriginalMapClientTop + 150);
  expect(state.map_focus == focus_before &&
             std::none_of(host.trace.begin(), host.trace.end(),
                          [](const std::string& token) {
                            return token == "pump" ||
                                   token.rfind("scroll", 0) == 0;
                          }),
         "no Main client: no drag commit");
  (void)original_map_pointer_up(ctx, host);
}

// Info: only the close box reacts (INFOWNDPROC 1120:0111).
void test_info(const Tables& tables) {
  auto document = staged_tower(tables, "busy");
  auto state = construction_state(tables, 24U);
  RecordingHost host;
  const auto ctx = context(document, tables, state);
  expect(!original_info_pointer_down(ctx, host, 100, 20) &&
             !original_info_pointer_down(ctx, host, 100, 3) &&
             host.trace.empty(),
         "Info content and drag row are not handled");
  expect(original_info_pointer_down(ctx, host, 5, 3) &&
             host.trace == std::vector<std::string>{"hide 1"},
         "Info close box hides");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: simtower_world_input_tests PACK\n");
    return 2;
  }
  const auto pack = read_file(argv[1]);
  Tables tables{OriginalResources(std::span<const std::byte>(pack)), {}, {},
                {}, {}};
  auto startup = load_original_startup_data(tables.resources);
  tables.part = std::move(startup.part);
  tables.costs = std::move(startup.construction_costs);
  tables.rent = std::move(startup.rent_income);
  tables.ratings = std::move(startup.command_ratings);

  test_selector_session();
  test_single_placements(tables);
  test_initial_lobby(tables);
  test_variant_cycling(tables);
  test_reject(tables);
  test_shift_replacement(tables);
  test_span_drag(tables, 0U, 35, 240);
  test_span_drag(tables, 24U, 10, 380);
  test_parking_drag(tables, 11U);
  test_parking_drag(tables, 44U);
  test_edge_scroll(tables);
  test_edit_tools(tables);
  test_command(tables);
  test_map(tables);
  test_info(tables);

  if (g_failures != 0) {
    std::fprintf(stderr, "world_input: %d failure(s)\n", g_failures);
    return 1;
  }
  std::fprintf(stderr, "world_input: all checks passed\n");
  return 0;
}
