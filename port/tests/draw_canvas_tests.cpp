// OriginalCanvasDrawContext primitives against the GDI rules they stand in
// for. Synthetic inputs only (no game assets), so this runs everywhere.

#include "original_canvas_font.hpp"
#include "original_dib.hpp"
#include "original_draw_canvas.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <utility>
#include <vector>

namespace {

using simtower::OriginalCanvasDrawContext;
using simtower::OriginalCanvasFont;
using simtower::OriginalRect;
using simtower::OriginalWorldRaster;

constexpr std::uint32_t kWhite = 0xffffffU;

OriginalWorldRaster blank(int width, int height) {
  return {width, height,
          std::vector<std::uint32_t>(
              static_cast<std::size_t>(width) * height, kWhite)};
}

std::uint32_t at(const OriginalWorldRaster& raster, int x, int y) {
  return raster.pixels[static_cast<std::size_t>(y) * raster.width + x];
}

std::set<std::pair<int, int>> pixels_of(const OriginalWorldRaster& raster,
                                        std::uint32_t rgb) {
  std::set<std::pair<int, int>> result;
  for (int y = 0; y < raster.height; ++y) {
    for (int x = 0; x < raster.width; ++x) {
      if (at(raster, x, y) == rgb) result.insert({x, y});
    }
  }
  return result;
}

// A Win3 DIB resource: BITMAPINFOHEADER, color table, packed scanlines in
// storage order (bottom-up when height > 0).
std::vector<std::byte> make_dib(int width,
                                int height,
                                std::uint16_t bits,
                                const std::vector<std::uint32_t>& colors,
                                const std::vector<std::uint8_t>& rows) {
  simtower::OriginalBitmapInfoHeader header{};
  header.biSize = sizeof(header);
  header.biWidth = width;
  header.biHeight = height;
  header.biPlanes = 1U;
  header.biBitCount = bits;
  header.biCompression = simtower::kOriginalDibCompressionRgb;
  header.biClrUsed = static_cast<std::uint32_t>(colors.size());
  std::vector<std::byte> bytes(sizeof(header) + colors.size() * 4U +
                               rows.size());
  std::memcpy(bytes.data(), &header, sizeof(header));
  for (std::size_t i = 0; i < colors.size(); ++i) {
    const simtower::OriginalRgbQuad quad{
        static_cast<std::uint8_t>(colors[i] & 0xffU),
        static_cast<std::uint8_t>((colors[i] >> 8U) & 0xffU),
        static_cast<std::uint8_t>((colors[i] >> 16U) & 0xffU), 0U};
    std::memcpy(bytes.data() + sizeof(header) + i * 4U, &quad, 4U);
  }
  std::memcpy(bytes.data() + sizeof(header) + colors.size() * 4U,
              rows.data(), rows.size());
  return bytes;
}

}  // namespace

