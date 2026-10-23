#pragma once

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

namespace simtower::sdl {

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

// Human-readable name for a facility/command type in summaries.
[[nodiscard]] std::string scenario_type_name(int type);

}  // namespace simtower::sdl
