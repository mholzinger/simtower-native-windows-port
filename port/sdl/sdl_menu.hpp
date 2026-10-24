#pragma once

// Main's menu bar and drop-downs in the SDL window, drawn from the live
// TOWER_MENU model (Session::menu) in the Windows 3.1 style, so they look
// the same on every platform (no native OS menus). The original customizes
// nothing about its menus (no owner-draw, no check bitmaps), so this
// reproduces standard USER drawing with the default system colours. Pure
// pixels and state (no SDL): the window host feeds events to MenuTracker in
// its own modal loop, and compose_window draws Session::menu_view.
//
// TEMP (open cosmetic questions): highlight navy 0x000080 with white text,
// white bar, Liberation Sans Regular 13 px (the bundled font has no bold
// System font), row heights, paddings, and no drop shadow.

#include "sdl_layout.hpp"
#include "sdl_session.hpp"

#include "original_menu.hpp"
#include "original_world.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace simtower::sdl {

// The menu strip inside Main's nonclient area (below the caption) and one
// title cell per bar item.
struct MenuBarLayout {
  PanelRect strip{};
  std::vector<PanelRect> titles{};
};
[[nodiscard]] MenuBarLayout menu_bar_layout(const OriginalMenuItems& menu,
                                            const WindowLayout& layout);
// The title cell under (x, y), or -1.
[[nodiscard]] int menu_title_at(const MenuBarLayout& bar, int x, int y);

// One drop-down at its window position: the outer rectangle (1-pixel
// border included) and one row per item.
struct MenuPopupLayout {
  PanelRect outer{};
  std::vector<PanelRect> rows{};
  int text_x{};
  int accelerator_x{};
};

// The open drop-down and cascade of a view (either may be absent).
struct MenuOpenLayout {
  const OriginalMenuItems* popup_items{};
  std::optional<MenuPopupLayout> popup{};
  const OriginalMenuItems* cascade_items{};
  std::optional<MenuPopupLayout> cascade{};
};
[[nodiscard]] MenuOpenLayout menu_open_layout(const OriginalMenuItems& menu,
                                              const WindowLayout& layout,
                                              const MenuView& view);

// Caption (TEMP navy title bar with the window title) and the menu strip.
void draw_main_caption_and_menu_bar(OriginalWorldRaster& window,
                                    const OriginalMenuItems& menu,
                                    const WindowLayout& layout,
                                    const MenuView& view,
                                    std::string_view title);
// The open drop-down and cascade, over everything else.
void draw_menu_popups(OriginalWorldRaster& window,
                      const OriginalMenuItems& menu,
                      const WindowLayout& layout,
                      const MenuView& view);

enum class MenuKey : std::uint8_t {
  left,
  right,
  up,
  down,
  enter,
  escape,
  toggle,     // Alt or F10
  character,  // a mnemonic letter
};

struct MenuStep {
  bool redraw{};
  bool close{};
  // The chosen command, sent after the menu closes (USER posts WM_COMMAND).
  std::optional<std::uint16_t> command{};
};

// The modal menu loop's state machine (USER's MenuSelect tracking): a press
// on a title opens its drop-down, moving over titles switches, a release
// on an enabled command chooses it, a press outside closes the menu and is
// consumed. Grayed items highlight but cannot be chosen. Keys: Left/Right
// switch titles (Right opens a cascade, Left closes it), Up/Down move over
// selectable rows, Enter chooses or opens, Esc backs out one level, and a
// mnemonic letter picks its item.
class MenuTracker {
 public:
  MenuTracker(const OriginalMenuItems& menu, const WindowLayout& layout);

  // A press on title `bar` (mouse), or Alt/F10 (keyboard, title only).
  MenuStep open(int bar, bool keyboard);
  MenuStep pointer(const InputEvent& event);
  MenuStep key(MenuKey key, char character = 0);
  [[nodiscard]] const MenuView& view() const { return view_; }

 private:
  [[nodiscard]] const OriginalMenuItems* popup_items() const;
  [[nodiscard]] const OriginalMenuItems* cascade_items() const;
  MenuStep choose(const OriginalMenuItems& items, int index, bool cascade);
  MenuStep select_bar(int bar, bool open_popup);

  const OriginalMenuItems& menu_;
  WindowLayout layout_;
  MenuBarLayout bar_layout_{};
  MenuView view_{};
  bool button_down_{};
};

// The first selectable (non-separator) row at or after `from` in
// direction step (+1 or -1), wrapping; -1 when there is none.
[[nodiscard]] int next_menu_row(const OriginalMenuItems& items, int from,
                                int step);

}  // namespace simtower::sdl
