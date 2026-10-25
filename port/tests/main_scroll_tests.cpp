// The shared MAINWNDPROC WM_HSCROLL / WM_VSCROLL handler
// (original_main_scroll.hpp) against a frozen copy of the Win32 code in
// native_main.cpp as it was before it shared it (the two main_window_proc
// cases and scroll_original). The frozen copy runs on a fake window whose
// calls are logged; the shared path runs on a host that logs the same
// names. Both must log the same calls in the same order and leave the
// same scroll positions, for every code, bar, position, extent, a failed
// GetScrollInfo, the modal lock, and a window that is not Main. No assets:
// runs natively and under Wine.

#include "original_main_scroll.hpp"
#include "original_ui.hpp"
#include "original_world.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace {

using Log = std::vector<std::string>;

constexpr int kSbHorz = 0;
constexpr int kSbVert = 1;
constexpr unsigned kWmHscroll = 0x0114U;
constexpr unsigned kWmVscroll = 0x0115U;

// The window state the Win32 calls read and write. SetScrollInfo clamps
// to the installed range [0, maximum] like USER.
struct FakeWindow {
  std::array<int, 2> position{};
  std::array<int, 2> maximum{};
  int client_width{};
  int client_height{};
  bool get_info_fails{};
  bool locked{};
  bool is_main{true};
  Log* log{};
};

// --- frozen Win32 code ----------------------------------------------------

namespace legacy {

struct ScrollInfo {
  unsigned mask{};
  int pos{};
};
constexpr unsigned kSifPos = 4U;
constexpr unsigned kSifAll = 0x17U;

FakeWindow* g_window{};

bool original_main_modal_input_locked() {
  g_window->log->push_back("locked?");
  return g_window->locked;
}
void InvalidateRect() { g_window->log->push_back("InvalidateRect"); }
bool GetScrollInfo(int bar, ScrollInfo* info) {
  g_window->log->push_back("GetScrollInfo " + std::to_string(bar));
  if (g_window->get_info_fails) return false;
  info->pos = g_window->position[bar];
  return true;
}
void GetClientRect(int* right, int* bottom) {
  g_window->log->push_back("GetClientRect");
  *right = g_window->client_width;
  *bottom = g_window->client_height;
}
void SetScrollInfo(int bar, const ScrollInfo& info, bool redraw) {
  g_window->log->push_back("SetScrollInfo " + std::to_string(bar) + " " +
                           std::to_string(info.pos) +
                           (redraw ? " redraw" : ""));
  if ((info.mask & kSifPos) != 0U) {
    g_window->position[bar] =
        std::clamp(info.pos, 0, std::max(0, g_window->maximum[bar]));
  }
}
void request_rebuild_with_sky() {
  g_window->log->push_back("rebuild_with_sky");
}
void adjust_original_derived_map_focus_synchronously() {
  g_window->log->push_back("map_focus");
}

// Frozen: scroll_original.
void scroll_original(int bar, std::uint16_t code, std::uint16_t thumb,
                     int world_extent) {
  ScrollInfo info{kSifAll};
  if (!GetScrollInfo(bar, &info)) {
    return;
  }
  int right{};
  int bottom{};
  GetClientRect(&right, &bottom);
  const int client_extent = bar == kSbHorz ? right : bottom;
  const auto position = simtower::original_main_scroll_request_position(
      code, info.pos, thumb, client_extent, world_extent - client_extent);
  if (!position) {
    return;
  }
  info.mask = kSifPos;
  info.pos = *position;
  SetScrollInfo(bar, info, true);
  for (const auto step : simtower::original_scrollbar_refresh_order()) {
    switch (step) {
      case simtower::OriginalScrollbarRefreshStep::main_rebuild_with_sky:
        request_rebuild_with_sky();
        break;
      case simtower::OriginalScrollbarRefreshStep::map_focus_adjustment:
        if (g_window->is_main) {
          adjust_original_derived_map_focus_synchronously();
        }
        break;
    }
  }
}

// Frozen: main_window_proc's WM_HSCROLL and WM_VSCROLL cases.
void main_window_proc(unsigned message, std::uint16_t code,
                      std::uint16_t thumb) {
  if (message == kWmHscroll) {
    if (original_main_modal_input_locked()) return;
    InvalidateRect();
    scroll_original(kSbHorz, code, thumb, simtower::kOriginalWorldWidth);
    return;
  }
  if (message == kWmVscroll) {
    if (original_main_modal_input_locked()) return;
    InvalidateRect();
    scroll_original(kSbVert, code, thumb, simtower::kOriginalWorldHeight);
  }
}

}  // namespace legacy

