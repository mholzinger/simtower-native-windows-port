// The shared ELVDLOGMAIN / ELVPOPUP controllers
// (original_elevator_control_controller.hpp) against frozen copies of the
// Win32 procedure branches in native_main.cpp as they were before the
// procedures shared them (elevator_control_dialog_proc's WM_INITDIALOG
// scroll bar and window disabling, WM_VSCROLL, WM_MOUSEMOVE,
// WM_LBUTTONDOWN, WM_LBUTTONUP with the grid, Simulate / Resume and close
// helpers, and elevator_popup_dialog_proc's pointer messages). Every USER
// or host call of the frozen copy is one recorded token, the same token the
// recording ops and host write for the controller's call, so the two event
// logs, the documents and the control state must be identical.
//
// Then the simulation oracle: the busy and full staged towers through the
// shared idle pass (run_original_idle_pass) with one reference idle host,
// once driven by the frozen procedure and once by the controller, opening
// Elevator Control on a standard shaft, editing its schedule and service
// floors, running Simulate for fifty gated passes, resuming and closing.
// The per-pass state digests (tower bytes, process RNG, simulation clock,
// transfer visuals, DS:783e and DS:b3ae) must be identical, and the
// invariants of the coupling must hold: an open with no edits leaves the
// digests of a run that never opened it; Simulate freezes the tower;
// Resume restores the shafts and keeps the schedule edits.
//
// Runs natively and under Wine; needs the pack (exit 77 without it).

#include "original_construction.hpp"
#include "original_dialog_model.hpp"
#include "original_draw_canvas.hpp"
#include "original_canvas_font.hpp"
#include "original_elevator_control.hpp"
#include "original_elevator_control_controller.hpp"
#include "original_idle_pass.hpp"
#include "original_resources.hpp"
#include "original_simulation.hpp"
#include "original_startup_data.hpp"
#include "original_tables.hpp"
#include "original_tdt.hpp"
#include "original_time.hpp"
#include "original_ui.hpp"
#include "original_world.hpp"
#include "test_scenario.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace simtower;
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

