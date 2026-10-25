#include "original_canvas_font.hpp"
#include "original_dib.hpp"
#include "original_draw_canvas.hpp"
#include "original_draw_context.hpp"
#include "original_dtmp.hpp"
#include "original_finance.hpp"
#include "original_info.hpp"
#include "original_resources.hpp"
#include "original_tables.hpp"
#include "original_tdt.hpp"

#ifdef _WIN32
#include "original_dib_win32.hpp"
#include "original_info_win32.hpp"
#include "legacy_info_painter_win32.hpp"
#endif

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

#ifdef _WIN32
struct TestDibSurface {
  HDC dc{};
  HBITMAP bitmap{};
  HGDIOBJ previous{};
  std::uint32_t* pixels{};
  int width{};
  int height{};

  TestDibSurface(int surface_width, int surface_height)
      : width(surface_width), height(surface_height) {
    dc = CreateCompatibleDC(nullptr);
    assert(dc);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1U;
    info.bmiHeader.biBitCount = 32U;
    info.bmiHeader.biCompression = BI_RGB;
    void* storage{};
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &storage,
                              nullptr, 0U);
    assert(bitmap && storage);
    pixels = static_cast<std::uint32_t*>(storage);
    previous = SelectObject(dc, bitmap);
  }

  ~TestDibSurface() {
    if (previous) SelectObject(dc, previous);
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
  }

  TestDibSurface(const TestDibSurface&) = delete;
  TestDibSurface& operator=(const TestDibSurface&) = delete;
};
#endif  // _WIN32

std::vector<std::byte> read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  assert(stream);
  std::vector<char> chars((std::istreambuf_iterator<char>(stream)),
                          std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(chars.size());
  for (std::size_t i = 0; i < chars.size(); ++i) {
    bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(chars[i]));
  }
  return bytes;
}

using Pixels = std::vector<std::uint32_t>;
constexpr int kInfoWidth = simtower::kOriginalInfoWidth;
constexpr int kInfoHeight =
    simtower::kOriginalInfoClientTop + simtower::kOriginalInfoBackingHeight;
constexpr std::uint32_t kDark = 0x00262626U;
constexpr std::uint32_t kRed = 0x00b30000U;

std::uint32_t pixel(const Pixels& pixels, int x, int y) {
  return pixels[static_cast<std::size_t>(y * kInfoWidth + x)];
}

// One way of painting the 431x49 Info client.
struct InfoBackend {
  const char* name{};
  // BITMAP/320 alone at client y 8.
  std::function<Pixels()> backing;
  std::function<Pixels(const simtower::OriginalTdtDocument*,
                       std::string_view)>
      info;
};

Pixels canvas_pixels(
    const std::function<void(simtower::OriginalDrawContext&)>& paint) {
  simtower::OriginalWorldRaster raster{
      kInfoWidth, kInfoHeight,
      Pixels(static_cast<std::size_t>(kInfoWidth) * kInfoHeight,
             0x00ffffffU)};
  simtower::OriginalCanvasDrawContext dc(
      raster, simtower::OriginalCanvasFont::bundled());
  paint(dc);
  return std::move(raster.pixels);
}

InfoBackend canvas_backend(const simtower::OriginalResources& resources) {
  return {
      "canvas",
      [&resources] {
        return canvas_pixels([&](simtower::OriginalDrawContext& dc) {
          dc.draw_dib(resources.find("BITMAP", 320), 0,
                      simtower::kOriginalInfoClientTop);
        });
      },
      [&resources](const simtower::OriginalTdtDocument* document,
                   std::string_view status) {
        return canvas_pixels([&](simtower::OriginalDrawContext& dc) {
          simtower::draw_original_info(dc, resources, document, status);
        });
      }};
}

#ifdef _WIN32
Pixels surface_pixels(const TestDibSurface& surface) {
  GdiFlush();
  return Pixels(surface.pixels,
                surface.pixels + static_cast<std::size_t>(surface.width) *
                                     surface.height);
}

InfoBackend gdi_backend(const simtower::OriginalResources& resources) {
  return {
      "gdi",
      [&resources] {
        TestDibSurface surface(kInfoWidth, kInfoHeight);
        simtower::draw_original_dib(surface.dc,
                                    resources.find("BITMAP", 320), 0,
                                    simtower::kOriginalInfoClientTop);
        return surface_pixels(surface);
      },
      [&resources](const simtower::OriginalTdtDocument* document,
                   std::string_view status) {
        TestDibSurface surface(kInfoWidth, kInfoHeight);
        simtower::draw_original_info(surface.dc, resources, document, status);
        return surface_pixels(surface);
      }};
}
#endif  // _WIN32

