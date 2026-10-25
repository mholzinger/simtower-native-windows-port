#include "original_main_scroll.hpp"

#include "original_ui.hpp"
#include "original_world.hpp"

namespace simtower {

void handle_original_main_scroll(OriginalMainScrollHost& host,
                                 OriginalMainScrollBar bar,
                                 std::uint16_t code,
                                 std::uint16_t position) {
  if (host.modal_input_locked()) return;
  host.invalidate_main();
  // The original reads GetScrollPos only for codes 0..3 (1158:01b4 /
  // 0259); reading it first for every code, as the Win32 host always
  // did, has no visible effect.
  const auto current = host.scroll_position(bar);
  if (!current) return;
  const int client = host.client_extent(bar);
  const bool horizontal = bar == OriginalMainScrollBar::horizontal;
  // DS:71de / DS:71e0 minus the client extent (1158:01cb / 0270).
  const int world = horizontal ? kOriginalWorldWidth : kOriginalWorldHeight;
  const auto requested = original_main_scroll_request_position(
      code, *current, position, client, world - client);
  if (!requested) return;
  host.set_scroll_position(bar, *requested);
  // 1158:024d: 1058:05f8.
  for (const auto step : original_scrollbar_refresh_order()) {
    switch (step) {
      case OriginalScrollbarRefreshStep::main_rebuild_with_sky:
        host.rebuild_main_with_sky();
        break;
      case OriginalScrollbarRefreshStep::map_focus_adjustment:
        host.adjust_map_focus();
        break;
    }
  }
}

}  // namespace simtower
