#pragma once

// Test support shared by simtower_sdl (--scenario, --direct-build) and the
// portable tests (idle_pass_tests, world_input_tests). It depends only on
// the portable core, never on SDL or Win32.
//
// Deterministic test towers for headless regression runs (--scenario).
// Built only through the portable construction functions, each followed by
// the host's common success tail (1148:0163 rating callback, then the
// literal 11f8:0ed8 route rebuild), as native_main.cpp runs after a
// successful click. Nothing here is original game behavior: it only stages
// a busy tower so the idle pass exercises people, elevators and events.

#include "original_tables.hpp"
#include "original_tdt.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <string_view>

namespace simtower {

struct ScenarioSummary {
  // Successful construction commands by facility/command type.
  std::map<int, std::size_t> built_by_type{};
  std::size_t elevator_cars{};
  std::size_t shaft_extensions{};
  // DIALOG requests from the 1148:0163 callback, logged instead of shown.
  std::size_t rating_dialogs{};
  std::int32_t balance_after{};
};

// Known names: "busy", "full". "none" is the plain New tower and is handled
// by the caller. Throws std::runtime_error naming the first failed command.
ScenarioSummary build_scenario(std::string_view name,
                               OriginalTdtDocument& document,
                               const OriginalPartTable& part,
                               const OriginalYenTable& construction_costs);

// --direct-build TYPE,FLOOR,LEFT[,VARIANT][,shift]: the constructor
// 11f8:07d8 reaches for one placement, written from the constructor table
// and independent of the shared input module, so a scripted click can be
// compared with it. Floor and Lobby take a one-cell or four-cell span from
// LEFT; Shift runs 11f8:09c1's replacement demolition over the footprint
// first. The common success tail (1148:0163, then 0ed8) follows a success.
struct DirectBuild {
  std::uint16_t type{};
  std::int16_t floor{};
  std::uint16_t left{};
  std::uint8_t variant{};
  bool shift{};
};

// Returns whether the constructor succeeded. Throws std::runtime_error for
// a type without a single-placement constructor.
bool apply_direct_build(OriginalTdtDocument& document,
                        const OriginalPartTable& part,
                        const OriginalYenTable& construction_costs,
                        const OriginalYenTable& rent_income,
                        const DirectBuild& build);

// Human-readable name for a facility/command type in summaries.
[[nodiscard]] std::string scenario_type_name(int type);

}  // namespace simtower