void check_info_fields(const simtower::OriginalResources&,
                       const InfoBackend& backend) {
  std::printf("info fields: %s\n", backend.name);
  {
    // Direct 1118:045d raster coverage. 1118:0ba4's 152x16 date field is
    // offset to client (151,13), filled with 0x999999, and leaves its
    // right/bottom edges excluded. Weekends emit STRL/713 item 10 in dark
    // red before returning to 0x262626 for the quarter/year suffix.
    const auto base = backend.backing();
    const auto weekday = backend.info(nullptr, {});
    assert(pixel(weekday, 151, 13) == 0x00999999U);
    assert(pixel(weekday, 302, 13) == 0x00999999U);
    assert(pixel(weekday, 150, 13) == pixel(base, 150, 13));
    assert(pixel(weekday, 151, 29) == pixel(base, 151, 29));
    std::size_t weekday_dark{};
    for (int y = 13; y < 29; ++y) {
      for (int x = 151; x < 303; ++x) {
        if (pixel(weekday, x, y) == kDark) ++weekday_dark;
      }
    }
    assert(weekday_dark != 0U);

    auto weekend_tower = simtower::make_original_new_tdt();
    weekend_tower.header.current_day = 2;
    const auto weekend = backend.info(&weekend_tower, {});
    std::size_t weekend_red{};
    std::size_t weekend_dark{};
    for (int y = 13; y < 29; ++y) {
      for (int x = 151; x < 303; ++x) {
        if (pixel(weekend, x, y) == kRed) ++weekend_red;
        if (pixel(weekend, x, y) == kDark) ++weekend_dark;
      }
    }
    assert(weekend_red != 0U && weekend_dark != 0U);
  }
  {
    // Direct 1118:0044/0b67 and 1208:0a42 raster coverage. Ratings one through
    // five repaint only the five clipped 21x19 BITMAP/322-or-323 star cells at
    // [43,148)x[11,30). Rating six instead clips BITMAP/327 to the special
    // [42,148)x[10,32) strip.
    auto tower = simtower::make_original_new_tdt();
    tower.header.rating = 1U;
    const auto rating_one = backend.info(&tower, {});
    tower.header.rating = 5U;
    const auto rating_five = backend.info(&tower, {});
    tower.header.rating = 6U;
    const auto rating_six = backend.info(&tower, {});
    bool ordinary_changed{};
    bool special_changed{};
    for (int y = 0; y < kInfoHeight; ++y) {
      for (int x = 0; x < kInfoWidth; ++x) {
        if (pixel(rating_one, x, y) != pixel(rating_five, x, y)) {
          assert(x >= 43 && x < 148 && y >= 11 && y < 30);
          ordinary_changed = true;
        }
        if (pixel(rating_five, x, y) != pixel(rating_six, x, y)) {
          assert(x >= 42 && x < 148 && y >= 10 && y < 32);
          special_changed = true;
        }
      }
    }
    assert(ordinary_changed && special_changed);
  }
  {
    // Direct 1118:0143 raster coverage: 1118:0bda's 70x14 balance rectangle
    // is offset by (10,8), filled with 0x999999, and receives right/baseline-
    // aligned font-13 text at its inclusive bottom-right pixel. Changing only
    // b3ce must therefore alter pixels solely inside client [354,424)x[13,27).
    auto tower = simtower::make_original_new_tdt();
    tower.header.balance = 123;
    const auto positive = backend.info(&tower, {});
    tower.header.balance = -1;
    const auto negative = backend.info(&tower, {});
    assert(pixel(positive, 354, 13) == 0x00999999U);
    assert(pixel(positive, 423, 13) == 0x00999999U);
    bool changed_inside{};
    std::size_t positive_dark{};
    std::size_t negative_dark{};
    for (int y = 0; y < kInfoHeight; ++y) {
      for (int x = 0; x < kInfoWidth; ++x) {
        const bool inside = x >= 354 && x < 424 && y >= 13 && y < 27;
        if (pixel(positive, x, y) != pixel(negative, x, y)) {
          assert(inside);
          changed_inside = true;
        }
        if (inside && pixel(positive, x, y) == kDark) ++positive_dark;
        if (inside && pixel(negative, x, y) == kDark) ++negative_dark;
      }
    }
    assert(changed_inside && positive_dark != 0U && negative_dark != 0U);
  }
  {
    // Direct 1118:026a raster coverage. 1118:0c0f supplies a 262x11 field at
    // (41,25); the painter offsets it by eight client pixels, fills 0xd9d9d9,
    // adds the one-pixel white lower edge, and clips font-12 status text to the
    // resulting client [41,303)x[33,44) rectangle.
    const auto empty = backend.info(nullptr, {});
    const auto message = backend.info(nullptr, "Construction status");
    assert(pixel(empty, 41, 33) == 0x00d9d9d9U);
    assert(pixel(empty, 302, 43) == 0x00d9d9d9U);
    assert(pixel(empty, 41, 44) == 0x00ffffffU);
    assert(pixel(empty, 302, 44) == 0x00ffffffU);
    bool changed_inside{};
    std::size_t dark_pixels{};
    for (int y = 0; y < kInfoHeight; ++y) {
      for (int x = 0; x < kInfoWidth; ++x) {
        const bool inside = x >= 41 && x < 303 && y >= 33 && y < 44;
        if (pixel(empty, x, y) != pixel(message, x, y)) {
          assert(inside);
          changed_inside = true;
        }
        if (inside && pixel(message, x, y) == kDark) ++dark_pixels;
      }
    }
    assert(changed_inside && dark_pixels != 0U);
  }
  {
    // Direct 1118:0368 raster coverage. 1118:0c44 supplies the 86x14 field at
    // client [337,423)x[31,45); population text is right-aligned by its
    // measured GDI extent at (right-extent,bottom-2).
    auto tower = simtower::make_original_new_tdt();
    tower.post_elevator.finance.total_population = 1;
    const auto one = backend.info(&tower, {});
    tower.post_elevator.finance.total_population = 123456;
    const auto many = backend.info(&tower, {});
    assert(pixel(one, 337, 31) == 0x00999999U);
    assert(pixel(one, 337, 44) == 0x00999999U);
    bool changed_inside{};
    std::size_t one_dark{};
    std::size_t many_dark{};
    for (int y = 0; y < kInfoHeight; ++y) {
      for (int x = 0; x < kInfoWidth; ++x) {
        const bool inside = x >= 337 && x < 423 && y >= 31 && y < 45;
        if (pixel(one, x, y) != pixel(many, x, y)) {
          assert(inside);
          changed_inside = true;
        }
        if (inside && pixel(one, x, y) == kDark) ++one_dark;
        if (inside && pixel(many, x, y) == kDark) ++many_dark;
      }
    }
    assert(changed_inside && one_dark != 0U && many_dark != 0U);
  }
  {
    // Direct 1118:073d/0c79/0cad and 1208:0d35 raster coverage. The retained
    // 31x31 clock face is presented at client [5,36)x[13,44); changing only
    // b3de therefore moves the two table-derived black hands without
    // touching any pixel outside it.
    auto tower = simtower::make_original_new_tdt();
    tower.header.frame_time = 0U;
    const auto morning = backend.info(&tower, {});
    tower.header.frame_time = 1200U;
    const auto afternoon = backend.info(&tower, {});
    bool changed_inside{};
    for (int y = 0; y < kInfoHeight; ++y) {
      for (int x = 0; x < kInfoWidth; ++x) {
        const bool inside = x >= 5 && x < 36 && y >= 13 && y < 44;
        if (pixel(morning, x, y) != pixel(afternoon, x, y)) {
          assert(inside);
          changed_inside = true;
        }
      }
    }
    assert(changed_inside);
  }
}

// Text-colored pixels the painter added over the BITMAP/320 backing.
struct TextBox {
  int left{1000};
  int top{1000};
  int right{-1};   // inclusive
  int bottom{-1};  // inclusive
  std::size_t count{};

  void add(int x, int y) {
    left = std::min(left, x);
    top = std::min(top, y);
    right = std::max(right, x);
    bottom = std::max(bottom, y);
    ++count;
  }
};

