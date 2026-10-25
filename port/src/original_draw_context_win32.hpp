#pragma once

// GDI implementation of the portable OriginalDrawContext. Every method makes
// exactly the Win32 call(s) named on the interface, in that order, so a
// painter routed through it issues the same call sequence as the direct GDI
// code it replaced.
#include "original_draw_context.hpp"

#include <windows.h>

#include <vector>

namespace simtower {

// An OriginalRgb (0x00RRGGBB) as a GDI COLORREF (0x00BBGGRR).
[[nodiscard]] constexpr COLORREF original_rgb_to_colorref(
    OriginalRgb rgb) noexcept {
  return RGB((rgb >> 16U) & 0xffU, (rgb >> 8U) & 0xffU, rgb & 0xffU);
}

// The std::runtime_error texts thrown when CreatePen / CreateSolidBrush
// fail, so each painter keeps the message its direct GDI code had.
struct OriginalGdiFailureText {
  const char* pen{"CreatePen failed for info clock"};
  const char* brush{"CreateSolidBrush failed for info window"};
};

// The texts of the old SelectedPen / fill_gray in the DTMP dialog chrome.
inline constexpr OriginalGdiFailureText kOriginalDialogGdiFailureText{
    "Could not create original dialog pen",
    "Could not create original dialog brush"};

class OriginalGdiDrawContext final : public OriginalDrawContext {
 public:
  // The DC is borrowed; it must outlive this object. The default failure
  // texts are the Info painter's.
  explicit OriginalGdiDrawContext(HDC dc,
                                  OriginalGdiFailureText failure = {}) noexcept
      : dc_(dc), failure_(failure) {}
  OriginalGdiDrawContext(const OriginalGdiDrawContext&) = delete;
  OriginalGdiDrawContext& operator=(const OriginalGdiDrawContext&) = delete;
  // Unwinds pens and fonts a painter left selected (an exception between
  // a push and its pop): restores the previous objects, deletes the pens.
  ~OriginalGdiDrawContext() override;

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
  // stretch_pixels and draw_dib_region_scaled complete the interface for
  // the portable Information painters; no Win32 procedure paints through
  // them yet (they keep their own GDI painters, see
  // original_*_information_controller.hpp), so they have no Win32 caller
  // or test. FOLLOW-UP (the gate for moving Win32 onto the portable
  // painters): a Wine DIB memcmp test of each portable Information painter
  // over this context against a frozen copy of its GDI painter, as
  // dialog_chrome_parity_tests does for the chrome. Until then the two
  // painters are checked only for arguments and text origins.
  void stretch_pixels(std::span<const std::uint32_t> pixels,
                      int width,
                      int height,
                      const OriginalRect& source,
                      const OriginalRect& destination) override;
  void draw_dib_region_scaled(std::span<const std::byte> resource,
                              const OriginalRect& source,
                              const OriginalRect& destination) override;

 private:
  struct SelectedPen {
    HPEN pen{};
    HGDIOBJ previous{};
  };

  HDC dc_{};
  OriginalGdiFailureText failure_{};
  // SelectObject results to restore, innermost last. A null previous font
  // means push_font selected nothing, so pop_font selects nothing either.
  std::vector<HGDIOBJ> previous_fonts_{};
  std::vector<SelectedPen> pens_{};
};

}  // namespace simtower
