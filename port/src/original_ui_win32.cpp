#include "original_ui_win32.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace simtower {

// The portable class-style and message tables in original_ui.hpp use these
// literal Win32 values.
static_assert(CS_DBLCLKS == 0x0008);
// original_menu.hpp keeps the Win16 MF_* values, which Win32 kept.
static_assert(MF_GRAYED == kOriginalMenuGrayed);
static_assert(MF_DISABLED == kOriginalMenuDisabled);
static_assert(MF_CHECKED == kOriginalMenuChecked);
static_assert(MF_POPUP == kOriginalMenuPopup);
static_assert(MF_MENUBARBREAK == kOriginalMenuBarBreak);
static_assert(MF_MENUBREAK == kOriginalMenuBreak);
static_assert(MF_HELP == kOriginalMenuHelp);
static_assert(MF_ENABLED == 0 && MF_UNCHECKED == 0 && MF_BYCOMMAND == 0);

namespace {

class Reader {
 public:
  explicit Reader(std::span<const std::byte> data) : data_(data) {}

  [[nodiscard]] std::size_t position() const { return position_; }
  [[nodiscard]] std::size_t remaining() const { return data_.size() - position_; }

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

  std::uint32_t u32() {
    const std::uint32_t low = u16();
    return low | (static_cast<std::uint32_t>(u16()) << 16U);
  }

  void skip(std::size_t count) {
    require(count);
    position_ += count;
  }

  std::string cstring() {
    std::string result;
    while (true) {
      const auto character = u8();
      if (character == 0) {
        return result;
      }
      result.push_back(static_cast<char>(character));
    }
  }

 private:
  void require(std::size_t count) const {
    if (count > data_.size() - position_) {
      throw std::runtime_error("Truncated original Win16 UI resource");
    }
  }

  std::span<const std::byte> data_;
  std::size_t position_ = 0;
};

std::wstring cp1252_to_wide(std::string_view value) {
  if (value.empty()) {
    return {};
  }
  const int length = MultiByteToWideChar(
      1252, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (length <= 0) {
    throw std::runtime_error("Could not decode original CP1252 menu text");
  }
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(1252, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), length);
  return result;
}

// AppendMenuW calls for one parsed level, in resource order, with the same
// flags the Win16 template carries (the model keeps them raw).
void append_menu_level(const OriginalMenuItems& level, HMENU parent) {
  for (const auto& item : level) {
    const std::wstring text = cp1252_to_wide(item.text);
    UINT native_flags = MF_STRING;
    native_flags |= item.flags & (MF_GRAYED | MF_DISABLED | MF_CHECKED |
                                  MF_MENUBREAK | MF_MENUBARBREAK | MF_HELP |
                                  MF_RIGHTJUSTIFY);
    if (item.popup()) {
      const HMENU child = CreatePopupMenu();
      if (!child) {
        throw std::runtime_error("CreatePopupMenu failed");
      }
      try {
        append_menu_level(item.children, child);
        if (!AppendMenuW(parent, native_flags | MF_POPUP,
                         reinterpret_cast<UINT_PTR>(child), text.c_str())) {
          throw std::runtime_error("AppendMenuW failed for original popup");
        }
      } catch (...) {
        DestroyMenu(child);
        throw;
      }
    } else {
      if (item.separator()) {
        native_flags = MF_SEPARATOR;
      }
      if (!AppendMenuW(parent, native_flags, item.id, text.c_str())) {
        throw std::runtime_error("AppendMenuW failed for original command");
      }
    }
  }
}

}  // namespace

void fill_original_white_rect(HDC dc, const RECT& rectangle) noexcept {
  if (!dc) return;
  HBRUSH brush = CreateSolidBrush(static_cast<COLORREF>(0x00ffffffU));
  if (!brush) return;
  FillRect(dc, &rectangle, brush);
  DeleteObject(brush);
}

HMENU create_original_menu(std::span<const std::byte> resource) {
  const auto model = parse_original_menu(resource);
  const HMENU menu = CreateMenu();
  if (!menu) {
    throw std::runtime_error("CreateMenu failed");
  }
  try {
    append_menu_level(model, menu);
    return menu;
  } catch (...) {
    DestroyMenu(menu);
    throw;
  }
}

