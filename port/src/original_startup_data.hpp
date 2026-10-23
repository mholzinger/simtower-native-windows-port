#pragma once

// Portable half of the Win32 host's validate_original_startup_data
// (1128:0ba3 / 1178:000c / 1140:0005). Both hosts call it so that a
// truncated or mismatched resource pack fails at startup, before any
// window or simulation state exists.

#include "original_command_palette.hpp"
#include "original_resources.hpp"
#include "original_tables.hpp"

#include <array>

namespace simtower {

struct OriginalStartupData {
  OriginalPartTable part{};
  OriginalYenTable construction_costs{};  // YEN/1000
  OriginalYenTable rent_income{};         // YEN/1001
  OriginalYenTable maintenance_costs{};   // YEN/1002
  std::array<OriginalCommandRatingState, 6> command_ratings{};
};

// Loads the immutable startup tables and parses every startup-validated
// resource (TABL 1001-1006, the startup BITMAP, DIALOG and DTMP sets, the
// STRL presence checks and the CLUT/CGPK size checks). Throws on the first
// missing, malformed or truncated resource.
[[nodiscard]] OriginalStartupData load_original_startup_data(
    const OriginalResources& resources);

}  // namespace simtower
