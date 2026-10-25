#pragma once

// Software OriginalDrawContext over an OriginalWorldRaster, for hosts
// without GDI (the SDL frontend and the portable tests). It keeps the DC
// state the painters use (clip, current position, text alignment, text color,
// background mode, font, pen) with SaveDC/RestoreDC semantics.
//
// GDI behaviours reproduced here were checked against Wine's GDI with a
// probe program (Arial, 32-bit DIB section); see the notes on each method in
// original_draw_canvas.cpp:
// - TextOutA with TA_UPDATECP draws at the current position and moves it by
//   the text extent: right for TA_LEFT, left for TA_RIGHT.
// - DrawTextA(DT_SINGLELINE) behaves like ExtTextOut at the rectangle's
//   top-left under the DC's alignment flags, clipped to the rectangle. With
//   TA_UPDATECP it therefore draws at the current position (on the baseline
//   with TA_BASELINE) and advances it, even when the rectangle clips every
//   pixel.
// - LineTo is Bresenham without the end pixel; an exact error tie steps the
//   minor axis only in octants 3, 5, 6 and 8 (all 1,681 vectors with
//   |dx|,|dy| <= 20 matched Wine).

#include "original_canvas_font.hpp"
#include "original_draw_context.hpp"
#include "original_world.hpp"

#include <optional>
#include <vector>

namespace simtower {

class OriginalCanvasDrawContext final : public OriginalDrawContext {
 public:
  // Logical (0,0) maps to raster (origin_x, origin_y). Drawing is clipped to
  // the raster and to bounds (logical coordinates), the stand-in for the
  // window's client area. Both references must outlive this object.
  OriginalCanvasDrawContext(OriginalWorldRaster& raster,
                            OriginalCanvasFont& font,
                            int origin_x,
                            int origin_y,
                            const OriginalRect& bounds);
  // The whole raster, origin at its top-left.
  OriginalCanvasDrawContext(OriginalWorldRaster& raster,
                            OriginalCanvasFont& font);

  int save() override;
  void restore(int saved) override;
  void fill_rect(const OriginalRect& rect, OriginalRgb rgb) override;
  void intersect_clip(const OriginalRect& rect) override;
  void draw_dib(std::span<const std::byte> resource, int x, int y) override;
  void push_solid_pen(OriginalRgb rgb) override;
  void pop_pen() override;
  void move_to(int x, int y) override;
  void line_to(int x, int y) override;
  void push_font(std::int16_t pixel_height) override;
  void pop_font() override;
  void set_text_color(OriginalRgb rgb) override;
  void set_bk_transparent() override;
  void set_text_align(std::uint16_t flags) override;
  void text_out(std::string_view text) override;
  void draw_text_single_line(std::string_view text,
                             const OriginalRect& rect) override;
  [[nodiscard]] int text_extent_cx(std::string_view text) override;
  // COLORONCOLOR as nearest-neighbour sampling: destination pixel (x, y)
  // reads source (left + (x - dleft) * sw / dw, top + (y - dtop) * sh / dh).
  void stretch_pixels(std::span<const std::uint32_t> pixels,
                      int width,
                      int height,
                      const OriginalRect& source,
                      const OriginalRect& destination) override;
  // The same nearest-neighbour sampling over the DIB's top-left pixels.
  void draw_dib_region_scaled(std::span<const std::byte> resource,
                              const OriginalRect& source,
                              const OriginalRect& destination) override;

  // Current position (GetCurrentPositionEx), for tests.
  [[nodiscard]] int current_x() const noexcept { return state_.cp_x; }
  [[nodiscard]] int current_y() const noexcept { return state_.cp_y; }

 private:
  struct State {
    OriginalRect clip{};
    int cp_x{};
    int cp_y{};
    std::uint16_t text_align{};
    OriginalRgb text_color{0x000000U};
    bool bk_transparent{};
    OriginalRgb bk_color{0xffffffU};
    // 0: the DC's stock SYSTEM_FONT, which has no portable equivalent; text
    // drawn with it is skipped and measures zero. No painter does that.
    std::int16_t font_height{};
    // The stock BLACK_PEN until a pen is selected.
    OriginalRgb pen_color{0x000000U};
  };

  void plot(int x, int y, OriginalRgb rgb, const OriginalRect& clip);
  // ExtTextOut at (x,y) under the current alignment, clipped to clip.
  void draw_string(std::string_view text, int x, int y,
                   const OriginalRect& clip);

  OriginalWorldRaster& raster_;
  OriginalCanvasFont& font_;
  int origin_x_{};
  int origin_y_{};
  State state_{};
  std::vector<State> saved_{};
  std::vector<std::int16_t> previous_fonts_{};
  std::vector<OriginalRgb> previous_pens_{};
};

}  // namespace simtower
