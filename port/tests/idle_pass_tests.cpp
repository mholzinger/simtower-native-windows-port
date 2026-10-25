// Locks the host-hook call order of the shared idle pass
// (run_original_idle_pass and the hidden-menu event boundaries). Both the
// Win32 host (native_main.cpp) and the SDL session implement
// OriginalIdlePassHost, so the order checked here is the order every host
// sees: clock reads, palette steps, dialogs (whose answers feed back into
// game state), focus, sounds, checkpoints and the presentation tail.
//
// A recording host drives a staged scenario tower for a fixed number of
// passes on a virtual clock, then:
// - checks per-pass structural invariants taken from 1258:023a and
//   1090:03ab(1) (checkpoint order, render/time-palette/tail placement,
//   dialog placement);
// - checks the run reached dialogs, focus, sounds and every checkpoint;
// - compares a hash of the whole trace with a pinned value.
//
// A pinned-hash failure means the hook sequence changed. If that change is
// intended (a corrected translation), pass a third argument to write the
// trace, diff it against the previous build's trace, and update the pin.
//
// Usage: simtower_idle_pass_tests <original_resources.pack> [trace-dir]

#include "test_scenario.hpp"

#include "original_construction.hpp"
#include "original_idle_pass.hpp"
#include "original_resources.hpp"
#include "original_simulation.hpp"
#include "original_startup_data.hpp"
#include "original_tdt.hpp"
#include "original_time.hpp"
#include "original_world.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace simtower;

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

const char* dialog_name(OriginalIdleDialog kind) {
  switch (kind) {
    case OriginalIdleDialog::bomb_offer: return "bomb_offer";
    case OriginalIdleDialog::bomb_followup: return "bomb_followup";
    case OriginalIdleDialog::bomb_event: return "bomb_event";
    case OriginalIdleDialog::fire_offer: return "fire_offer";
    case OriginalIdleDialog::fire_event: return "fire_event";
    case OriginalIdleDialog::fire_crew_offer: return "fire_crew_offer";
    case OriginalIdleDialog::fire_crew_followup: return "fire_crew_followup";
    case OriginalIdleDialog::hotel: return "hotel";
    case OriginalIdleDialog::rating: return "rating";
  }
  return "unknown";
}

// Every hook appends one token. Queries are recorded too: the pass must ask
// at the original point (for example DS:783e after the Find toggle).
class RecordingHost final : public OriginalIdlePassHost {
 public:
  RecordingHost(const OriginalResources& resources,
                OriginalTdtDocument& document,
                OriginalPaletteRuntime& palette,
                std::uint32_t& clock_ms,
                OriginalIdleView view)
      : resources_(resources),
        document_(document),
        palette_(palette),
        clock_ms_(clock_ms),
        view_(view) {}

  std::vector<std::string> trace;

  void mark(std::string token) { trace.push_back(std::move(token)); }