int main() {
  auto& font = OriginalCanvasFont::bundled();

  {
    // FillRect excludes right/bottom; SaveDC/IntersectClipRect/RestoreDC
    // limits a fill and then lifts the clip again.
    auto raster = blank(20, 20);
    OriginalCanvasDrawContext dc(raster, font);
    dc.fill_rect({2, 3, 6, 5}, 0x112233U);
    assert(pixels_of(raster, 0x112233U).size() == 8U);
    assert(at(raster, 2, 3) == 0x112233U && at(raster, 5, 4) == 0x112233U);
    assert(at(raster, 6, 4) == kWhite && at(raster, 5, 5) == kWhite);
    const int saved = dc.save();
    assert(saved == 1);
    dc.intersect_clip({10, 10, 12, 12});
    dc.fill_rect({0, 0, 20, 20}, 0x445566U);
    assert(pixels_of(raster, 0x445566U).size() == 4U);
    dc.restore(saved);
    dc.fill_rect({0, 19, 20, 20}, 0x778899U);
    assert(pixels_of(raster, 0x778899U).size() == 20U);
    // An inverted rectangle fills nothing.
    dc.fill_rect({5, 5, 4, 6}, 0x010101U);
    assert(pixels_of(raster, 0x010101U).empty());
  }

  {
    // The origin offset and the bounds (a window client inside a larger
    // window raster): logical (0,0) lands at the origin and nothing escapes
    // the bounds.
    auto raster = blank(30, 30);
    OriginalCanvasDrawContext dc(raster, font, 10, 5, {0, 0, 4, 3});
    dc.fill_rect({-5, -5, 50, 50}, 0x123456U);
    const auto filled = pixels_of(raster, 0x123456U);
    assert(filled.size() == 12U);
    assert(filled.count({10, 5}) == 1U && filled.count({13, 7}) == 1U);
  }

  {
    // SetDIBitsToDevice with DIB_RGB_COLORS: 8-bit bottom-up, 4-bit and
    // 1-bit top-down, each through its own color table, clipped.
    auto raster = blank(16, 16);
    OriginalCanvasDrawContext dc(raster, font);
    // 2x2 8-bit, bottom-up: stored rows are bottom (2,3) then top (0,1).
    const auto dib8 =
        make_dib(2, 2, 8U, {0x000001U, 0x000002U, 0x000003U, 0x000004U},
                 {2, 3, 0, 0, 0, 1, 0, 0});
    dc.draw_dib(dib8, 1, 1);
    assert(at(raster, 1, 1) == 0x000001U && at(raster, 2, 1) == 0x000002U);
    assert(at(raster, 1, 2) == 0x000003U && at(raster, 2, 2) == 0x000004U);
    // 3x1 4-bit, top-down: nibbles 1,2,3.
    const auto dib4 = make_dib(3, -1, 4U, std::vector<std::uint32_t>(16, 0U),
                               {0x12, 0x30, 0, 0});
    auto dib4_colors = dib4;
    {
      // Entry n is 0x0000n0 so the sampled index is visible.
      for (std::uint32_t i = 0; i < 16U; ++i) {
        const simtower::OriginalRgbQuad quad{
            static_cast<std::uint8_t>(i << 4U), 0U, 0U, 0U};
        std::memcpy(dib4_colors.data() + 40U + i * 4U, &quad, 4U);
      }
    }
    dc.draw_dib(dib4_colors, 5, 5);
    assert(at(raster, 5, 5) == 0x10U && at(raster, 6, 5) == 0x20U &&
           at(raster, 7, 5) == 0x30U);
    // 10x1 1-bit, top-down, clipped to two columns.
    const auto dib1 =
        make_dib(10, -1, 1U, {0xaaaaaaU, 0x555555U}, {0xa0, 0x40, 0, 0});
    const int saved = dc.save();
    dc.intersect_clip({0, 8, 2, 9});
    dc.draw_dib(dib1, 0, 8);
    dc.restore(saved);
    assert(at(raster, 0, 8) == 0x555555U && at(raster, 1, 8) == 0xaaaaaaU);
    assert(at(raster, 2, 8) == kWhite);
  }

  {
    // LineTo: end pixel excluded, current position moves to the end, and
    // exact error ties follow Wine's octant bias (checked with a GDI probe).
    auto raster = blank(64, 64);
    OriginalCanvasDrawContext dc(raster, font);
    dc.push_solid_pen(0x000000U);
    const auto line = [&](int dx, int dy) {
      std::fill(raster.pixels.begin(), raster.pixels.end(), kWhite);
      dc.move_to(32, 32);
      dc.line_to(32 + dx, 32 + dy);
      assert(dc.current_x() == 32 + dx && dc.current_y() == 32 + dy);
      std::set<std::pair<int, int>> relative;
      for (const auto& [x, y] : pixels_of(raster, 0x000000U)) {
        relative.insert({x - 32, y - 32});
      }
      return relative;
    };
    using Points = std::set<std::pair<int, int>>;
    // Pixel lists taken from Wine's GDI for the same vectors.
    assert(line(3, 1) == (Points{{0, 0}, {1, 0}, {2, 1}}));
    assert(line(-3, 1) == (Points{{-1, 0}, {0, 0}, {-2, 1}}));
    assert(line(2, 1) == (Points{{0, 0}, {1, 0}}));
    assert(line(-2, -1) == (Points{{-1, -1}, {0, 0}}));
    assert(line(0, 0).empty());
    assert(line(0, -3) == (Points{{0, 0}, {0, -1}, {0, -2}}));
    dc.pop_pen();
  }

  {
    // Text: rounded per-glyph advances with no kerning. The extents equal
    // Wine GDI's GetTextExtentPoint32A with Arial at the same sizes.
    assert(font.extent(12, "1st WD/1Q/1st Year") == 106);
    assert(font.extent(13, "1st WD/1Q/1st Year") == 117);
    assert(font.extent(14, "1st WD/1Q/1st Year") == 126);
    assert(font.extent(12, "$12300") == 42);
    assert(font.extent(14, "$12300") == 48);
    assert(font.extent(12, "Hg") == 16 && font.extent(14, "Hg") == 18);
    // Glyph masks are one bit per pixel.
    const auto& g = font.glyph(14, 'W');
    assert(!g.mask.empty());
    for (const auto bit : g.mask) assert(bit == 0U || bit == 1U);
    // Dropout control at 12 pixels keeps strokes that straddle two pixel
    // rows: the 't' crossbar and the closed top of 'm'. It leaves the gap
    // between the dot and the stem of 'i'.
    const auto longest_row_run = [&](unsigned char c) {
      const auto& glyph = font.glyph(12, c);
      int best = 0;
      for (int y = 0; y < glyph.height; ++y) {
        int run = 0;
        for (int x = 0; x < glyph.width; ++x) {
          run = glyph.mask[static_cast<std::size_t>(y * glyph.width + x)] != 0U
                    ? run + 1
                    : 0;
          best = std::max(best, run);
        }
      }
      return best;
    };
    assert(longest_row_run('t') >= 3);
    assert(longest_row_run('m') >= font.glyph(12, 'm').width - 2);
    {
      const auto& i = font.glyph(12, 'i');
      std::vector<bool> lit(static_cast<std::size_t>(i.height), false);
      for (int y = 0; y < i.height; ++y) {
        for (int x = 0; x < i.width; ++x) {
          if (i.mask[static_cast<std::size_t>(y * i.width + x)] != 0U) {
            lit[static_cast<std::size_t>(y)] = true;
          }
        }
      }
      assert(lit.front() && lit.back());
      assert(std::find(lit.begin(), lit.end(), false) != lit.end());
    }
    assert(simtower::original_cp1252_to_unicode(0x80U) == 0x20acU);
    assert(simtower::original_cp1252_to_unicode('A') == 'A');
    assert(simtower::original_cp1252_to_unicode(0xe9U) == 0xe9U);
  }

  {
    // TextOutA with TA_UPDATECP: left alignment draws right of the current
    // position and advances it; TA_RIGHT draws left of it and moves it back
    // (Wine: 10 -> 26 and 100 -> 84 for "Hg" at 12 pixels). TA_BASELINE
    // puts the baseline on the current position.
    auto raster = blank(120, 40);
    OriginalCanvasDrawContext dc(raster, font);
    dc.push_font(12);
    dc.set_bk_transparent();
    dc.set_text_color(0x000000U);
    dc.set_text_align(simtower::kOriginalTaUpdateCp |
                      simtower::kOriginalTaBaseline);
    dc.move_to(10, 30);
    dc.text_out("Hg");
    assert(dc.current_x() == 26 && dc.current_y() == 30);
    int left = 1000;
    int right = -1;
    int top = 1000;
    for (const auto& [x, y] : pixels_of(raster, 0x000000U)) {
      left = std::min(left, x);
      right = std::max(right, x);
      top = std::min(top, y);
    }
    assert(left >= 10 && right < 26 && top < 30);
    // 'H' sits on the baseline: its lowest pixel is the row above it.
    bool on_baseline_row = false;
    for (const auto& [x, y] : pixels_of(raster, 0x000000U)) {
      if (y == 29 && x < 19) on_baseline_row = true;
      assert(!(y >= 30 && x < 19));
    }
    assert(on_baseline_row);

    std::fill(raster.pixels.begin(), raster.pixels.end(), kWhite);
    dc.set_text_align(simtower::kOriginalTaUpdateCp | simtower::kOriginalTaRight |
                      simtower::kOriginalTaBaseline);
    dc.move_to(100, 30);
    dc.text_out("Hg");
    assert(dc.current_x() == 84);
    for (const auto& [x, y] : pixels_of(raster, 0x000000U)) {
      assert(x >= 84 && x < 100);
    }
    dc.pop_font();
  }

  {
    // DrawTextA(DT_SINGLELINE) under TA_UPDATECP|TA_BASELINE draws at the
    // current position, clips to the rectangle, and still advances the
    // position when the rectangle hides everything (Wine).
    auto raster = blank(200, 60);
    OriginalCanvasDrawContext dc(raster, font);
    dc.push_font(12);
    dc.set_bk_transparent();
    dc.set_text_color(0x000000U);
    dc.set_text_align(simtower::kOriginalTaUpdateCp |
                      simtower::kOriginalTaBaseline);
    dc.move_to(10, 40);
    dc.draw_text_single_line("Hg", {50, 5, 190, 30});
    assert(pixels_of(raster, 0x000000U).empty());
    assert(dc.current_x() == 26 && dc.current_y() == 40);
    dc.move_to(60, 25);
    dc.draw_text_single_line("Hg", {50, 5, 190, 30});
    const auto drawn = pixels_of(raster, 0x000000U);
    assert(!drawn.empty());
    for (const auto& [x, y] : drawn) assert(x >= 60 && x < 76 && y < 30);
    assert(dc.current_x() == 76);
    // Prefix characters: "&&" is one '&', a lone '&' is dropped.
    dc.move_to(0, 50);
    dc.draw_text_single_line("A&&B&C", {0, 0, 200, 60});
    assert(dc.current_x() == font.extent(12, "A&BC"));
    dc.pop_font();
  }

  {
    // The 1208:0ba7 bank: requests below nine alias the nine-pixel entry,
    // and the ten-entry bank refuses new heights (and even cached ones).
    OriginalCanvasFont bank(simtower::original_canvas_bundled_font_data());
    assert(bank.bank_select(4) == 9);
    assert(bank.bank_select(12) == 12);
    for (std::int16_t h = 20; h < 28; ++h) assert(bank.bank_select(h) == h);
    assert(bank.bank_select(30) == 0);
    assert(bank.bank_select(12) == 0);

    // A refused selection leaves the previous font in place: here the
    // stock font, which the canvas does not model (no text, zero extent).
    auto raster = blank(10, 10);
    OriginalCanvasDrawContext dc(raster, bank);
    dc.push_font(30);
    assert(dc.text_extent_cx("Hg") == 0);
    dc.pop_font();
    // The full bank refuses even the cached nine-pixel entry.
    dc.push_font(9);
    assert(dc.text_extent_cx("Hg") == 0);
    dc.pop_font();
  }

  {
    // RestoreDC brings back the font, alignment, colour and position.
    auto raster = blank(10, 10);
    OriginalCanvasDrawContext dc(raster, font);
    dc.push_font(12);
    dc.move_to(1, 2);
    const int saved = dc.save();
    dc.push_font(14);
    dc.move_to(7, 8);
    assert(dc.text_extent_cx("Hg") == 18);
    dc.restore(saved);
    assert(dc.text_extent_cx("Hg") == 16);
    assert(dc.current_x() == 1 && dc.current_y() == 2);
    // A second restore of the same level fails, as RestoreDC does.
    dc.move_to(3, 3);
    dc.restore(saved);
    assert(dc.current_x() == 3);
  }
  return 0;
}