HACCEL create_original_accelerators(std::span<const std::byte> resource) {
  std::vector<ACCEL> entries;
  for (const auto& original : parse_original_accelerators(resource)) {
    ACCEL entry{};
    entry.fVirt = static_cast<BYTE>(original.flags & 0x7FU);
    entry.key = original.key;
    entry.cmd = original.command;
    entries.push_back(entry);
  }
  const HACCEL table = CreateAcceleratorTableW(
      entries.data(), static_cast<int>(entries.size()));
  if (!table) {
    throw std::runtime_error("CreateAcceleratorTableW failed");
  }
  return table;
}

void apply_original_menu_ops(HMENU menu,
                             std::span<const OriginalMenuOp> ops) {
  for (const auto& op : ops) {
    HMENU target = menu;
    for (std::uint8_t step = 0; step < op.depth && target; ++step) {
      target = GetSubMenu(target, op.path[step]);
    }
    if (!target) continue;
    const UINT flags =
        op.flags | (op.by_position ? MF_BYPOSITION : MF_BYCOMMAND);
    switch (op.kind) {
      case OriginalMenuOpKind::check:
        CheckMenuItem(target, op.target, flags);
        break;
      case OriginalMenuOpKind::enable:
        EnableMenuItem(target, op.target, flags);
        break;
      case OriginalMenuOpKind::remove:
        DeleteMenu(target, op.target, flags);
        break;
    }
  }
}

HICON create_original_icon(const OriginalResources& resources,
                           std::string_view group_name) {
  Reader group(resources.find("GROUP_ICON", group_name));
  if (group.u16() != 0U || group.u16() != 1U) {
    throw std::runtime_error("Invalid original group-icon header");
  }
  const std::uint16_t count = group.u16();
  if (count == 0U) {
    throw std::runtime_error("Original group icon is empty");
  }

  const int width = group.u8();
  const int height = group.u8();
  group.u8();   // color count
  group.u8();   // reserved
  group.u16();  // planes
  group.u16();  // bit depth
  const std::uint32_t declared_size = group.u32();
  const std::uint16_t resource_id = group.u16();

  auto image = resources.find("ICON", resource_id);
  if (declared_size > image.size()) {
    throw std::runtime_error("Truncated original icon image");
  }
  image = image.first(declared_size);
  HICON icon = CreateIconFromResourceEx(
      reinterpret_cast<PBYTE>(const_cast<std::byte*>(image.data())),
      static_cast<DWORD>(image.size()), TRUE, 0x00030000,
      width == 0 ? 256 : width, height == 0 ? 256 : height,
      LR_DEFAULTCOLOR);
  if (!icon) {
    throw std::runtime_error("CreateIconFromResourceEx rejected original icon");
  }
  return icon;
}

HCURSOR create_original_cursor(const OriginalResources& resources,
                               std::uint16_t group_id) {
  Reader group(resources.find("GROUP_CURSOR", group_id));
  if (group.u16() != 0U || group.u16() != 2U) {
    throw std::runtime_error("Invalid original group-cursor header");
  }
  const std::uint16_t count = group.u16();
  if (count == 0U) {
    throw std::runtime_error("Original group cursor is empty");
  }

  const std::uint16_t width = group.u16();
  const std::uint16_t mask_height = group.u16();
  group.u16();  // planes
  group.u16();  // bit depth
  const std::uint32_t declared_size = group.u32();
  const std::uint16_t resource_id = group.u16();
  auto image = resources.find("CURSOR", resource_id);
  if (declared_size > image.size()) {
    throw std::runtime_error("Truncated original cursor image");
  }
  image = image.first(declared_size);
  HCURSOR cursor = static_cast<HCURSOR>(CreateIconFromResourceEx(
      reinterpret_cast<PBYTE>(const_cast<std::byte*>(image.data())),
      static_cast<DWORD>(image.size()), FALSE, 0x00030000,
      width, mask_height / 2U, LR_DEFAULTCOLOR));
  if (!cursor) {
    throw std::runtime_error(
        "CreateIconFromResourceEx rejected original cursor");
  }
  return cursor;
}

}  // namespace simtower
