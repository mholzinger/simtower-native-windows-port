#pragma once

// Windows-1252 text helpers: the ANSI code page the original's strings and
// WM_CHAR bytes use on an English Windows. Used by the canvas font (glyph
// lookup) and the SDL host (typed text).

#include <cstdint>
#include <string>
#include <string_view>

namespace simtower {

// Windows-1252 to Unicode (the ANSI code page of an English Windows).
[[nodiscard]] std::uint32_t original_cp1252_to_unicode(
    unsigned char ansi) noexcept;

// UTF-8 text (typed text, e.g. SDL_EVENT_TEXT_INPUT) as the Windows-1252
// bytes WM_CHAR would carry: the reverse of original_cp1252_to_unicode for
// 0x20..0xFF except 0x7F. Code points the code page lacks (and controls)
// are dropped, as are invalid or truncated sequences, which never read past
// the end of the text.
[[nodiscard]] std::string original_utf8_to_cp1252(std::string_view utf8);

}  // namespace simtower