std::uint64_t fnv1a(const Pixels& pixels) {
  std::uint64_t hash = 0xcbf29ce484222325ULL;
  for (const auto value : pixels) {
    for (int shift = 0; shift < 32; shift += 8) {
      hash ^= (value >> shift) & 0xffU;
      hash *= 0x100000001b3ULL;
    }
  }
  return hash;
}

simtower::OriginalTdtDocument fixed_info_tower() {
  auto tower = simtower::make_original_new_tdt();
  tower.header.rating = 3U;
  tower.header.balance = 1234567;
  tower.header.current_day = 2;  // weekend: red "WE" then dark text
  tower.header.frame_time = 1500U;
  tower.post_elevator.finance.total_population = 12345;
  return tower;
}
constexpr std::string_view kFixedStatus = "Office workers demand Parking";

void check_info_canvas(const simtower::OriginalResources& resources) {
  // The canvas painter on a fixed document: a stable image, and every text
  // pixel inside the field it belongs to, placed as GDI would place it.
  const auto backend = canvas_backend(resources);
  const auto tower = fixed_info_tower();
  const auto image = backend.info(&tower, kFixedStatus);
  assert(image == backend.info(&tower, kFixedStatus));
  const auto hash = fnv1a(image);
  std::printf("info canvas fnv1a %016llx\n",
              static_cast<unsigned long long>(hash));
  // Depends on BITMAP/320..327, the bundled font, and the canvas rules.
  // Update deliberately (and look at the image) when one of those changes.
  assert(hash == 0x4def2975a8e1bc52ULL);

  const auto base = backend.backing();
  {
    // The status baseline moved up one row in the cleanup (field.bottom - 1,
    // 1118:0307's 1208:0cf5(0, -1)), which shows the top descender row of
    // the 'g' in "Parking". Only the status field {41, 33, 303, 44} (filled
    // 0xd9d9d9, text clipped to it) changed: shifting its rows 33..42 down
    // one row and refilling row 33 reproduces the frame from before. Row 33
    // has no ink now, so no glyph reaches above the field either way.
    auto before = image;
    constexpr std::uint32_t kStatusFill = 0xd9d9d9U;
    for (int x = 41; x < 303; ++x) {
      assert(before[static_cast<std::size_t>(33) * kInfoWidth + x] ==
             kStatusFill);
    }
    for (int y = 43; y > 33; --y) {
      for (int x = 41; x < 303; ++x) {
        before[static_cast<std::size_t>(y) * kInfoWidth + x] =
            before[static_cast<std::size_t>(y - 1) * kInfoWidth + x];
      }
    }
    for (int x = 41; x < 303; ++x) {
      before[static_cast<std::size_t>(33) * kInfoWidth + x] = kStatusFill;
    }
    std::printf("info canvas before status baseline fnv1a %016llx\n",
                static_cast<unsigned long long>(fnv1a(before)));
    assert(fnv1a(before) == 0x30d4dddc8b544117ULL);
    // Then, on that frame: the row rule of the canvas font's dropout pass
    // (the 11-pixel '0' top) changed it in exactly two pixels: the bowl
    // top of the 14-pixel 'a' in "Year" (date field) moves column 3 from
    // row 4 to row 3. Swapping them back reproduces the frame from before
    // that rule.
    const std::size_t a_top = static_cast<std::size_t>(23) * kInfoWidth + 251;
    const std::size_t a_low = a_top + static_cast<std::size_t>(kInfoWidth);
    assert(before[a_top] == kDark && before[a_low] == 0x999999U);
    std::swap(before[a_top], before[a_low]);
    std::printf("info canvas before row rule fnv1a %016llx\n",
                static_cast<unsigned long long>(fnv1a(before)));
    assert(fnv1a(before) == 0x758b18a75848d323ULL);
    // The 11-pixel bowl fix in the canvas font's dropout pass changed this
    // frame in exactly one pixel: the bowl corner of 'P' in "Parking"
    // (12-pixel status line), which it closes. Putting the backing pixel
    // back reproduces the frame from before the fix.
    const std::size_t corner = static_cast<std::size_t>(39) * kInfoWidth + 176;
    assert(before[corner] != base[corner]);
    before[corner] = base[corner];
    std::printf("info canvas before bowl fix fnv1a %016llx\n",
                static_cast<unsigned long long>(fnv1a(before)));
    assert(fnv1a(before) == 0xaadb862c54ae0f66ULL);
  }
  auto& font = simtower::OriginalCanvasFont::bundled();
  // The date text is not clipped: descenders (the 'Q' tail) reach below the
  // field fill, as GDI's do.
  const simtower::OriginalRect date{151, 13, 303, 31};
  const simtower::OriginalRect balance{354, 13, 424, 27};
  const simtower::OriginalRect status{41, 33, 303, 44};
  const simtower::OriginalRect population{337, 31, 423, 45};
  const auto inside = [](const simtower::OriginalRect& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
  };
  TextBox red;
  TextBox date_dark;
  TextBox balance_box;
  TextBox status_box;
  TextBox population_box;
  for (int y = 0; y < kInfoHeight; ++y) {
    for (int x = 0; x < kInfoWidth; ++x) {
      const auto value = pixel(image, x, y);
      if (value == pixel(base, x, y) || (value != kDark && value != kRed)) {
        continue;
      }
      if (value == kRed) {
        assert(inside(date, x, y));
        red.add(x, y);
      } else if (inside(date, x, y)) {
        date_dark.add(x, y);
      } else if (inside(balance, x, y)) {
        balance_box.add(x, y);
      } else if (inside(status, x, y)) {
        status_box.add(x, y);
      } else if (inside(population, x, y)) {
        population_box.add(x, y);
      } else {
        std::printf("stray text pixel %d,%d %06x\n", x, y, value);
        assert(false);
      }
    }
  }
  assert(red.count != 0U && date_dark.count != 0U &&
         balance_box.count != 0U && status_box.count != 0U &&
         population_box.count != 0U);
  for (const auto* box : {&red, &date_dark, &balance_box, &status_box,
                          &population_box}) {
    std::printf("text box [%d,%d]x[%d,%d] %zu px\n", box->left, box->right,
                box->top, box->bottom, box->count);
  }
  // Date: "WE" from the current position (159, baseline 28), then the dark
  // text from where the red text's extent left the position.
  const int red_end = 159 + font.extent(14, "WE");
  assert(red.left >= 159 && red.left <= 160 && red.right < red_end);
  assert(date_dark.left >= red_end - 1 && date_dark.left <= red_end + 1);
  assert(red.bottom == 27 && date_dark.top >= 13);
  // Balance: right-aligned so the text ends at x 423 (exclusive), on
  // baseline 26 (digits end on row 25; the '$' stem may reach row 26).
  assert(balance_box.right >= 420 && balance_box.right <= 422);
  assert(balance_box.bottom >= 25 && balance_box.bottom <= 26);
  // Status: from (43, baseline 44), clipped to the field's bottom row 43
  // (descenders are cut off, as GDI's DrawText clip does).
  assert(status_box.left >= 43 && status_box.left <= 44);
  assert(status_box.bottom == 43);
  // Population: measured, then drawn to end at x 423, on baseline 43.
  assert(population_box.right >= 420 && population_box.right <= 422);
  assert(population_box.left >= 423 - font.extent(13, "12345"));
  assert(population_box.bottom == 42);

  // The canvas DrawText drops '&' prefixes but draws no underline. That is
  // only safe while no Info status string has one.
  for (const int id : {1003, 1007, 1009, 1010}) {
    const auto strl = resources.find("STRL", id);
    for (std::uint16_t index = 1U; index < 256U; ++index) {
      assert(simtower::original_strl_entry(strl, index).find('&') ==
             std::string::npos);
    }
  }
}