// --- the shared path, as Win32MainScrollHost runs it -----------------------

class LogHost final : public simtower::OriginalMainScrollHost {
 public:
  explicit LogHost(FakeWindow& window) : w_(window) {}
  bool modal_input_locked() override {
    w_.log->push_back("locked?");
    return w_.locked;
  }
  void invalidate_main() override { w_.log->push_back("InvalidateRect"); }
  std::optional<int> scroll_position(
      simtower::OriginalMainScrollBar bar) override {
    const int index = static_cast<int>(bar);
    w_.log->push_back("GetScrollInfo " + std::to_string(index));
    if (w_.get_info_fails) return std::nullopt;
    return w_.position[index];
  }
  int client_extent(simtower::OriginalMainScrollBar bar) override {
    w_.log->push_back("GetClientRect");
    return bar == simtower::OriginalMainScrollBar::horizontal
               ? w_.client_width
               : w_.client_height;
  }
  void set_scroll_position(simtower::OriginalMainScrollBar bar,
                           int position) override {
    const int index = static_cast<int>(bar);
    w_.log->push_back("SetScrollInfo " + std::to_string(index) + " " +
                      std::to_string(position) + " redraw");
    w_.position[index] =
        std::clamp(position, 0, std::max(0, w_.maximum[index]));
  }
  void rebuild_main_with_sky() override {
    w_.log->push_back("rebuild_with_sky");
  }
  void adjust_map_focus() override {
    if (w_.is_main) w_.log->push_back("map_focus");
  }

 private:
  FakeWindow& w_;
};

FakeWindow make_window(int width, int height, int x, int y) {
  FakeWindow window{};
  window.client_width = width;
  window.client_height = height;
  window.maximum = {std::max(0, simtower::kOriginalWorldWidth - width),
                    std::max(0, simtower::kOriginalWorldHeight - height)};
  window.position = {x, y};
  return window;
}

int compared = 0;

void compare(FakeWindow window, unsigned message, std::uint16_t code,
             std::uint16_t thumb) {
  Log legacy_log;
  Log shared_log;
  FakeWindow legacy_window = window;
  legacy_window.log = &legacy_log;
  legacy::g_window = &legacy_window;
  legacy::main_window_proc(message, code, thumb);

  FakeWindow shared_window = window;
  shared_window.log = &shared_log;
  LogHost host(shared_window);
  simtower::handle_original_main_scroll(
      host,
      message == kWmHscroll ? simtower::OriginalMainScrollBar::horizontal
                            : simtower::OriginalMainScrollBar::vertical,
      code, thumb);
  if (legacy_log != shared_log ||
      legacy_window.position != shared_window.position) {
    std::printf("mismatch: message %x code %u thumb %u\n", message, code,
                thumb);
    for (const auto& line : legacy_log) {
      std::printf("  legacy %s\n", line.c_str());
    }
    for (const auto& line : shared_log) {
      std::printf("  shared %s\n", line.c_str());
    }
    assert(false);
  }
  ++compared;
}

