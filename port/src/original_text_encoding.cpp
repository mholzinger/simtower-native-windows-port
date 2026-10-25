#include "original_text_encoding.hpp"

#include <array>
#include <cstddef>

namespace simtower {

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

std::string original_utf8_to_cp1252(std::string_view utf8) {
  std::string result;
  std::size_t index = 0U;
  while (index < utf8.size()) {
    const auto lead = static_cast<unsigned char>(utf8[index]);
    std::uint32_t code = 0U;
    std::size_t length = 0U;
    if (lead < 0x80U) {
      code = lead;
      length = 1U;
    } else if ((lead & 0xE0U) == 0xC0U) {
      code = lead & 0x1FU;
      length = 2U;
    } else if ((lead & 0xF0U) == 0xE0U) {
      code = lead & 0x0FU;
      length = 3U;
    } else if ((lead & 0xF8U) == 0xF0U) {
      code = lead & 0x07U;
      length = 4U;
    }
    // The continuation bytes that are there (none past the end).
    std::size_t taken = 1U;
    while (taken < length && index + taken < utf8.size() &&
           (static_cast<unsigned char>(utf8[index + taken]) & 0xC0U) ==
               0x80U) {
      code = (code << 6U) |
             (static_cast<unsigned char>(utf8[index + taken]) & 0x3FU);
      ++taken;
    }
    index += taken;
    // A stray continuation byte, an invalid lead or a truncated sequence.
    if (length == 0U || taken != length) continue;
    for (unsigned byte = 0x20U; byte <= 0xFFU; ++byte) {
      if (byte == 0x7FU) continue;
      if (original_cp1252_to_unicode(static_cast<unsigned char>(byte)) ==
          code) {
        result.push_back(static_cast<char>(byte));
        break;
      }
    }
  }
  return result;
}

}  // namespace simtower