// Records the painter's OriginalDrawContext calls, one line per call.
class RecordingDrawContext final : public simtower::OriginalDrawContext {
 public:
  explicit RecordingDrawContext(const simtower::OriginalResources& resources)
      : resources_(resources) {}

  std::vector<std::string> calls;

  int save() override {
    calls.push_back("save");
    return ++depth_;
  }
  void restore(int saved) override {
    calls.push_back("restore " + std::to_string(saved));
    depth_ = saved - 1;
  }
  void fill_rect(const simtower::OriginalRect& r,
                 simtower::OriginalRgb rgb) override {
    calls.push_back("fill " + rect(r) + " " + hex(rgb));
  }
  void intersect_clip(const simtower::OriginalRect& r) override {
    calls.push_back("clip " + rect(r));
  }
  void draw_dib(std::span<const std::byte> resource, int x, int y) override {
    std::string id = "?";
    for (const int candidate : {320, 321, 322, 323, 327}) {
      if (resources_.find("BITMAP", candidate).data() == resource.data()) {
        id = std::to_string(candidate);
      }
    }
    calls.push_back("dib " + id + " " + point(x, y));
  }
  void push_solid_pen(simtower::OriginalRgb rgb) override {
    calls.push_back("pen+ " + hex(rgb));
  }
  void pop_pen() override { calls.push_back("pen-"); }
  void move_to(int x, int y) override { calls.push_back("move " + point(x, y)); }
  void line_to(int x, int y) override { calls.push_back("line " + point(x, y)); }
  void push_font(std::int16_t h) override {
    calls.push_back("font+ " + std::to_string(h));
  }
  void pop_font() override { calls.push_back("font-"); }
  void set_text_color(simtower::OriginalRgb rgb) override {
    calls.push_back("color " + hex(rgb));
  }
  void set_bk_transparent() override { calls.push_back("transparent"); }
  void set_text_align(std::uint16_t flags) override {
    calls.push_back("align " + std::to_string(flags));
  }
  void text_out(std::string_view text) override {
    calls.push_back("text " + std::string(text));
  }
  void draw_text_single_line(std::string_view text,
                             const simtower::OriginalRect& r) override {
    calls.push_back("drawtext " + std::string(text) + " " + rect(r));
  }
  int text_extent_cx(std::string_view text) override {
    calls.push_back("extent " + std::string(text));
    return 20;
  }

 private:
  static std::string point(int x, int y) {
    return std::to_string(x) + "," + std::to_string(y);
  }
  static std::string rect(const simtower::OriginalRect& r) {
    return point(r.left, r.top) + "," + point(r.right, r.bottom);
  }
  static std::string hex(simtower::OriginalRgb rgb) {
    char text[8];
    std::snprintf(text, sizeof(text), "%06x", static_cast<unsigned>(rgb));
    return text;
  }

  const simtower::OriginalResources& resources_;
  int depth_{};
};

void check_info_call_sequence(const simtower::OriginalResources& resources) {
  // The painter's call order, which OriginalGdiDrawContext turns 1:1 into
  // GDI calls. It is the order of the direct GDI code it replaced
  // (legacy_info_painter_win32.hpp); see original_draw_context.hpp for the
  // GDI call(s) behind each entry.
  auto tower = simtower::make_original_new_tdt();
  tower.header.rating = 3U;
  tower.header.balance = 123;
  tower.header.current_day = 2;
  tower.header.frame_time = 0U;
  tower.post_elevator.finance.total_population = 456;
  const auto content =
      simtower::build_original_info_content(resources, &tower, "Status");
  RecordingDrawContext recorder(resources);
  simtower::draw_original_info(recorder, resources, &tower, "Status");
  const auto hand = [](const simtower::OriginalInfoPoint& p) {
    return "line " + std::to_string(20 + p.x) + "," + std::to_string(28 + p.y);
  };
  const std::vector<std::string> expected{
      "save",
      "dib 320 0,8",
      // 1118:045d date
      "fill 151,13,303,29 999999",
      "font+ 14",
      "transparent",
      "align 25",
      "move 159,28",
      "color b30000",
      "text " + content.date_red,
      "color 262626",
      "text " + content.date_dark,
      "font-",
      // 1118:0044 rating 3: five clipped star cells
      "save", "clip 43,11,64,30", "dib 322 43,11", "restore 2",
      "save", "clip 64,11,85,30", "dib 322 64,11", "restore 2",
      "save", "clip 85,11,106,30", "dib 322 85,11", "restore 2",
      "save", "clip 106,11,127,30", "dib 323 106,11", "restore 2",
      "save", "clip 127,11,148,30", "dib 323 127,11", "restore 2",
      // 1118:0143 balance
      "fill 354,13,424,27 999999",
      "font+ 13",
      "color 262626",
      "transparent",
      "align 27",
      "move 423,26",
      "text $12300",
      "font-",
      // 1118:026a status
      "fill 41,33,303,44 d9d9d9",
      "fill 41,44,303,45 ffffff",
      "font+ 12",
      "color 262626",
      "transparent",
      "align 25",
      "move 43,43",  // 1118:02fb then 0307 (0, -1): field.bottom - 1
      "drawtext Status 41,33,303,44",
      "font-",
      // 1118:0368 population
      "fill 337,31,423,45 999999",
      "font+ 13",
      "extent 456",
      "color 262626",
      "transparent",
      "align 25",
      "move 403,43",
      "text 456",
      "font-",
      // 1118:073d clock
      "save", "clip 5,13,36,44", "dib 321 5,13", "restore 2",
      "pen+ 000000",
      "move 20,28",
      hand(content.minute_hand),
      "move 20,28",
      hand(content.hour_hand),
      "pen-",
      "restore 1",
  };
  if (recorder.calls != expected) {
    for (std::size_t i = 0; i < std::max(recorder.calls.size(),
                                         expected.size());
         ++i) {
      std::printf("%3zu %-34s %s\n", i,
                  i < recorder.calls.size() ? recorder.calls[i].c_str() : "-",
                  i < expected.size() ? expected[i].c_str() : "-");
    }
  }
  assert(recorder.calls == expected);
}