void parity_tests() {
  const std::array<std::pair<int, int>, 3> sizes{
      {{612, 400}, {816, 576}, {100, 100}}};
  const std::array<std::pair<int, int>, 4> origins{
      {{0, 0}, {1194, 3872}, {2388, 3744}, {8, 20}}};
  const std::array<std::uint16_t, 6> thumbs{0U, 1U, 600U, 2388U, 4000U,
                                            65535U};
  for (const auto& [width, height] : sizes) {
    for (const auto& [x, y] : origins) {
      for (const unsigned message : {kWmHscroll, kWmVscroll}) {
        for (std::uint16_t code = 0U; code <= 9U; ++code) {
          for (const auto thumb : thumbs) {
            auto window = make_window(width, height, x, y);
            compare(window, message, code, thumb);
            window.locked = true;
            compare(window, message, code, thumb);
            window.locked = false;
            window.get_info_fails = true;
            compare(window, message, code, thumb);
            window.get_info_fails = false;
            window.is_main = false;
            compare(window, message, code, thumb);
          }
        }
      }
    }
  }
}

// The handler's own results: the original's 16-pixel line, client - 16
// page, clamp, raw thumb positions and the 1058:05f8 tail.
void behavior_tests() {
  Log log;
  auto window = make_window(612, 400, 1194, 3872);
  window.log = &log;
  LogHost host(window);
  using Bar = simtower::OriginalMainScrollBar;

  simtower::handle_original_main_scroll(host, Bar::horizontal, 1U, 0U);
  assert(window.position[0] == 1210);
  assert((log == Log{"locked?", "InvalidateRect", "GetScrollInfo 0",
                     "GetClientRect", "SetScrollInfo 0 1210 redraw",
                     "rebuild_with_sky", "map_focus"}));
  simtower::handle_original_main_scroll(host, Bar::horizontal, 0U, 0U);
  assert(window.position[0] == 1194);
  // Page: client width - 16.
  simtower::handle_original_main_scroll(host, Bar::horizontal, 3U, 0U);
  assert(window.position[0] == 1194 + 596);
  simtower::handle_original_main_scroll(host, Bar::horizontal, 2U, 0U);
  assert(window.position[0] == 1194);
  // Vertical page: client height - 16; clamped at the world bottom.
  simtower::handle_original_main_scroll(host, Bar::vertical, 3U, 0U);
  assert(window.position[1] == simtower::kOriginalWorldHeight - 400);
  simtower::handle_original_main_scroll(host, Bar::vertical, 2U, 0U);
  assert(window.position[1] == simtower::kOriginalWorldHeight - 400 - 384);
  // Thumb codes take the raw position; the range clamps it.
  simtower::handle_original_main_scroll(host, Bar::horizontal, 5U, 300U);
  assert(window.position[0] == 300);
  simtower::handle_original_main_scroll(host, Bar::horizontal, 4U, 9000U);
  assert(window.position[0] == simtower::kOriginalWorldWidth - 612);
  // A line up at 0 stays at 0 and still rebuilds.
  window.position[1] = 8;
  log.clear();
  simtower::handle_original_main_scroll(host, Bar::vertical, 0U, 0U);
  assert(window.position[1] == 0);
  log.clear();
  simtower::handle_original_main_scroll(host, Bar::vertical, 0U, 0U);
  assert(window.position[1] == 0);
  assert(log.back() == "map_focus");
  // SB_TOP, SB_BOTTOM and SB_ENDSCROLL only invalidate.
  for (const std::uint16_t code : {6U, 7U, 8U}) {
    log.clear();
    simtower::handle_original_main_scroll(host, Bar::vertical, code, 0U);
    assert((log == Log{"locked?", "InvalidateRect", "GetScrollInfo 1",
                       "GetClientRect"}));
  }
  // DS:24b8 consumes the message before the invalidation.
  window.locked = true;
  log.clear();
  simtower::handle_original_main_scroll(host, Bar::vertical, 1U, 0U);
  assert((log == Log{"locked?"}));
}

}  // namespace

int main() {
  parity_tests();
  behavior_tests();
  std::printf("main scroll tests passed (%d parity cases)\n", compared);
  return 0;
}
