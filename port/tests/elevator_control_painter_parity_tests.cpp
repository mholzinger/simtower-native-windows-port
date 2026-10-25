// Win32-only pixel parity of the shared Elevator Control painters
// (paint_original_elevator_control / paint_original_elevator_popup in
// original_elevator_control_controller.hpp) through OriginalGdiDrawContext
// with the frozen direct-GDI painters (legacy_elevator_control_painter_
// win32.hpp), on a 32-bit DIB section, over every shaft of the busy and
// full towers: both schedule banks, three day phases, the scroll range
// ends, isolation off and on, nothing / Close / Simulate pressed and a
// tower with every other floor unserved; the popup with no hover and each
// band. The two must be identical pixel for pixel.
//
// The software canvas (the SDL host) runs the same painter. Its text comes
// from the bundled font, not the GDI one, so it is compared outside the
// text boxes only (the floor-label column and DTMP 41/42); the count of
// differing pixels inside them is printed as the record of that difference.
// Exit 77 (skipped) without the pack.

#include "legacy_elevator_control_painter_win32.hpp"

#include "original_canvas_font.hpp"
#include "original_dialog_model.hpp"
#include "original_draw_canvas.hpp"
#include "original_draw_context_win32.hpp"
#include "original_elevator_control.hpp"
#include "original_elevator_control_controller.hpp"
#include "original_resources.hpp"
#include "original_startup_data.hpp"
#include "original_tdt.hpp"
#include "test_scenario.hpp"

#include <windows.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <vector>

namespace {

using namespace simtower;

std::optional<std::vector<std::byte>> read_file(
    const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::nullopt;
  const std::vector<char> chars((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(chars.size());
  for (std::size_t index = 0; index < chars.size(); ++index) {
    bytes[index] =
        static_cast<std::byte>(static_cast<unsigned char>(chars[index]));
  }
  return bytes;
}

constexpr std::uint32_t kSentinel = 0x00123456U;

class Dib {
 public:
  Dib(int width, int height) : width_(width), height_(height) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    dc_ = CreateCompatibleDC(nullptr);
    assert(dc_);
    bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);
    assert(bitmap_ && bits_);
    previous_ = SelectObject(dc_, bitmap_);
  }
  ~Dib() {
    SelectObject(dc_, previous_);
    DeleteObject(bitmap_);
    DeleteDC(dc_);
  }
  Dib(const Dib&) = delete;
  Dib& operator=(const Dib&) = delete;

  [[nodiscard]] HDC dc() const { return dc_; }
  void clear() {
    GdiFlush();
    auto* pixels = static_cast<std::uint32_t*>(bits_);
    std::fill_n(pixels, static_cast<std::size_t>(width_) * height_, kSentinel);
  }
  [[nodiscard]] std::vector<std::uint32_t> pixels() const {
    GdiFlush();
    const auto* pixels = static_cast<const std::uint32_t*>(bits_);
    std::vector<std::uint32_t> result(
        pixels, pixels + static_cast<std::size_t>(width_) * height_);
    for (auto& pixel : result) pixel &= 0x00FFFFFFU;
    return result;
  }

 private:
  int width_{};
  int height_{};
  HDC dc_{};
  HBITMAP bitmap_{};
  HGDIOBJ previous_{};
  void* bits_{};
};

// No-op dialog ops and host: only the press that sets DS:31b4 runs.
class NullOps final : public OriginalDialogOps {
 public:
  void show_item(std::uint16_t, bool) override {}
  void invalidate() override {}
  void update_window() override {}
  void resize(int, int) override {}
  void enable_item(std::uint16_t, bool) override {}
  void set_scroll_range(std::uint16_t, int, int) override {}
  void set_scroll_pos(std::uint16_t, int) override {}
};

class NullHost final : public OriginalElevatorControlHost {
 public:
  NullHost(OriginalTdtDocument& document, const OriginalStartupData& startup)
      : document_(document), startup_(startup) {}
  OriginalTdtDocument* document() override { return &document_; }
  const OriginalPartTable& part() override { return startup_.part; }
  const OriginalYenTable& rent_income() override {
    return startup_.rent_income;
  }
  bool& build_mode() override { return build_mode_; }
  int alert(int, std::uint16_t, std::uint16_t) override { return 1; }
  int popup(const OriginalDtmpRect&, std::size_t, std::uint8_t) override {
    return -1;
  }
  void consume_family_dispatches(
      std::span<const OriginalPersonFamilyDispatchResult>) override {}
  void play_movement_sound() override {}
  void rebuild_main_without_sky() override {}
  void clear_transfer_visuals() override {}
  void mark_document_dirty() override {}
  void invalidate_main() override {}
  void invalidate_map() override {}
  void invalidate_info() override {}
  void invalidate_command() override {}
  void enable_window(OriginalElevatorControlCloseWindow, bool) override {}
  void unpublish() override {}
  // The button is still down: a phase press opens the (cancelled) popup,
  // as the interface's old default answered.
  bool primary_button_down() override { return true; }
  void audio_pump() override {}
  void sample_auxiliary_pointer_tick() override {}
  void destroy() override {}