#ifdef _WIN32
std::vector<std::pair<simtower::OriginalTdtDocument, std::string>>
gdi_cases() {
  std::vector<std::pair<simtower::OriginalTdtDocument, std::string>> cases;
  auto tower = simtower::make_original_new_tdt();
  cases.push_back({tower, ""});
  cases.push_back({fixed_info_tower(), std::string(kFixedStatus)});
  for (std::uint16_t rating = 1U; rating <= 6U; ++rating) {
    tower.header.rating = rating;
    cases.push_back({tower, ""});
  }
  for (const std::int32_t day : {1, 2, 3, 12, 14, 400}) {
    tower.header.current_day = day;
    cases.push_back({tower, "Not enough money for construction"});
  }
  for (const std::int32_t balance : {-1, 0, 123, 99999999}) {
    tower.header.balance = balance;
    cases.push_back({tower, "A&&B &x"});
  }
  for (const std::int32_t population : {0, 7, 123456}) {
    tower.post_elevator.finance.total_population = population;
    cases.push_back({tower, "Income from Office"});
  }
  for (const std::uint16_t frame_time : {0U, 399U, 1200U, 2399U, 2600U}) {
    tower.header.frame_time = frame_time;
    cases.push_back({tower, ""});
  }
  return cases;
}

void check_info_gdi_unchanged(const simtower::OriginalResources& resources) {
  // The refactored GDI path (draw_original_info(HDC) -> OriginalGdiDrawContext)
  // paints exactly what the direct GDI painter painted, pixel for pixel.
  // Also covers the null-document startup paint.
  std::size_t compared{};
  const auto compare = [&](const simtower::OriginalTdtDocument* document,
                           std::string_view status) {
    TestDibSurface legacy(kInfoWidth, kInfoHeight);
    TestDibSurface current(kInfoWidth, kInfoHeight);
    legacy_info::draw_original_info(legacy.dc, resources, document, status);
    simtower::draw_original_info(current.dc, resources, document, status);
    assert(surface_pixels(legacy) == surface_pixels(current));
    ++compared;
  };
  compare(nullptr, {});
  compare(nullptr, "Construction status");
  for (const auto& [document, status] : gdi_cases()) {
    compare(&document, status);
  }
  std::printf("info gdi unchanged: %zu cases identical\n", compared);
}

void check_info_gdi_canvas_parity(
    const simtower::OriginalResources& resources) {
  // GDI and the canvas agree exactly outside the text (BITMAP art, field
  // fills, star clips, clock hands), and place each field's text within two
  // pixels of each other. Glyph shapes differ: GDI uses the system Arial with
  // hinting (and possibly smoothing), the canvas the bundled Liberation Sans,
  // unhinted and thresholded. Text is not clipped except the status, so each
  // field's text region is wider than its fill; outside the fill, text is
  // whatever differs from the BITMAP/320 backing.
  const auto gdi = gdi_backend(resources);
  const auto canvas = canvas_backend(resources);
  struct Field {
    simtower::OriginalRect text;
    simtower::OriginalRect fill;
    std::uint32_t fill_rgb;
  };
  const Field fields[] = {
      {{151, 13, 303, 31}, {151, 13, 303, 29}, 0x00999999U},  // date
      {{303, 10, 424, 28}, {354, 13, 424, 27}, 0x00999999U},  // balance
      {{41, 33, 303, 44}, {41, 33, 303, 44}, 0x00d9d9d9U},    // status
      {{303, 31, 423, 46}, {337, 31, 423, 45}, 0x00999999U},  // population
  };
  const auto inside = [](const simtower::OriginalRect& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
  };
  const auto backing_a = gdi.backing();
  const auto backing_b = canvas.backing();
  // Worst |gdi - canvas| per text-box edge: left, right, top, bottom.
  int worst[4]{};
  for (const auto& [document, status] : gdi_cases()) {
    const auto a = gdi.info(&document, status);
    const auto b = canvas.info(&document, status);
    TextBox boxes_a[4];
    TextBox boxes_b[4];
    for (int y = simtower::kOriginalInfoClientTop; y < kInfoHeight; ++y) {
      for (int x = 0; x < kInfoWidth; ++x) {
        int field = -1;
        for (int i = 0; i < 4; ++i) {
          if (inside(fields[i].text, x, y)) field = i;
        }
        if (field < 0) {
          if (pixel(a, x, y) != pixel(b, x, y)) {
            std::printf("gdi/canvas differ at %d,%d: %06x %06x\n", x, y,
                        pixel(a, x, y), pixel(b, x, y));
          }
          assert(pixel(a, x, y) == pixel(b, x, y));
          continue;
        }
        const bool filled = inside(fields[field].fill, x, y);
        const auto background_a =
            filled ? fields[field].fill_rgb : pixel(backing_a, x, y);
        const auto background_b =
            filled ? fields[field].fill_rgb : pixel(backing_b, x, y);
        if (pixel(a, x, y) != background_a) boxes_a[field].add(x, y);
        if (pixel(b, x, y) != background_b) boxes_b[field].add(x, y);
      }
    }
    for (int i = 0; i < 4; ++i) {
      assert((boxes_a[i].count == 0U) == (boxes_b[i].count == 0U));
      if (boxes_a[i].count == 0U) continue;
      const int deltas[] = {boxes_a[i].left - boxes_b[i].left,
                            boxes_a[i].right - boxes_b[i].right,
                            boxes_a[i].top - boxes_b[i].top,
                            boxes_a[i].bottom - boxes_b[i].bottom};
      for (int edge = 0; edge < 4; ++edge) {
        worst[edge] = std::max(worst[edge], std::abs(deltas[edge]));
      }
    }
  }
  std::printf("info gdi/canvas parity: worst text box delta left %d right %d "
              "top %d bottom %d px\n",
              worst[0], worst[1], worst[2], worst[3]);
  assert(worst[0] <= 2 && worst[1] <= 2 && worst[2] <= 2);
  // Liberation Sans' 'Q' tail reaches three rows below the baseline at
  // 14 pixels; hinted Arial keeps it on the baseline.
  assert(worst[3] <= 3);
}
#endif  // _WIN32

}  // namespace

