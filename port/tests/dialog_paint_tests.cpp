// The shared DTMP chrome painter (original_dialog_paint.hpp) through the
// software canvas on every platform: the path the SDL dialog runtime uses.
// Synthetic probes need no assets; with the pack, every event DTMP and the
// About chrome are painted and probed too (exit 77 without it, after the
// synthetic half). The Win32-only dialog_chrome_parity_tests compares the
// same painter with GDI.

#include "original_canvas_font.hpp"
#include "original_dialog_model.hpp"
#include "original_dialog_paint.hpp"
#include "original_draw_canvas.hpp"
#include "original_dtmp_runtime.hpp"
#include "original_resources.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <vector>

namespace {

constexpr std::uint32_t kSentinel = 0x00123456U;

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

constexpr std::uint32_t gray(std::uint32_t level) {
  return (level << 16U) | (level << 8U) | level;
}

struct Painted {
  simtower::OriginalWorldRaster raster{};
  [[nodiscard]] std::uint32_t at(int x, int y) const {
    return raster.pixels[static_cast<std::size_t>(y) * raster.width + x];
  }
};

Painted paint(const simtower::OriginalDtmp& dtmp, int width, int height,
              const simtower::OriginalDialogHasChild& has_child) {
  Painted painted{};
  painted.raster.width = width;
  painted.raster.height = height;
  painted.raster.pixels.assign(static_cast<std::size_t>(width) * height,
                               kSentinel);
  simtower::OriginalCanvasDrawContext canvas(
      painted.raster, simtower::OriginalCanvasFont::bundled());
  simtower::paint_original_dtmp_chrome(canvas, {0, 0, width, height}, dtmp,
                                       has_child);
  return painted;
}

// 11e0:06d9 over the whole client: outer 179 / 140, inner 102 / 230, fill.
void check_frame(const Painted& painted, int width, int height) {
  assert(painted.at(0, 0) == gray(179));
  assert(painted.at(2, 2) == gray(179));
  assert(painted.at(0, height / 2) == gray(179));
  assert(painted.at(width / 2, 0) == gray(179));
  assert(painted.at(width - 1, height - 1) == gray(140));
  assert(painted.at(width / 2, height - 3) == gray(140));
  assert(painted.at(width - 3, height / 2) == gray(140));
  assert(painted.at(3, 3) == gray(102));
  assert(painted.at(5, height / 2) == gray(102));
  assert(painted.at(width - 4, height - 4) == gray(230));
  assert(painted.at(width / 2, height - 6) == gray(230));
}

// 11e0:0950 on rect: 89 top/left, 255 bottom/right, 230 inside.
void check_placeholder(const Painted& painted,
                       const simtower::OriginalDtmpRect& rect) {
  const int left = rect.left;
  const int top = rect.top;
  const int right = rect.right;
  const int bottom = rect.bottom;
  assert(painted.at(left, top) == gray(89));
  assert(painted.at(left + 1, top + 1) == gray(89));
  assert(painted.at(left, (top + bottom) / 2) == gray(89));
  assert(painted.at(right - 1, bottom - 1) == gray(255));
  assert(painted.at((left + right) / 2, bottom - 2) == gray(255));
  assert(painted.at((left + right) / 2, (top + bottom) / 2) == gray(230));
  assert(painted.at(left + 2, top + 2) == gray(230));
}

void synthetic_tests() {
  simtower::OriginalDtmp dtmp{};
  dtmp.rectangles.push_back({20, 20, 60, 50});
  dtmp.rectangles.push_back({0, 0, 0, 0});        // empty: never painted
  dtmp.rectangles.push_back({70, 10, 10001, 0});  // a bitmap entry: empty
  const auto none = paint(dtmp, 100, 80, [](std::uint16_t) { return false; });
  check_frame(none, 100, 80);
  check_placeholder(none, dtmp.rectangles[0]);
  // The 204 fill outside the placeholder.
  assert(none.at(10, 60) == gray(204));
  assert(none.at(75, 20) == gray(204));

  // A rectangle whose child exists keeps the 204 fill.
  const auto child = paint(dtmp, 100, 80,
                           [](std::uint16_t id) { return id == 1U; });
  check_frame(child, 100, 80);
  assert(child.at(20, 20) == gray(204));
  assert(child.at(40, 35) == gray(204));
}

void resource_tests(const simtower::OriginalResources& resources) {
  int cases = 0;
  for (std::uint16_t id = 3000U; id <= 3040U; ++id) {
    std::optional<simtower::OriginalEventDialogModel> model;
    try {
      model = simtower::prepare_original_event_dialog(resources, {id, 0, 0});
    } catch (const std::exception&) {
      continue;
    }
    const auto size =
        simtower::original_dtmp_window_size(model->dtmp, resources);
    const auto painted =
        paint(model->dtmp, size.width, size.height,
              [](std::uint16_t) { return false; });
    check_frame(painted, size.width, size.height);
    for (const auto& rect : model->dtmp.rectangles) {
      if (rect.right <= rect.left || rect.bottom <= rect.top) continue;
      check_placeholder(painted, rect);
    }
    ++cases;
  }
  assert(cases == 20);
  const auto about = simtower::prepare_original_about_dialog(resources);
  const auto painted = paint(about.chrome, about.layout.window_width,
                             about.layout.window_height,
                             [](std::uint16_t) { return false; });
  check_frame(painted, about.layout.window_width,
              about.layout.window_height);
  check_placeholder(painted, about.layout.credits_outer);
}

}  // namespace

int main(int argc, char** argv) {
  synthetic_tests();
  if (argc < 2) return 77;
  const auto pack = read_file(argv[1]);
  if (!pack) {
    std::printf("skipped: no resource pack at %s\n", argv[1]);
    return 77;
  }
  const simtower::OriginalResources resources(*pack);
  resource_tests(resources);
  std::printf("dialog paint tests passed\n");
  return 0;
}