  std::uint32_t now_ms() override {
    mark("now");
    return clock_ms_;
  }
  std::uint32_t coarse_tick(std::uint32_t now_ms) override {
    mark("tick");
    return original_coarse_tick(now_ms);
  }
  void expire_find_marker() override { mark("find"); }
  bool build_mode_enabled() override {
    mark("build?");
    return true;
  }
  bool elevator_finger_capture_active() override {
    mark("finger?");
    return false;
  }
  bool fast_mode_enabled() override {
    mark("fast?");
    return true;
  }
  bool elevator_isolation_active() override {
    mark("isolation?");
    return false;
  }
  bool ambient_sound_enabled() override {
    mark("ambient?");
    return true;
  }
  OriginalIdleView main_view() override {
    mark("view");
    return view_;
  }
  bool step_effect_palette() override {
    const bool changed = step_original_effect_palette(
        document_, palette_, true, original_coarse_tick(clock_ms_));
    mark(changed ? "fx:1" : "fx:0");
    return changed;
  }
  bool refresh_time_palette() override {
    const bool changed =
        refresh_original_time_palette(resources_, document_, palette_);
    mark(changed ? "time:1" : "time:0");
    return changed;
  }
  bool advance_visible_people() override {
    mark("visible_people");
    return false;
  }
  std::uint16_t event_dialog(const OriginalEventDialogRequest& dialog,
                             OriginalIdleDialog kind) override {
    // The SDL host's answers: search for the bomb (no ransom), decline the
    // fire crew, acknowledge everything else.
    mark(std::string("dialog:") + dialog_name(kind) + ":" +
         std::to_string(dialog.dialog_id) + ":" +
         std::to_string(dialog.argument) + ":" +
         std::to_string(dialog.wave_resource));
    return 1U;
  }
  void focus(std::int16_t floor, std::int16_t x) override {
    mark("focus:" + std::to_string(floor) + "," + std::to_string(x));
  }
  void notification_status(std::uint16_t code) override {
    mark("notify:" + std::to_string(code));
  }
  void income_status(std::uint16_t code) override {
    mark("income:" + std::to_string(code));
  }
  void expire_info_status() override { mark("expire_info"); }
  void play_sound(std::int32_t resource,
                  std::uint16_t repeat,
                  std::uint16_t priority,
                  OriginalIdleSound source) override {
    mark("sound:" + std::to_string(resource) + ":" + std::to_string(repeat) +
         ":" + std::to_string(priority) + ":" +
         std::to_string(static_cast<int>(source)));
  }
  void play_event_sounds(
      std::span<const OriginalEventSoundRequest> requests) override {
    std::string token = "event_sounds";
    for (const auto& request : requests) {
      token += ":" + std::to_string(request.resource) + "/" +
               std::to_string(request.repeat) + "/" +
               std::to_string(request.priority) + "/" +
               std::to_string(request.reserved_if_idle ? 1 : 0);
    }
    mark(std::move(token));
  }
  void stop_all_audio(bool force) override {
    mark(force ? "stop_audio:1" : "stop_audio:0");
  }
  void fire_menu_enabled(bool enabled) override {
    mark(enabled ? "fire_menu:1" : "fire_menu:0");
  }
  void checkpoint(OriginalIdleCheckpoint point) override {
    mark("chk:" + std::to_string(static_cast<int>(point)));
  }
  void idle_palette_changed() override { mark("idle_palette"); }
  void before_scheduler_step(bool sample_cursor) override {
    mark(sample_cursor ? "before_step:1" : "before_step:0");
  }
  void preview_only_pass() override { mark("preview_only"); }
  void begin_full_frame(bool day_changed) override {
    mark(day_changed ? "begin:1" : "begin:0");
  }
  void bomb_ransom_paid() override { mark("ransom"); }
  void rating_promoted_before_dialog(std::uint16_t rating) override {
    mark("rating_before:" + std::to_string(rating));
  }
  void rating_promoted_after_dialog(std::uint16_t rating) override {
    mark("rating_after:" + std::to_string(rating));
  }
  void map_cadence_refresh() override { mark("map_cadence"); }
  void restore_preview() override { mark("restore_preview"); }
  bool render_full_frame(bool renderer_surface_dirty) override {
    mark(renderer_surface_dirty ? "render:1" : "render:0");
    return false;
  }
  void palette_recolor_rebuild() override { mark("recolor"); }
  void commit_full_frame_preview() override { mark("commit_preview"); }
  void palette_recolor_invalidate_map() override { mark("recolor_map"); }
  void present_main_direct() override { mark("present_main"); }
  void invalidate_command() override { mark("invalidate_command"); }
  void present_info_direct(OriginalIdleInfoLatches& latches) override {
    mark("info");
    latches.consume_hotel_checkout();
  }
  void final_effect_palette_changed() override { mark("final_fx"); }

 private:
  const OriginalResources& resources_;
  OriginalTdtDocument& document_;
  OriginalPaletteRuntime& palette_;
  std::uint32_t& clock_ms_;
  OriginalIdleView view_;
};