 private:
  OriginalTdtDocument& document_;
  const OriginalStartupData& startup_;
  bool build_mode_{true};
};

struct Totals {
  int cases{};
  int popup_cases{};
  int failures{};
  std::size_t canvas_text_pixels{};
  // Painted floor rows with a three-digit label (9-pixel font, insets
  // 3 / -1).
  std::size_t small_font_rows{};
};

// Whether (x, y) is in one of the text boxes, where the canvas's bundled
// font differs from GDI's: the floor-label column (with a three-pixel
// margin: the 13-pixel labels overhang their 12-pixel cells onto the grid
// lines and below the grid; on a row with a 9-pixel three-digit label,
// drawn 3 pixels right of center, the bundled glyphs reach up to eight
// pixels past the cell) and DTMP 41/42 (text from left+2, top+3).
bool in_text_box(const OriginalElevatorControlController& controller,
                 const OriginalTdtDocument* document, int x, int y) {
  const auto inside = [&](int left, int top, int right, int bottom) {
    return x >= left && x < right && y >= top && y < bottom;
  };
  const auto first = original_elevator_control_cell_rect(-1, 0);
  const auto last = original_elevator_control_cell_rect(
      -1, kOriginalElevatorControlVisibleFloors - 1);
  if (inside(std::min(first.left, last.left) - 3,
             std::min(first.top, last.top) - 3,
             std::max(first.right, last.right) + 3,
             std::max(first.bottom, last.bottom) + 3)) {
    return true;
  }
  const auto& state = controller.state();
  if (document && state.valid &&
      state.elevator_index < document->elevators.size()) {
    const auto& elevator = document->elevators[state.elevator_index];
    for (std::int16_t row = 0; row < kOriginalElevatorControlVisibleFloors;
         ++row) {
      const auto plan =
          original_elevator_control_floor_cell_plan(elevator, state, row);
      if (plan.above_top || !plan.small_font) continue;
      const auto cell = original_elevator_control_cell_rect(-1, row);
      if (inside(cell.left - 3, cell.top - 3, cell.right + 8,
                 cell.bottom + 3)) {
        return true;
      }
    }
  }
  for (const auto index : {kOriginalElevatorControlWaitingRectangle,
                           kOriginalElevatorControlDepartureRectangle}) {
    if (const auto* rect = controller.rectangle(index)) {
      if (inside(rect->left, rect->top, rect->right + 24, rect->bottom + 8)) {
        return true;
      }
    }
  }
  return false;
}

void control_case(const OriginalResources& resources,
                  const OriginalElevatorControlController& controller,
                  const OriginalTdtDocument* document, const char* name,
                  Totals& totals) {
  const int width = kOriginalElevatorControlClientWidth;
  const int height = kOriginalElevatorControlClientHeight;
  Dib dib(width, height);
  dib.clear();
  legacy_elevator_control::paint_original_elevator_control(
      dib.dc(), resources, controller, document);
  const auto legacy = dib.pixels();

  dib.clear();
  {
    OriginalGdiDrawContext context(dib.dc());
    paint_original_elevator_control(context, resources, controller, document);
  }
  const auto shared = dib.pixels();

  OriginalWorldRaster raster{};
  raster.width = width;
  raster.height = height;
  raster.pixels.assign(static_cast<std::size_t>(width) * height, kSentinel);
  {
    OriginalCanvasDrawContext canvas(raster, OriginalCanvasFont::bundled());
    paint_original_elevator_control(canvas, resources, controller, document);
  }
  std::size_t gdi_diff = 0U;
  std::size_t canvas_diff = 0U;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto at = static_cast<std::size_t>(y) * width + x;
      if (legacy[at] != shared[at]) ++gdi_diff;
      if (legacy[at] != raster.pixels[at]) {
        if (in_text_box(controller, document, x, y)) {
          ++totals.canvas_text_pixels;
        } else {
          if (canvas_diff == 0U) {
            std::printf("%s: first canvas difference at %d,%d %06x/%06x\n",
                        name, x, y, static_cast<unsigned>(legacy[at]),
                        static_cast<unsigned>(raster.pixels[at]));
          }
          ++canvas_diff;
        }
      }
    }
  }
  ++totals.cases;
  const auto& state = controller.state();
  if (document && state.valid &&
      state.elevator_index < document->elevators.size()) {
    const auto& elevator = document->elevators[state.elevator_index];
    for (std::int16_t row = 0; row < kOriginalElevatorControlVisibleFloors;
         ++row) {
      const auto plan =
          original_elevator_control_floor_cell_plan(elevator, state, row);
      if (!plan.above_top && plan.small_font) ++totals.small_font_rows;
    }
  }
  if (gdi_diff != 0U || canvas_diff != 0U) {
    std::printf("%s: gdi %zu canvas %zu pixels differ\n", name, gdi_diff,
                canvas_diff);
    ++totals.failures;
  }
}

