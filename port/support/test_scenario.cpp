#include "test_scenario.hpp"

#include "original_construction.hpp"
#include "original_simulation.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace simtower {
namespace {

// Runtime floor 10 is the ground lobby, 11+ above ground, 9..0 basement.
constexpr std::int16_t kGround = 10;
constexpr std::int16_t kTop = 24;  // 15 storeys including the lobby.
constexpr std::uint16_t kLeft = 100;
constexpr std::uint16_t kRight = 380;
// Two standard shafts at the outer edges, a stair column next to the left
// shaft (outside 10a0:10e8's eight-cell shaft clearance, so the shaft can
// still be extended later), and the facility band between them.
constexpr std::uint16_t kShaftA = 104;
constexpr std::uint16_t kShaftB = 372;
constexpr std::uint16_t kStairColumn = 118;
constexpr std::uint16_t kBandLeft = 128;
constexpr std::uint16_t kBandRight = 366;
constexpr std::int32_t kStartingBalance = 50'000'000;

class Builder {
 public:
  Builder(OriginalTdtDocument& document,
          const OriginalPartTable& part,
          const OriginalYenTable& costs,
          ScenarioSummary& summary)
      : document_(document), part_(part), costs_(costs), summary_(summary) {}

  void lobby() {
    commit("lobby", kGround, kLeft, 24U,
           build_original_initial_lobby(document_, kLeft, kRight, 1U,
                                        costs_));
  }

  void floor(std::int16_t floor) {
    commit("floor", floor, kLeft, 0U,
           build_original_floor(document_, floor, kLeft, kRight, costs_));
  }

  // Returns the new shaft's elevator index.
  std::size_t shaft(std::uint16_t x, std::int16_t bottom, std::int16_t top,
                    std::size_t cars) {
    const auto result =
        build_original_standard_elevator(document_, bottom, x, costs_, part_);
    if (!result.new_elevator_shaft_created) {
      fail("elevator shaft", bottom, x, result.status,
           result.construction_status_code);
    }
    commit("elevator shaft", bottom, x, 1U, result);
    const auto index = find_shaft(x);
    extend(index, top);
    // Extra cars spread their home floors across the shaft.
    for (std::size_t car = 1; car < cars; ++car) {
      const auto home = static_cast<std::int16_t>(
          bottom + static_cast<int>(car) * (top - bottom) /
                       static_cast<int>(cars));
      add_car(x, home);
    }
    return index;
  }

  void extend(std::size_t index, std::int16_t target) {
    const auto result =
        extend_original_elevator_shaft(document_, index, target, costs_);
    if (!result.succeeded()) {
      fail("elevator extension", target, document_.elevators[index].x,
           result.status, 0U);
    }
    ++summary_.shaft_extensions;
  }

  void add_car(std::uint16_t x, std::int16_t home) {
    const auto result =
        build_original_standard_elevator(document_, home, x, costs_, part_);
    if (result.new_elevator_shaft_created) {
      fail("elevator car (made a new shaft)", home, x, result.status, 0U);
    }
    commit("elevator car", home, x, 1U, result);
    ++summary_.elevator_cars;
  }

  void transport(std::uint8_t type, std::int16_t floor, std::uint16_t x) {
    commit(type == 22U ? "stairs" : "escalator", floor, x, type,
           build_original_vertical_transport(document_, type, floor, x,
                                             costs_));
  }

  // Places facilities of one type left to right from `left`; returns the
  // next free cell.
  std::uint16_t row(std::uint8_t type, std::int16_t floor, std::uint16_t left,
                    std::size_t count) {
    const auto width = original_facility_width_cells(type);
    for (std::size_t index = 0; index < count; ++index) {
      if (left + width > kBandRight) {
        fail("row overflows the facility band", floor, left,
             OriginalConstructionStatus::invalid_span, 0U);
      }
      place(type, floor, left);
      left = static_cast<std::uint16_t>(left + width);
    }
    return left;
  }

