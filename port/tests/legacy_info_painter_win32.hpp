#pragma once

// Test-only copy of draw_original_info(HDC, ...) exactly as it was before the
// painter moved behind OriginalDrawContext (git: port/src/
// original_info_win32.cpp at the feat/sdl3-construction base). info_tests
// compares its pixels with the refactored GDI path, so any change in the GDI
// call sequence that alters output shows up. Do not edit except to track an
// intended change in the original painter.

#include "original_dib_win32.hpp"
#include "original_font_win32.hpp"
#include "original_info.hpp"

#include <windows.h>

#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace legacy_info {
using namespace simtower;
namespace {

constexpr COLORREF kDarkText = RGB(0x26, 0x26, 0x26);
constexpr COLORREF kWeekendText = RGB(0xb3, 0x00, 0x00);
constexpr COLORREF kFieldGray = RGB(0x99, 0x99, 0x99);
constexpr COLORREF kStatusGray = RGB(0xd9, 0xd9, 0xd9);

struct SavedDc {
  HDC dc{};
  int state{};

  explicit SavedDc(HDC value) : dc(value), state(SaveDC(value)) {}
  ~SavedDc() {
    if (state != 0) RestoreDC(dc, state);
  }

  SavedDc(const SavedDc&) = delete;
  SavedDc& operator=(const SavedDc&) = delete;
};

void fill_color(HDC dc, const RECT& rectangle, COLORREF color) {
  HBRUSH brush = CreateSolidBrush(color);
  if (!brush) throw std::runtime_error("CreateSolidBrush failed for info window");
  FillRect(dc, &rectangle, brush);
  DeleteObject(brush);
}

HFONT make_original_font(int pixel_height) {
  return original_cached_font(static_cast<std::int16_t>(pixel_height));
}

struct SelectedFont {
  HDC dc{};
  HFONT font{};
  HGDIOBJ previous{};

  SelectedFont(HDC destination, int pixel_height)
      : dc(destination), font(make_original_font(pixel_height)) {
    if (font) previous = SelectObject(dc, font);
  }
  ~SelectedFont() {
    if (previous) SelectObject(dc, previous);
  }

