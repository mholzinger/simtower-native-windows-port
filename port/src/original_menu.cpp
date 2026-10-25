#include "original_menu.hpp"

#include <stdexcept>
#include <utility>

namespace simtower {
namespace {

class Reader {
 public:
  explicit Reader(std::span<const std::byte> data) : data_(data) {}

  [[nodiscard]] std::size_t remaining() const {
    return data_.size() - position_;
  }

  std::uint8_t u8() {
    require(1);
    return static_cast<std::uint8_t>(data_[position_++]);
  }

  std::uint16_t u16() {
    require(2);
    const auto result = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data_[position_]) |
        (static_cast<std::uint16_t>(data_[position_ + 1]) << 8U));
    position_ += 2;
    return result;
  }

  void skip(std::size_t count) {
    require(count);
    position_ += count;
  }

  std::string cstring() {
    std::string result;
    while (true) {
      const auto character = u8();
      if (character == 0) return result;
      result.push_back(static_cast<char>(character));
    }
  }

 private:
  void require(std::size_t count) const {
    // The message of the Win32 loader this replaces (original_ui_win32.cpp).
    if (count > data_.size() - position_) {
      throw std::runtime_error("Truncated original Win16 UI resource");
    }
  }

  std::span<const std::byte> data_;
  std::size_t position_ = 0;
};

void parse_level(Reader& reader, OriginalMenuItems& level) {
  while (true) {
    OriginalMenuItem item{};
    item.flags = reader.u16();
    if (!item.popup()) item.id = reader.u16();
    item.text = reader.cstring();
    if (item.popup()) parse_level(reader, item.children);
    const bool last = (item.flags & kOriginalMenuEnd) != 0U;
    level.push_back(std::move(item));
    if (last) return;
  }
}

// The menu an op addresses: the bar, then up to two GetSubMenu steps.
OriginalMenuItems* resolve(OriginalMenuItems& bar, const OriginalMenuOp& op) {
  OriginalMenuItems* menu = &bar;
  for (std::uint8_t step = 0; step < op.depth && menu; ++step) {
    menu = original_sub_menu(*menu, op.path[step]);
  }
  return menu;
}

// The item an op addresses, and the menu that holds it.
std::pair<OriginalMenuItems*, std::size_t> locate(OriginalMenuItems& menu,
                                                  bool by_position,
                                                  std::uint16_t target) {
  if (by_position) {
    if (target < menu.size()) return {&menu, target};
    return {nullptr, 0U};
  }
  for (std::size_t index = 0; index < menu.size(); ++index) {
    auto& item = menu[index];
    if (!item.popup() && item.id == target) return {&menu, index};
    if (item.popup()) {
      const auto found = locate(item.children, false, target);
      if (found.first) return found;
    }
  }
  return {nullptr, 0U};
}

OriginalMenuOp bar_op(OriginalMenuOpKind kind, std::uint16_t target,
                      std::uint16_t flags) {
  return {kind, 0U, {}, false, target, flags};
}

}  // namespace

OriginalMenuItems parse_original_menu(std::span<const std::byte> resource) {
  Reader reader(resource);
  const std::uint16_t version = reader.u16();
  const std::uint16_t header_size = reader.u16();
  if (version != 0U) {
    throw std::runtime_error("Unsupported original Win16 menu version");
  }
  reader.skip(header_size);
  OriginalMenuItems menu;
  parse_level(reader, menu);
  return menu;
}

OriginalMenuItems* original_sub_menu(OriginalMenuItems& menu,
                                     std::size_t position) {
  if (position >= menu.size() || !menu[position].popup()) return nullptr;
  return &menu[position].children;
}

const OriginalMenuItems* original_sub_menu(const OriginalMenuItems& menu,
                                           std::size_t position) {
  if (position >= menu.size() || !menu[position].popup()) return nullptr;
  return &menu[position].children;
}

OriginalMenuItem* find_original_menu_command(OriginalMenuItems& menu,
                                             std::uint16_t command) {
  const auto [level, index] = locate(menu, false, command);
  return level ? &(*level)[index] : nullptr;
}

const OriginalMenuItem* find_original_menu_command(
    const OriginalMenuItems& menu, std::uint16_t command) {
  return find_original_menu_command(const_cast<OriginalMenuItems&>(menu),
                                    command);
}

