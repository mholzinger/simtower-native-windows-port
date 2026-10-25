#include "original_dib.hpp"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace simtower {

OriginalDibView original_dib_view(std::span<const std::byte> resource) {
  if (resource.size() < sizeof(OriginalBitmapInfoHeader)) {
    throw std::runtime_error("Truncated original bitmap header");
  }

  OriginalBitmapInfoHeader header{};
  std::memcpy(&header, resource.data(), sizeof(header));
  if (header.biSize != sizeof(OriginalBitmapInfoHeader) ||
      header.biPlanes != 1U || header.biWidth <= 0 || header.biHeight == 0 ||
      header.biCompression != kOriginalDibCompressionRgb) {
    throw std::runtime_error("Unsupported original bitmap format");
  }

  std::uint32_t color_count = header.biClrUsed;
  if (color_count == 0U && header.biBitCount <= 8U) {
    color_count = 1U << header.biBitCount;
  }
  const std::size_t pixel_offset =
      static_cast<std::size_t>(header.biSize) +
      static_cast<std::size_t>(color_count) * sizeof(OriginalRgbQuad);
  if (pixel_offset > resource.size()) {
    throw std::runtime_error("Truncated original bitmap palette");
  }

  const std::uint64_t row_bits =
      static_cast<std::uint64_t>(header.biWidth) * header.biBitCount;
  const std::uint64_t row_bytes = ((row_bits + 31U) / 32U) * 4U;
  const std::uint64_t pixel_size =
      row_bytes * static_cast<std::uint64_t>(std::abs(header.biHeight));
  if (pixel_size > resource.size() - pixel_offset) {
    throw std::runtime_error("Truncated original bitmap scanlines");
  }

  return {
      reinterpret_cast<const OriginalBitmapInfo*>(resource.data()),
      resource.subspan(pixel_offset, static_cast<std::size_t>(pixel_size)),
      header.biWidth,
      header.biHeight,
      header.biBitCount,
  };
}

}  // namespace simtower
