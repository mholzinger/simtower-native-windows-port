// The portable menu model (original_menu.hpp): TOWER_MENU and its
// accelerator parsed from the resource pack must have the exact structure
// and Win16 flags dumped from SIMTOWER.EXE, and the WM_CREATE, Sound,
// recent-file, fire and toggle op lists must leave the model as USER leaves
// the HMENU. Synthetic templates cover the parser's edge cases without
// assets.
//
// Usage: simtower_menu_tests [original_resources.pack]
// Without a readable pack the resource half is skipped (exit 77 when that is
// all there is to run).

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
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace simtower;

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

struct Expected {
  std::uint16_t flags;
  std::uint16_t id;
  const char* text;
};

void expect_level(const OriginalMenuItems& level,
                  std::initializer_list<Expected> expected) {
  assert(level.size() == expected.size());
  std::size_t index = 0;
  for (const auto& item : expected) {
    const auto& actual = level[index++];
    if (actual.flags != item.flags || actual.id != item.id ||
        actual.text != item.text) {
      std::fprintf(stderr, "menu item %zu: got flags %#x id %u '%s'\n",
                   index - 1, actual.flags, actual.id, actual.text.c_str());
    }
    assert(actual.flags == item.flags);
    assert(actual.id == item.id);
    assert(actual.text == item.text);
  }
}

void test_tower_menu(const OriginalResources& resources) {
  auto menu = parse_original_menu(resources.find("MENU", "TOWER_MENU"));
  // The bar: File, Options, Windows, Help (Help ends the template, 0x90).
  expect_level(menu, {{0x10, 0, "&File"},
                      {0x10, 0, "&Options"},
                      {0x10, 0, "&Windows"},
                      {0x90, 0, "&Help"}});
  expect_level(menu[0].children, {{0x00, 40005, "&New..."},
                                  {0x00, 40004, "&Open..."},
                                  {0x00, 0, ""},
                                  {0x00, 40003, "&Save"},
                                  {0x00, 40002, "Save As..."},
                                  {0x00, 0, ""},
                                  {0x80, 40001, "&Exit"}});
  expect_level(menu[1].children, {{0x10, 0, "Animation"},
                                  {0x10, 0, "Sound"},
                                  {0x00, 40007, "Fast Mode"},
                                  {0x00, 0, ""},
                                  {0x81, 40008, "Call Fire Rescue"}});
  expect_level(menu[1].children[0].children,
               {{0x08, 40009, "People"}, {0x88, 40010, "Effects"}});
  expect_level(menu[1].children[1].children, {{0x08, 40011, "Elevators"},
                                              {0x08, 40012, "Background"},
                                              {0x88, 40013, "Events"}});
  expect_level(menu[2].children, {{0x00, 40014, "Tool Bar"},
                                  {0x00, 40015, "Info Bar"},
                                  {0x00, 40016, "Map Window"},
                                  {0x00, 0, ""},
                                  {0x00, 40017, "Finance Window..."},
                                  {0x00, 0, ""},
                                  {0x00, 40019, "Find Person..."},
                                  {0x80, 40020, "Find Tenant..."}});
  expect_level(menu[3].children, {{0x00, 40021, "&Contents\tF1"},
                                  {0x00, 40018, "&About SimTower..."},
                                  {0x00, 0, ""},
                                  {0x00, 9000, "Terrorist"},
                                  {0x00, 9001, "Treasure"},
                                  {0x80, 9002, "Fire"}});
  // No break or MF_HELP flags anywhere: Help is not right-justified.
  const auto no_breaks = [](const OriginalMenuItems& level,
                            auto& self) -> void {
    for (const auto& item : level) {
      assert((item.flags & (kOriginalMenuBreak | kOriginalMenuBarBreak |
                            kOriginalMenuHelp)) == 0U);
      self(item.children, self);
    }
  };
  no_breaks(menu, no_breaks);
  assert(menu[1].children[4].grayed());
  assert(menu[1].children[3].separator());
  assert(!menu[1].children[0].separator());

  // WM_CREATE: Fast Mode checked by position, the debug items and their
  // separator gone.
  apply_original_menu_ops(menu, original_main_create_menu_ops(true));
  assert(menu[1].children[2].checked());
  expect_level(menu[3].children,
               {{0x00, 40021, "&Contents\tF1"},
                {0x00, 40018, "&About SimTower..."}});
  // Applying it again changes nothing more (USER would fail every delete).
  apply_original_menu_ops(menu, original_main_create_menu_ops(false));
  assert(!menu[1].children[2].checked());
  assert(menu[3].children.size() == 2U);
  assert(find_original_menu_command(menu, 9000) == nullptr);

  // Sound profile: checks per category, grayed while sound is off.
  apply_original_menu_ops(menu,
                          original_sound_menu_ops({true, false, true}, false));
  const auto& sound = menu[1].children[1].children;
  assert(sound[0].checked() && sound[0].grayed());
  assert(!sound[1].checked() && sound[1].grayed());
  assert(sound[2].checked() && sound[2].grayed());
  assert((sound[2].flags & kOriginalMenuEnd) != 0U);  // raw flag kept
  apply_original_menu_ops(menu,
                          original_sound_menu_ops({true, true, true}, true));
  for (const auto& item : sound) assert(item.checked() && !item.grayed());

  // Recent-file removal targets Options (five items): a no-op.
  assert(original_recent_file_removal_ops(menu[1].children.size()).empty());
  const auto removal = original_recent_file_removal_ops(7U);
  assert(removal.size() == 2U && removal[0].target == 6U &&
         removal[1].target == 5U && removal[0].by_position &&
         removal[0].path[0] == 1U);

  // Fire rescue by command, and the toggles' CheckMenuItem by command.
  const OriginalMenuOp enable_fire = original_fire_menu_op(true);
  apply_original_menu_ops(menu, {&enable_fire, 1U});
  assert(!find_original_menu_command(menu, 40008)->grayed());
  const OriginalMenuOp gray_fire = original_fire_menu_op(false);
  apply_original_menu_ops(menu, {&gray_fire, 1U});
  assert(find_original_menu_command(menu, 40008)->grayed());
  const OriginalMenuOp uncheck_people = original_menu_check_op(40009, false);
  apply_original_menu_ops(menu, {&uncheck_people, 1U});
  assert(!menu[1].children[0].children[0].checked());
  // Tool Bar, Info Bar and Map Window start unchecked and nothing checks them.
  for (std::size_t index = 0; index < 3; ++index) {
    assert(!menu[2].children[index].checked());
  }

  // The accelerator: one entry, F1 -> Help Contents.
  const auto accelerators =
      parse_original_accelerators(resources.find("ACCELERATOR", "TOWER_MENU"));
  assert(accelerators.size() == 1U);
  assert(accelerators[0].flags == 0x81U);
  assert(accelerators[0].key == kOriginalVirtualKeyF1);
  assert(accelerators[0].command == kOriginalCommandHelpContents);
  std::printf("menu_tests: TOWER_MENU structure and ops ok\n");
}

