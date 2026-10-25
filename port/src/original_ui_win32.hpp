#pragma once

// Win32 menu, accelerator, icon, cursor, and GDI helpers for the portable
// UI rules in original_ui.hpp.
#include "original_menu.hpp"
#include "original_ui.hpp"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace simtower {

// Native equivalents of the original Win16 MENU/ACCELERATOR/GROUP_ICON
// resource consumers. The input remains the exact bytes embedded in the NE.
[[nodiscard]] HMENU create_original_menu(std::span<const std::byte> resource);
[[nodiscard]] HACCEL create_original_accelerators(
    std::span<const std::byte> resource);
// Replays a portable menu op list (original_menu.hpp) through USER:
// GetSubMenu along op.path, then CheckMenuItem / EnableMenuItem / DeleteMenu
// with MF_BYPOSITION or MF_BYCOMMAND. A missing sub-menu skips the op.
void apply_original_menu_ops(HMENU menu,
                             std::span<const OriginalMenuOp> ops);
[[nodiscard]] HICON create_original_icon(const OriginalResources& resources,
                                          std::string_view group_name);

// Exact 1208:05a9 auxiliary-surface clearing wrapper. The Win16 routine
// creates a temporary COLORREF 0x00ffffff solid brush, fills the caller's
// complete rectangle, and deletes that brush before returning.
void fill_original_white_rect(HDC dc, const RECT& rectangle) noexcept;

// Exact custom-cursor resource path initialized by 11e0:0cfb. Win16
// GROUP_CURSOR entries use WORD width/height fields and point at one of the
// four CURSOR resources; the returned Win32 cursor owns the same image and
// hotspot and must be released with DestroyCursor.
[[nodiscard]] HCURSOR create_original_cursor(
    const OriginalResources& resources,
    std::uint16_t group_id);

// The active, non-modal WM_ACTIVATE paths at CMDBTNWNDPROC 1050:008d,
// INFOWNDPROC 1120:007d, and MAPWNDPROC 1168:009f call GetClientRect and then
// overwrite only RECT.bottom with the literal 8 before ValidateRect. Thus the
// title strip is validated while any pending content-area update survives.
[[nodiscard]] constexpr RECT original_palette_activation_validation_rect(
    RECT client) noexcept {
  client.bottom = kOriginalPaletteFrameHeight;
  return client;
}

}  // namespace simtower
