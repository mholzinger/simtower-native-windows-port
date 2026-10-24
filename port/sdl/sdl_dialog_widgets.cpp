// The Win3.1-style painters of the SDL dialog controls (sdl_dialog.hpp):
// push buttons, text statics, single-line edits, list boxes (with their
// WS_VSCROLL bar), drop-down-list combo boxes and SB_CTL scroll bars, all
// through OriginalDrawContext. TEMP look and metrics (no Win3.1 reference
// yet): see sdl_dialog.hpp.

#include "sdl_dialog.hpp"
#include "sdl_style.hpp"

#include "original_dialog_paint.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace simtower::sdl {
namespace {

constexpr OriginalRgb kBlack = style::kBlack;
constexpr OriginalRgb kWhite = style::kWhite;
constexpr OriginalRgb kButtonFace = style::kButtonFace;
constexpr OriginalRgb kButtonShadow = style::kButtonShadow;

void dotted_rect(OriginalDrawContext& context, const OriginalRect& focus,
                 OriginalRgb rgb) {
  const auto dot = [&](int px, int py) {
    if (((px + py) & 1) == 0) context.fill_rect({px, py, px + 1, py + 1}, rgb);
  };
  for (int px = focus.left; px < focus.right; ++px) {
    dot(px, focus.top);
    dot(px, focus.bottom - 1);
  }
  for (int py = focus.top + 1; py < focus.bottom - 1; ++py) {
    dot(focus.left, py);
    dot(focus.right - 1, py);
  }
}

// One line of text in a colour, clipped.
void text_line(OriginalDrawContext& context, OriginalCanvasFont& font,
               int pixels, std::string_view text, int x, int y,
               const OriginalRect& clip, OriginalRgb ink) {
  OriginalSavedDrawState saved(context);
  context.set_bk_transparent();
  context.set_text_color(ink);
  OriginalSelectedFont selected(context, pixels);
  draw_dialog_text_line(context, font, pixels, text, x, y, clip);
}

// A static control: the WM_CTLCOLORSTATIC brush fill, then DrawText with
// DT_WORDBREAK in the static's own font (the dialog's template font).
void draw_static(OriginalDrawContext& context, OriginalCanvasFont& font,
                 const DialogControl& control, int pixel_height,
                 OriginalRgb background) {
  context.fill_rect(control.rect, background);
  OriginalSavedDrawState saved(context);
  context.set_bk_transparent();
  context.set_text_color(kBlack);
  OriginalSelectedFont selected(context, pixel_height);
  const auto text = (control.style & kOriginalStaticNoPrefix) != 0U
                        ? control.text
                        : strip_dialog_mnemonic(control.text);
  const int width = control.rect.right - control.rect.left;
  const int height = dialog_line_height(font, pixel_height);
  int y = control.rect.top;
  for (const auto& line : wrap_dialog_text(font, pixel_height, text, width)) {
    const int extent = font.extent(pixel_height, line);
    int x = control.rect.left;
    const auto align = control.style & kOriginalStaticTypeMask;
    if (align == kOriginalStaticCenter) x += (width - extent) / 2;
    if (align == kOriginalStaticRight) x = control.rect.right - extent;
    draw_dialog_text_line(context, font, pixel_height, line, x, y,
                          control.rect);
    y += height;
  }
}

// TEMP: a Win3.1-style push button (USER draws it; no resource holds it):
// a black frame without corner pixels (doubled for the default button), a
// white/gray bevel that flips when pressed, centered text with its mnemonic
// underlined and a dotted focus rectangle around it.
void draw_push_button(OriginalDrawContext& context, OriginalCanvasFont& font,
                      const DialogControl& control, int pixel_height,
                      bool pressed, bool focused, bool is_default) {
  // TEMP look; the label and focus rectangle come from push_button_label.
  const auto& r = control.rect;
  if (r.right - r.left < 6 || r.bottom - r.top < 6) return;
  context.fill_rect({r.left + 1, r.top, r.right - 1, r.top + 1}, kBlack);
  context.fill_rect({r.left + 1, r.bottom - 1, r.right - 1, r.bottom}, kBlack);
  context.fill_rect({r.left, r.top + 1, r.left + 1, r.bottom - 1}, kBlack);
  context.fill_rect({r.right - 1, r.top + 1, r.right, r.bottom - 1}, kBlack);
  auto inner = original_deflate_rect(r, 1);
  if (is_default) {
    draw_dialog_outline(context, inner, kBlack);
    inner = original_deflate_rect(inner, 1);
  }
  context.fill_rect(inner, kButtonFace);
  if (pressed) {
    context.fill_rect({inner.left, inner.top, inner.right, inner.top + 1},
                      kButtonShadow);
    context.fill_rect({inner.left, inner.top, inner.left + 1, inner.bottom},
                      kButtonShadow);
  } else {
    context.fill_rect({inner.left, inner.top, inner.right - 1, inner.top + 1},
                      kWhite);
    context.fill_rect(
        {inner.left, inner.top, inner.left + 1, inner.bottom - 1}, kWhite);
    context.fill_rect(
        {inner.left + 1, inner.bottom - 2, inner.right, inner.bottom},
        kButtonShadow);
    context.fill_rect(
        {inner.right - 2, inner.top + 1, inner.right, inner.bottom},
        kButtonShadow);
  }
  const auto label =
      push_button_label(font, control, pixel_height, pressed, is_default);
  const OriginalRgb ink = control.enabled ? kBlack : kButtonShadow;
  {
    OriginalSavedDrawState saved(context);
    context.set_bk_transparent();
    context.set_text_color(ink);
    OriginalSelectedFont selected(context, pixel_height);
    draw_dialog_text_line(context, font, pixel_height, label.text, label.x,
                          label.y, inner);
  }
  if (label.underline.right > label.underline.left) {
    // DrawText's '&' prefix underline, in the text colour.
    OriginalSavedDrawState saved(context);
    context.intersect_clip(inner);
    context.fill_rect(label.underline, ink);
  }
  if (!focused) return;
  dotted_rect(context, label.focus, kBlack);
}


// A raised Win3.1 face: black frame, white top-left and gray bottom-right
// bevel (one pixel each), face fill.
void raised_box(OriginalDrawContext& context, const OriginalRect& r) {
  if (r.right - r.left < 3 || r.bottom - r.top < 3) return;
  draw_dialog_outline(context, r, kBlack);
  const auto inner = original_deflate_rect(r, 1);
  context.fill_rect(inner, kButtonFace);
  context.fill_rect({inner.left, inner.top, inner.right - 1, inner.top + 1},
                    kWhite);
  context.fill_rect({inner.left, inner.top, inner.left + 1, inner.bottom - 1},
                    kWhite);
  context.fill_rect(
      {inner.left, inner.bottom - 1, inner.right, inner.bottom},
      kButtonShadow);
  context.fill_rect({inner.right - 1, inner.top, inner.right, inner.bottom},
                    kButtonShadow);
}

// A solid triangle pointing up or down, centered in r.
void triangle(OriginalDrawContext& context, const OriginalRect& r, bool up,
              OriginalRgb rgb) {
  const int rows = std::clamp((r.right - r.left) / 3, 2, 4);
  const int cx = (r.left + r.right) / 2;
  const int top = (r.top + r.bottom - rows) / 2;
  for (int row = 0; row < rows; ++row) {
    const int half = up ? row : rows - 1 - row;
    context.fill_rect({cx - half, top + row, cx + half + 1, top + row + 1},
                      rgb);
  }
}

// A vertical scroll bar: arrow buttons, the gray trough and the thumb (none
// for an empty range). tracking: the thumb's ghost position while dragged.
void draw_scroll_bar(OriginalDrawContext& context, const OriginalRect& bar,
                     int min, int max, int position, bool enabled) {
  const auto parts = dialog_scroll_parts(bar, min, max, position);
  context.fill_rect(bar, kButtonFace);
  draw_dialog_outline(context, bar, kBlack);
  raised_box(context, parts.up);
  raised_box(context, parts.down);
  const OriginalRgb ink = enabled && max > min ? kBlack : kButtonShadow;
  triangle(context, original_deflate_rect(parts.up, 2), true, ink);
  triangle(context, original_deflate_rect(parts.down, 2), false, ink);
  if (parts.thumb.bottom > parts.thumb.top) raised_box(context, parts.thumb);
}

void draw_edit(OriginalDrawContext& context, OriginalCanvasFont& font,
               const DialogWindow& dialog, const DialogControl& edit) {
  const int pixels = dialog.font_pixels();
  const auto& r = edit.rect;
  const bool border = (edit.style & kOriginalWindowStyleBorder) != 0U;
  if (border) draw_dialog_outline(context, r, kBlack);
  const auto inner = original_deflate_rect(r, border ? 1 : 0);
  // WM_CTLCOLOREDIT's brush (Rename: the 0xCC gray), else COLOR_WINDOW.
  context.fill_rect(inner,
                    dialog.descriptor().edit_background.value_or(kWhite));
  const auto layout = dialog_edit_layout(edit);
  const int line = dialog_line_height(font, pixels);
  const auto low = std::min(edit.anchor, edit.caret);
  const auto high = std::max(edit.anchor, edit.caret);
  const bool focused = dialog.focus() == edit.id;
  const OriginalRgb ink = edit.enabled ? kBlack : style::kGrayText;
  const std::string_view text(edit.text);
  int x = layout.text_x;
  const auto segment = [&](std::size_t from, std::size_t to, bool selected) {
    if (to <= from) return;
    const auto part = text.substr(from, to - from);
    const int extent = font.extent(pixels, part);
    if (selected) {
      context.fill_rect({x, layout.text_y, x + extent, layout.text_y + line},
                        style::kHighlight);
    }
    text_line(context, font, pixels, part, x, layout.text_y, inner,
              selected ? style::kHighlightText : ink);
    x += extent;
  };
  // The selection shows only while the edit has the focus.
  const bool show_selection = focused && low != high;
  segment(0U, show_selection ? low : text.size(), false);
  if (show_selection) {
    segment(low, high, true);
    segment(high, text.size(), false);
  }
  if (focused && dialog.caret_visible()) {
    const int caret_x =
        layout.text_x + font.extent(pixels, text.substr(0, edit.caret));
    OriginalSavedDrawState saved(context);
    context.intersect_clip(inner);
    context.fill_rect(
        {caret_x, layout.text_y, caret_x + 1, layout.text_y + line}, kBlack);
  }
}

// Rows of strings from top, the selection highlighted, the caret row's
// dotted focus rectangle while focused (drawn as the XOR of the known
// background, as DrawFocusRect would).
void draw_rows(OriginalDrawContext& context, OriginalCanvasFont& font,
               int pixels, const DialogControl& list,
               const OriginalRect& rows, OriginalRgb background, bool focused,
               bool show_caret) {
  const int line = dialog_line_height(font, pixels);
  context.fill_rect(rows, background);
  for (int index = list.top_index;
       index < static_cast<int>(list.items.size()); ++index) {
    const int top = rows.top + (index - list.top_index) * line;
    if (top >= rows.bottom) break;
    const OriginalRect row{rows.left, top, rows.right, top + line};
    const bool selected = index == list.selection;
    OriginalSavedDrawState saved(context);
    context.intersect_clip(rows);
    if (selected) context.fill_rect(row, style::kHighlight);
    text_line(context, font, pixels,
              list.items[static_cast<std::size_t>(index)],
              rows.left + kDialogListInsetX, top, rows,
              selected ? style::kHighlightText
                       : (list.enabled ? kBlack : style::kGrayText));
    if (focused && show_caret && index == list.caret_index) {
      dotted_rect(context, row,
                  (selected ? style::kHighlight : background) ^ 0xFFFFFFU);
    }
  }
}

void draw_list(OriginalDrawContext& context, OriginalCanvasFont& font,
               const DialogWindow& dialog, const DialogControl& list) {
  const auto& r = list.rect;
  const bool border = (list.style & kOriginalWindowStyleBorder) != 0U;
  if (border) draw_dialog_outline(context, r, kBlack);
  auto rows = dialog_list_rows_rect(list);
  const bool bar = dialog.list_has_scroll_bar(list);
  if (bar) rows.right = dialog.scroll_track(list).left;
  draw_rows(context, font, dialog.font_pixels(), list, rows,
            dialog.descriptor().list_background.value_or(kWhite),
            dialog.focus() == list.id, true);
  if (bar) {
    const int most = std::max(
        0, static_cast<int>(list.items.size()) - dialog.list_rows(list));
    draw_scroll_bar(context, dialog.scroll_track(list), 0, most,
                    list.top_index, list.enabled);
  }
}

void draw_combo(OriginalDrawContext& context, OriginalCanvasFont& font,
                const DialogWindow& dialog, const DialogControl& combo) {
  const int pixels = dialog.font_pixels();
  const int line = dialog_line_height(font, pixels);
  const auto field = dialog_combo_field_rect(combo, line);
  draw_dialog_outline(context, field, kBlack);
  const OriginalRect button{field.right - kDialogComboButtonWidth, field.top,
                            field.right, field.bottom};
  auto text_area = original_deflate_rect(field, 1);
  text_area.right = button.left;
  context.fill_rect(text_area, kWhite);
  if (combo.selection >= 0 &&
      combo.selection < static_cast<int>(combo.items.size())) {
    const bool focused = dialog.focus() == combo.id && combo.enabled;
    const auto& text = combo.items[static_cast<std::size_t>(combo.selection)];
    const int top =
        text_area.top + (text_area.bottom - text_area.top - line) / 2;
    if (focused) {
      context.fill_rect(original_deflate_rect(text_area, 1),
                        style::kHighlight);
    }
    text_line(context, font, pixels, text, text_area.left + kDialogListInsetX,
              top, text_area,
              !combo.enabled ? style::kGrayText
                             : (focused ? style::kHighlightText : kBlack));
  }
  raised_box(context, button);
  // TEMP (no Win3.1 reference): the drop-down arrow is the down triangle
  // raised two pixels with a one-pixel bar one blank row under it, as wide
  // as the triangle's top row.
  const OriginalRgb ink = combo.enabled ? kBlack : kButtonShadow;
  auto arrow = original_deflate_rect(button, 2);
  arrow.top -= 2;
  arrow.bottom -= 2;
  triangle(context, arrow, false, ink);
  const int rows = std::clamp((arrow.right - arrow.left) / 3, 2, 4);
  const int cx = (arrow.left + arrow.right) / 2;
  const int bar = (arrow.top + arrow.bottom - rows) / 2 + rows + 1;
  context.fill_rect({cx - (rows - 1), bar, cx + rows, bar + 1}, ink);
}

void draw_combo_list(OriginalDrawContext& context, OriginalCanvasFont& font,
                     const DialogWindow& dialog, const DialogControl& combo) {
  const int pixels = dialog.font_pixels();
  const auto list = dialog_combo_list_rect(combo,
                                           dialog_line_height(font, pixels));
  draw_dialog_outline(context, list, kBlack);
  auto shown = combo;
  shown.caret_index = combo.selection;
  draw_rows(context, font, pixels, shown, original_deflate_rect(list, 1),
            kWhite, false, false);
}

}  // namespace

