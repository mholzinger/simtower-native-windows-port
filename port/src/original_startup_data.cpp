#include "original_startup_data.hpp"

#include "original_dialog.hpp"
#include "original_dib.hpp"
#include "original_dtmp.hpp"
#include "original_startup.hpp"
#include "original_world.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace simtower {

OriginalStartupData load_original_startup_data(
    const OriginalResources& resources) {
  // 1128:0ba3 invokes 11a0:134c's floor-offset precompute and 11f8:033a's
  // world/facility sheet pack. Native transforms and embedded resource views
  // perform the same work directly, so validation touches their inputs here.
  for (const auto action : original_startup_precompute_plan()) {
    switch (action) {
      case OriginalStartupPrecomputeAction::precompute_floor_offsets:
        for (std::int32_t floor = 0; floor < 60; ++floor) {
          (void)original_precomputed_floor_offset(floor, 1);
        }
        break;
      case OriginalStartupPrecomputeAction::pack_world_and_facility_sheets:
        // Native rendering samples the immutable embedded DIBs directly; the
        // validation pass below opens those same sheet-pack inputs in this
        // recovered second phase.
        break;
    }
  }

  OriginalStartupData data{};
  // Exact 1178:000c loads construction, rent-income, and maintenance tables
  // from YEN/1000, YEN/1001, and YEN/1002 respectively.
  data.part = original_part_table(resources.find("PART", 1000));
  data.construction_costs = original_yen_table(resources.find("YEN", 1000));
  data.rent_income = original_yen_table(resources.find("YEN", 1001));
  data.maintenance_costs = original_yen_table(resources.find("YEN", 1002));
  // 1140:0005 initializes rating one and loads TABL/1001; preload all six
  // immutable native rating states so promotion only changes the active view.
  for (int rating = 1001; rating <= 1006; ++rating) {
    (void)original_word_table(resources.find("TABL", rating));
    data.command_ratings[static_cast<std::size_t>(rating - 1001)] =
        original_command_rating_state(
            resources, static_cast<std::uint16_t>(rating - 1000));
  }

  const auto bitmap = [&resources](int id) {
    (void)original_dib_view(resources.find("BITMAP", id));
  };
  for (int id = 600; id <= 606; ++id) bitmap(id);
  bitmap(320);
  bitmap(352);
  for (int id = 850; id <= 857; ++id) bitmap(id);
  bitmap(905);
  for (int id = 400; id <= 408; ++id) bitmap(id);
  for (int id = 410; id <= 411; ++id) bitmap(id);
  for (int id = 20256; id <= 20265; ++id) bitmap(id);

  const auto dialog = [&resources](int id) {
    (void)parse_original_dialog(resources.find("DIALOG", id));
  };
  const auto dialog_and_dtmp = [&resources, &dialog](int id) {
    dialog(id);
    (void)parse_original_dtmp(resources.find("DTMP", id));
  };
  dialog(124);
  dialog_and_dtmp(400);
  for (int id = 730; id <= 732; ++id) dialog_and_dtmp(id);
  for (int id = 748; id <= 764; ++id) dialog_and_dtmp(id);
  for (const int strings : {420, 710, 712, 713, 714, 715, 716}) {
    if (resources.find("STRL", strings).empty()) {
      throw std::runtime_error(
          "Original Facility Information strings are missing");
    }
  }
  for (int id = 3030; id <= 3034; ++id) dialog_and_dtmp(id);
  dialog_and_dtmp(3040);

  if (resources.find("CLUT", 1000).size() < 2048U ||
      resources.find("CGPK", 2536).size() < 123U * 288U ||
      resources.find("CGPK", 2600).size() < 82U * 288U ||
      resources.find("CGPK", 2664).size() < 41U * 288U) {
    throw std::runtime_error("Original world graphics resources are truncated");
  }
  return data;
}

}  // namespace simtower
