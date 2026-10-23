#include "original_resources.hpp"

#include "original_resources.generated.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

// Portable lookup over the packed resources; the Win32 module loader is in
// original_resources_win32.cpp.
namespace simtower {
namespace {

std::string uppercase(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  return result;
}

std::span<const std::byte> view_descriptor(
    std::span<const std::byte> pack,
    const generated::ResourceDescriptor& descriptor) {
  const auto offset = static_cast<std::size_t>(descriptor.offset);
  const auto size = static_cast<std::size_t>(descriptor.size);
  if (offset > pack.size() || size > pack.size() - offset) {
    throw std::runtime_error("Original resource descriptor exceeds the embedded pack");
  }
  return pack.subspan(offset, size);
}

}  // namespace

std::span<const std::byte> OriginalResources::find(
    std::string_view type,
    std::int32_t numeric_id) const {
  const std::string normalized_type = uppercase(type);
  for (const auto& descriptor : generated::kResources) {
    if (descriptor.type == normalized_type && descriptor.numeric_id == numeric_id) {
      return view_descriptor(pack_, descriptor);
    }
  }
  return {};
}

std::span<const std::byte> OriginalResources::find(
    std::string_view type,
    std::string_view string_id) const {
  const std::string normalized_type = uppercase(type);
  const std::string normalized_id = uppercase(string_id);
  for (const auto& descriptor : generated::kResources) {
    if (descriptor.type == normalized_type && uppercase(descriptor.string_id) == normalized_id) {
      return view_descriptor(pack_, descriptor);
    }
  }
  return {};
}

}  // namespace simtower
