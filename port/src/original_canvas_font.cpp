#include "original_canvas_font.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace simtower {

struct OriginalCanvasFont::Impl {
  stbtt_fontinfo info{};
};

namespace {

// Half coverage; see the rendering rules in original_canvas_font.hpp.
constexpr unsigned char kCoverageThreshold = 128U;
// Lowest coverage the dropout pass may turn on. Below the 't' and 'f'
// crossbar halves at 12 pixels (102 and 91), above the gap between the dot
// and the stem of 'i' and 'j' (68 and 74).
constexpr unsigned char kDropoutThreshold = 88U;

int round_pixels(float value) noexcept {
  return static_cast<int>(std::floor(value + 0.5F));
}

// Mask values while the dropout pass runs; the stored mask is 0/1.
enum : std::uint8_t {
  kMaskOff = 0U,
  kMaskOn = 1U,           // coverage >= kCoverageThreshold
  kMaskHorizontal = 2U,   // turned on to keep a horizontal stroke
  kMaskVertical = 3U,     // turned on to keep a vertical stroke
};

// Dropout control in the spirit of the TrueType scan converter's (SCANTYPE
// with stubs excluded): a thin stroke that straddles two pixel rows or
// columns has under half coverage in both and vanishes at the plain
// threshold (the 't' crossbar, the top of 'm' and 'n', and the hyphen and
// the comma at 12-13 pixels). A pixel with kDropoutThreshold <= coverage <
// kCoverageThreshold is turned on when
// - across the stroke, neither neighbour is on and it has the most coverage
//   of the three (>= the first, > the second, so a 50/50 straddle picks
//   one), and
// - along the stroke, it bridges two on pixels or the stroke continues
//   (stepping over on pixels, the next pixel has coverage >=
//   kDropoutThreshold). This leaves stubs such as the extremes of bowls
//   and the tops of round letters alone.
// Candidates are visited by descending coverage (ties in raster order) and
// see the pixels already turned on, so the result is deterministic.
void apply_dropout_control(const std::vector<unsigned char>& coverage,
                           int width, int height,
                           std::vector<std::uint8_t>& mask) {
  const auto at = [&](const auto& plane, int x, int y) -> int {
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    return plane[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)];
  };
  const auto cov = [&](int x, int y) { return at(coverage, x, y); };
  const auto state = [&](int x, int y) { return at(mask, x, y); };
  const auto on = [&](int x, int y) { return state(x, y) != kMaskOff; };
  // dx/dy is the direction along the stroke; rule is the mark this pixel
  // would get. Pixels kept for the other direction belong to a crossing
  // stroke, so the walk steps over them like plain on pixels.
  const auto continues = [&](int x, int y, int dx, int dy,
                             std::uint8_t rule) {
    if (on(x - dx, y - dy) && on(x + dx, y + dy)) return true;
    for (const int sign : {-1, 1}) {
      int cx = x + sign * dx;
      int cy = y + sign * dy;
      while (state(cx, cy) == kMaskOn ||
             (state(cx, cy) > kMaskOn && state(cx, cy) != rule)) {
        cx += sign * dx;
        cy += sign * dy;
      }
      if (cov(cx, cy) >= kDropoutThreshold) return true;
    }
    return false;
  };

  std::vector<std::size_t> candidates;
  for (std::size_t i = 0; i < coverage.size(); ++i) {
    if (coverage[i] >= kDropoutThreshold && coverage[i] < kCoverageThreshold) {
      candidates.push_back(i);
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [&](std::size_t a, std::size_t b) {
                     return coverage[a] > coverage[b];
                   });
  for (const std::size_t i : candidates) {
    const int x = static_cast<int>(i % static_cast<std::size_t>(width));
    const int y = static_cast<int>(i / static_cast<std::size_t>(width));
    const int value = coverage[i];
    if (!on(x, y - 1) && !on(x, y + 1) && value >= cov(x, y - 1) &&
        value > cov(x, y + 1) && continues(x, y, 1, 0, kMaskHorizontal)) {
      mask[i] = kMaskHorizontal;
    } else if (!on(x - 1, y) && !on(x + 1, y) && value >= cov(x - 1, y) &&
               value > cov(x + 1, y) &&
               continues(x, y, 0, 1, kMaskVertical)) {
      mask[i] = kMaskVertical;
    }
  }
  for (auto& bit : mask) bit = bit != kMaskOff ? 1U : 0U;
}

}  // namespace

