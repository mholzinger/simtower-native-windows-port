// The SDL frontend's help lookup (sdl/sdl_help.hpp; no SDL in it): which
// page a WinHelp command opens in a converted help directory, and the file
// URL handed to the browser. A synthetic directory only: no game assets,
// so this runs everywhere (also under Wine).

#include "sdl_help.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

using simtower::sdl::file_url;
using simtower::sdl::help_page;
using simtower::sdl::kWinHelpContents;

void help_page_tests(const std::filesystem::path& work) {
  const auto directory = work / "help";
  std::filesystem::remove_all(directory);
  // No directory (help not converted) and an empty one: no page.
  assert(!help_page(directory, kWinHelpContents));
  assert(!help_page({}, kWinHelpContents));
  std::filesystem::create_directories(directory);
  assert(!help_page(directory, kWinHelpContents));
  std::ofstream(directory / "contents.html") << "<!DOCTYPE html>\n";
  const auto page = help_page(directory, kWinHelpContents);
  assert(page && page->filename() == "contents.html");
  // HELP_CONTEXT (1) and HELP_QUIT (2) are never sent by the original.
  assert(!help_page(directory, 1U));
  assert(!help_page(directory, 2U));
}

void file_url_tests(const std::filesystem::path& work) {
  const auto url = file_url(work / "a b" / "c#d%.html");
  assert(url.rfind("file:///", 0) == 0);
  assert(url.size() > 20U &&
         url.substr(url.size() - 20U) == "/a%20b/c%23d%25.html");
  assert(url.find('\\') == std::string::npos);
  // Non-ASCII names are UTF-8, percent-encoded.
  const auto accented = file_url(work / std::filesystem::path(u8"é"));
  assert(accented.substr(accented.size() - 7U) == "/%C3%A9");
  // A relative path is made absolute.
  const auto relative = file_url("x.html");
  assert(relative.rfind("file:///", 0) == 0);
  assert(relative.substr(relative.size() - 7U) == "/x.html");
}

}  // namespace

int main() {
  std::error_code error;
  const auto work =
      std::filesystem::temp_directory_path(error) / "simtower_sdl_help_tests";
  assert(!error);
  help_page_tests(work);
  file_url_tests(work);
  std::filesystem::remove_all(work, error);
  std::printf("sdl help tests passed\n");
  return 0;
}