void apply_original_menu_ops(OriginalMenuItems& menu,
                             std::span<const OriginalMenuOp> ops) {
  for (const auto& op : ops) {
    OriginalMenuItems* target_menu = resolve(menu, op);
    if (!target_menu) continue;
    const auto [level, index] =
        locate(*target_menu, op.by_position, op.target);
    if (!level) continue;
    auto& item = (*level)[index];
    switch (op.kind) {
      case OriginalMenuOpKind::check:
        item.flags = static_cast<std::uint16_t>(
            (item.flags & ~kOriginalMenuChecked) |
            (op.flags & kOriginalMenuChecked));
        break;
      case OriginalMenuOpKind::enable:
        item.flags = static_cast<std::uint16_t>(
            (item.flags & ~(kOriginalMenuGrayed | kOriginalMenuDisabled)) |
            (op.flags & (kOriginalMenuGrayed | kOriginalMenuDisabled)));
        break;
      case OriginalMenuOpKind::remove:
        level->erase(level->begin() + static_cast<std::ptrdiff_t>(index));
        break;
    }
  }
}

OriginalMenuOp original_menu_check_op(std::uint16_t command, bool checked) {
  return bar_op(OriginalMenuOpKind::check, command,
                checked ? kOriginalMenuChecked : 0U);
}

OriginalMenuOp original_menu_enable_op(std::uint16_t command, bool enabled) {
  return bar_op(OriginalMenuOpKind::enable, command,
                enabled ? 0U : kOriginalMenuGrayed);
}

std::vector<OriginalMenuOp> original_main_create_menu_ops(
    bool fast_mode_enabled) {
  using Kind = OriginalMenuOpKind;
  return {
      // 1158:006d-0091: GetSubMenu(menu, 1), CheckMenuItem(2, BYPOSITION).
      {Kind::check, 1U, {1U, 0U}, true, 2U,
       fast_mode_enabled ? kOriginalMenuChecked : std::uint16_t{0U}},
      // GetSubMenu(menu, 3): DeleteMenu 9000, 9001, 9002 by command, then
      // the separator left at position 2.
      {Kind::remove, 1U, {3U, 0U}, false, kOriginalCommandBomb, 0U},
      {Kind::remove, 1U, {3U, 0U}, false, kOriginalCommandTreasure, 0U},
      {Kind::remove, 1U, {3U, 0U}, false, kOriginalCommandFire, 0U},
      {Kind::remove, 1U, {3U, 0U}, true, 2U, 0U},
  };
}

std::vector<OriginalMenuOp> original_sound_menu_ops(
    const std::array<bool, 3>& categories_enabled,
    bool sound_enabled) {
  constexpr std::array<std::uint16_t, 3> commands{
      kOriginalCommandSoundElevators, kOriginalCommandSoundBackground,
      kOriginalCommandSoundEvents};
  std::vector<OriginalMenuOp> ops;
  for (std::size_t category = 0; category < commands.size(); ++category) {
    ops.push_back(original_menu_check_op(commands[category],
                                         categories_enabled[category]));
    ops.push_back(original_menu_enable_op(commands[category], sound_enabled));
  }
  return ops;
}

std::vector<OriginalMenuOp> original_recent_file_removal_ops(
    std::size_t options_item_count) {
  if (options_item_count < 6U) return {};
  return {
      {OriginalMenuOpKind::remove, 1U, {1U, 0U}, true, 6U, 0U},
      {OriginalMenuOpKind::remove, 1U, {1U, 0U}, true, 5U, 0U},
  };
}

OriginalMenuOp original_fire_menu_op(bool enabled) {
  return original_menu_enable_op(kOriginalCommandFireRescue, enabled);
}

OriginalMenuLabel original_menu_label(std::string_view raw) {
  OriginalMenuLabel label{};
  const auto tab = raw.find('\t');
  const auto text = raw.substr(0, tab);
  if (tab != std::string_view::npos) label.accelerator = raw.substr(tab + 1);
  // DrawText's prefix rule (as the canvas applies it): "&&" is a literal
  // '&', "&x" underlines x, and a trailing '&' is dropped.
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] == '&') {
      if (index + 1 >= text.size()) break;
      ++index;
      if (text[index] != '&' && !label.mnemonic) {
        label.mnemonic = label.text.size();
      }
    }
    label.text.push_back(text[index]);
  }
  return label;
}

char original_menu_mnemonic(std::string_view raw) {
  const auto label = original_menu_label(raw);
  if (!label.mnemonic) return 0;
  char character = label.text[*label.mnemonic];
  if (character >= 'A' && character <= 'Z') {
    character = static_cast<char>(character - 'A' + 'a');
  }
  return character;
}

std::vector<OriginalAccelerator> parse_original_accelerators(
    std::span<const std::byte> resource) {
  Reader reader(resource);
  std::vector<OriginalAccelerator> entries;
  while (reader.remaining() >= 5U) {
    OriginalAccelerator entry{};
    entry.flags = reader.u8();
    entry.key = reader.u16();
    entry.command = reader.u16();
    entries.push_back(entry);
    if ((entry.flags & kOriginalAcceleratorEnd) != 0U) break;
  }
  if (entries.empty()) {
    throw std::runtime_error("Invalid original accelerator resource");
  }
  return entries;
}

}  // namespace simtower