void popup_case(const OriginalResources& resources, std::int16_t hover,
                Totals& totals) {
  const int width = kOriginalElevatorControlPopupWidth;
  const int height = kOriginalElevatorControlPopupHeight;
  Dib dib(width, height);
  dib.clear();
  legacy_elevator_control::paint_original_elevator_popup(dib.dc(), resources,
                                                         hover);
  const auto legacy = dib.pixels();
  dib.clear();
  {
    OriginalGdiDrawContext context(dib.dc());
    paint_original_elevator_popup(context, resources, hover);
  }
  const auto shared = dib.pixels();
  OriginalWorldRaster raster{};
  raster.width = width;
  raster.height = height;
  raster.pixels.assign(static_cast<std::size_t>(width) * height, kSentinel);
  {
    OriginalCanvasDrawContext canvas(raster, OriginalCanvasFont::bundled());
    paint_original_elevator_popup(canvas, resources, hover);
  }
  ++totals.popup_cases;
  if (legacy != shared || legacy != raster.pixels) {
    std::printf("popup hover %d: gdi %s canvas %s\n", hover,
                legacy == shared ? "same" : "DIFFERENT",
                legacy == raster.pixels ? "same" : "DIFFERENT");
    ++totals.failures;
  }
}

void shaft_cases(const OriginalResources& resources,
                 const OriginalStartupData& startup,
                 OriginalTdtDocument& document, std::size_t index,
                 const char* scenario, Totals& totals) {
  const auto dtmp = parse_original_dtmp(resources.find("DTMP", 400));
  for (const std::uint8_t calendar : {0U, 1U}) {
    for (const std::int8_t phase : {0, 3, 5}) {
      auto base = make_original_elevator_control_state(document, index,
                                                       calendar, phase);
      if (!base.valid) return;
      std::vector<std::int16_t> scrolls{base.scroll_position};
      if (original_elevator_control_has_scrollbar(base)) {
        scrolls.push_back(base.scroll_min);
        scrolls.push_back(base.scroll_max);
      }
      for (const auto scroll : scrolls) {
        for (const bool isolated : {false, true}) {
          for (const int pressed : {-1, 1, 2}) {
            auto state = base;
            state.scroll_position = scroll;
            state.isolation_active = isolated;
            OriginalElevatorControlController controller(dtmp, state);
            if (pressed > 0) {
              const auto* rect = controller.rectangle(
                  static_cast<std::size_t>(pressed));
              assert(rect);
              OriginalDialogSignal down{
                  OriginalDialogSignal::Kind::button_down};
              down.x = static_cast<std::int16_t>(rect->left + 1);
              down.y = static_cast<std::int16_t>(rect->top + 1);
              NullOps ops;
              NullHost host(document, startup);
              controller.signal(down, ops, host);
              assert(controller.pressed_item() == pressed);
              // The shared painter draws the pressed look (D2), the legacy
              // GDI copy the DS:31b4 latch; a press on 1 or 2 sets both.
              assert(controller.pressed_visual() == pressed);
            }
            char name[96];
            std::snprintf(name, sizeof(name),
                          "%s shaft %zu bank %u phase %d scroll %d iso %d "
                          "pressed %d",
                          scenario, index, calendar, phase, scroll, isolated,
                          pressed);
            control_case(resources, controller, &document, name, totals);
          }
        }
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  const auto pack = read_file(argv[1]);
  if (!pack) {
    std::printf("skipped: no resource pack at %s\n", argv[1]);
    return 77;
  }
  const OriginalResources resources(*pack);
  const auto startup = load_original_startup_data(resources);
  Totals totals{};

  {
    // Without a document only BITMAP/400 is drawn.
    auto document = make_original_new_tdt();
    (void)build_scenario("busy", document, startup.part,
                         startup.construction_costs);
    OriginalElevatorControlController controller(
        parse_original_dtmp(resources.find("DTMP", 400)),
        make_original_elevator_control_state(document, 0U, 0U, 0));
    control_case(resources, controller, nullptr, "no document", totals);
  }
  std::size_t shafts = 0U;
  for (const char* scenario : {"busy", "full"}) {
    auto document = make_original_new_tdt();
    (void)build_scenario(scenario, document, startup.part,
                         startup.construction_costs);
    for (std::size_t index = 0; index < document.elevators.size(); ++index) {
      if (document.elevators[index].used == 0U) continue;
      ++shafts;
      shaft_cases(resources, startup, document, index, scenario, totals);
      // Every other floor unserved: the crossed white label cells.
      auto sparse = document;
      for (std::size_t floor = 0; floor < 120U; floor += 2U) {
        sparse.elevators[index].serviced_floors[floor] = std::byte{0};
      }
      shaft_cases(resources, startup, sparse, index, scenario, totals);
    }
  }
  {
    // No scenario shaft reaches floor index 109 (displayed 100), so a busy
    // shaft is stretched to index 114: at the scroll minimum the top rows
    // carry the three-digit labels.
    auto document = make_original_new_tdt();
    (void)build_scenario("busy", document, startup.part,
                         startup.construction_costs);
    for (std::size_t index = 0; index < document.elevators.size(); ++index) {
      if (document.elevators[index].used == 0U) continue;
      document.elevators[index].top_floor = 114;
      for (std::size_t floor = 100U; floor <= 114U; floor += 3U) {
        document.elevators[index].serviced_floors[floor] = std::byte{1};
      }
      shaft_cases(resources, startup, document, index, "tall", totals);
      break;
    }
  }
  for (const std::int16_t hover : {-1, 0, 1, 2}) {
    popup_case(resources, hover, totals);
  }
  std::printf("elevator control painter parity: %zu shafts, %d control "
              "cases (%zu three-digit rows), %d popup cases, %zu canvas "
              "text-box pixels differ (bundled font)\n",
              shafts, totals.cases, totals.small_font_rows,
              totals.popup_cases, totals.canvas_text_pixels);
  assert(shafts >= 2U);
  assert(totals.small_font_rows > 0U);
  assert(totals.failures == 0);
  return 0;
}
