#pragma once

// TEMP: the Windows 3.1 system colours the SDL host draws its USER-owned
// pieces with (window frames, captions, the menu, push buttons, message
// boxes). No resource holds them; USER reads them from WIN.INI. The navy
// caption and highlight (2026-10-23) and the grey inactive caption
// (2026-10-24) are owner decisions; the others are the Windows 3.1
// defaults until a reference exists. One table so the
// panels, the menu and the dialog runtime agree.

#include <cstdint>

namespace simtower::sdl::style {

inline constexpr std::uint32_t kBlack = 0x00000000U;
inline constexpr std::uint32_t kWhite = 0x00FFFFFFU;
// COLOR_ACTIVECAPTION and COLOR_HIGHLIGHT (owner decision 2026-10-23).
inline constexpr std::uint32_t kCaption = 0x00000080U;
inline constexpr std::uint32_t kCaptionText = kWhite;
// COLOR_INACTIVECAPTION and COLOR_INACTIVECAPTIONTEXT for a window that is
// not active: the Windows classic (Win95+) COLOR_INACTIVECAPTION defaults,
// owner decision 2026-10-24 (the Windows 3.1 defaults were a white bar
// with a black title). What the Win32 host shows depends on the system
// theme.
inline constexpr std::uint32_t kInactiveCaption = 0x00808080U;
inline constexpr std::uint32_t kInactiveCaptionText = 0x00C0C0C0U;
inline constexpr std::uint32_t kHighlight = 0x00000080U;
inline constexpr std::uint32_t kHighlightText = kWhite;
// COLOR_BTNFACE, COLOR_BTNSHADOW, COLOR_BTNHIGHLIGHT, COLOR_WINDOWFRAME.
inline constexpr std::uint32_t kButtonFace = 0x00C0C0C0U;
inline constexpr std::uint32_t kButtonShadow = 0x00808080U;
inline constexpr std::uint32_t kButtonHighlight = kWhite;
inline constexpr std::uint32_t kFrame = kBlack;
// COLOR_MENU, COLOR_MENUTEXT, COLOR_GRAYTEXT, COLOR_WINDOW.
inline constexpr std::uint32_t kMenu = kWhite;
inline constexpr std::uint32_t kMenuText = kBlack;
inline constexpr std::uint32_t kGrayText = 0x00808080U;
inline constexpr std::uint32_t kWindow = kWhite;
// COLOR_APPWORKSPACE / desktop behind the panels.
inline constexpr std::uint32_t kDesktop = 0x00808080U;

}  // namespace simtower::sdl::style
