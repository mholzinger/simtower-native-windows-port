// The SDL menu bar's layout, drawing and modal tracking (sdl/sdl_menu.hpp)
// on TOWER_MENU from the pack, after WM_CREATE's ops: mouse picks
// (including the Options > Sound cascade), grayed items, presses outside,
// keyboard navigation and mnemonics, the drawn highlight and border, and
// the caption's active and inactive (Windows grey / light grey) colours.
// No SDL calls.
//
// Usage: simtower_sdl_menu_tests <original_resources.pack>
// Exits 77 (skipped) without a readable pack.

#include "sdl_layout.hpp"
#include "sdl_menu.hpp"

#include "original_menu.hpp"
#include "original_resources.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

namespace {

using namespace simtower;
using namespace simtower::sdl;

std::vector<std::byte> read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return {};
  const std::vector<char> chars((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(chars.size());
  for (std::size_t index = 0; index < chars.size(); ++index) {
    bytes[index] =
        static_cast<std::byte>(static_cast<unsigned char>(chars[index]));
  }
  return bytes;
}

InputEvent at(InputKind kind, const PanelRect& rect) {
  return {kind, rect.x + rect.width / 2, rect.y + rect.height / 2, 0U};
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 77;
  const auto pack = read_file(argv[1]);
  if (pack.empty()) return 77;
  const OriginalResources resources{std::span<const std::byte>(pack)};
  auto menu = parse_original_menu(resources.find("MENU", "TOWER_MENU"));
  apply_original_menu_ops(menu, original_main_create_menu_ops(true));
  const auto layout = make_window_layout(600, 400, 157);

  // The strip is Main's nonclient row below the caption.
  const auto bar = menu_bar_layout(menu, layout);
  assert(bar.titles.size() == 4U);
  assert(bar.strip.x == layout.main_outer.x + 4);
  assert(bar.strip.y + bar.strip.height == layout.main_client.y);
  for (std::size_t index = 1; index < bar.titles.size(); ++index) {
    assert(bar.titles[index].x ==
           bar.titles[index - 1].x + bar.titles[index - 1].width);
  }

  // Press on Options, drag to Fast Mode, release: 40007.
  {
    MenuTracker tracker(menu, layout);
    tracker.open(1, false);
    auto open = menu_open_layout(menu, layout, tracker.view());
    assert(open.popup && open.popup->rows.size() == 5U);
    assert(open.popup->outer.y == bar.strip.y + bar.strip.height);
    auto step = tracker.pointer(at(InputKind::move, open.popup->rows[2]));
    assert(step.redraw && tracker.view().item == 2);
    step = tracker.pointer(at(InputKind::up, open.popup->rows[2]));
    assert(step.close && step.command == 40007U);
  }
  // Click Options (release on the title keeps it open), hover Sound, then
  // click Events in the cascade: 40013.
  {
    MenuTracker tracker(menu, layout);
    tracker.open(1, false);
    auto step = tracker.pointer(at(InputKind::up, bar.titles[1]));
    assert(!step.close && tracker.view().bar == 1);
    auto open = menu_open_layout(menu, layout, tracker.view());
    step = tracker.pointer(at(InputKind::move, open.popup->rows[1]));
    assert(tracker.view().cascade_open && tracker.view().item == 1);
    open = menu_open_layout(menu, layout, tracker.view());
    assert(open.cascade && open.cascade->rows.size() == 3U);
    assert(open.cascade->outer.x > open.popup->outer.x);
    step = tracker.pointer(at(InputKind::down, open.cascade->rows[2]));
    assert(!step.close && tracker.view().cascade_item == 2);
    step = tracker.pointer(at(InputKind::up, open.cascade->rows[2]));
    assert(step.close && step.command == 40013U);
  }
  // A grayed item (Call Fire Rescue) highlights but cannot be chosen; a
  // press outside then closes without a command; hovering another title
  // switches drop-downs.
  {
    MenuTracker tracker(menu, layout);
    tracker.open(1, false);
    auto open = menu_open_layout(menu, layout, tracker.view());
    auto step = tracker.pointer(at(InputKind::up, open.popup->rows[4]));
    assert(!step.close && !step.command && tracker.view().item == 4);
    step = tracker.pointer(at(InputKind::move, bar.titles[2]));
    assert(tracker.view().bar == 2 && tracker.view().item == -1);
    step = tracker.pointer({InputKind::down, layout.main_client.x + 500,
                            layout.main_client.y + 300, 0U});
    assert(step.close && !step.command);
  }
  // A press on the open title closes the menu.
  {
    MenuTracker tracker(menu, layout);
    tracker.open(0, false);
    tracker.pointer(at(InputKind::up, bar.titles[0]));
    const auto step = tracker.pointer(at(InputKind::down, bar.titles[0]));
    assert(step.close && !step.command);
  }
  // Keyboard: Alt opens File's title; Down drops it on New; Down, Down skips
  // the separator to Save; Enter chooses it.
  {
    MenuTracker tracker(menu, layout);
    tracker.open(0, true);
    assert(tracker.view().title_only);
    tracker.key(MenuKey::down);
    assert(!tracker.view().title_only && tracker.view().item == 0);
    tracker.key(MenuKey::down);
    tracker.key(MenuKey::down);
    assert(tracker.view().item == 3);
    tracker.key(MenuKey::up);
    tracker.key(MenuKey::up);
    tracker.key(MenuKey::up);
    assert(tracker.view().item == 6);  // wraps to Exit
    const auto step = tracker.key(MenuKey::enter);
    assert(step.close && step.command == 40001U);
  }
  // Right to Options, Enter drops it, Right opens Animation, Enter: People.
  {
    MenuTracker tracker(menu, layout);
    tracker.open(0, true);
    tracker.key(MenuKey::right);
    assert(tracker.view().bar == 1 && tracker.view().title_only);
    tracker.key(MenuKey::enter);
    assert(tracker.view().item == 0);
    tracker.key(MenuKey::right);
    assert(tracker.view().cascade_open && tracker.view().cascade_item == 0);
    tracker.key(MenuKey::left);
    assert(!tracker.view().cascade_open && tracker.view().bar == 1);
    tracker.key(MenuKey::right);
    const auto step = tracker.key(MenuKey::enter);
    assert(step.close && step.command == 40009U);
  }
  // Mnemonics: H opens Help, A picks About; Esc backs out level by level.
  {
    MenuTracker tracker(menu, layout);
    tracker.open(0, true);
    tracker.key(MenuKey::character, 'h');
    assert(tracker.view().bar == 3 && tracker.view().item == 0);
    auto step = tracker.key(MenuKey::character, 'A');
    assert(step.close && step.command == 40018U);
    tracker.open(1, true);
    tracker.key(MenuKey::enter);
    step = tracker.key(MenuKey::escape);
    assert(!step.close && tracker.view().title_only);
    step = tracker.key(MenuKey::escape);
    assert(step.close);
  }

  // Drawing: the open title and the highlighted row are navy (TEMP), the
  // drop-down has a black border, and an unopened title stays white.
  OriginalWorldRaster raster{};
  raster.width = layout.window_width;
  raster.height = layout.window_height;
  raster.pixels.assign(
      static_cast<std::size_t>(raster.width) * raster.height, 0x00123456U);
  MenuView view{};
  view.bar = 2;
  view.item = 4;
  draw_main_caption_and_menu_bar(raster, menu, layout, view,
                                 "SimTower - untitled", true);
  draw_menu_popups(raster, menu, layout, view);
  const auto pixel = [&](int x, int y) {
    return raster.pixels[static_cast<std::size_t>(y) * raster.width + x];
  };
  // The caption between the system-menu box and the title, and how many
  // of its title pixels are in a colour.
  const auto& metrics = kMainNonclientMetrics;
  const int caption_x = layout.main_outer.x + metrics.frame_width;
  const int caption_y = layout.main_outer.y + metrics.frame_height;
  const int caption_width =
      layout.main_outer.width - 2 * metrics.frame_width;
  const auto title_pixels = [&](const OriginalWorldRaster& image,
                                std::uint32_t rgb) {
    int count = 0;
    for (int y = caption_y; y < caption_y + metrics.caption_height; ++y) {
      for (int x = caption_x + 3 * metrics.caption_height;
           x < caption_x + caption_width - 3 * metrics.caption_height; ++x) {
        if (image.pixels[static_cast<std::size_t>(y) * image.width + x] ==
            rgb) {
          ++count;
        }
      }
    }
    return count;
  };
  // Active: COLOR_ACTIVECAPTION (TEMP navy) with a white title.
  assert(pixel(caption_x + metrics.caption_height + 2, caption_y + 2) ==
         0x00000080U);
  assert(title_pixels(raster, 0x00FFFFFFU) > 0);
  assert(title_pixels(raster, 0x00000000U) == 0);
  {
    // Inactive (a palette, Elevator Control or a modal is active): the
    // grey COLOR_INACTIVECAPTION with a light grey title (owner decision
    // 2026-10-24); no navy left.
    OriginalWorldRaster inactive = raster;
    draw_main_caption_and_menu_bar(inactive, menu, layout, view,
                                   "SimTower - untitled", false);
    assert(inactive.pixels[static_cast<std::size_t>(caption_y + 2) *
                               inactive.width +
                           caption_x + metrics.caption_height + 2] ==
           0x00808080U);
    assert(title_pixels(inactive, 0x00C0C0C0U) ==
           title_pixels(raster, 0x00FFFFFFU));
    assert(title_pixels(inactive, 0x00000080U) == 0);
    // The menu strip does not depend on the caption's state.
    assert(inactive.pixels[static_cast<std::size_t>(bar.titles[2].y + 1) *
                               inactive.width +
                           bar.titles[2].x + 1] == 0x00000080U);
  }
  const auto open = menu_open_layout(menu, layout, view);
  assert(pixel(bar.titles[2].x + 1, bar.titles[2].y + 1) == 0x00000080U);
  assert(pixel(bar.titles[1].x + 1, bar.titles[1].y + 1) == 0x00FFFFFFU);
  assert(pixel(open.popup->outer.x, open.popup->outer.y + 3) == 0U);
  const auto& row = open.popup->rows[4];
  assert(pixel(row.x + 1, row.y + 1) == 0x00000080U);
  assert(pixel(open.popup->rows[0].x + 1, open.popup->rows[0].y + 1) ==
         0x00FFFFFFU);
  // Nothing is drawn below the drop-down.
  assert(pixel(open.popup->outer.x + 2,
               open.popup->outer.y + open.popup->outer.height + 2) ==
         0x00123456U);
  std::printf("sdl_menu_tests: layout, tracking and drawing ok\n");
  return 0;
}
