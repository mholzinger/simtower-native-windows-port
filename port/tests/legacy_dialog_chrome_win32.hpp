#pragma once

// Frozen copy of the direct-GDI DTMP chrome painter in
// original_dtmp_runtime_win32.cpp before it became a wrapper around the
// portable paint_original_dtmp_chrome. Only the two window queries are
// parameters here (GetClientRect -> client, GetDlgItem/IsWindow ->
// has_child), so the parity test needs no window. Do not change the
// drawing code.

#include "original_dtmp.hpp"

#include <windows.h>

#include <cstddef>
#include <functional>
#include <stdexcept>

namespace legacy_dialog_chrome {

class SelectedPen {
 public:
  SelectedPen(HDC dc, COLORREF color) : dc_(dc) {
    pen_ = CreatePen(PS_SOLID, 1, color);
    if (!pen_) {
      throw std::runtime_error("Could not create original dialog pen");
    }
    previous_ = SelectObject(dc_, pen_);
  }
  ~SelectedPen() {
    if (previous_) {
      SelectObject(dc_, previous_);
    }
    if (pen_) {
      DeleteObject(pen_);
    }
  }

  SelectedPen(const SelectedPen&) = delete;
  SelectedPen& operator=(const SelectedPen&) = delete;

 private:
  HDC dc_ = nullptr;
  HPEN pen_ = nullptr;
  HGDIOBJ previous_ = nullptr;
};

inline void select_gray_and_draw_outer_top_left(HDC dc, const RECT& rect,
                                                BYTE gray) {
  SelectedPen pen(dc, RGB(gray, gray, gray));
  for (int inset = 0; inset < 3; ++inset) {
    MoveToEx(dc, rect.right - 2 - inset, rect.top + inset, nullptr);
    LineTo(dc, rect.left + inset, rect.top + inset);
    LineTo(dc, rect.left + inset, rect.bottom - 2 - inset);
  }
}

inline void select_gray_and_draw_outer_bottom_right(HDC dc, const RECT& rect,
                                                    BYTE gray) {
  SelectedPen pen(dc, RGB(gray, gray, gray));
  for (int inset = 0; inset < 3; ++inset) {
    MoveToEx(dc, rect.left + inset, rect.bottom - 1 - inset, nullptr);
    LineTo(dc, rect.right - 1 - inset, rect.bottom - 1 - inset);
    LineTo(dc, rect.right - 1 - inset, rect.top + inset);
  }
}

inline void select_gray_and_draw_inner_top_left(HDC dc, const RECT& rect,
                                                BYTE gray) {
  SelectedPen pen(dc, RGB(gray, gray, gray));
  for (int inset = 0; inset < 3; ++inset) {
    MoveToEx(dc, rect.right - 1 - inset, rect.top + inset, nullptr);
    LineTo(dc, rect.left + inset, rect.top + inset);
    LineTo(dc, rect.left + inset, rect.bottom - 1 - inset);
  }
}

inline void select_gray_and_draw_inner_bottom_right(HDC dc, const RECT& rect,
                                                    BYTE gray) {
  SelectedPen pen(dc, RGB(gray, gray, gray));
  for (int inset = 0; inset < 3; ++inset) {
    MoveToEx(dc, rect.left + inset + 1, rect.bottom - 1 - inset, nullptr);
    LineTo(dc, rect.right - 1 - inset, rect.bottom - 1 - inset);
    LineTo(dc, rect.right - 1 - inset, rect.top + inset + 1);
  }
}

inline void fill_gray(HDC dc, const RECT& rect, BYTE gray) {
  HBRUSH brush = CreateSolidBrush(RGB(gray, gray, gray));
  if (!brush) {
    throw std::runtime_error("Could not create original dialog brush");
  }
  FillRect(dc, &rect, brush);
  DeleteObject(brush);
}

inline void draw_original_dialog_frame(HDC dc, RECT rect) {
  select_gray_and_draw_outer_top_left(dc, rect, 179);
  select_gray_and_draw_outer_bottom_right(dc, rect, 140);
  InflateRect(&rect, -3, -3);
  select_gray_and_draw_inner_top_left(dc, rect, 102);
  select_gray_and_draw_inner_bottom_right(dc, rect, 230);
  InflateRect(&rect, -3, -3);
  fill_gray(dc, rect, 204);
}

inline void draw_original_placeholder_frame(HDC dc, RECT rect) {
  {
    SelectedPen pen(dc, RGB(89, 89, 89));
    for (int inset = 0; inset < 2; ++inset) {
      MoveToEx(dc, rect.right - 1 - inset, rect.top + inset, nullptr);
      LineTo(dc, rect.left + inset, rect.top + inset);
      LineTo(dc, rect.left + inset, rect.bottom - 2 - inset);
    }
  }
  {
    SelectedPen pen(dc, RGB(255, 255, 255));
    for (int inset = 0; inset < 2; ++inset) {
      MoveToEx(dc, rect.left + inset, rect.bottom - 1 - inset, nullptr);
      LineTo(dc, rect.right - 1 - inset, rect.bottom - 1 - inset);
      LineTo(dc, rect.right - 1 - inset, rect.top + inset + 1);
    }
  }
  InflateRect(&rect, -2, -2);
  fill_gray(dc, rect, 230);
}

inline RECT native_rect(const simtower::OriginalDtmpRect& source) {
  return {static_cast<LONG>(source.left), static_cast<LONG>(source.top),
          static_cast<LONG>(source.right), static_cast<LONG>(source.bottom)};
}

inline void paint_original_dialog_chrome(
    const RECT& client,
    const std::function<bool(int)>& has_child,
    HDC destination,
    const simtower::OriginalDtmp& dtmp) {
  draw_original_dialog_frame(destination, client);

  for (std::size_t index = 0; index < dtmp.rectangles.size(); ++index) {
    RECT rect = native_rect(dtmp.rectangles[index]);
    if (IsRectEmpty(&rect)) {
      continue;
    }
    if (!has_child(static_cast<int>(index + 1U))) {
      draw_original_placeholder_frame(destination, rect);
    }
  }
  SetBkColor(destination, RGB(255, 255, 255));
}

}  // namespace legacy_dialog_chrome