  void place(std::uint8_t type, std::int16_t floor, std::uint16_t left) {
    const auto variant = [this, type](std::uint8_t cycle) {
      return static_cast<std::uint8_t>(variants_[type]++ % cycle);
    };
    OriginalConstructionResult result{};
    switch (type) {
      case 3: result = build_original_hotel_room(document_, 3, floor, left,
                                                 variant(2), costs_); break;
      case 4: result = build_original_hotel_room(document_, 4, floor, left,
                                                 variant(4), costs_); break;
      case 5: result = build_original_hotel_room(document_, 5, floor, left,
                                                 variant(2), costs_); break;
      case 6: result = build_original_restaurant(document_, floor, left,
                                                 costs_); break;
      case 7: result = build_original_office(document_, floor, left,
                                             variant(6), costs_); break;
      case 9: result = build_original_condo(document_, floor, left,
                                            variant(3), costs_); break;
      case 10: result = build_original_retail_shop(document_, floor, left,
                                                   costs_); break;
      case 11: result = build_original_parking(document_, floor, left,
                                               costs_); break;
      case 12: result = build_original_fast_food(document_, floor, left,
                                                 costs_); break;
      case 13: result = build_original_medical_center(document_, floor, left,
                                                      variant(3), costs_);
        break;
      case 14: result = build_original_security(document_, floor, left,
                                                costs_); break;
      case 15: result = build_original_housekeeping(document_, floor, left,
                                                    costs_); break;
      case 18: result = build_original_movie_theater(document_, floor, left,
                                                     costs_); break;
      case 20: result = build_original_recycling_center(document_, floor,
                                                        left, costs_); break;
      case 29: result = build_original_party_hall(document_, floor, left,
                                                  costs_); break;
      case 31: result = build_original_metro_station(document_, floor, left,
                                                     costs_); break;
      case 44: result = build_original_parking_ramp(document_, floor, left,
                                                    costs_); break;
      default:
        throw std::runtime_error("scenario: no builder for type " +
                                 std::to_string(type));
    }
    commit(scenario_type_name(type).c_str(), floor, left, type, result);
  }

 private:
  std::size_t find_shaft(std::uint16_t x) const {
    for (std::size_t index = 0; index < document_.elevators.size(); ++index) {
      if (document_.elevators[index].used != 0U &&
          document_.elevators[index].x == x) {
        return index;
      }
    }
    throw std::runtime_error("scenario: new shaft at x " + std::to_string(x) +
                             " not found");
  }

  [[noreturn]] static void fail(const char* what, int floor, int x,
                                OriginalConstructionStatus status,
                                std::uint16_t code) {
    throw std::runtime_error(
        std::string("scenario: ") + what + " at floor " +
        std::to_string(floor) + " x " + std::to_string(x) +
        " failed: status " + std::to_string(static_cast<int>(status)) +
        " STRL/1003 code " + std::to_string(code));
  }

  // The host's common success tail (native_main.cpp 2086-2102): 1148:0163,
  // then the literal 0ed8 route rebuild for its six types.
  void commit(const char* what, int floor, int x, std::uint16_t type,
              const OriginalConstructionResult& result) {
    if (!result.succeeded()) {
      fail(what, floor, x, result.status, result.construction_status_code);
    }
    const auto rating =
        complete_original_rating_construction(document_, part_, type);
    if (rating.dialog.valid()) ++summary_.rating_dialogs;
    if (original_construction_rebuilds_routes(type)) {
      rebuild_original_transport_route_graphs(document_);
    }
    ++summary_.built_by_type[type];
  }