std::uint32_t original_cp1252_to_unicode(unsigned char ansi) noexcept {
  // 0x80..0x9f differ from Latin-1; the five unassigned bytes map to the
  // C1 controls as MultiByteToWideChar does.
  static constexpr std::array<std::uint16_t, 32> kHigh{
      0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
      0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
  if (ansi >= 0x80U && ansi < 0xa0U) return kHigh[ansi - 0x80U];
  return ansi;
}

OriginalCanvasFont::OriginalCanvasFont(std::span<const std::byte> ttf)
    : impl_(std::make_unique<Impl>()) {
  const auto* data = reinterpret_cast<const unsigned char*>(ttf.data());
  const int offset = stbtt_GetFontOffsetForIndex(data, 0);
  if (ttf.empty() || offset < 0 ||
      !stbtt_InitFont(&impl_->info, data, offset)) {
    throw std::runtime_error("Canvas font is not a TrueType font");
  }
}

OriginalCanvasFont::~OriginalCanvasFont() = default;

OriginalCanvasFont& OriginalCanvasFont::bundled() {
  static OriginalCanvasFont font(original_canvas_bundled_font_data());
  return font;
}

const OriginalCanvasGlyph& OriginalCanvasFont::glyph(int pixel_height,
                                                     unsigned char ansi) {
  auto& slot = glyphs_[pixel_height][ansi];
  if (slot) return *slot;

  auto glyph = std::make_unique<OriginalCanvasGlyph>();
  const float scale = stbtt_ScaleForMappingEmToPixels(
      &impl_->info, static_cast<float>(pixel_height));
  const int index =
      stbtt_FindGlyphIndex(&impl_->info,
                           static_cast<int>(original_cp1252_to_unicode(ansi)));
  int advance{};
  int bearing{};
  stbtt_GetGlyphHMetrics(&impl_->info, index, &advance, &bearing);
  glyph->advance = round_pixels(static_cast<float>(advance) * scale);

  int x0{};
  int y0{};
  int x1{};
  int y1{};
  stbtt_GetGlyphBitmapBox(&impl_->info, index, scale, scale, &x0, &y0, &x1,
                          &y1);
  if (x1 > x0 && y1 > y0) {
    const int width = x1 - x0;
    const int height = y1 - y0;
    std::vector<unsigned char> coverage(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    stbtt_MakeGlyphBitmap(&impl_->info, coverage.data(), width, height, width,
                          scale, scale, index);
    glyph->left = x0;
    glyph->top = y0;
    glyph->width = width;
    glyph->height = height;
    glyph->mask.resize(coverage.size());
    for (std::size_t i = 0; i < coverage.size(); ++i) {
      glyph->mask[i] = coverage[i] >= kCoverageThreshold ? kMaskOn : kMaskOff;
    }
    apply_dropout_control(coverage, width, height, glyph->mask);
  }
  slot = std::move(glyph);
  return *slot;
}

OriginalCanvasFontMetrics OriginalCanvasFont::metrics(int pixel_height) const {
  const float scale = stbtt_ScaleForMappingEmToPixels(
      &impl_->info, static_cast<float>(pixel_height));
  int ascent{};
  int descent{};
  int gap{};
  stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &gap);
  return {round_pixels(static_cast<float>(ascent) * scale),
          round_pixels(static_cast<float>(-descent) * scale)};
}

int OriginalCanvasFont::extent(int pixel_height, std::string_view ansi) {
  int total = 0;
  for (const char c : ansi) {
    total += glyph(pixel_height, static_cast<unsigned char>(c)).advance;
  }
  return total;
}

std::int16_t OriginalCanvasFont::bank_select(std::int16_t requested_height) {
  const auto decision = original_font_cache_decision(bank_, requested_height);
  switch (decision.action) {
    case OriginalFontCacheAction::no_selection:
      return 0;
    case OriginalFontCacheAction::select_existing:
      return decision.pixel_height;
    case OriginalFontCacheAction::create_and_select:
      bank_.push_back(decision.pixel_height);
      return decision.pixel_height;
  }
  return 0;
}

}  // namespace simtower
