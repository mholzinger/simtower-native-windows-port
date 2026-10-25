#pragma once

// FNV-1a 64 over explicitly sized little-endian fields, for the headless
// regression oracle (--state-digest). Fields are fed one by one, never as
// raw structs, so padding and host endianness never reach the hash.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace simtower::sdl {

class Fnv64 {
 public:
  void byte(std::uint8_t value) noexcept {
    hash_ ^= value;
    hash_ *= 0x100000001b3ULL;
  }
  void bytes(std::span<const std::byte> data) noexcept {
    for (const auto value : data) byte(std::to_integer<std::uint8_t>(value));
  }
  // Signed and unsigned integers are widened to 64 bits, so a field's C++
  // width can change in a refactor without changing the digest.
  void integer(std::int64_t value) noexcept {
    auto bits = static_cast<std::uint64_t>(value);
    for (int index = 0; index < 8; ++index) {
      byte(static_cast<std::uint8_t>(bits & 0xffU));
      bits >>= 8U;
    }
  }
  void unsigned_integer(std::uint64_t value) noexcept {
    integer(static_cast<std::int64_t>(value));
  }
  void boolean(bool value) noexcept { byte(value ? 1U : 0U); }
  void text(std::string_view value) noexcept {
    unsigned_integer(value.size());
    for (const char character : value) {
      byte(static_cast<std::uint8_t>(character));
    }
  }
  [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

 private:
  std::uint64_t hash_{0xcbf29ce484222325ULL};
};

}  // namespace simtower::sdl