  OriginalTdtDocument& document_;
  const OriginalPartTable& part_;
  const OriginalYenTable& costs_;
  ScenarioSummary& summary_;
  std::map<std::uint8_t, std::size_t> variants_{};
};

void build_busy(Builder& build) {
  build.lobby();
  for (std::int16_t floor = kGround + 1; floor <= kTop; ++floor) {
    build.floor(floor);
  }
  (void)build.shaft(kShaftA, kGround, kTop, 4U);
  (void)build.shaft(kShaftB, kGround, kTop, 3U);

  // 11: shops and food.
  auto x = build.row(10, 11, kBandLeft, 6U);
  x = build.row(12, 11, x, 2U);
  x = build.row(6, 11, x, 2U);
  (void)build.row(10, 11, x, 7U);
  // 12: food and shops.
  x = build.row(6, 12, kBandLeft, 2U);
  x = build.row(12, 12, x, 3U);
  (void)build.row(10, 12, x, 11U);
  // 13: services and offices.
  x = build.row(14, 13, kBandLeft, 1U);
  x = build.row(15, 13, x, 2U);
  (void)build.row(7, 13, x, 21U);
  // 14-17 offices, 18-20 condos.
  for (std::int16_t floor = 14; floor <= 17; ++floor) {
    (void)build.row(7, floor, kBandLeft, 26U);
  }
  for (std::int16_t floor = 18; floor <= 20; ++floor) {
    (void)build.row(9, floor, kBandLeft, 14U);
  }
  // 21-23 hotel, 24 housekeeping and offices.
  (void)build.row(4, 21, kBandLeft, 39U);
  x = build.row(3, 22, kBandLeft, 30U);
  (void)build.row(4, 22, x, 19U);
  (void)build.row(5, 23, kBandLeft, 23U);
  x = build.row(15, 24, kBandLeft, 2U);
  (void)build.row(7, 24, x, 23U);

  // Escalator off the lobby, stairs between office and condo floors.
  build.transport(27U, 11, kStairColumn);
  for (std::int16_t floor = 14; floor <= 20; floor += 2) {
    build.transport(22U, floor, kStairColumn);
  }
}

void build_basement(Builder& build, std::size_t shaft_a) {
  for (std::int16_t floor = kGround - 1; floor >= 0; --floor) {
    build.floor(floor);
  }
  // 1..109 only (floor 0 is never a shaft end).
  build.extend(shaft_a, 1);

  // Parking: ramps stacked at one x on B1/B2, segments beside them.
  constexpr std::uint16_t kRamp = 344;
  build.place(44U, 9, kRamp);
  build.place(44U, 8, kRamp);
  (void)build.row(11U, 9, kRamp - 40U, 10U);
  (void)build.row(11U, 8, kRamp - 40U, 10U);

  // Paired and stacked facilities at the band's left edge; the Movie
  // entrance expands seven cells left of the body on activation, so the
  // next facility starts with a gap.
  build.place(18U, 9, kBandLeft + 8U);
  build.place(13U, 9, 200U);
  build.place(13U, 8, 200U);
  build.place(29U, 7, kBandLeft);
  build.place(20U, 5, kBandLeft);
  build.place(31U, 2, kBandLeft);
  (void)build.row(14U, 3, kBandLeft, 1U);
}

}  // namespace

ScenarioSummary build_scenario(std::string_view name,
                               OriginalTdtDocument& document,
                               const OriginalPartTable& part,
                               const OriginalYenTable& construction_costs) {
  ScenarioSummary summary{};
  document.header.balance = kStartingBalance;
  Builder build(document, part, construction_costs, summary);
  if (name == "busy") {
    build_busy(build);
  } else if (name == "full") {
    build_busy(build);
    // Shaft A is the first allocated elevator slot.
    std::size_t shaft_a = document.elevators.size();
    for (std::size_t index = 0; index < document.elevators.size(); ++index) {
      if (document.elevators[index].used != 0U &&
          document.elevators[index].x == kShaftA) {
        shaft_a = index;
        break;
      }
    }
    build_basement(build, shaft_a);
  } else {
    throw std::runtime_error("unknown scenario: " + std::string(name));
  }
  summary.balance_after = document.header.balance;
  return summary;
}

