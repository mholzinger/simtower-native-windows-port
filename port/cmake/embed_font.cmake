# Writes OUTPUT, a C++ source defining
# simtower::original_canvas_bundled_font_data() over the bytes of INPUT.
# Run with cmake -DINPUT=... -DOUTPUT=... -P embed_font.cmake.
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_length)
math(EXPR byte_count "${hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){24})" "\\1\n" bytes "${bytes}")
get_filename_component(name "${INPUT}" NAME)
file(WRITE "${OUTPUT}.tmp"
"// Generated from ${name} by cmake/embed_font.cmake. Do not edit.
#include \"original_canvas_font.hpp\"

namespace simtower {
namespace {
const unsigned char kBundledFont[${byte_count}] = {
${bytes}
};
}  // namespace

std::span<const std::byte> original_canvas_bundled_font_data() {
  return std::as_bytes(std::span<const unsigned char>(kBundledFont));
}

}  // namespace simtower
")
file(RENAME "${OUTPUT}.tmp" "${OUTPUT}")