std::vector<std::byte> bytes(std::initializer_list<int> values) {
  std::vector<std::byte> out;
  for (const int value : values) out.push_back(static_cast<std::byte>(value));
  return out;
}

void test_synthetic() {
  // Version 0, header 2 (skipped), then "&A" popup { 7 "x", sep(end) },
  // 9 "B&&c\tCtrl+B" (end).
  const auto data = bytes({0, 0, 2, 0, 0xAA, 0xBB,
                           0x10, 0, '&', 'A', 0,
                           0x00, 0, 7, 0, 'x', 0,
                           0x80, 0, 0, 0, 0,
                           0x80, 0, 9, 0, 'B', '&', '&', 'c', '\t', 'C',
                           't', 'r', 'l', '+', 'B', 0});
  const auto menu = parse_original_menu(data);
  assert(menu.size() == 2U);
  assert(menu[0].popup() && menu[0].children.size() == 2U);
  assert(menu[0].children[1].separator());
  assert(find_original_menu_command(menu, 7)->text == "x");
  assert(find_original_menu_command(menu, 9) == &menu[1]);
  assert(original_sub_menu(menu, 1) == nullptr);

  const auto label = original_menu_label(menu[1].text);
  assert(label.text == "B&c" && label.accelerator == "Ctrl+B");
  assert(!label.mnemonic);
  const auto file = original_menu_label("&File");
  assert(file.text == "File" && file.mnemonic == 0U);
  assert(original_menu_mnemonic("Save &As") == 'a');
  assert(original_menu_mnemonic("Fast Mode") == 0);

  bool threw = false;
  try {
    (void)parse_original_menu(bytes({1, 0, 0, 0}));
  } catch (const std::runtime_error&) {
    threw = true;
  }
  assert(threw);
  threw = false;
  try {
    (void)parse_original_menu(bytes({0, 0, 0, 0, 0x00, 0, 7}));
  } catch (const std::runtime_error&) {
    threw = true;
  }
  assert(threw);
  threw = false;
  try {
    (void)parse_original_accelerators(bytes({0x81, 0x70}));
  } catch (const std::runtime_error&) {
    threw = true;
  }
  assert(threw);
  std::printf("menu_tests: synthetic templates ok\n");
}

}  // namespace

int main(int argc, char** argv) {
  test_synthetic();
  if (argc < 2) return 0;
  const auto pack = read_file(argv[1]);
  if (pack.empty()) {
    std::printf("menu_tests: no pack at %s, TOWER_MENU skipped\n", argv[1]);
    return 77;
  }
  const OriginalResources resources{std::span<const std::byte>(pack)};
  test_tower_menu(resources);
  return 0;
}
