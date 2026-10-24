// Win32-only pixel parity of the shared DTMP chrome painter
// (original_dialog_paint.hpp) through OriginalGdiDrawContext with the frozen
// direct-GDI painter (legacy_dialog_chrome_win32.hpp), on a 32-bit DIB
// section for every DTMP resource in the pack (the event dialogs 3000-3040
// and every other caller of paint_original_dialog_chrome: startup 10000,
// Elevator Control 400, Finance 500, Find 510/520, Rename and movie
// 730-732, facility, transport and person information 748-764), each with
// the children of its DIALOG template (when there is one) and with none,
// and the About chrome. The software canvas must produce the same pixels
// too. Exit 77 (skipped) without the pack.

#include "legacy_dialog_chrome_win32.hpp"

#include "original_canvas_font.hpp"
#include "original_dialog_model.hpp"
#include "original_dialog_paint.hpp"
#include "original_draw_canvas.hpp"
#include "original_draw_context_win32.hpp"
#include "original_dtmp_runtime.hpp"
#include "original_resources.generated.hpp"
#include "original_resources.hpp"

#include <windows.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <vector>

namespace {

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

// One chrome case: legacy GDI, shared painter on GDI, shared painter on the
// canvas. Returns whether all three agree.
bool same_chrome(const simtower::OriginalDtmp& dtmp, int width, int height,
                 const std::function<bool(std::uint16_t)>& has_child,
                 const char* name) {
  Dib dib(width, height);
  dib.clear();
  const RECT client{0, 0, width, height};
  legacy_dialog_chrome::paint_original_dialog_chrome(
      client, [&](int id) { return has_child(static_cast<std::uint16_t>(id)); },
      dib.dc(), dtmp);
  const auto legacy = dib.pixels();

  dib.clear();
  {
    simtower::OriginalGdiDrawContext context(dib.dc());
    simtower::paint_original_dtmp_chrome(context, {0, 0, width, height}, dtmp,
                                         has_child);
  }
  const auto shared = dib.pixels();

  simtower::OriginalWorldRaster raster{};
  raster.width = width;
  raster.height = height;
  raster.pixels.assign(static_cast<std::size_t>(width) * height, kSentinel);
  {
    simtower::OriginalCanvasDrawContext canvas(
        raster, simtower::OriginalCanvasFont::bundled());
    simtower::paint_original_dtmp_chrome(canvas, {0, 0, width, height}, dtmp,
                                         has_child);
  }
  const bool gdi_same = legacy == shared;
  const bool canvas_same = legacy == raster.pixels;
  if (!gdi_same || !canvas_same) {
    std::printf("%s: %dx%d gdi %s canvas %s\n", name, width, height,
                gdi_same ? "same" : "DIFFERENT",
                canvas_same ? "same" : "DIFFERENT");
  }
  return gdi_same && canvas_same;
}

}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  const auto pack = read_file(argv[1]);
  if (!pack) {
    std::printf("skipped: no resource pack at %s\n", argv[1]);
    return 77;
  }
  const simtower::OriginalResources resources(*pack);

  int cases = 0;
  int dtmps = 0;
  int event_dtmps = 0;
  bool ok = true;
  for (const auto& descriptor : simtower::generated::kResources) {
    if (descriptor.type != "DTMP" || descriptor.numeric_id < 0) continue;
    const auto id = descriptor.numeric_id;
    const auto dtmp =
        simtower::parse_original_dtmp(resources.find("DTMP", id));
    // The children of DIALOG/<id> (GetDlgItem + IsWindow), when it exists.
    std::vector<std::uint16_t> children;
    if (const auto dialog = resources.find("DIALOG", id); !dialog.empty()) {
      for (const auto& item : simtower::parse_original_dialog(dialog).items) {
        children.push_back(item.id);
      }
    }
    // The DTMP's window size, or (a bitmap-less DTMP without a header
    // size) the bounding box of its rectangles plus a margin.
    auto size = simtower::original_dtmp_window_size(dtmp, resources);
    if (size.width <= 0 || size.height <= 0) {
      size.width = 16;
      size.height = 16;
      for (const auto& rect : dtmp.rectangles) {
        size.width = std::max<int>(size.width, rect.right + 8);
        size.height = std::max<int>(size.height, rect.bottom + 8);
      }
    }
    const auto has_child = [&](std::uint16_t child) {
      return std::ranges::find(children, child) != children.end();
    };
    char name[32];
    std::snprintf(name, sizeof(name), "DTMP %d", static_cast<int>(id));
    ok = same_chrome(dtmp, size.width, size.height, has_child, name) && ok;
    // Every rectangle as a placeholder (no children) exercises 11e0:0950.
    ok = same_chrome(dtmp, size.width, size.height,
                     [](std::uint16_t) { return false; }, name) &&
         ok;
    cases += 2;
    ++dtmps;
    if (id >= 3000 && id <= 3040) ++event_dtmps;
  }
  const auto about = simtower::prepare_original_about_dialog(resources);
  ok = same_chrome(about.chrome, about.layout.window_width,
                   about.layout.window_height,
                   [](std::uint16_t) { return false; }, "About") &&
       ok;
  ++cases;
  // The 20 event dialogs and at least the 25 other DTMP callers.
  assert(event_dtmps == 20 && dtmps >= 45);
  assert(cases == 2 * dtmps + 1);
  assert(ok);
  std::printf("dialog chrome parity: %d DTMPs, %d cases\n", dtmps, cases);
  return 0;
}