int main(int argc, char** argv) {
  // Unbuffered, so diagnostics printed before a failed assert survive.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  {
    // Direct 1060:0083 modal-launch wrapper coverage.
    assert(simtower::original_finance_launcher_contract() ==
           (simtower::OriginalFinanceLauncherContract{500U, true, false}));
    // COUNTDLOGMAIN 1060:02a4-037e visibly presses and releases its custom
    // button before closing for Return/Space; unrelated key-up is ignored.
    using Action = simtower::OriginalFinanceDialogMessageAction;
    using MessagePlan = simtower::OriginalFinanceDialogMessagePlan;
    using Presentation = simtower::OriginalFinanceKeyPresentation;
    assert(simtower::original_finance_dialog_message_plan(0x000fU) ==
           (MessagePlan{Action::paint, false}));
    assert(simtower::original_finance_dialog_message_plan(0x0019U) ==
           (MessagePlan{Action::control_color, false}));
    assert(simtower::original_finance_dialog_message_plan(0x0101U) ==
           (MessagePlan{Action::key_up, false}));
    assert(simtower::original_finance_dialog_message_plan(0x0110U) ==
           (MessagePlan{Action::initialize, true}));
    assert(simtower::original_finance_dialog_message_plan(0x0201U) ==
           (MessagePlan{Action::left_button_down, false}));
    assert(simtower::original_finance_dialog_message_plan(0x0202U) ==
           (MessagePlan{Action::left_button_up, false}));
    assert(simtower::original_finance_dialog_message_plan(0x0010U) ==
           (MessagePlan{Action::unhandled, false}));
    assert(simtower::original_finance_key_presentation(0x0dU) ==
           (Presentation{true, true, true}));
    assert(simtower::original_finance_key_presentation(0x20U) ==
           (Presentation{true, true, true}));
    assert(simtower::original_finance_key_presentation(0x1bU) ==
           (Presentation{false, false, false}));
    // Direct 11e0:00ca and 1208:0c89/07a5 formatter-placement coverage after
    // text measurement.
    using ValuePosition = simtower::OriginalFinanceValuePosition;
    assert(simtower::original_finance_value_position(6U, 100, 20, 17) ==
           (ValuePosition{83, 21}));
    assert(simtower::original_finance_value_position(8U, 100, 20, 17) ==
           (ValuePosition{143, 22}));
    assert(simtower::original_finance_value_position(9U, 100, 20, 17) ==
           (ValuePosition{143, 22}));
    assert(simtower::original_finance_value_position(10U, 100, 20, 17) ==
           (ValuePosition{147, 21}));
    assert(simtower::original_finance_value_position(14U, 100, 20, 17) ==
           (ValuePosition{147, 21}));
    assert(simtower::original_finance_value_position(15U, 100, 20, 17) ==
           (ValuePosition{83, 21}));
  }

  assert(argc == 2);
  const auto pack = read_file(argv[1]);
  const simtower::OriginalResources resources(pack);

  // Direct 1128:13fc coverage: startup's Info backing is the complete
  // 431x41 BITMAP/320 surface; the four adjoining resources are the exact
  // rating, clock, and special-rating overlays consumed by its painters.
  const struct {
    int id;
    int width;
    int height;
  } bitmaps[] = {{320, 431, 41}, {321, 32, 31}, {322, 24, 19},
                 {323, 24, 19},  {327, 108, 22}};
  for (const auto& expected : bitmaps) {
    const auto dib = simtower::original_dib_view(
        resources.find("BITMAP", expected.id));
    assert(dib.width == expected.width);
    assert(dib.height == expected.height);
    assert(dib.bit_count == 8U);
  }

  // The Info painter's field checks on every backend: the software canvas
  // everywhere, and GDI on Windows.
  check_info_fields(resources, canvas_backend(resources));
#ifdef _WIN32
  check_info_fields(resources, gdi_backend(resources));
#endif
  check_info_canvas(resources);
  check_info_call_sequence(resources);
#ifdef _WIN32
  check_info_gdi_unchanged(resources);
  check_info_gdi_canvas_parity(resources);
#endif

  for (const int id : {500, 501}) {
    const auto dib = simtower::original_dib_view(
        resources.find("BITMAP", id));
    assert(dib.width == 343);
    assert(dib.height == 364);
    assert(dib.bit_count == 8U);
  }
  // Direct COUNTDLOGMAIN 1060:00d3 coverage: its custom Finance surface is
  // BITMAP/500, DTMP/500 rectangle one is swapped from BITMAP/501 while the
  // mouse is held, and 0479 derives the displayed table and summary values.
  // The resource/raster/data assertions below exercise those paths without
  // opening the modal dialog.
  const auto finance_dtmp = simtower::parse_original_dtmp(
      resources.find("DTMP", 500));
  assert(finance_dtmp.bitmap_reference == "500");
  assert(finance_dtmp.width_or_header == 0U);
  assert(finance_dtmp.height_or_header == 0U);
  assert(finance_dtmp.rectangles ==
         std::vector<simtower::OriginalDtmpRect>({
             {131, 326, 223, 347}, {32, 335, 96, 349},
             {87, 78, 133, 210},   {133, 78, 189, 210},
             {268, 78, 324, 210},  {72, 40, 153, 54},
             {225, 40, 306, 54},   {98, 7, 121, 21},
             {170, 7, 193, 21},    {165, 232, 246, 246},
             {165, 249, 246, 263}, {165, 266, 246, 280},
             {165, 283, 246, 297}, {165, 300, 246, 314},
         }));