  SelectedFont(const SelectedFont&) = delete;
  SelectedFont& operator=(const SelectedFont&) = delete;
};

void draw_clipped_dib(HDC dc,
                      std::span<const std::byte> resource,
                      int x,
                      int y,
                      const RECT& clip) {
  // 1208:0a42 installs a CreateRectRgnIndirect region, selects it as the clip,
  // and deletes it. SaveDC/IntersectClipRect/RestoreDC preserves that visible
  // result without leaking the clip into later native drawing.
  SavedDc saved(dc);
  IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
  draw_original_dib(dc, resource, x, y);
}

void draw_date(HDC dc, const OriginalInfoContent& content) {
  // 1118:0ba4 constructs this 152x16 field at (151,5); the caller's shared
  // eight-pixel palette-frame offset yields client coordinates (151,13).
  const RECT field{151, 13, 303, 29};
  fill_color(dc, field, kFieldGray);

  SelectedFont font(dc, 14);
  SetBkMode(dc, TRANSPARENT);
  SetTextAlign(dc, TA_UPDATECP | TA_BASELINE);
  MoveToEx(dc, 159, 28, nullptr);
  if (!content.date_red.empty()) {
    SetTextColor(dc, kWeekendText);
    TextOutA(dc, 0, 0, content.date_red.data(),
             static_cast<int>(content.date_red.size()));
  }
  SetTextColor(dc, kDarkText);
  TextOutA(dc, 0, 0, content.date_dark.data(),
           static_cast<int>(content.date_dark.size()));
}

void draw_rating(HDC dc,
                 const OriginalResources& resources,
                 std::uint16_t rating) {
  if (rating == 6U) {
    const RECT clip{42, 10, 148, 32};
    draw_clipped_dib(dc, resources.find("BITMAP", 327), clip.left, clip.top,
                     clip);
    return;
  }

  // 1118:0b67 returns each 21x19 star strip at x=43+(index-1)*21, y=3;
  // applying the palette-frame offset produces the rectangles below.
  for (int index = 1; index <= 5; ++index) {
    const RECT clip{43 + (index - 1) * 21, 11,
                    43 + index * 21, 30};
    const int bitmap = index <= static_cast<int>(rating) ? 322 : 323;
    draw_clipped_dib(dc, resources.find("BITMAP", bitmap), clip.left,
                     clip.top, clip);
  }
}

void draw_balance(HDC dc, const std::string& text) {
  // 1118:0bda's 70x14 base rectangle plus 1118:0143's (10,8) offset.
  const RECT field{354, 13, 424, 27};
  fill_color(dc, field, kFieldGray);

  SelectedFont font(dc, 13);
  SetTextColor(dc, kDarkText);
  SetBkMode(dc, TRANSPARENT);
  SetTextAlign(dc, TA_UPDATECP | TA_RIGHT | TA_BASELINE);
  MoveToEx(dc, field.right - 1, field.bottom - 1, nullptr);
  TextOutA(dc, 0, 0, text.data(), static_cast<int>(text.size()));
}

void draw_status(HDC dc, const std::string& text) {
  // 1118:0c0f's 262x11 status rectangle plus the eight-pixel client offset.
  const RECT field{41, 33, 303, 44};
  const RECT highlight{41, 44, 303, 45};
  fill_color(dc, field, kStatusGray);
  fill_color(dc, highlight, RGB(0xff, 0xff, 0xff));

  SelectedFont font(dc, 12);
  SetTextColor(dc, kDarkText);
  SetBkMode(dc, TRANSPARENT);
  SetTextAlign(dc, TA_UPDATECP | TA_BASELINE);
  // 1118:02ee-02fb establishes the DrawText current position explicitly.
  // With TA_UPDATECP/TA_BASELINE this is observable: omitting it can leave the
  // status string at the current position inherited from the balance painter.
  // Tracked change (cleanup, 1118:0307's 1208:0cf5(0, -1)): the baseline
  // at field.bottom - 1, was field.bottom.
  MoveToEx(dc, field.left + 2, field.bottom - 1, nullptr);
  RECT text_rect = field;
  DrawTextA(dc, text.data(), static_cast<int>(text.size()), &text_rect,
            DT_SINGLELINE);
}

void draw_population(HDC dc, const std::string& text) {
  // 1118:0c44's 86x14 population rectangle plus the client-frame offset.
  const RECT field{337, 31, 423, 45};
  fill_color(dc, field, kFieldGray);

  SelectedFont font(dc, 13);
  SIZE extent{};
  GetTextExtentPoint32A(dc, text.data(), static_cast<int>(text.size()),
                        &extent);
  SetTextColor(dc, kDarkText);
  SetBkMode(dc, TRANSPARENT);
  SetTextAlign(dc, TA_UPDATECP | TA_BASELINE);
  MoveToEx(dc, field.right - extent.cx, field.bottom - 2, nullptr);
  TextOutA(dc, 0, 0, text.data(), static_cast<int>(text.size()));
}

void draw_clock(HDC dc,
                const OriginalResources& resources,
                const OriginalInfoContent& content) {
  // 11f8:033a precomposes BITMAP/321 into 1118:0cad's shared-surface source.
  // Each 1118:073d paint restores it into 1118:0c79's 31x31 local face at
  // (5,5), then applies the eight-pixel palette-frame offset used below.
  const RECT clip{5, 13, 36, 44};
  draw_clipped_dib(dc, resources.find("BITMAP", 321), clip.left, clip.top,
                   clip);

  // The two hands preserve 1208:0d35's relative-line primitive: get the
  // current position and LineTo(current.x+dx, current.y+dy).
  HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
  if (!pen) throw std::runtime_error("CreatePen failed for info clock");
  HGDIOBJ previous = SelectObject(dc, pen);
  constexpr int center_x = 20;
  constexpr int center_y = 28;
  MoveToEx(dc, center_x, center_y, nullptr);
  LineTo(dc, center_x + content.minute_hand.x,
         center_y + content.minute_hand.y);
  MoveToEx(dc, center_x, center_y, nullptr);
  LineTo(dc, center_x + content.hour_hand.x,
         center_y + content.hour_hand.y);
  SelectObject(dc, previous);
  DeleteObject(pen);
}

}  // namespace

inline void draw_original_info(HDC destination,
                               const OriginalResources& resources,
                               const OriginalTdtDocument* document,
                               std::string_view status) {
  if (!destination) return;
  SavedDc saved(destination);
  draw_original_dib(destination, resources.find("BITMAP", 320), 0,
                    kOriginalInfoClientTop);
  const auto content =
      build_original_info_content(resources, document, status);
  draw_date(destination, content);
  draw_rating(destination, resources, content.rating);
  draw_balance(destination, content.balance);
  draw_status(destination, content.status);
  draw_population(destination, content.population);
  draw_clock(destination, resources, content);
}

}  // namespace legacy_info
