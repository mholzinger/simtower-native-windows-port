#pragma once

// Portable model of the original Win16 MENU and ACCELERATOR resources
// (TOWER_MENU) and of the USER menu calls the original makes on them.
//
// - parse_original_menu reads the exact RT_MENU template bytes the Win32
//   host used to feed straight into AppendMenuW (u16 version 0, u16 header
//   size, then per item u16 flags, a u16 id for non-popup items and a CP1252
//   C string; 0x10 opens a popup level, 0x80 ends a level). An item with id
//   0 and empty text is a separator. The raw Win16 flags are kept, so the
//   GRAYED, DISABLED, CHECKED, MENUBREAK, MENUBARBREAK and HELP bits survive.
// - The CheckMenuItem / EnableMenuItem / DeleteMenu sites of the original
//   (WM_CREATE 1158:0062, the Sound profile 1128:0ae7, the File-command
//   recent-file removal 10d0:003e/0238, the fire-crew item 10d0:0b31 and
//   10e8:01d9/0251/02c2, and the WM_COMMAND toggles 1158:073e) are built as
//   op lists. The Win32 host replays an op list through USER
//   (original_ui_win32.hpp: apply_original_menu_ops); other hosts apply it to
//   the model (apply_original_menu_ops below), so both see the same menu.
//
// The flag values are the Win16 ones, which Win32 kept unchanged.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace simtower {

inline constexpr std::uint16_t kOriginalMenuGrayed = 0x0001U;    // MF_GRAYED
inline constexpr std::uint16_t kOriginalMenuDisabled = 0x0002U;  // MF_DISABLED
inline constexpr std::uint16_t kOriginalMenuChecked = 0x0008U;   // MF_CHECKED
inline constexpr std::uint16_t kOriginalMenuPopup = 0x0010U;     // MF_POPUP
// MF_MENUBARBREAK
inline constexpr std::uint16_t kOriginalMenuBarBreak = 0x0020U;
inline constexpr std::uint16_t kOriginalMenuBreak = 0x0040U;     // MF_MENUBREAK
inline constexpr std::uint16_t kOriginalMenuEnd = 0x0080U;       // template end
inline constexpr std::uint16_t kOriginalMenuHelp = 0x4000U;      // MF_HELP

// TOWER_MENU command ids (WM_COMMAND, 1158:06b9).
inline constexpr std::uint16_t kOriginalCommandExit = 40001U;
inline constexpr std::uint16_t kOriginalCommandSaveAs = 40002U;
inline constexpr std::uint16_t kOriginalCommandSave = 40003U;
inline constexpr std::uint16_t kOriginalCommandOpen = 40004U;
inline constexpr std::uint16_t kOriginalCommandNew = 40005U;
inline constexpr std::uint16_t kOriginalCommandFastMode = 40007U;
inline constexpr std::uint16_t kOriginalCommandFireRescue = 40008U;  // 0x9c48
inline constexpr std::uint16_t kOriginalCommandPeople = 40009U;
inline constexpr std::uint16_t kOriginalCommandEffects = 40010U;
inline constexpr std::uint16_t kOriginalCommandSoundElevators = 40011U;
inline constexpr std::uint16_t kOriginalCommandSoundBackground = 40012U;
inline constexpr std::uint16_t kOriginalCommandSoundEvents = 40013U;
inline constexpr std::uint16_t kOriginalCommandToolBar = 40014U;
inline constexpr std::uint16_t kOriginalCommandInfoBar = 40015U;
inline constexpr std::uint16_t kOriginalCommandMapWindow = 40016U;
inline constexpr std::uint16_t kOriginalCommandFinance = 40017U;
inline constexpr std::uint16_t kOriginalCommandAbout = 40018U;
inline constexpr std::uint16_t kOriginalCommandFindPerson = 40019U;
inline constexpr std::uint16_t kOriginalCommandFindTenant = 40020U;
inline constexpr std::uint16_t kOriginalCommandHelpContents = 40021U;
inline constexpr std::uint16_t kOriginalCommandBomb = 9000U;
inline constexpr std::uint16_t kOriginalCommandTreasure = 9001U;
inline constexpr std::uint16_t kOriginalCommandFire = 9002U;
inline constexpr std::uint16_t kOriginalCommandPresent = 9003U;

struct OriginalMenuItem {
  // Raw resource flags (the 0x80 end marker included as read).
  std::uint16_t flags{};
  // Command id; zero for popups (which carry none) and separators.
  std::uint16_t id{};
  std::string text{};  // CP1252, '&' mnemonics and '\t' kept
  std::vector<OriginalMenuItem> children{};

  [[nodiscard]] bool popup() const noexcept {
    return (flags & kOriginalMenuPopup) != 0U;
  }
  [[nodiscard]] bool separator() const noexcept {
    return !popup() && id == 0U && text.empty();
  }
  [[nodiscard]] bool grayed() const noexcept {
    return (flags & (kOriginalMenuGrayed | kOriginalMenuDisabled)) != 0U;
  }
  [[nodiscard]] bool checked() const noexcept {
    return (flags & kOriginalMenuChecked) != 0U;
  }
};

using OriginalMenuItems = std::vector<OriginalMenuItem>;

// Throws std::runtime_error on a truncated template or a version other
// than zero (the messages the Win32 loader used).
[[nodiscard]] OriginalMenuItems parse_original_menu(
    std::span<const std::byte> resource);