std::uint64_t fnv(std::span<const std::byte> bytes,
                  std::uint64_t hash = 0xcbf29ce484222325ULL) {
  for (const auto byte : bytes) {
    hash ^= std::to_integer<std::uint8_t>(byte);
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

std::uint64_t fnv_value(std::uint64_t hash, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    hash ^= (value >> shift) & 0xffU;
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

std::string window_name(OriginalElevatorControlCloseWindow window) {
  switch (window) {
    case OriginalElevatorControlCloseWindow::command: return "command";
    case OriginalElevatorControlCloseWindow::map: return "map";
    case OriginalElevatorControlCloseWindow::info: return "info";
    case OriginalElevatorControlCloseWindow::main: return "main";
  }
  return "?";
}

// What one side's host world holds: the document, DS:783e, the transfer
// slots, the scripted alert and popup answers and the event log.
struct World {
  OriginalTdtDocument document{};
  bool build_mode{true};
  bool dirty{};
  bool published{true};
  bool destroyed{};
  std::vector<OriginalElevatorTransferVisual> transfer_visuals{};
  const OriginalStartupData* startup{};
  std::deque<int> alert_answers{};
  std::deque<int> popup_answers{};
  // Called for the synchronous rebuild_without_sky (the oracle's Main
  // presentation step); null records only.
  std::function<void()> rebuild{};
  Log log{};
  // GetAsyncKeyState(primary button) for a phase press (the frozen copy
  // never asks: the Win32 port always found it down).
  bool primary_down{true};

  int next_alert() {
    if (alert_answers.empty()) return 1;
    const int answer = alert_answers.front();
    alert_answers.pop_front();
    alert_answers.push_back(answer);
    return answer;
  }
  int next_popup() {
    if (popup_answers.empty()) return -1;
    const int answer = popup_answers.front();
    popup_answers.pop_front();
    popup_answers.push_back(answer);
    return answer;
  }
};

class RecordingOps final : public OriginalDialogOps {
 public:
  explicit RecordingOps(Log& log) : log_(log) {}
  void show_item(std::uint16_t id, bool shown) override {
    log_.push_back("show " + std::to_string(id) + " " +
                   std::to_string(shown));
  }
  void invalidate() override { log_.push_back("invalidate"); }
  void update_window() override { log_.push_back("update"); }
  void resize(int, int) override { log_.push_back("resize"); }
  void enable_item(std::uint16_t id, bool enabled) override {
    log_.push_back("enable " + std::to_string(id) + " " +
                   std::to_string(enabled));
  }
  void set_scroll_range(std::uint16_t id, int min, int max) override {
    log_.push_back("range " + std::to_string(id) + " " + std::to_string(min) +
                   " " + std::to_string(max));
  }
  void set_scroll_pos(std::uint16_t id, int position) override {
    log_.push_back("pos " + std::to_string(id) + " " +
                   std::to_string(position));
  }

 private:
  Log& log_;
};

class RecordingHost final : public OriginalElevatorControlHost {
 public:
  explicit RecordingHost(World& world) : w_(world) {}
  OriginalTdtDocument* document() override { return &w_.document; }
  const OriginalPartTable& part() override { return w_.startup->part; }
  const OriginalYenTable& rent_income() override {
    return w_.startup->rent_income;
  }
  bool& build_mode() override { return w_.build_mode; }
  int alert(int alert_id, std::uint16_t strl_id,
            std::uint16_t strl_index) override {
    const int answer = w_.next_alert();
    w_.log.push_back("alert " + std::to_string(alert_id) + " " +
                     std::to_string(strl_id) + "/" +
                     std::to_string(strl_index) + " -> " +
                     std::to_string(answer));
    return answer;
  }
  int popup(const OriginalDtmpRect& rect, std::size_t item,
            std::uint8_t mode) override {
    const int answer = w_.next_popup();
    w_.log.push_back("popup " + std::to_string(item) + " " +
                     std::to_string(mode) + " at " +
                     std::to_string(rect.left) + "," +
                     std::to_string(rect.top) + " -> " +
                     std::to_string(answer));
    return answer;
  }
  void consume_family_dispatches(
      std::span<const OriginalPersonFamilyDispatchResult> dispatches)
      override {
    w_.log.push_back("dispatches " + std::to_string(dispatches.size()));
  }
  void play_movement_sound() override { w_.log.push_back("sound 6002"); }
  void rebuild_main_without_sky() override {
    w_.log.push_back("rebuild");
    if (w_.rebuild) w_.rebuild();
  }
  void clear_transfer_visuals() override {
    w_.log.push_back("clear transfer");
    w_.transfer_visuals.clear();
  }
  void mark_document_dirty() override {
    w_.log.push_back("dirty");
    w_.dirty = true;
  }
  void invalidate_main() override { w_.log.push_back("inv main"); }
  void invalidate_map() override { w_.log.push_back("inv map"); }
  void invalidate_info() override { w_.log.push_back("inv info"); }
  void invalidate_command() override { w_.log.push_back("inv command"); }
  void enable_window(OriginalElevatorControlCloseWindow window,
                     bool enabled) override {
    w_.log.push_back("window " + window_name(window) + " " +
                     std::to_string(enabled));
  }
  void unpublish() override {
    w_.log.push_back("unpublish");
    w_.published = false;
  }
  void activate_main() override { w_.log.push_back("activate main"); }
  bool primary_button_down() override { return w_.primary_down; }
  void destroy() override {
    w_.log.push_back("destroy");
    w_.destroyed = true;
  }

 private:
  World& w_;
};

// ---- Frozen copy of the Win32 procedure (native_main.cpp before it ran
// the shared controller). Each USER call is the token RecordingOps or
// RecordingHost writes for the controller's equivalent call.

struct Frozen {
  OriginalDtmp dtmp{};
  OriginalElevatorControlState state{};
  int pressed_item{-1};
};

const OriginalDtmpRect* frozen_rectangle(const Frozen& context,
                                         std::size_t one_based_index) {
  if (one_based_index == 0U ||
      one_based_index > context.dtmp.rectangles.size()) {
    return nullptr;
  }
  return &context.dtmp.rectangles[one_based_index - 1U];
}

bool frozen_point_in_rectangle(const OriginalDtmpRect& rectangle, int x,
                               int y) {
  return rectangle.left != 0xffffU && rectangle.top != 0xffffU &&
         x >= rectangle.left && x < rectangle.right &&
         y >= rectangle.top && y < rectangle.bottom;
}

int frozen_hit_item(const Frozen& context, int x, int y) {
  for (std::size_t item = 1U; item <= 28U; ++item) {
    const auto* rectangle = frozen_rectangle(context, item);
    if (rectangle && frozen_point_in_rectangle(*rectangle, x, y)) {
      return static_cast<int>(item);
    }
  }
  return -1;
}

void frozen_refresh_scrollbar(World& w, const Frozen& context) {
  // HWND scrollbar = GetDlgItem(dialog, 7) exists in DIALOG/400.
  const bool enabled = original_elevator_control_has_scrollbar(context.state);
  w.log.push_back("enable 7 " + std::to_string(enabled));
  if (enabled) {
    w.log.push_back("range 7 " + std::to_string(context.state.scroll_min) +
                    " " + std::to_string(context.state.scroll_max));
    w.log.push_back("pos 7 " + std::to_string(context.state.scroll_position));
  }
}

void frozen_initialize(World& w, const Frozen& context) {
  // WM_INITDIALOG: EnableWindow(main/map/info/command, FALSE), placement,
  // render_original_dtmp, the scroll bar refresh.
  w.log.push_back("window main 0");
  w.log.push_back("window map 0");
  w.log.push_back("window info 0");
  w.log.push_back("window command 0");
  frozen_refresh_scrollbar(w, context);
}

void frozen_mark_changed(World& w, bool world_changed) {
  w.log.push_back("dirty");
  w.dirty = true;
  w.log.push_back("invalidate");
  if (world_changed) {
    w.log.push_back("inv main");
    w.log.push_back("inv info");
    w.log.push_back("inv map");
  }
}

std::uint8_t frozen_run_popup(World& w, const Frozen& context,
                              std::size_t phase_item) {
  if (context.state.elevator_index >= w.document.elevators.size()) {
    return 0U;
  }
  const auto* rectangle = frozen_rectangle(context, phase_item);
  if (!rectangle) return 0U;
  const auto& elevator = w.document.elevators[context.state.elevator_index];
  auto popup_state = context.state;
  popup_state.day_phase = static_cast<std::uint8_t>(phase_item - 23U);
  const auto mode = std::min<std::uint8_t>(
      2U, original_elevator_control_floor_mode(elevator, popup_state));
  const int result = w.next_popup();
  w.log.push_back("popup " + std::to_string(phase_item) + " " +
                  std::to_string(mode) + " at " +
                  std::to_string(rectangle->left) + "," +
                  std::to_string(rectangle->top) + " -> " +
                  std::to_string(result));
  return result >= 0 && result <= 2 ? static_cast<std::uint8_t>(result)
                                    : mode;
}

int frozen_alert(World& w, int id, std::uint16_t strl, std::uint16_t index) {
  const int answer = w.next_alert();
  w.log.push_back("alert " + std::to_string(id) + " " + std::to_string(strl) +
                  "/" + std::to_string(index) + " -> " +
                  std::to_string(answer));
  return answer;
}

void frozen_grid_action(World& w, Frozen& context, int x, int y) {
  const auto hit =
      original_elevator_control_grid_hit(w.document, context.state, x, y);
  if (!hit.hit()) return;
  if (context.state.isolation_active) {
    (void)frozen_alert(w, 1000, 1004U, 1U);
    return;
  }
  bool changed = false;
  if (hit.kind == OriginalElevatorControlGridKind::service_floor) {
    const auto elevator_index = context.state.elevator_index;
    const auto floor = hit.floor;
    const auto& elevator = w.document.elevators[elevator_index];
    const bool serviced =
        floor >= 0 && floor < 120 &&
        elevator.serviced_floors[static_cast<std::size_t>(floor)] !=
            std::byte{0};
    if (!serviced) {
      changed = original_elevator_control_add_service_floor(
          w.document, elevator_index, floor);
    } else {
      const auto warning = original_elevator_service_floor_warning_code(
          w.document, elevator_index, floor);
      if (warning != 0U) {
        if (frozen_alert(w, 1005, 1005U, warning) == 2) return;
      }
      const auto removal = original_elevator_control_remove_service_floor(
          w.document, elevator_index, floor, w.startup->part,
          w.startup->rent_income);
      changed = removal.cleanup.status ==
                OriginalElevatorFloorPeopleCleanupStatus::cleaned;
      if (changed) {
        w.log.push_back("dispatches " +
                        std::to_string(removal.family_dispatches.size()));
      }
    }
  } else if (hit.kind == OriginalElevatorControlGridKind::car) {
    changed = original_elevator_control_set_car_home(
        w.document, context.state.elevator_index,
        static_cast<std::size_t>(hit.car_index), hit.floor);
  }
  if (changed) frozen_mark_changed(w, true);
}

void frozen_resume(World& w, Frozen& context) {
  if (!context.state.isolation_active) return;
  if (resume_original_elevator_control_isolation(w.document, context.state,
                                                 w.build_mode)) {
    w.log.push_back("clear transfer");
    w.transfer_visuals.clear();
    w.log.push_back("invalidate");
    w.log.push_back("inv main");
    w.log.push_back("inv map");
    w.log.push_back("inv info");
    w.log.push_back("inv command");
  }
}

void frozen_toggle_simulation(World& w, Frozen& context) {
  if (context.state.isolation_active) {
    frozen_resume(w, context);
    return;
  }
  if (!begin_original_elevator_control_isolation(w.document, context.state,
                                                 w.build_mode)) {
    return;
  }
  const auto preview = prepare_original_elevator_control_preview(
      w.document, context.state, w.startup->part, w.startup->rent_income);
  if (!preview.prepared) {
    (void)frozen_alert(w, 1000, 1004U, 2U);
    frozen_resume(w, context);
    return;
  }
  for (std::size_t index = 0U; index < preview.movement_sound_requests;
       ++index) {
    w.log.push_back("sound 6002");
  }
  w.log.push_back("rebuild");
  if (w.rebuild) w.rebuild();
  w.log.push_back("invalidate");
  w.log.push_back("inv command");
}

void frozen_close(World& w, Frozen& context) {
  const auto plan = original_elevator_control_close_plan();
  if (plan.resume_isolation) frozen_resume(w, context);
  if (plan.clear_published_window_before_destroy) {
    w.log.push_back("unpublish");
    w.published = false;
  }
  for (const auto window : plan.enable_order) {
    w.log.push_back("window " + window_name(window) + " 1");
  }
  if (plan.explicitly_activate_main) w.log.push_back("activate main");
  w.log.push_back("destroy");
  w.destroyed = true;
}

// The procedure's message switch for the four shared messages.
void frozen_procedure(World& w, Frozen& context,
                      const OriginalDialogSignal& signal) {
  switch (signal.kind) {
    case Kind::scroll: {
      std::optional<OriginalElevatorControlScrollCommand> command;
      using C = OriginalElevatorControlScrollCommand;
      switch (signal.notify_code) {
        case 0: command = C::line_up; break;
        case 1: command = C::line_down; break;
        case 2: command = C::page_up; break;
        case 3: command = C::page_down; break;
        case 4: command = C::thumb_position; break;
        case 5: command = C::thumb_track; break;
      }
      if (command && original_elevator_control_scroll(
                         context.state, *command,
                         static_cast<std::int16_t>(signal.value))) {
        w.log.push_back("pos 7 " +
                        std::to_string(context.state.scroll_position));
        w.log.push_back("invalidate");
        w.log.push_back("update");
      }
      return;
    }
    case Kind::mouse_move:
      if (context.pressed_item >= 1 && context.pressed_item <= 2) {
        w.log.push_back("invalidate");
        w.log.push_back("update");
      }
      return;
    case Kind::button_down: {
      const int item = frozen_hit_item(context, signal.x, signal.y);
      context.pressed_item = item;
      bool changed = false;
      if (item == 8) {
        changed = original_elevator_control_adjust_waiting(
            w.document, context.state, 1);
      } else if (item == 9) {
        changed = original_elevator_control_adjust_waiting(
            w.document, context.state, -1);
      } else if (item == 10) {
        changed = original_elevator_control_adjust_departure(
            w.document, context.state, 1);
      } else if (item == 11) {
        changed = original_elevator_control_adjust_departure(
            w.document, context.state, -1);
      } else if (item >= 23 && item <= 28) {
        const auto mode =
            frozen_run_popup(w, context, static_cast<std::size_t>(item));
        changed = original_elevator_control_select_phase(
                      context.state, static_cast<std::uint8_t>(item - 23)) ||
                  changed;
        changed = original_elevator_control_set_floor_mode(
                      w.document, context.state, mode) ||
                  changed;
      }
      if (changed) {
        frozen_mark_changed(w, false);
      } else {
        w.log.push_back("invalidate");
      }
      w.log.push_back("update");
      return;
    }
    case Kind::button_up: {
      const int item = frozen_hit_item(context, signal.x, signal.y);
      const int pressed = context.pressed_item;
      context.pressed_item = -1;
      w.log.push_back("invalidate");
      w.log.push_back("update");
      if (item != pressed) return;
      if (item == 1) {
        frozen_close(w, context);
        return;
      }
      if (item == 2) {
        frozen_toggle_simulation(w, context);
        return;
      }
      if (item == 5 &&
          original_elevator_control_toggle_show(
              w.document, context.state.elevator_index)) {
        frozen_mark_changed(w, true);
        return;
      }
      if (item == 6) {
        frozen_grid_action(w, context, signal.x, signal.y);
        return;
      }
      if ((item == 12 || item == 13) &&
          original_elevator_control_select_bank(
              context.state, static_cast<std::uint8_t>(item - 12))) {
        w.log.push_back("invalidate");
        w.log.push_back("update");
        return;
      }
      return;
    }
    default:
      return;
  }
}

// The frozen ELVPOPUP pointer branches (EndDialog result or nullopt; the
// hover in *hover).
std::optional<int> frozen_popup(std::uint8_t original_mode,
                                std::int16_t& hover, Log& log,
                                const OriginalDialogSignal& signal) {
  const auto selection = original_elevator_control_popup_selection(
      signal.x, signal.y, kOriginalElevatorControlPopupWidth,
      kOriginalElevatorControlPopupHeight);
  switch (signal.kind) {
    case Kind::mouse_move:
      if (!selection) return original_mode;
      if (static_cast<std::int16_t>(*selection) != hover) {
        hover = static_cast<std::int16_t>(*selection);
        log.push_back("invalidate");
        log.push_back("update");
      }
      return std::nullopt;
    case Kind::button_up:
      return static_cast<int>(selection.value_or(original_mode));
    case Kind::right_button_up:
      return original_mode;
    default:
      return std::nullopt;
  }
}

// ---- Shared helpers for the two sides --------------------------------

bool same_elevator(const OriginalTdtElevator& a, const OriginalTdtElevator& b) {
  if (a.used != b.used || a.type != b.type || a.capacity != b.capacity ||
      a.cars != b.cars || a.schedule != b.schedule ||
      a.word_3c != b.word_3c || a.x != b.x || a.top_floor != b.top_floor ||
      a.bottom_floor != b.bottom_floor ||
      a.serviced_floors != b.serviced_floors ||
      a.car_home_floors != b.car_home_floors || a.block_c2 != b.block_c2 ||
      a.block_2a2 != b.block_2a2 || a.block_31a != b.block_31a ||
      a.floor_records.size() != b.floor_records.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.floor_records.size(); ++i) {
    if (a.floor_records[i].exact_bytes != b.floor_records[i].exact_bytes ||
        a.floor_records[i].floor != b.floor_records[i].floor ||
        a.floor_records[i].mapped_index != b.floor_records[i].mapped_index) {
      return false;
    }
  }
  for (std::size_t i = 0; i < a.car_records.size(); ++i) {
    if (a.car_records[i].exact_bytes != b.car_records[i].exact_bytes) {
      return false;
    }
  }
  return true;
}

bool same_state(const OriginalElevatorControlState& a,
                const OriginalElevatorControlState& b) {
  return a.valid == b.valid && a.elevator_index == b.elevator_index &&
         a.schedule_bank == b.schedule_bank && a.day_phase == b.day_phase &&
         a.scroll_min == b.scroll_min && a.scroll_max == b.scroll_max &&
         a.scroll_position == b.scroll_position &&
         a.isolation_active == b.isolation_active &&
         a.saved_build_mode == b.saved_build_mode &&
         a.saved_elevator_used == b.saved_elevator_used &&
         a.saved_elevator_record.has_value() ==
             b.saved_elevator_record.has_value() &&
         (!a.saved_elevator_record ||
          same_elevator(*a.saved_elevator_record, *b.saved_elevator_record));
}

OriginalDialogSignal pointer(Kind kind, int x, int y) {
  OriginalDialogSignal signal{kind};
  signal.x = x;
  signal.y = y;
  return signal;
}

OriginalDialogSignal scroll(std::uint16_t code, std::int32_t position) {
  OriginalDialogSignal signal{Kind::scroll, kOriginalElevatorControlScrollBarId,
                              code};
  signal.value = position;
  return signal;
}

std::pair<int, int> center(const OriginalDtmpRect& rect) {
  return {(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
}

// A deterministic message script over every item, the grid cells and the
// scroll codes, with Simulate / Resume in the middle and the close last.
std::vector<OriginalDialogSignal> signal_script(const OriginalDtmp& dtmp) {
  std::vector<OriginalDialogSignal> script;
  const auto click = [&](int x, int y) {
    script.push_back(pointer(Kind::button_down, x, y));
    script.push_back(pointer(Kind::mouse_move, x + 1, y));
    script.push_back(pointer(Kind::button_up, x, y));
  };
  const auto item = [&](std::size_t one_based) {
    const auto [x, y] = center(dtmp.rectangles[one_based - 1U]);
    click(x, y);
  };
  // Every item but Close (1) and Simulate (2), in order.
  for (std::size_t index = 3U; index <= 28U && index <= dtmp.rectangles.size();
       ++index) {
    item(index);
  }
  // Waiting and departure limits.
  for (int repeat = 0; repeat < 5; ++repeat) item(8U);
  for (int repeat = 0; repeat < 7; ++repeat) item(11U);
  for (int repeat = 0; repeat < 4; ++repeat) item(10U);
  // A press on one item released on another, and a press outside.
  {
    const auto [x8, y8] = center(dtmp.rectangles[7]);
    const auto [x9, y9] = center(dtmp.rectangles[8]);
    script.push_back(pointer(Kind::button_down, x8, y8));
    script.push_back(pointer(Kind::button_up, x9, y9));
    script.push_back(pointer(Kind::button_down, 1, 1));
    script.push_back(pointer(Kind::button_up, 1, 1));
  }
  // The scroll bar: every code, thumb positions in and out of range.
  for (std::uint16_t code = 0U; code <= 8U; ++code) {
    script.push_back(scroll(code, 3));
  }
  for (const int position : {0, 5, 9, 15, 30, -4}) {
    script.push_back(scroll(4U, position));
    script.push_back(scroll(5U, position));
  }
  script.push_back(scroll(2U, 0));
  script.push_back(scroll(3U, 0));
  script.push_back(scroll(1U, 0));
  // The grid: service-floor labels and car cells on every row, twice
  // (add, then remove), with bank 1 and the phase popup between.
  for (int pass = 0; pass < 2; ++pass) {
    for (std::int16_t row = 0; row < kOriginalElevatorControlVisibleFloors;
         ++row) {
      for (std::int16_t column = -1; column < 4; ++column) {
        const auto cell = original_elevator_control_cell_rect(column, row);
        click((cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2);
      }
    }
    item(13U);
    item(25U);
    item(12U);
  }
  // Mouse moves with Close and Simulate pressed.
  for (const std::size_t pressed : {1U, 2U}) {
    const auto [x, y] = center(dtmp.rectangles[pressed - 1U]);
    script.push_back(pointer(Kind::button_down, x, y));
    script.push_back(pointer(Kind::mouse_move, x + 2, y + 2));
    script.push_back(pointer(Kind::mouse_move, 1, 1));
    script.push_back(pointer(Kind::button_up, 1, 1));
  }
  // Simulate, a grid click and edits while isolated, Resume, Simulate
  // again, then Close (which resumes).
  item(2U);
  {
    const auto cell = original_elevator_control_cell_rect(-1, 3);
    click((cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2);
  }
  item(8U);
  item(5U);
  item(24U);
  item(2U);
  item(2U);
  item(1U);
  // After the close nothing reaches the procedure any more.
  item(8U);
  return script;
}

// Tokens every parity run must have reached, across all cases.
std::map<std::string, std::size_t> g_coverage{};

void count_coverage(const Log& log) {
  for (const auto& token : log) {
    for (const char* prefix :
         {"alert 1000 1004/1", "alert 1000 1004/2", "alert 1005", "popup",
          "dispatches", "sound 6002", "rebuild", "clear transfer", "pos 7",
          "range 7", "enable 7 1", "enable 7 0", "dirty", "destroy"}) {
      if (token.rfind(prefix, 0) == 0) ++g_coverage[prefix];
    }
  }
}

void parity_case(const OriginalResources& resources,
                 const OriginalStartupData& startup,
                 const OriginalTdtDocument& base, std::size_t elevator_index,
                 std::uint8_t calendar, std::int8_t phase) {
  auto dtmp = parse_original_dtmp(resources.find("DTMP", 400));
  const auto state = make_original_elevator_control_state(
      base, elevator_index, calendar, phase);
  if (!state.valid) return;

  World frozen_world{base};
  World shared_world{base};
  for (auto* world : {&frozen_world, &shared_world}) {
    world->startup = &startup;
    world->alert_answers = {1, 2, 1};
    world->popup_answers = {-1, 0, 1, 2, 7};
  }
  Frozen frozen{dtmp, state};
  OriginalElevatorControlController controller(dtmp, state);
  RecordingOps ops(shared_world.log);
  RecordingHost host(shared_world);

  frozen_initialize(frozen_world, frozen);
  controller.disable_windows(host);
  controller.initialize_scroll_bar(ops);
  assert(frozen_world.log == shared_world.log);

  for (const auto& signal : signal_script(dtmp)) {
    if (!frozen_world.destroyed) frozen_procedure(frozen_world, frozen, signal);
    controller.signal(signal, ops, host);
    if (frozen_world.log != shared_world.log) {
      std::printf("parity: elevator %zu diverged at log entry %zu\n",
                  elevator_index,
                  std::min(frozen_world.log.size(), shared_world.log.size()));
      for (std::size_t i = 0; i < std::max(frozen_world.log.size(),
                                           shared_world.log.size());
           ++i) {
        std::printf("  %-40s | %s\n",
                    i < frozen_world.log.size() ? frozen_world.log[i].c_str()
                                                : "",
                    i < shared_world.log.size() ? shared_world.log[i].c_str()
                                                : "");
      }
      assert(false);
    }
    assert(frozen.pressed_item == controller.pressed_item() ||
           controller.closed());
    assert(same_state(frozen.state, controller.state()));
    assert(frozen_world.build_mode == shared_world.build_mode);
    assert(serialize_original_tdt(frozen_world.document) ==
           serialize_original_tdt(shared_world.document));
    assert(frozen_world.destroyed == controller.closed());
  }
  assert(controller.closed());
  assert(!shared_world.published);
  count_coverage(shared_world.log);
}

void popup_parity() {
  for (std::uint8_t mode = 0U; mode < 3U; ++mode) {
    std::vector<OriginalDialogSignal> script;
    for (int y = -2; y < 70; y += 3) {
      script.push_back(pointer(Kind::mouse_move, 40, y));
    }
    script.push_back(pointer(Kind::button_up, 40, 30));
    script.push_back(pointer(Kind::button_up, 90, 30));
    script.push_back(pointer(Kind::right_button_up, 40, 30));
    script.push_back(pointer(Kind::button_down, 40, 30));
    for (const auto& signal : script) {
      Log frozen_log;
      Log shared_log;
      std::int16_t hover = -1;
      OriginalElevatorPopupController controller(mode);
      RecordingOps ops(shared_log);
      // Each signal from a fresh popup and after one hovering move.
      for (const auto& first : {pointer(Kind::mouse_move, 40, 50), signal}) {
        const auto expected = frozen_popup(mode, hover, frozen_log, first);
        const auto actual = controller.signal(first, ops).result;
        assert(expected == actual);
        assert(hover == controller.hover_mode());
        assert(frozen_log == shared_log);
        if (expected) break;
      }
    }
  }
}

// ---- Simulation oracle ------------------------------------------------

class OracleIdleHost final : public OriginalIdlePassHost {
 public:
  OracleIdleHost(const OriginalResources& resources,
                 OriginalTdtDocument& document,
                 OriginalPaletteRuntime& palette, std::uint32_t& clock_ms,
                 OriginalIdleView view, bool& build_mode,
                 std::function<bool()> isolation)
      : resources_(resources),
        document_(document),
        palette_(palette),
        clock_ms_(clock_ms),
        view_(view),
        build_mode_(build_mode),
        isolation_(std::move(isolation)) {}

  std::uint32_t now_ms() override { return clock_ms_; }
  std::uint32_t coarse_tick(std::uint32_t now) override {
    return original_coarse_tick(now);
  }
  void expire_find_marker() override {}
  bool build_mode_enabled() override { return build_mode_; }
  bool elevator_finger_capture_active() override { return false; }
  bool fast_mode_enabled() override { return true; }
  bool elevator_isolation_active() override { return isolation_(); }
  bool ambient_sound_enabled() override { return false; }
  OriginalIdleView main_view() override { return view_; }
  bool step_effect_palette() override {
    return step_original_effect_palette(document_, palette_, true,
                                        original_coarse_tick(clock_ms_));
  }
  bool refresh_time_palette() override {
    return refresh_original_time_palette(resources_, document_, palette_);
  }
  bool advance_visible_people() override { return advance_people(); }
  // The Win32 synchronous rebuild_without_sky: visible people only.
  bool advance_people() {
    return step_original_visible_facility_people(document_, view_.x, view_.y,
                                                 view_.width, view_.height,
                                                 true, false)
               .dispatched_tenants != 0U;
  }
  std::uint16_t event_dialog(const OriginalEventDialogRequest&,
                             OriginalIdleDialog) override {
    return 1U;
  }
  void focus(std::int16_t, std::int16_t) override {}
  void notification_status(std::uint16_t) override {}
  void income_status(std::uint16_t) override {}
  void expire_info_status() override {}
  void play_sound(std::int32_t, std::uint16_t, std::uint16_t,
                  OriginalIdleSound) override {}
  void play_event_sounds(
      std::span<const OriginalEventSoundRequest>) override {}
  void stop_all_audio(bool) override {}
  void fire_menu_enabled(bool) override {}
  void checkpoint(OriginalIdleCheckpoint) override {}
  void idle_palette_changed() override {}
  void before_scheduler_step(bool) override {}
  void preview_only_pass() override {}
  void begin_full_frame(bool) override {}
  void bomb_ransom_paid() override {}
  void rating_promoted_before_dialog(std::uint16_t) override {}
  void rating_promoted_after_dialog(std::uint16_t) override {}
  void map_cadence_refresh() override {}
  void restore_preview() override {}
  bool render_full_frame(bool) override { return false; }
  void palette_recolor_rebuild() override {}
  void commit_full_frame_preview() override {}
  void palette_recolor_invalidate_map() override {}
  void present_main_direct() override {}
  void invalidate_command() override {}
  void present_info_direct(OriginalIdleInfoLatches& latches) override {
    latches.consume_hotel_checkout();
  }
  void final_effect_palette_changed() override {}

 private:
  const OriginalResources& resources_;
  OriginalTdtDocument& document_;
  OriginalPaletteRuntime& palette_;
  std::uint32_t& clock_ms_;
  OriginalIdleView view_;
  bool& build_mode_;
  std::function<bool()> isolation_;
};

enum class Driver : std::uint8_t { none, open_close, frozen, shared };

struct OracleRun {
  std::vector<std::uint64_t> tower{};    // per pass, after it
  std::vector<std::uint64_t> digest{};   // everything below
  std::vector<std::byte> used_before_simulate{};
  std::vector<std::byte> used_after_resume{};
  OriginalTdtElevator record_after_resume{};
  OriginalTdtElevator record_before_simulate{};
  bool frozen_while_isolated{true};
  bool transfer_empty_after_simulate{};
  bool transfer_empty_after_resume{};
  Log log{};
};

std::size_t first_standard_shaft(const OriginalTdtDocument& document) {
  for (std::size_t index = 0; index < document.elevators.size(); ++index) {
    if (document.elevators[index].used != 0U) return index;
  }
  return document.elevators.size();
}

// What the gated passes must leave alone while Simulate runs: the clock
// and every shaft (the effect palette of 1020:00cb still runs each pass).
std::uint64_t isolated_digest(const OriginalTdtDocument& document,
                              const OriginalSimulationState& simulation) {
  std::uint64_t hash = fnv_value(0xcbf29ce484222325ULL, simulation.frame_time);
  hash = fnv_value(hash, static_cast<std::uint64_t>(simulation.current_day));
  hash = fnv_value(hash, document.header.frame_time);
  for (const auto& elevator : document.elevators) {
    hash = fnv_value(hash, elevator.used);
    hash = fnv(elevator.serviced_floors, hash);
    hash = fnv(elevator.schedule, hash);
    for (const auto& car : elevator.car_records) {
      hash = fnv(car.exact_bytes, hash);
    }
    for (const auto& floor : elevator.floor_records) {
      hash = fnv(floor.exact_bytes, hash);
    }
  }
  return hash;
}

OracleRun run_oracle(const OriginalResources& resources,
                     const OriginalStartupData& startup,
                     const char* scenario, Driver driver) {
  constexpr int kWidth = 640;
  constexpr int kHeight = 400;
  constexpr int kOpenPass = 300;
  constexpr int kSimulatePass = 340;
  constexpr int kResumePass = 390;
  constexpr int kClosePass = 400;
  constexpr int kPasses = 700;
  const auto origin = original_initial_view(kWidth, kHeight);

  World world{make_original_new_tdt()};
  world.startup = &startup;
  world.alert_answers = {1};
  world.popup_answers = {2, 1};
  auto& document = world.document;
  document.header.view_x = static_cast<std::uint16_t>(origin.x);
  document.header.view_y = static_cast<std::uint16_t>(origin.y);
  carry_original_process_random_state(nullptr, document);
  reset_original_loaded_simulation_state(document);
  std::uint32_t clock_ms = 1'000'000U;
  OriginalSimulationState simulation{};
  simulation.frame_time = document.header.frame_time;
  simulation.current_day = document.header.current_day;
  simulation.day_phase = original_day_phase(simulation.frame_time);
  simulation.calendar_phase = original_calendar_phase(simulation.current_day);
  simulation.last_tick = original_coarse_tick(clock_ms);
  std::uint32_t fast_frame_tick = clock_ms - kNativeFastModeFramePeriodMs;
  OriginalPaletteRuntime palette{};
  reset_original_palette_runtime(resources, &document, palette,
                                 original_coarse_tick(clock_ms));
  (void)build_scenario(scenario, document, startup.part,
                       startup.construction_costs);

  std::optional<Frozen> frozen;
  std::optional<OriginalElevatorControlController> controller;
  const auto isolation = [&] {
    if (frozen) return frozen->state.isolation_active;
    if (controller) return controller->state().isolation_active;
    return false;
  };
  OriginalIdlePassContext context{&document,
                                  simulation,
                                  fast_frame_tick,
                                  world.transfer_visuals,
                                  startup.part,
                                  startup.rent_income,
                                  startup.maintenance_costs};
  OracleIdleHost host(resources, document, palette, clock_ms,
                      {origin.x, origin.y, kWidth, kHeight}, world.build_mode,
                      isolation);
  world.rebuild = [&] { (void)host.advance_people(); };
  const auto dtmp = parse_original_dtmp(resources.find("DTMP", 400));
  const auto deliver = [&](const OriginalDialogSignal& signal) {
    if (frozen && !world.destroyed) frozen_procedure(world, *frozen, signal);
    if (controller && !controller->closed()) {
      RecordingOps ops(world.log);
      RecordingHost elevator_host(world);
      controller->signal(signal, ops, elevator_host);
    }
  };
  const auto click_item = [&](std::size_t item) {
    const auto [x, y] = center(dtmp.rectangles[item - 1U]);
    deliver(pointer(Kind::button_down, x, y));
    deliver(pointer(Kind::button_up, x, y));
  };
  const auto click_cell = [&](std::int16_t column, std::int16_t row) {
    const auto cell = original_elevator_control_cell_rect(column, row);
    const int x = (cell.left + cell.right) / 2;
    const int y = (cell.top + cell.bottom) / 2;
    deliver(pointer(Kind::button_down, x, y));
    deliver(pointer(Kind::button_up, x, y));
  };

  OracleRun run;
  const std::size_t index = first_standard_shaft(document);
  std::uint64_t isolated_tower = 0U;
  for (int pass = 0; pass < kPasses; ++pass) {
    if (pass == kOpenPass && driver != Driver::none) {
      const auto state = make_original_elevator_control_state(
          document, index, simulation.calendar_phase, simulation.day_phase);
      assert(state.valid);
      if (driver == Driver::frozen) {
        frozen = Frozen{dtmp, state};
        frozen_initialize(world, *frozen);
      } else {
        controller.emplace(dtmp, state);
        RecordingOps ops(world.log);
        RecordingHost elevator_host(world);
        controller->disable_windows(elevator_host);
        controller->initialize_scroll_bar(ops);
      }
      if (driver == Driver::open_close) {
        click_item(1U);
      } else {
        // Schedule and service edits while the tower runs.
        click_item(8U);
        click_item(8U);
        click_item(10U);
        click_item(5U);
        click_item(26U);
        click_cell(-1, 2);   // remove floor 12's service (or add it)
        click_cell(-1, 2);   // and back
        click_cell(0, 6);    // car 0 home
        deliver(scroll(1U, 0));
      }
    }
    if (driver == Driver::frozen || driver == Driver::shared) {
      if (pass == kSimulatePass) {
        for (const auto& elevator : document.elevators) {
          run.used_before_simulate.push_back(std::byte{elevator.used});
        }
        run.record_before_simulate = document.elevators[index];
        click_item(2U);
        run.transfer_empty_after_simulate = world.transfer_visuals.empty();
        // Edits while isolated: the grid alerts, the schedule changes.
        click_cell(-1, 4);
        click_item(9U);
        isolated_tower = isolated_digest(document, simulation);
      }
      if (pass > kSimulatePass && pass < kResumePass &&
          isolated_digest(document, simulation) != isolated_tower) {
        run.frozen_while_isolated = false;
      }
      if (pass == kResumePass) {
        click_item(2U);
        run.transfer_empty_after_resume = world.transfer_visuals.empty();
        for (const auto& elevator : document.elevators) {
          run.used_after_resume.push_back(std::byte{elevator.used});
        }
        run.record_after_resume = document.elevators[index];
      }
      if (pass == kClosePass) click_item(1U);
    }
    clock_ms += kNativeFastModeFramePeriodMs;
    (void)run_original_idle_pass(context, host);
    const auto tower = fnv(serialize_original_tdt(document));
    std::uint64_t digest = fnv_value(tower, document.random_state);
    digest = fnv_value(digest, simulation.frame_time);
    digest = fnv_value(digest, static_cast<std::uint64_t>(
                                   simulation.current_day));
    digest = fnv_value(digest, simulation.last_tick);
    digest = fnv_value(digest, world.transfer_visuals.size());
    digest = fnv_value(digest, world.build_mode);
    digest = fnv_value(digest, isolation());
    run.tower.push_back(tower);
    run.digest.push_back(digest);
  }
  run.log = world.log;
  return run;
}

void oracle_tests(const OriginalResources& resources,
                  const OriginalStartupData& startup) {
  for (const char* scenario : {"busy", "full"}) {
    const auto none = run_oracle(resources, startup, scenario, Driver::none);
    const auto open_close =
        run_oracle(resources, startup, scenario, Driver::open_close);
    // Opening and closing with no edits changes nothing the pass reads.
    assert(none.digest == open_close.digest);

    const auto frozen =
        run_oracle(resources, startup, scenario, Driver::frozen);
    const auto shared =
        run_oracle(resources, startup, scenario, Driver::shared);
    assert(frozen.log == shared.log);
    assert(frozen.digest == shared.digest);
    // The edits do reach the tower.
    assert(shared.digest != none.digest);
    // Simulate gates the pass (DS:783e = 0): the clock and the shafts stay
    // as the preview left them until Resume.
    assert(shared.frozen_while_isolated);
    assert(shared.transfer_empty_after_simulate);
    assert(shared.transfer_empty_after_resume);
    // Resume restores every `used` byte and keeps the schedule / SHOW of
    // the edited record.
    assert(shared.used_after_resume == shared.used_before_simulate);
    assert(shared.record_after_resume.schedule !=
               shared.record_before_simulate.schedule ||
           shared.record_after_resume.word_3c !=
               shared.record_before_simulate.word_3c);
    std::printf("oracle %s: %zu passes, %zu procedure events\n", scenario,
                shared.digest.size(), shared.log.size());
  }
}

void presentation_tests(const OriginalTdtDocument& base) {
  auto document = base;
  const auto index = first_standard_shaft(document);
  auto state =
      make_original_elevator_control_state(document, index, 0U, 0);
  assert(state.valid);
  std::optional<OriginalTdtDocument> storage;
  // Not isolated (or no control): the live document itself.
  assert(original_elevator_control_presentation_document(document, nullptr,
                                                         storage) ==
         &document);
  assert(original_elevator_control_presentation_document(document, &state,
                                                         storage) ==
         &document);
  assert(!original_elevator_control_waiting_view(&state));
  assert(!original_elevator_control_waiting_view(nullptr));
  bool build_mode = true;
  assert(begin_original_elevator_control_isolation(document, state,
                                                   build_mode));
  assert(!build_mode);
  const auto* shown = original_elevator_control_presentation_document(
      document, &state, storage);
  assert(shown != &document && storage);
  for (std::size_t i = 0; i < shown->elevators.size(); ++i) {
    assert(shown->elevators[i].used == base.elevators[i].used);
  }
  const auto view = original_elevator_control_waiting_view(&state);
  assert(view && view->elevator_index == index &&
         view->saved_elevator == &*state.saved_elevator_record);
}

// 1098:0cf2: a phase press whose button is already up selects the phase,
// marks the document and repaints, with no ELVPOPUP and no schedule write.
void released_phase_press_tests(const OriginalResources& resources,
                                const OriginalStartupData& startup,
                                const OriginalTdtDocument& base) {
  const auto index = first_standard_shaft(base);
  const auto dtmp = parse_original_dtmp(resources.find("DTMP", 400));
  World world{base};
  world.startup = &startup;
  world.popup_answers = {2};
  world.primary_down = false;
  OriginalElevatorControlController controller(
      dtmp, make_original_elevator_control_state(base, index, 0U, 0));
  RecordingOps ops(world.log);
  RecordingHost host(world);
  const auto before = serialize_original_tdt(world.document);
  const auto [x, y] = center(dtmp.rectangles[24]);
  controller.signal(pointer(Kind::button_down, x, y), ops, host);
  assert(controller.pressed_item() == 25);
  assert(controller.state().day_phase == 2U);
  assert(serialize_original_tdt(world.document) == before);
  assert((world.log == Log{"dirty", "invalidate", "update"}));
  controller.signal(pointer(Kind::button_up, x, y), ops, host);
  // Still down: the popup runs and its answer is written.
  world.primary_down = true;
  world.log.clear();
  const auto [x2, y2] = center(dtmp.rectangles[25]);
  (void)controller.signal(pointer(Kind::button_down, x2, y2), ops, host);
  assert(!world.log.empty() && world.log.front().rfind("popup 26", 0) == 0);
  assert(controller.state().day_phase == 3U);
}

void painter_tests(const OriginalResources& resources,
                   const OriginalTdtDocument& document) {
  const auto index = first_standard_shaft(document);
  OriginalElevatorControlController controller(
      parse_original_dtmp(resources.find("DTMP", 400)),
      make_original_elevator_control_state(document, index, 0U, 0));
  const auto paint = [&](const OriginalTdtDocument* shown) {
    OriginalWorldRaster raster{};
    raster.width = kOriginalElevatorControlClientWidth;
    raster.height = kOriginalElevatorControlClientHeight;
    raster.pixels.assign(static_cast<std::size_t>(raster.width) *
                             raster.height,
                         0x123456U);
    OriginalCanvasDrawContext context(raster, OriginalCanvasFont::bundled(),
                                      0, 0, {0, 0, raster.width,
                                             raster.height});
    paint_original_elevator_control(context, resources, controller, shown);
    return raster;
  };
  const auto empty = paint(nullptr);
  const auto full = paint(&document);
  // BITMAP/400 covers the whole client; the grid adds to it.
  assert(std::find(empty.pixels.begin(), empty.pixels.end(), 0x123456U) ==
         empty.pixels.end());
  assert(empty.pixels != full.pixels);
  assert(paint(&document).pixels == full.pixels);
  // The grid frame (DTMP 6) is black on its top-left corner.
  const auto& grid = *controller.rectangle(6U);
  assert(full.pixels[static_cast<std::size_t>(grid.top) * full.width +
                     grid.left] == 0x000000U);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 77;
  const auto pack = read_file(argv[1]);
  if (!pack) {
    std::printf("skipped: no resource pack at %s\n", argv[1]);
    return 77;
  }
  const OriginalResources resources{std::span<const std::byte>(*pack)};
  const auto startup = load_original_startup_data(resources);

  popup_parity();
  for (const char* scenario : {"busy", "full"}) {
    auto document = make_original_new_tdt();
    (void)build_scenario(scenario, document, startup.part,
                         startup.construction_costs);
    std::size_t cases = 0U;
    for (std::size_t index = 0; index < document.elevators.size(); ++index) {
      if (document.elevators[index].used == 0U) continue;
      for (const std::uint8_t calendar : {0U, 1U}) {
        for (const std::int8_t phase : {0, 3, 6}) {
          parity_case(resources, startup, document, index, calendar, phase);
          ++cases;
        }
      }
    }
    assert(cases != 0U);
    std::printf("parity %s: %zu cases\n", scenario, cases);
    presentation_tests(document);
    painter_tests(resources, document);
    released_phase_press_tests(resources, startup, document);
  }
  for (const auto& [token, count] : g_coverage) {
    std::printf("  covered %-20s %zu\n", token.c_str(), count);
  }
  for (const char* required :
       {"alert 1000 1004/1", "popup", "dispatches", "sound 6002", "rebuild",
        "clear transfer", "pos 7", "range 7", "enable 7 1", "enable 7 0",
        "dirty", "destroy"}) {
    assert(g_coverage[required] != 0U);
  }
  oracle_tests(resources, startup);
  std::printf("elevator control controller tests passed\n");
  return 0;
}
