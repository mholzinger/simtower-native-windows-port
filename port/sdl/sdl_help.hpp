#pragma once

// Help for the SDL frontend (decision 3a). The original calls
// WinHelp(SIMTOWER.HLP, HELP_CONTENTS) from one site, 1158:091a, reached by
// Help > Contents and the F1 accelerator (40021); it never sends
// HELP_CONTEXT, HELP_HELPONHELP or HELP_QUIT. Here that call opens a page of
// the HTML that tools/convert_winhelp.py makes from the user's own
// SIMTOWER.HLP: the build writes it to "help" next to simtower_sdl, or
// --help-dir names another directory. No SDL in this header: the window
// host supplies a HelpViewer (SDL_OpenURL); headless runs install none, so
// a request is only logged with its resolved URL.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace simtower::sdl {

// WinHelp commands (WINDOWS.H).
inline constexpr std::uint16_t kWinHelpContents = 3U;  // HELP_CONTENTS

// The page the converter writes for HELP_CONTENTS: it forwards to the help
// file's contents topic.
inline constexpr const char* kHelpContentsPage = "contents.html";

// Opens a URL in the user's browser.
class HelpViewer {
 public:
  virtual ~HelpViewer() = default;
  // False when the platform could not open it.
  virtual bool open_url(const std::string& url) = 0;
};

// The last help request: the WinHelp command, and the page and URL it
// resolved to (both empty when the directory holds no converted help).
struct HelpRequest {
  std::uint16_t command{};
  std::string page{};
  std::string url{};
  bool opened{};
};

// The page for a WinHelp command in a converted help directory, or nullopt
// when the command has no page or the directory does not hold it.
[[nodiscard]] std::optional<std::filesystem::path> help_page(
    const std::filesystem::path& directory, std::uint16_t command);

// A file URL for a path (made absolute): UTF-8, percent-encoded outside
// the unreserved characters and '/', with Windows drive paths as
// file:///C:/...
[[nodiscard]] std::string file_url(const std::filesystem::path& path);

}  // namespace simtower::sdl