std::uint64_t fnv1a(const std::vector<std::string>& trace) {
  std::uint64_t hash = 0xcbf29ce484222325ULL;
  for (const auto& token : trace) {
    for (const char c : token) {
      hash ^= static_cast<unsigned char>(c);
      hash *= 0x100000001b3ULL;
    }
    hash ^= static_cast<unsigned char>('\n');
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

bool starts_with(const std::string& text, const char* prefix) {
  return text.rfind(prefix, 0) == 0;
}

std::string checkpoint_token(OriginalIdleCheckpoint point) {
  return "chk:" + std::to_string(static_cast<int>(point));
}

// Index of the first token equal to `token` in [begin, end), or end.
std::size_t find_token(const std::vector<std::string>& trace,
                       std::size_t begin,
                       std::size_t end,
                       const std::string& token) {
  for (std::size_t index = begin; index < end; ++index) {
    if (trace[index] == token) return index;
  }
  return end;
}

std::size_t count_token(const std::vector<std::string>& trace,
                        std::size_t begin,
                        std::size_t end,
                        const std::string& token) {
  return static_cast<std::size_t>(
      std::count(trace.begin() + static_cast<std::ptrdiff_t>(begin),
                 trace.begin() + static_cast<std::ptrdiff_t>(end), token));
}

// One idle pass's tokens [begin, end) and its outcome.
struct PassSpan {
  std::size_t begin{};
  std::size_t end{};
  OriginalIdlePassOutcome outcome{};
};

void check_pass(const std::vector<std::string>& trace, const PassSpan& pass) {
  const auto begin = pass.begin;
  const auto end = pass.end;
  assert(end > begin);
  // 1258:023a: the effects palette step (and its repaint), then the
  // 10e0:051d Find timeout, then the DS:783e gate read.
  std::size_t index = begin;
  assert(starts_with(trace[index], "fx:"));
  if (trace[index] == "fx:1") {
    ++index;
    assert(trace[index] == "idle_palette");
  }
  ++index;
  assert(trace[index] == "find");
  assert(trace[index + 1] == "finger?");
  assert(trace[index + 2] == "build?");

  const auto first_checkpoint = std::find_if(
      trace.begin() + static_cast<std::ptrdiff_t>(begin),
      trace.begin() + static_cast<std::ptrdiff_t>(end),
      [](const std::string& token) { return starts_with(token, "chk:"); });
  if (pass.outcome != OriginalIdlePassOutcome::full_frame) {
    assert(first_checkpoint ==
           trace.begin() + static_cast<std::ptrdiff_t>(end));
    assert(find_token(trace, begin, end, "render:0") == end &&
           find_token(trace, begin, end, "render:1") == end);
    return;
  }

  // 1090:03ab(1) checkpoint order; the Elevator checkpoint repeats once per
  // used Elevator.
  using C = OriginalIdleCheckpoint;
  const C expected_order[] = {C::scheduler_tail, C::construction,
                              C::rating,         C::first_effect_palette,
                              C::people,         C::map_cadence,
                              C::preview_restore};
  std::vector<std::size_t> checkpoints;
  for (std::size_t at = begin; at < end; ++at) {
    if (starts_with(trace[at], "chk:")) checkpoints.push_back(at);
  }
  std::size_t next = 0;
  for (const auto point : expected_order) {
    assert(next < checkpoints.size());
    assert(trace[checkpoints[next]] == checkpoint_token(point));
    ++next;
  }
  while (next < checkpoints.size() &&
         trace[checkpoints[next]] == checkpoint_token(C::elevator)) {
    ++next;
  }
  for (const auto point :
       {C::vertical_transport, C::time_palette, C::final_frame}) {
    assert(next < checkpoints.size());
    assert(trace[checkpoints[next]] == checkpoint_token(point));
    ++next;
  }
  assert(next == checkpoints.size());

  const auto at = [&](C point) {
    return find_token(trace, begin, end, checkpoint_token(point));
  };
  const auto begin_frame = std::min(find_token(trace, begin, end, "begin:0"),
                                    find_token(trace, begin, end, "begin:1"));
  assert(begin_frame < at(C::scheduler_tail));
  // The scheduler's own 1208:05e6 tail sample precedes the full frame.
  const auto last_tick_before_frame = [&] {
    std::size_t last = end;
    for (std::size_t i = begin_frame; i < at(C::scheduler_tail); ++i) {
      if (trace[i] == "tick") last = i;
    }
    return last;
  }();
  assert(last_tick_before_frame != end);

  // 1090:0580/05ee renderer, then 1020:098b, then the third 1020:00cb.
  const auto render =
      std::min(find_token(trace, begin, end, "render:0"),
               find_token(trace, begin, end, "render:1"));
  assert(render != end);
  assert(count_token(trace, begin, end, "render:0") +
             count_token(trace, begin, end, "render:1") ==
         1U);
  assert(at(C::vertical_transport) < render);
  assert(trace[render - 1] == "visible_people");
  assert(starts_with(trace[render + 1], "time:"));
  assert(starts_with(trace[render + 2], "fx:"));
  assert(render < at(C::time_palette));

  // Mirror resync and presentation tail after the time palette.
  const auto commit = find_token(trace, begin, end, "commit_preview");
  const auto expire = find_token(trace, begin, end, "expire_info");
  assert(at(C::time_palette) < commit);
  assert(commit < expire);
  assert(expire < at(C::final_frame));
  assert(count_token(trace, begin, end, "expire_info") == 1U);
  assert(trace[expire + 1] == "fx:0" || trace[expire + 1] == "fx:1");

  // Dialog placement: rating modals sit between the construction and rating
  // checkpoints, the fire-crew offer between the rating and first effects
  // checkpoints, and each promotion brackets its modal.
  for (std::size_t i = begin; i < end; ++i) {
    if (starts_with(trace[i], "dialog:rating:")) {
      assert(at(C::construction) < i && i < at(C::rating));
      assert(starts_with(trace[i - 1], "rating_before:"));
      assert(starts_with(trace[i + 1], "rating_after:"));
    }
    if (starts_with(trace[i], "dialog:fire_crew_offer:")) {
      assert(at(C::rating) < i && i < at(C::first_effect_palette));
    }
  }
}

struct EventAt {
  int pass{};
  bool bomb{};
};

struct RunResult {
  std::vector<std::string> trace;
  std::vector<PassSpan> passes;
};

RunResult run_scenario(const OriginalResources& resources,
                       const OriginalStartupData& startup,
                       const char* scenario,
                       int pass_count,
                       const std::vector<EventAt>& events) {
  // Session::new_tower + install_document on a fresh process.
  constexpr int kClientWidth = 640;
  constexpr int kClientHeight = 400;
  const auto origin = original_initial_view(kClientWidth, kClientHeight);
  auto document = make_original_new_tdt();
  document.header.view_x = static_cast<std::uint16_t>(origin.x);
  document.header.view_y = static_cast<std::uint16_t>(origin.y);
  carry_original_process_random_state(nullptr, document);
  reset_original_loaded_simulation_state(document);

  std::uint32_t clock_ms = 1'000'000U;
  OriginalSimulationState simulation{};
  simulation.frame_time = document.header.frame_time;
  simulation.current_day = document.header.current_day;
  simulation.day_phase = original_day_phase(simulation.frame_time);
  simulation.calendar_phase =
      original_calendar_phase(simulation.current_day);
  simulation.last_tick = original_coarse_tick(clock_ms);
  std::uint32_t fast_frame_tick = clock_ms - kNativeFastModeFramePeriodMs;
  OriginalPaletteRuntime palette{};
  reset_original_palette_runtime(resources, &document, palette,
                                 original_coarse_tick(clock_ms));
  (void)build_scenario(scenario, document, startup.part,
                            startup.construction_costs);

  std::vector<OriginalElevatorTransferVisual> transfer_visuals;
  OriginalIdlePassContext context{&document,
                                  simulation,
                                  fast_frame_tick,
                                  transfer_visuals,
                                  startup.part,
                                  startup.rent_income,
                                  startup.maintenance_costs};
  RecordingHost host(resources, document, palette, clock_ms,
                     {origin.x, origin.y, kClientWidth, kClientHeight});
  RunResult result;
  for (int pass = 0; pass < pass_count; ++pass) {
    for (const auto& event : events) {
      if (event.pass != pass) continue;
      host.mark(event.bomb ? "menu:bomb" : "menu:fire");
      const bool started =
          event.bomb ? run_original_bomb_offer_boundary(context, host)
                     : run_original_fire_offer_boundary(context, host);
      host.mark(started ? "menu:started" : "menu:not_offered");
    }
    clock_ms += kNativeFastModeFramePeriodMs;
    host.mark("pass:" + std::to_string(pass));
    const auto begin = host.trace.size();
    const auto outcome = run_original_idle_pass(context, host);
    result.passes.push_back({begin, host.trace.size(), outcome});
  }
  result.trace = std::move(host.trace);
  return result;
}

std::size_t count_prefix(const std::vector<std::string>& trace,
                         const char* prefix) {
  return static_cast<std::size_t>(
      std::count_if(trace.begin(), trace.end(),
                    [prefix](const std::string& token) {
                      return starts_with(token, prefix);
                    }));
}

struct Case {
  const char* name;
  const char* scenario;
  int passes;
  std::vector<EventAt> events;
  // Pinned FNV-1a 64 of the trace; see the file comment before changing.
  std::uint64_t pinned_hash;
  // Token prefixes the run must reach.
  std::vector<const char*> required_tokens;
};

bool run_case(const OriginalResources& resources,
              const OriginalStartupData& startup,
              const Case& test_case,
              const std::optional<std::filesystem::path>& trace_dir) {
  const auto result = run_scenario(resources, startup, test_case.scenario,
                                   test_case.passes, test_case.events);
  const auto& trace = result.trace;
  // Written first so a failed check below can still be diffed.
  if (trace_dir) {
    const auto path =
        *trace_dir / (std::string("idle-pass-") + test_case.name + ".txt");
    std::ofstream out(path);
    for (const auto& token : trace) out << token << '\n';
  }
  std::size_t full_frames = 0;
  for (const auto& pass : result.passes) {
    check_pass(trace, pass);
    if (pass.outcome == OriginalIdlePassOutcome::full_frame) ++full_frames;
  }
  // Fast Mode on a virtual clock: every pass is a full frame.
  assert(full_frames == result.passes.size());
  for (int point = 0;
       point <= static_cast<int>(OriginalIdleCheckpoint::final_frame);
       ++point) {
    assert(count_prefix(trace, ("chk:" + std::to_string(point)).c_str()) >
           0U);
  }
  for (const auto* token : test_case.required_tokens) {
    if (count_prefix(trace, token) == 0U) {
      std::fprintf(stderr, "%s: no '%s' in the trace\n", test_case.name,
                   token);
      assert(false);
    }
  }

  const auto hash = fnv1a(trace);
  std::printf("%s: %zu passes, %zu hook calls, trace %016llx\n",
              test_case.name, result.passes.size(), trace.size(),
              static_cast<unsigned long long>(hash));
  if (hash != test_case.pinned_hash) {
    std::fprintf(stderr,
                 "%s: hook trace changed: %016llx, pinned %016llx\n",
                 test_case.name, static_cast<unsigned long long>(hash),
                 static_cast<unsigned long long>(test_case.pinned_hash));
    for (const char* prefix :
         {"dialog:", "focus:", "sound:", "event_sounds", "notify:", "income:",
          "fire_menu:", "rating_before:", "map_cadence", "recolor",
          "present_main", "invalidate_command", "info", "final_fx"}) {
      std::fprintf(stderr, "  %-20s %zu\n", prefix,
                   count_prefix(trace, prefix));
    }
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  assert(argc >= 2);
  const auto pack = read_file(argv[1]);
  const OriginalResources resources{std::span<const std::byte>(pack)};
  const auto startup = load_original_startup_data(resources);
  std::optional<std::filesystem::path> trace_dir;
  if (argc >= 3) trace_dir = argv[2];

  const Case cases[] = {
      // Bomb offer (searched), its follow-up and progression, rating
      // promotions, Hotel checkout and ambient/elevator sounds.
      {"busy-bomb",
       "busy",
       3000,
       {{100, true}},
       // Re-pinned for development PR #2 (DS:779e balance-changed latch):
       // 67 added sound:10013 cash-sound calls, nothing else changed.
       0x3283928cc2fd2d4eULL,
       {"dialog:bomb_offer:", "dialog:bomb_followup:", "dialog:bomb_event:",
        "dialog:rating:", "focus:", "sound:", "rating_before:"}},
      // Fire offer, declined fire crew, its follow-up and the 2000 finish.
      {"busy-fire",
       "busy",
       1200,
       {{300, false}},
       // Re-pinned for development PR #2 (DS:779e balance-changed latch):
       // 68 added sound:10013 cash-sound calls, nothing else changed.
       0x98b0df3d5f47ce13ULL,
       {"dialog:fire_offer:", "dialog:fire_crew_offer:",
        "dialog:fire_crew_followup:", "dialog:fire_event:", "fire_menu:"}},
  };
  bool ok = true;
  for (const auto& test_case : cases) {
    ok = run_case(resources, startup, test_case, trace_dir) && ok;
  }
  return ok ? 0 : 1;
}
