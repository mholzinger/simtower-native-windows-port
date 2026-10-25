#include "sdl_help.hpp"

#include <system_error>

namespace simtower::sdl {

std::optional<std::filesystem::path> help_page(
    const std::filesystem::path& directory, std::uint16_t command) {
  if (command != kWinHelpContents || directory.empty()) return std::nullopt;
  const auto page = directory / kHelpContentsPage;
  std::error_code error;
  if (!std::filesystem::is_regular_file(page, error)) return std::nullopt;
  return page;
}

std::string file_url(const std::filesystem::path& path) {
  std::error_code error;
  auto absolute = std::filesystem::absolute(path, error);
  if (error) absolute = path;
  const auto utf8 = absolute.lexically_normal().generic_u8string();
  std::string url = "file://";
  // A Windows drive path ("C:/x") needs the third slash.
  if (!utf8.empty() && utf8.front() != u8'/') url += '/';
  static constexpr char kHex[] = "0123456789ABCDEF";
  for (const char8_t unit : utf8) {
    const auto byte = static_cast<unsigned char>(unit);
    const bool keep = (byte >= 'A' && byte <= 'Z') ||
                      (byte >= 'a' && byte <= 'z') ||
                      (byte >= '0' && byte <= '9') || byte == '-' ||
                      byte == '.' || byte == '_' || byte == '~' ||
                      byte == '/' || byte == ':';
    if (keep) {
      url += static_cast<char>(byte);
    } else {
      url += '%';
      url += kHex[byte >> 4];
      url += kHex[byte & 0x0F];
    }
  }
  return url;
}

}  // namespace simtower::sdl