#ifdef _WIN32
  // The partial DIB path used for the Finance pressed state must preserve
  // every BITMAP/500 pixel outside DTMP rectangle one and reproduce the same
  // BITMAP/501 pixels inside it. This is a memory-DC test; no window opens.
  TestDibSurface finance_base(343, 364);
  TestDibSurface finance_pressed(343, 364);
  TestDibSurface finance_composite(343, 364);
  simtower::draw_original_dib(
      finance_base.dc, resources.find("BITMAP", 500), 0, 0);
  simtower::draw_original_dib(
      finance_pressed.dc, resources.find("BITMAP", 501), 0, 0);
  simtower::draw_original_dib(
      finance_composite.dc, resources.find("BITMAP", 500), 0, 0);
  const auto& finance_button = finance_dtmp.rectangles[0];
  simtower::draw_original_dib_region(
      finance_composite.dc, resources.find("BITMAP", 501),
      finance_button.left, finance_button.top,
      finance_button.left, finance_button.top,
      finance_button.right - finance_button.left,
      finance_button.bottom - finance_button.top);
  bool finance_button_changed = false;
  for (int y = 0; y < 364; ++y) {
    for (int x = 0; x < 343; ++x) {
      const auto index = static_cast<std::size_t>(y * 343 + x);
      const bool inside = x >= finance_button.left &&
                          x < finance_button.right &&
                          y >= finance_button.top &&
                          y < finance_button.bottom;
      if (inside) {
        assert(finance_composite.pixels[index] ==
               finance_pressed.pixels[index]);
        finance_button_changed = finance_button_changed ||
            finance_base.pixels[index] != finance_pressed.pixels[index];
      } else {
        assert(finance_composite.pixels[index] == finance_base.pixels[index]);
      }
    }
  }
  assert(finance_button_changed);