void paint_dialog_controls(OriginalDrawContext& context,
                           OriginalCanvasFont& font,
                           const DialogWindow& dialog) {
  // The first template child is on top of the Z order: paint bottom up.
  const auto& controls = dialog.controls();
  const auto default_id = dialog.default_button();
  const int pixels = dialog.font_pixels();
  for (auto it = controls.rbegin(); it != controls.rend(); ++it) {
    if (!it->visible) continue;
    switch (it->kind) {
      case DialogControlKind::static_text:
        draw_static(context, font, *it, pixels,
                    dialog.descriptor().static_background);
        break;
      case DialogControlKind::push_button:
      case DialogControlKind::default_push_button:
        draw_push_button(context, font, *it, pixels,
                         dialog.captured() == it->id && dialog.pressed(),
                         dialog.focus() == it->id, default_id == it->id);
        break;
      case DialogControlKind::edit:
        draw_edit(context, font, dialog, *it);
        break;
      case DialogControlKind::list_box:
        draw_list(context, font, dialog, *it);
        break;
      case DialogControlKind::combo_box:
        draw_combo(context, font, dialog, *it);
        break;
      case DialogControlKind::scroll_bar: {
        const auto ghost = dialog.scroll_tracking(it->id);
        draw_scroll_bar(context, it->rect, it->scroll_min, it->scroll_max,
                        ghost.value_or(it->scroll_pos), it->enabled);
        break;
      }
      case DialogControlKind::other:
        break;
    }
  }
  // An open drop-down list is above everything in the dialog.
  for (const auto& control : controls) {
    if (control.visible && control.dropped) {
      draw_combo_list(context, font, dialog, control);
    }
  }
}

}  // namespace simtower::sdl