// GetSubMenu: the popup at position, or null.
[[nodiscard]] OriginalMenuItems* original_sub_menu(OriginalMenuItems& menu,
                                                   std::size_t position);
[[nodiscard]] const OriginalMenuItems* original_sub_menu(
    const OriginalMenuItems& menu, std::size_t position);
// MF_BYCOMMAND lookup: depth-first, searching each popup's children in
// place, as USER does. Null when absent.
[[nodiscard]] OriginalMenuItem* find_original_menu_command(
    OriginalMenuItems& menu, std::uint16_t command);
[[nodiscard]] const OriginalMenuItem* find_original_menu_command(
    const OriginalMenuItems& menu, std::uint16_t command);

enum class OriginalMenuOpKind : std::uint8_t {
  check,   // CheckMenuItem
  enable,  // EnableMenuItem
  remove,  // DeleteMenu
};

// One USER menu call. The target menu is reached from the menu bar by up to
// two GetSubMenu calls (path), then the call addresses `target` by command
// or by position. `flags` is the call's MF_CHECKED/MF_UNCHECKED or
// MF_ENABLED/MF_GRAYED word without MF_BYPOSITION.
struct OriginalMenuOp {
  OriginalMenuOpKind kind{OriginalMenuOpKind::check};
  std::uint8_t depth{};
  std::array<std::uint8_t, 2> path{};
  bool by_position{};
  std::uint16_t target{};
  std::uint16_t flags{};

  friend bool operator==(const OriginalMenuOp&,
                         const OriginalMenuOp&) = default;
};

// Applies each op to the model as USER would apply it to the HMENU. An op
// whose sub-menu or target is missing does nothing (USER returns -1/FALSE).
void apply_original_menu_ops(OriginalMenuItems& menu,
                             std::span<const OriginalMenuOp> ops);

// CheckMenuItem(menu bar, command, MF_BYCOMMAND | checked).
[[nodiscard]] OriginalMenuOp original_menu_check_op(std::uint16_t command,
                                                    bool checked);
// EnableMenuItem(menu bar, command, MF_BYCOMMAND | MF_ENABLED/MF_GRAYED).
[[nodiscard]] OriginalMenuOp original_menu_enable_op(std::uint16_t command,
                                                     bool enabled);

// MAINWNDPROC WM_CREATE 1158:0062-00d2: Fast Mode (Options position 2) is
// checked by position from DS:de34; Help (position 3) deletes the hidden
// 9000/9001/9002 debug items by command and then its separator by position.
[[nodiscard]] std::vector<OriginalMenuOp> original_main_create_menu_ops(
    bool fast_mode_enabled);

// The Sound profile (native apply_original_audio_menu_state, 1128:0ae7):
// 40011..40013 by command, checked from the three category words and grayed
// while the master sound switch is off. [INFERRED 60%] The original only ORs
// the 0x01 bit into CheckMenuItem, which USER ignores, so the original
// would never gray these; the native host's EnableMenuItem is kept as is
// until that is checked dynamically.
[[nodiscard]] std::vector<OriginalMenuOp> original_sound_menu_ops(
    const std::array<bool, 3>& categories_enabled,
    bool sound_enabled);

// 10d0:003e-0075 (New) and 10d0:0238-026f (Open): GetSubMenu(menu, 1) is
// Options (not File), and only a count of at least six deletes positions 6
// and 5. TOWER_MENU's Options has five items, so this is a no-op for the
// shipped resource; it is kept because the binary does it.
[[nodiscard]] std::vector<OriginalMenuOp> original_recent_file_removal_ops(
    std::size_t options_item_count);

// The fire-crew item (0x9c48 = 40008), enabled or grayed by command.
[[nodiscard]] OriginalMenuOp original_fire_menu_op(bool enabled);

// A menu label split for drawing: '&x' marks x as the mnemonic ("&&" is a
// literal '&', a trailing '&' is dropped), and text after the first '\t' is
// the right-hand accelerator column.
struct OriginalMenuLabel {
  std::string text{};
  std::string accelerator{};
  // Byte index in text of the underlined character.
  std::optional<std::size_t> mnemonic{};
};
[[nodiscard]] OriginalMenuLabel original_menu_label(std::string_view raw);
// The mnemonic character, lower-cased ASCII, or 0.
[[nodiscard]] char original_menu_mnemonic(std::string_view raw);

// One ACCELERATOR entry: Win16 flags (0x80 ends the table, 0x01 FVIRTKEY),
// key, and the WM_COMMAND id.
struct OriginalAccelerator {
  std::uint8_t flags{};
  std::uint16_t key{};
  std::uint16_t command{};

  friend bool operator==(const OriginalAccelerator&,
                         const OriginalAccelerator&) = default;
};
inline constexpr std::uint8_t kOriginalAcceleratorVirtualKey = 0x01U;
inline constexpr std::uint8_t kOriginalAcceleratorEnd = 0x80U;
inline constexpr std::uint16_t kOriginalVirtualKeyF1 = 0x70U;

// Throws std::runtime_error when the table holds no entry.
[[nodiscard]] std::vector<OriginalAccelerator> parse_original_accelerators(
    std::span<const std::byte> resource);

}  // namespace simtower