#endif  // _WIN32

  // Direct 1118:0ce7 coverage: values outside 10..19 use signed IDIV by ten
  // and select STRL/713 entries 11..13 only for positive remainders 1..3;
  // the teens and every other remainder use entry 14.
  assert(simtower::original_info_ordinal(resources, -1) == "-1th ");
  assert(simtower::original_info_ordinal(resources, 0) == "0th ");
  assert(simtower::original_info_ordinal(resources, 1) == "1st ");
  assert(simtower::original_info_ordinal(resources, 2) == "2nd ");
  assert(simtower::original_info_ordinal(resources, 3) == "3rd ");
  assert(simtower::original_info_ordinal(resources, 4) == "4th ");
  assert(simtower::original_info_ordinal(resources, 11) == "11th ");
  assert(simtower::original_info_ordinal(resources, 12) == "12th ");
  assert(simtower::original_info_ordinal(resources, 13) == "13th ");
  assert(simtower::original_info_ordinal(resources, 19) == "19th ");
  assert(simtower::original_info_ordinal(resources, 20) == "20th ");
  assert(simtower::original_info_ordinal(resources, 21) == "21st ");
  assert(simtower::original_info_ordinal(resources, 101) == "101st ");

  // Direct 1200:058d coverage: exercise every phase switch and each adjoining
  // 400-tick boundary, including the two half-speed midnight phases and the
  // final twelve-hour sweep.
  assert((simtower::original_info_clock_time(0) ==
          simtower::OriginalInfoClockTime{7, 0}));
  assert((simtower::original_info_clock_time(399) ==
          simtower::OriginalInfoClockTime{11, 59}));
  assert((simtower::original_info_clock_time(400) ==
          simtower::OriginalInfoClockTime{0, 0}));
  assert((simtower::original_info_clock_time(799) ==
          simtower::OriginalInfoClockTime{0, 29}));
  assert((simtower::original_info_clock_time(800) ==
          simtower::OriginalInfoClockTime{0, 30}));
  assert((simtower::original_info_clock_time(1199) ==
          simtower::OriginalInfoClockTime{0, 59}));
  assert((simtower::original_info_clock_time(1200) ==
          simtower::OriginalInfoClockTime{1, 0}));
  assert((simtower::original_info_clock_time(1599) ==
          simtower::OriginalInfoClockTime{4, 59}));
  assert((simtower::original_info_clock_time(1600) ==
          simtower::OriginalInfoClockTime{5, 0}));
  assert((simtower::original_info_clock_time(1999) ==
          simtower::OriginalInfoClockTime{8, 59}));
  assert((simtower::original_info_clock_time(2000) ==
          simtower::OriginalInfoClockTime{9, 0}));
  assert((simtower::original_info_clock_time(2399) ==
          simtower::OriginalInfoClockTime{0, 59}));
  assert((simtower::original_info_clock_time(2400) ==
          simtower::OriginalInfoClockTime{1, 0}));

  // Direct 1200:0037 and 1000:0f1c/1049 table/runtime-helper coverage: the
  // executable uses index*314/3000, floors both trigonometric products, then
  // negates the floored cosine.
  assert((simtower::original_info_clock_point(0, 15) ==
          simtower::OriginalInfoPoint{0, -15}));
  assert((simtower::original_info_clock_point(15, 15) ==
          simtower::OriginalInfoPoint{14, 0}));
  assert((simtower::original_info_clock_point(30, 15) ==
          simtower::OriginalInfoPoint{0, 15}));
  assert((simtower::original_info_clock_point(45, 15) ==
          simtower::OriginalInfoPoint{-15, 1}));
  assert((simtower::original_info_clock_point(15, 9) ==
          simtower::OriginalInfoPoint{8, 0}));

  // 1208:05e6 applies 1000:39b5's signed 32-bit shift before every timestamp.
  assert(simtower::original_coarse_tick(0U) == 0U);
  assert(simtower::original_coarse_tick(15U) == 0U);
  assert(simtower::original_coarse_tick(16U) == 1U);
  assert(simtower::original_coarse_tick(0x7fffffffU) == 0x07ffffffU);
  assert(simtower::original_coarse_tick(0x80000000U) == 0xf8000000U);
  assert(simtower::original_coarse_tick(0xffffffffU) == 0xffffffffU);
  assert(simtower::original_tick_magnitude_delta(900U, 1000U) == 100U);
  assert(simtower::original_tick_magnitude_delta(0x10U, 0xfffffff0U) ==
         0x20U);

  // 1118:0933/09be/0a49/0ad5 share DS:784c but retain their original
  // resource lists, priorities, zero-index clearing, and replacement gates.
  simtower::OriginalInfoStatusState status{};
  assert(simtower::set_original_info_income_status(resources, status, 1U,
                                                    1000U));
  assert(status.text == "Income from Office" && status.priority == 0 &&
         status.started_tick == 1000U);
  assert(simtower::set_original_info_command_status(resources, status, 8U,
                                                     1010U));
  assert(status.text == "Office - $40000" && status.priority == 1 &&
         status.started_tick == 1010U);
  assert(!simtower::set_original_info_income_status(resources, status, 2U,
                                                     1020U));
  assert(status.text == "Office - $40000" && status.started_tick == 1010U);
  assert(simtower::set_original_info_notification_status(resources, status,
                                                          5U, 1030U));
  assert(status.text == "Office workers demand Parking" &&
         status.priority == 2 && status.started_tick == 1030U);
  assert(!simtower::set_original_info_command_status(resources, status, 4U,
                                                      1040U));
  assert(status.text == "Office workers demand Parking");
  assert(simtower::set_original_info_construction_status(resources, status,
                                                          7U, 1050U));
  assert(status.text == "Not enough money for construction" &&
         status.priority == 2 && status.started_tick == 1050U);
  // Direct 1118:08f3 boundary: equality at 300 retains the message and the
  // first greater magnitude delta clears through the construction writer.
  assert(!simtower::expire_original_info_status(status, 1350U));
  assert(simtower::expire_original_info_status(status, 1351U));
  assert(status.text.empty() && status.started_tick == 0U &&
         status.priority == 0);

  // 1000:39ea takes the signed magnitude of a wrapping tick difference.
  assert(simtower::set_original_info_income_status(resources, status, 2U,
                                                    1000U));
  assert(!simtower::expire_original_info_status(status, 900U));
  assert(status.text == "Income from Hotel");
  assert(simtower::expire_original_info_status(status, 699U));

  assert(simtower::set_original_info_command_status(resources, status, 4U,
                                                     2000U));
  assert(simtower::set_original_info_command_status(resources, status, 0U,
                                                     2010U));
  assert(status.text.empty() && status.priority == 0 &&
         status.started_tick == 0U);
  assert(!simtower::expire_original_info_status(status, 0xffffffffU));

  // The 300-unit comparisons above are coarse ticks, not milliseconds:
  // 4,800 ms remains visible and the following 16-ms bucket expires it.
  assert(simtower::set_original_info_income_status(
      resources, status, 1U, simtower::original_coarse_tick(16U)));
  assert(!simtower::expire_original_info_status(
      status, simtower::original_coarse_tick(4816U)));
  assert(simtower::expire_original_info_status(
      status, simtower::original_coarse_tick(4832U)));

  auto tower = simtower::make_original_new_tdt();
  tower.header.rating = 3U;
  tower.header.balance = 123;
  tower.header.frame_time = 0U;
  tower.header.current_day = 0;
  tower.post_elevator.finance.total_population = 456;
  // Direct 1208:0004 coverage: the original shared _wsprintf thunk formats
  // the signed balance/population/date integers used by these Info fields.
  auto content = simtower::build_original_info_content(
      resources, &tower, "Construction message");
  assert(content.rating == 3U);
  assert(content.balance == "$12300");
  assert(content.population == "456");
  assert(content.date_red.empty());
  assert(content.date_dark == "1st WD/1Q/1st Year");
  assert(content.status == "Construction message");
  assert((content.clock == simtower::OriginalInfoClockTime{7, 0}));
  assert((content.minute_hand == simtower::OriginalInfoPoint{0, -15}));

  tower.header.current_day = 1;
  content = simtower::build_original_info_content(resources, &tower);
  assert(content.date_red.empty());
  assert(content.date_dark == "2nd WD/1Q/1st Year");

  tower.header.current_day = 2;
  content = simtower::build_original_info_content(resources, &tower);
  assert(content.date_red == "WE");
  assert(content.date_dark == "/1Q/1st Year");

  tower.header.current_day = 3;
  content = simtower::build_original_info_content(resources, &tower);
  assert(content.date_red.empty());
  assert(content.date_dark == "1st WD/2Q/1st Year");

  tower.header.current_day = 12;
  content = simtower::build_original_info_content(resources, &tower);
  assert(content.date_dark == "1st WD/1Q/2nd Year");

  tower.header.balance = -1;
  content = simtower::build_original_info_content(resources, &tower);
  assert(content.balance == "$-100");

  // 1060:0479 copies the three persisted ten-row finance series and derives
  // the nine DTMP/500 summary fields with signed IDIV and wrapping SUB.
  tower.header.current_day = 14;
  tower.header.other_income = -200;
  tower.header.construction_costs = -300;
  tower.header.last_quarter_money = 400;
  tower.header.balance = 500;
  for (std::size_t index = 0U; index < 10U; ++index) {
    tower.post_elevator.finance.population_by_category[index] =
        static_cast<std::int32_t>(index + 1U);
    tower.post_elevator.finance.income_by_category[index] =
        static_cast<std::int32_t>(100U + index);
    tower.post_elevator.finance.maintenance_by_category[index] =
        -static_cast<std::int32_t>(200U + index);
  }
  tower.post_elevator.finance.total_income = 1000;
  tower.post_elevator.finance.total_maintenance = 250;
  auto finance = simtower::derive_original_finance_view(tower);
  assert(finance.population ==
         tower.post_elevator.finance.population_by_category);
  assert(finance.income == tower.post_elevator.finance.income_by_category);
  assert(finance.maintenance ==
         tower.post_elevator.finance.maintenance_by_category);
  assert(finance.total_income == 1000);
  assert(finance.total_maintenance == 250);
  assert(finance.year == 2);
  assert(finance.quarter == 1);
  assert(finance.net_revenues == 750);
  assert(finance.other_income == -200);
  assert(finance.construction_costs == -300);
  assert(finance.last_quarter_balance == 400);
  assert(finance.total_balance == 500);

  tower.header.current_day = -1;
  tower.post_elevator.finance.total_income =
      std::numeric_limits<std::int32_t>::min();
  tower.post_elevator.finance.total_maintenance = 1;
  finance = simtower::derive_original_finance_view(tower);
  assert(finance.year == 1);
  assert(finance.quarter == 1);
  assert(finance.net_revenues ==
         std::numeric_limits<std::int32_t>::max());
  return 0;
}
