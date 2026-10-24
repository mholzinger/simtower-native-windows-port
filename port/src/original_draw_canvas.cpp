#include "original_draw_canvas.hpp"

#include "original_dib.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace simtower {
namespace {

OriginalRect intersect(const OriginalRect& a, const OriginalRect& b) noexcept {
  return {std::max(a.left, b.left), std::max(a.top, b.top),
          std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
}

// DrawTextA without DT_NOPREFIX: "&&" draws one '&' and a lone '&' is
// removed. GDI also underlines the character after a lone '&'.
// TEMP: no underline yet; none of the Info status strings contain '&'
// (checked by info_tests).
std::string strip_prefixes(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '&') {
      if (i + 1 < text.size() && text[i + 1] == '&') {
        result.push_back('&');
        ++i;
      }
      continue;
    }
    result.push_back(text[i]);
  }
  return result;
}

// Indexed DIB pixel through its own color table (DIB_RGB_COLORS).
class DibSampler {
 public:
  explicit DibSampler(std::span<const std::byte> resource)
      : view_(original_dib_view(resource)),
        height_(std::abs(view_.height)),
        row_bytes_(((static_cast<std::size_t>(view_.width) *
                         view_.bit_count +
                     31U) /
                    32U) *
                   4U) {
    if (view_.bit_count != 1U && view_.bit_count != 4U &&
        view_.bit_count != 8U) {
      throw std::runtime_error("Canvas DIB is not 1/4/8-bit indexed");
    }
  }

  [[nodiscard]] int width() const noexcept { return view_.width; }
  [[nodiscard]] int height() const noexcept { return height_; }

  [[nodiscard]] OriginalRgb sample(int x, int y) const {
    const int row = view_.height > 0 ? height_ - 1 - y : y;
    const auto* line =
        view_.pixels.data() + static_cast<std::size_t>(row) * row_bytes_;
    unsigned index{};
    switch (view_.bit_count) {
      case 8U:
        index = std::to_integer<unsigned>(line[x]);
        break;
      case 4U: {
        const auto byte = std::to_integer<unsigned>(line[x / 2]);
        index = (x & 1) != 0 ? (byte & 0x0fU) : (byte >> 4U);
        break;
      }
      default: {
        const auto byte = std::to_integer<unsigned>(line[x / 8]);
        index = (byte >> (7U - static_cast<unsigned>(x & 7))) & 1U;
        break;
      }
    }
    const OriginalRgbQuad color = view_.info->bmiColors[index];
    return (static_cast<OriginalRgb>(color.rgbRed) << 16U) |
           (static_cast<OriginalRgb>(color.rgbGreen) << 8U) |
           static_cast<OriginalRgb>(color.rgbBlue);
  }

 private:
  OriginalDibView view_{};
  int height_{};
  std::size_t row_bytes_{};
};

// Wine's GDI line rule (dlls/gdi32 dibdrv): octants numbered 1..8
// counter-clockwise from +x in screen coordinates (y down).
int octant(int dx, int dy) noexcept {
  if (dy > 0) {
    if (dx > 0) return dx > dy ? 1 : 2;
    return -dx > dy ? 4 : 3;
  }
  if (dx < 0) return -dx > -dy ? 5 : 6;
  return dx > -dy ? 8 : 7;
}

}  // namespace

OriginalCanvasDrawContext::OriginalCanvasDrawContext(
    OriginalWorldRaster& raster,
    OriginalCanvasFont& font,
    int origin_x,
    int origin_y,
    const OriginalRect& bounds)
    : raster_(raster), font_(font), origin_x_(origin_x), origin_y_(origin_y) {
  state_.clip = intersect(bounds, {-origin_x, -origin_y,
                                   raster.width - origin_x,
                                   raster.height - origin_y});
}

OriginalCanvasDrawContext::OriginalCanvasDrawContext(
    OriginalWorldRaster& raster,
    OriginalCanvasFont& font)
    : OriginalCanvasDrawContext(raster, font, 0, 0,
                                {0, 0, raster.width, raster.height}) {}

void OriginalCanvasDrawContext::plot(int x, int y, OriginalRgb rgb,
                                     const OriginalRect& clip) {
  if (!original_rect_contains(clip, x, y)) return;
  raster_.pixels[static_cast<std::size_t>(y + origin_y_) *
                     static_cast<std::size_t>(raster_.width) +
                 static_cast<std::size_t>(x + origin_x_)] = rgb;
}