bool apply_direct_build(OriginalTdtDocument& document,
                        const OriginalPartTable& part,
                        const OriginalYenTable& costs,
                        const OriginalYenTable& rent_income,
                        const DirectBuild& build) {
  const auto type = build.type;
  const auto floor = build.floor;
  const auto left = build.left;
  if (build.shift) {
    const auto width = original_facility_width_cells(type);
    if (width == 0U) {
      throw std::runtime_error("direct build: no footprint for shift with "
                               "type " + std::to_string(type));
    }
    const auto replacement = apply_original_replacement_demolition(
        document, rent_income, type, floor, static_cast<std::int16_t>(left),
        static_cast<std::int16_t>(left + width));
    if (!replacement.completed) return false;
  }
  OriginalConstructionResult result{};
  switch (type) {
    case 0:
      result = build_original_floor(document, floor, left,
                                    static_cast<std::uint16_t>(left + 1U),
                                    costs);
      break;
    case 24: {
      const auto right = static_cast<std::uint16_t>(left + 4U);
      if (floor == kGround && document.header.lobby_height == 0U) {
        result = build_original_initial_lobby(document, left, right, 1U, costs);
      } else if (floor == kGround) {
        result = extend_original_lobby(document, left, right, costs);
      } else {
        result = build_original_sky_lobby(document, floor, left, right, costs);
      }
      break;
    }
    case 1: case 42: case 43:
      result = build_original_elevator(document, type, floor, left, costs,
                                       part);
      break;
    case 22: case 27:
      result = build_original_vertical_transport(
          document, static_cast<std::uint8_t>(type), floor, left, costs);
      break;
    case 3: case 4: case 5:
      result = build_original_hotel_room(document,
                                         static_cast<std::uint8_t>(type),
                                         floor, left, build.variant, costs);
      break;
    case 6: result = build_original_restaurant(document, floor, left, costs);
      break;
    case 7:
      result = build_original_office(document, floor, left, build.variant,
                                     costs);
      break;
    case 9:
      result = build_original_condo(document, floor, left, build.variant,
                                    costs);
      break;
    case 10: result = build_original_retail_shop(document, floor, left, costs);
      break;
    case 12: result = build_original_fast_food(document, floor, left, costs);
      break;
    case 13:
      result = build_original_medical_center(document, floor, left,
                                             build.variant, costs);
      break;
    case 14: result = build_original_security(document, floor, left, costs);
      break;
    case 15: result = build_original_housekeeping(document, floor, left, costs);
      break;
    case 17: result = build_original_secom_center(document, floor, left, costs);
      break;
    case 18:
      result = build_original_movie_theater(document, floor, left, costs);
      break;
    case 20:
      result = build_original_recycling_center(document, floor, left, costs);
      break;
    case 29: result = build_original_party_hall(document, floor, left, costs);
      break;
    case 31:
      result = build_original_metro_station(document, floor, left, costs);
      break;
    case 36: result = build_original_cathedral(document, floor, left, costs);
      break;
    default:
      throw std::runtime_error("direct build: no constructor for type " +
                               std::to_string(type));
  }
  if (!result.succeeded()) return false;
  const auto rating = complete_original_rating_construction(document, part,
                                                            type);
  (void)rating;
  if (original_construction_rebuilds_routes(type)) {
    rebuild_original_transport_route_graphs(document);
  }
  return true;
}

std::string scenario_type_name(int type) {
  switch (type) {
    case 0: return "floor";
    case 1: return "standard elevator";
    case 3: return "single room";
    case 4: return "twin room";
    case 5: return "suite";
    case 6: return "restaurant";
    case 7: return "office";
    case 9: return "condo";
    case 10: return "retail";
    case 11: return "parking";
    case 12: return "fast food";
    case 13: return "medical";
    case 14: return "security";
    case 15: return "housekeeping";
    case 18: return "movie theater";
    case 20: return "recycling";
    case 22: return "stairs";
    case 24: return "lobby";
    case 27: return "escalator";
    case 29: return "party hall";
    case 31: return "metro";
    case 44: return "parking ramp";
    default: return "type " + std::to_string(type);
  }
}

}  // namespace simtower
