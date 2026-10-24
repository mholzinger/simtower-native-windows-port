#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace simtower {

struct OriginalDtmpRect {
  std::uint16_t left = 0;
  std::uint16_t top = 0;
  std::uint16_t right = 0;
  std::uint16_t bottom = 0;
  friend bool operator==(const OriginalDtmpRect&, const OriginalDtmpRect&) = default;
};

// Value ownership is the native replacement for 1070:051f/0570/05a1's
// per-HWND resource-slot search, unlock/free, slot clear and reference count.
// A parsed layout now releases automatically with its dialog context.
struct OriginalDtmp {
  std::string bitmap_reference;
  int bitmap_resource_id = -1;
  std::uint16_t width_or_header = 0;
  std::uint16_t height_or_header = 0;
  std::vector<OriginalDtmpRect> rectangles;
};

// Exact layout used by 1070:0005 and 1070:0231: optional C-string bitmap ID,
// two words, a rectangle count, then count RECTs, all words little-endian.
// Parsing the complete array also subsumes 1070:061d, which locked the same
// resource and copied one 1-based RECT at a time in the Win16 host.
[[nodiscard]] OriginalDtmp parse_original_dtmp(
    std::span<const std::byte> resource);

// PtInRect on a DTMP rectangle, where a 0xffff left or top marks an absent
// rectangle that nothing hits.
[[nodiscard]] constexpr bool original_dtmp_rect_contains(
    const OriginalDtmpRect& rectangle, int x, int y) noexcept {
  return rectangle.left != 0xffffU && rectangle.top != 0xffffU &&
         x >= rectangle.left && x < rectangle.right && y >= rectangle.top &&
         y < rectangle.bottom;
}

}  // namespace simtower