int OriginalCanvasDrawContext::save() {
  saved_.push_back(state_);
  return static_cast<int>(saved_.size());
}

void OriginalCanvasDrawContext::restore(int saved) {
  // RestoreDC: a positive level restores that SaveDC and discards the ones
  // above it; a negative one counts back from the most recent.
  const int count = static_cast<int>(saved_.size());
  const int level = saved < 0 ? count + saved + 1 : saved;
  if (level < 1 || level > count) return;
  state_ = saved_[static_cast<std::size_t>(level - 1)];
  saved_.resize(static_cast<std::size_t>(level - 1));
}

void OriginalCanvasDrawContext::fill_rect(const OriginalRect& rect,
                                          OriginalRgb rgb) {
  // FillRect excludes the right and bottom edges.
  const auto area = intersect(rect, state_.clip);
  for (int y = area.top; y < area.bottom; ++y) {
    for (int x = area.left; x < area.right; ++x) {
      plot(x, y, rgb, state_.clip);
    }
  }
}

void OriginalCanvasDrawContext::intersect_clip(const OriginalRect& rect) {
  state_.clip = intersect(state_.clip, rect);
}

void OriginalCanvasDrawContext::draw_dib(std::span<const std::byte> resource,
                                         int x,
                                         int y) {
  const DibSampler dib(resource);
  const auto area =
      intersect(state_.clip, {x, y, x + dib.width(), y + dib.height()});
  for (int row = area.top; row < area.bottom; ++row) {
    for (int column = area.left; column < area.right; ++column) {
      plot(column, row, dib.sample(column - x, row - y), state_.clip);
    }
  }
}

void OriginalCanvasDrawContext::push_solid_pen(OriginalRgb rgb) {
  previous_pens_.push_back(state_.pen_color);
  state_.pen_color = rgb;
}

void OriginalCanvasDrawContext::pop_pen() {
  if (previous_pens_.empty()) return;
  state_.pen_color = previous_pens_.back();
  previous_pens_.pop_back();
}

void OriginalCanvasDrawContext::move_to(int x, int y) {
  state_.cp_x = x;
  state_.cp_y = y;
}

void OriginalCanvasDrawContext::line_to(int x, int y) {
  // Bresenham from the current position, end pixel excluded. Wine biases
  // octants 3, 5, 6 and 8: there an exact tie (error 0) steps the minor
  // axis; elsewhere it does not.
  const int dx = x - state_.cp_x;
  const int dy = y - state_.cp_y;
  if (dx != 0 || dy != 0) {
    const int o = octant(dx, dy);
    const int bias = (o == 3 || o == 5 || o == 6 || o == 8) ? 0 : 1;
    const int adx = std::abs(dx);
    const int ady = std::abs(dy);
    const int sx = dx > 0 ? 1 : -1;
    const int sy = dy > 0 ? 1 : -1;
    const bool x_major = adx >= ady;
    const int major = x_major ? adx : ady;
    const int minor = x_major ? ady : adx;
    int error = 2 * minor - major - bias;
    int px = state_.cp_x;
    int py = state_.cp_y;
    for (int step = 0; step < major; ++step) {
      plot(px, py, state_.pen_color, state_.clip);
      if (error >= 0) {
        if (x_major) {
          py += sy;
        } else {
          px += sx;
        }
        error += 2 * minor - 2 * major;
      } else {
        error += 2 * minor;
      }
      if (x_major) {
        px += sx;
      } else {
        py += sy;
      }
    }
  }
  state_.cp_x = x;
  state_.cp_y = y;
}

void OriginalCanvasDrawContext::push_font(std::int16_t pixel_height) {
  // original_cached_font's bank rules; a full bank leaves the font as is.
  const std::int16_t selected = font_.bank_select(pixel_height);
  previous_fonts_.push_back(selected != 0 ? state_.font_height
                                          : std::int16_t{-1});
  if (selected != 0) state_.font_height = selected;
}

void OriginalCanvasDrawContext::pop_font() {
  if (previous_fonts_.empty()) return;
  const std::int16_t previous = previous_fonts_.back();
  previous_fonts_.pop_back();
  if (previous >= 0) state_.font_height = previous;
}

void OriginalCanvasDrawContext::set_text_color(OriginalRgb rgb) {
  state_.text_color = rgb;
}

void OriginalCanvasDrawContext::set_bk_transparent() {
  state_.bk_transparent = true;
}

void OriginalCanvasDrawContext::set_text_align(std::uint16_t flags) {
  state_.text_align = flags;
}

void OriginalCanvasDrawContext::draw_string(std::string_view text,
                                            int x,
                                            int y,
                                            const OriginalRect& clip) {
  if (state_.font_height == 0) return;
  const int height = state_.font_height;
  const int extent = font_.extent(height, text);
  const auto metrics = font_.metrics(height);
  int left = x;
  switch (state_.text_align & kOriginalTaCenter) {
    case kOriginalTaRight: left = x - extent; break;
    case kOriginalTaCenter: left = x - extent / 2; break;
    default: break;
  }
  int baseline = y;
  switch (state_.text_align & kOriginalTaBaseline) {
    case kOriginalTaBaseline: break;
    case kOriginalTaBottom: baseline = y - metrics.descent; break;
    default: baseline = y + metrics.ascent; break;
  }
  if (!state_.bk_transparent) {
    // OPAQUE: the character cells take the background color first.
    const auto cell = intersect(clip, {left, baseline - metrics.ascent,
                                       left + extent,
                                       baseline + metrics.descent});
    for (int row = cell.top; row < cell.bottom; ++row) {
      for (int column = cell.left; column < cell.right; ++column) {
        plot(column, row, state_.bk_color, clip);
      }
    }
  }
  int pen = left;
  for (const char c : text) {
    const auto& glyph = font_.glyph(height, static_cast<unsigned char>(c));
    for (int row = 0; row < glyph.height; ++row) {
      for (int column = 0; column < glyph.width; ++column) {
        if (glyph.mask[static_cast<std::size_t>(row * glyph.width + column)] !=
            0U) {
          plot(pen + glyph.left + column, baseline + glyph.top + row,
               state_.text_color, clip);
        }
      }
    }
    pen += glyph.advance;
  }
}

void OriginalCanvasDrawContext::text_out(std::string_view text) {
  // TextOutA(dc, 0, 0, ...): with TA_UPDATECP the current position is the
  // reference point and afterwards moves by the extent (right for TA_LEFT,
  // left for TA_RIGHT). [INFERRED] TA_CENTER leaves it unchanged.
  const bool update = (state_.text_align & kOriginalTaUpdateCp) != 0U;
  const int x = update ? state_.cp_x : 0;
  const int y = update ? state_.cp_y : 0;
  draw_string(text, x, y, state_.clip);
  if (!update || state_.font_height == 0) return;
  const int extent = font_.extent(state_.font_height, text);
  switch (state_.text_align & kOriginalTaCenter) {
    case kOriginalTaRight: state_.cp_x -= extent; break;
    case kOriginalTaCenter: break;
    default: state_.cp_x += extent; break;
  }
}

void OriginalCanvasDrawContext::draw_text_single_line(
    std::string_view text,
    const OriginalRect& rect) {
  // DrawTextA(DT_SINGLELINE) is ExtTextOut at the rectangle's top-left with
  // the DC's alignment, clipped to the rectangle. With TA_UPDATECP the
  // current position replaces that point and advances afterwards.
  const std::string visible = strip_prefixes(text);
  const bool update = (state_.text_align & kOriginalTaUpdateCp) != 0U;
  const int x = update ? state_.cp_x : rect.left;
  const int y = update ? state_.cp_y : rect.top;
  draw_string(visible, x, y, intersect(state_.clip, rect));
  if (!update || state_.font_height == 0) return;
  const int extent = font_.extent(state_.font_height, visible);
  switch (state_.text_align & kOriginalTaCenter) {
    case kOriginalTaRight: state_.cp_x -= extent; break;
    case kOriginalTaCenter: break;
    default: state_.cp_x += extent; break;
  }
}

int OriginalCanvasDrawContext::text_extent_cx(std::string_view text) {
  if (state_.font_height == 0) return 0;
  return font_.extent(state_.font_height, text);
}

}  // namespace simtower
