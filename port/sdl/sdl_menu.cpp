#include "sdl_menu.hpp"
#include "sdl_style.hpp"

#include "original_canvas_font.hpp"
#include "original_draw_canvas.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace simtower::sdl {
namespace {

// TEMP colours: the shared Windows 3.1 table (sdl_style.hpp; the navy
// highlight and caption are an owner decision, 2026-10-23).
constexpr OriginalRgb kMenuRgb = style::kMenu;
constexpr OriginalRgb kMenuTextRgb = style::kMenuText;
constexpr OriginalRgb kGrayTextRgb = style::kGrayText;
constexpr OriginalRgb kHighlightRgb = style::kHighlight;
constexpr OriginalRgb kHighlightTextRgb = style::kHighlightText;
constexpr OriginalRgb kBorderRgb = style::kFrame;
constexpr OriginalRgb kCaptionRgb = style::kCaption;
constexpr OriginalRgb kCaptionTextRgb = style::kCaptionText;
constexpr OriginalRgb kButtonFaceRgb = style::kButtonFace;
constexpr OriginalRgb kButtonShadowRgb = style::kButtonShadow;

// TEMP metrics (USER's are System-font based: 16-pixel cells on VGA).
constexpr int kFontHeight = 13;
constexpr int kTitlePadding = 7;   // each side of a bar title
constexpr int kRowHeight = 17;     // a command or popup row
constexpr int kSeparatorHeight = 8;
constexpr int kCheckColumn = 16;   // left margin holding the check mark
constexpr int kArrowColumn = 16;   // right margin holding a cascade arrow
constexpr int kAcceleratorGap = 16;

// Menus measure and draw with their own font instance, so their heights
// never enter the shared 1208:0ba7 bank the Info painter's canvas uses.
OriginalCanvasFont& menu_font() {
  static OriginalCanvasFont font(original_canvas_bundled_font_data());
  return font;
}

int extent(std::string_view text) {
  return menu_font().extent(kFontHeight, text);
}

OriginalRect to_rect(const PanelRect& rect) {
  return {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
}

class Painter {
 public:
  Painter(OriginalWorldRaster& raster, const PanelRect& bounds)
      : context_(raster, menu_font(), 0, 0, to_rect(bounds)) {
    context_.push_font(kFontHeight);
    context_.set_bk_transparent();
    // TA_UPDATECP: text_out draws at move_to's point without the DrawText
    // prefix rule, which original_menu_label already applied.
    context_.set_text_align(kOriginalTaUpdateCp);
  }

  void fill(const PanelRect& rect, OriginalRgb rgb) {
    context_.fill_rect(to_rect(rect), rgb);
  }
  void outline(const PanelRect& rect, OriginalRgb rgb) {
    fill({rect.x, rect.y, rect.width, 1}, rgb);
    fill({rect.x, rect.y + rect.height - 1, rect.width, 1}, rgb);
    fill({rect.x, rect.y, 1, rect.height}, rgb);
    fill({rect.x + rect.width - 1, rect.y, 1, rect.height}, rgb);
  }
  // Text whose cell is vertically centred in [top, top + height).
  void text(std::string_view value, int x, int top, int height,
            OriginalRgb rgb, std::optional<std::size_t> mnemonic = {}) {
    const auto metrics = menu_font().metrics(kFontHeight);
    const int y = top + (height - (metrics.ascent + metrics.descent)) / 2;
    context_.set_text_color(rgb);
    context_.move_to(x, y);
    context_.text_out(value);
    if (mnemonic && *mnemonic < value.size()) {
      const int left = x + extent(value.substr(0, *mnemonic));
      const int width = extent(value.substr(*mnemonic, 1));
      fill({left, y + metrics.ascent + 1, std::max(1, width - 1), 1}, rgb);
    }
  }
  void pixel(int x, int y, OriginalRgb rgb) { fill({x, y, 1, 1}, rgb); }

 private:
  OriginalCanvasDrawContext context_;
};

// OBM_CHECK stand-in (TEMP, hand drawn 7x7).
constexpr std::array<const char*, 7> kCheckMark{
    "......#", ".....##", "#...##.", "##.##..", ".###...", "..#....",
    "......."};

void draw_check(Painter& painter, int x, int center_y, OriginalRgb rgb) {
  const int top = center_y - 3;
  for (int row = 0; row < 7; ++row) {
    for (int column = 0; column < 7; ++column) {
      if (kCheckMark[row][column] == '#') {
        painter.pixel(x + column, top + row, rgb);
      }
    }
  }
}

// The cascade arrow: a right-pointing triangle, 4 wide and 7 tall.
void draw_arrow(Painter& painter, int x, int center_y, OriginalRgb rgb) {
  for (int column = 0; column < 4; ++column) {
    painter.fill({x + column, center_y - 3 + column, 1, 7 - 2 * column}, rgb);
  }
}

MenuPopupLayout popup_layout(const OriginalMenuItems& items, int x, int y,
                             int window_width) {
  int text_width = 0;
  int accelerator_width = 0;
  for (const auto& item : items) {
    const auto label = original_menu_label(item.text);
    text_width = std::max(text_width, extent(label.text));
    if (!label.accelerator.empty()) {
      accelerator_width =
          std::max(accelerator_width, extent(label.accelerator));
    }
  }
  const int inner_width =
      kCheckColumn + text_width +
      (accelerator_width > 0 ? kAcceleratorGap + accelerator_width : 0) +
      kArrowColumn;
  MenuPopupLayout layout{};
  // A drop-down that would leave the window moves left, as USER keeps
  // popups on the screen.
  const int width = inner_width + 2;
  const int left = std::max(0, std::min(x, window_width - width));
  int row_y = y + 1;
  for (const auto& item : items) {
    const int height = item.separator() ? kSeparatorHeight : kRowHeight;
    layout.rows.push_back({left + 1, row_y, inner_width, height});
    row_y += height;
  }
  layout.outer = {left, y, width, row_y + 1 - y};
  layout.text_x = left + 1 + kCheckColumn;
  layout.accelerator_x = layout.text_x + text_width + kAcceleratorGap;
  return layout;
}

int row_at(const MenuPopupLayout& layout, int x, int y) {
  for (std::size_t index = 0; index < layout.rows.size(); ++index) {
    if (layout.rows[index].contains(x, y)) return static_cast<int>(index);
  }
  return -1;
}

void draw_popup(Painter& painter, const OriginalMenuItems& items,
                const MenuPopupLayout& layout, int highlight) {
  painter.fill(layout.outer, kMenuRgb);
  painter.outline(layout.outer, kBorderRgb);
  for (std::size_t index = 0; index < items.size(); ++index) {
    const auto& item = items[index];
    const auto& row = layout.rows[index];
    if (item.separator()) {
      painter.fill({row.x, row.y + row.height / 2 - 1, row.width, 1},
                   kBorderRgb);
      continue;
    }
    const bool lit = static_cast<int>(index) == highlight;
    if (lit) painter.fill(row, kHighlightRgb);
    // Win 3.1 keeps grayed text gray, also on the highlight.
    const OriginalRgb text = item.grayed() ? kGrayTextRgb
                             : lit         ? kHighlightTextRgb
                                           : kMenuTextRgb;
    const auto label = original_menu_label(item.text);
    painter.text(label.text, layout.text_x, row.y, row.height, text,
                 label.mnemonic);
    if (!label.accelerator.empty()) {
      painter.text(label.accelerator, layout.accelerator_x, row.y, row.height,
                   text);
    }
    const int center_y = row.y + row.height / 2;
    if (item.checked()) draw_check(painter, row.x + 4, center_y, text);
    if (item.popup()) {
      draw_arrow(painter, row.x + row.width - kArrowColumn + 6, center_y,
                 text);
    }
  }
}

}  // namespace

int next_menu_row(const OriginalMenuItems& items, int from, int step) {
  const int count = static_cast<int>(items.size());
  if (count == 0) return -1;
  int index = from;
  for (int tries = 0; tries < count; ++tries) {
    index = ((index % count) + count) % count;
    if (!items[static_cast<std::size_t>(index)].separator()) return index;
    index += step;
  }
  return -1;
}

MenuBarLayout menu_bar_layout(const OriginalMenuItems& menu,
                              const WindowLayout& layout) {
  constexpr auto& metrics = kMainNonclientMetrics;
  const auto& outer = layout.main_outer;
  MenuBarLayout bar{};
  bar.strip = {outer.x + metrics.frame_width,
               outer.y + metrics.frame_height + metrics.caption_height,
               outer.width - 2 * metrics.frame_width, metrics.menu_height};
  int x = bar.strip.x;
  for (const auto& item : menu) {
    const int width =
        extent(original_menu_label(item.text).text) + 2 * kTitlePadding;
    // The bottom row of the strip is the line under the bar.
    bar.titles.push_back({x, bar.strip.y, width, bar.strip.height - 1});
    x += width;
  }
  return bar;
}

int menu_title_at(const MenuBarLayout& bar, int x, int y) {
  for (std::size_t index = 0; index < bar.titles.size(); ++index) {
    if (bar.titles[index].contains(x, y)) return static_cast<int>(index);
  }
  return -1;
}

MenuOpenLayout menu_open_layout(const OriginalMenuItems& menu,
                                const WindowLayout& layout,
                                const MenuView& view) {
  MenuOpenLayout open{};
  const auto bar = menu_bar_layout(menu, layout);
  if (view.bar < 0 || view.title_only) return open;
  open.popup_items =
      original_sub_menu(menu, static_cast<std::size_t>(view.bar));
  if (!open.popup_items) return open;
  const auto& title = bar.titles[static_cast<std::size_t>(view.bar)];
  open.popup = popup_layout(*open.popup_items, title.x,
                            bar.strip.y + bar.strip.height,
                            layout.window_width);
  if (!view.cascade_open || view.item < 0) return open;
  open.cascade_items = original_sub_menu(*open.popup_items,
                                         static_cast<std::size_t>(view.item));
  if (!open.cascade_items) return open;
  const auto& row = open.popup->rows[static_cast<std::size_t>(view.item)];
  open.cascade = popup_layout(*open.cascade_items,
                              open.popup->outer.x + open.popup->outer.width - 3,
                              row.y - 1, layout.window_width);
  return open;
}

void draw_main_caption_and_menu_bar(OriginalWorldRaster& window,
                                    const OriginalMenuItems& menu,
                                    const WindowLayout& layout,
                                    const MenuView& view,
                                    std::string_view title) {
  constexpr auto& metrics = kMainNonclientMetrics;
  const auto& outer = layout.main_outer;
  Painter painter(window, {0, 0, window.width, window.height});
  // TEMP caption: navy with the centred title, a system-menu box and the
  // minimize/maximize boxes in button face.
  const PanelRect caption{outer.x + metrics.frame_width,
                          outer.y + metrics.frame_height,
                          outer.width - 2 * metrics.frame_width,
                          metrics.caption_height};
  painter.fill(caption, kCaptionRgb);
  const int button = metrics.caption_height;
  const auto draw_button = [&](int x) {
    const PanelRect box{x, caption.y, button, caption.height};
    painter.fill(box, kButtonFaceRgb);
    painter.outline(box, kBorderRgb);
    painter.fill({box.x + 1, box.y + box.height - 2, box.width - 2, 1},
                 kButtonShadowRgb);
  };
  draw_button(caption.x);
  draw_button(caption.x + caption.width - button);
  draw_button(caption.x + caption.width - 2 * button);
  const int title_width = extent(title);
  painter.text(title,
               caption.x + (caption.width - title_width) / 2, caption.y,
               caption.height, kCaptionTextRgb);

  const auto bar = menu_bar_layout(menu, layout);
  painter.fill(bar.strip, kMenuRgb);
  painter.fill({bar.strip.x, bar.strip.y + bar.strip.height - 1,
                bar.strip.width, 1},
               kBorderRgb);
  for (std::size_t index = 0; index < menu.size(); ++index) {
    const auto& cell = bar.titles[index];
    const bool lit = static_cast<int>(index) == view.bar;
    if (lit) painter.fill(cell, kHighlightRgb);
    const auto label = original_menu_label(menu[index].text);
    const OriginalRgb text = menu[index].grayed() ? kGrayTextRgb
                             : lit                ? kHighlightTextRgb
                                                  : kMenuTextRgb;
    painter.text(label.text, cell.x + kTitlePadding, cell.y, cell.height, text,
                 label.mnemonic);
  }
}

void draw_menu_popups(OriginalWorldRaster& window,
                      const OriginalMenuItems& menu,
                      const WindowLayout& layout,
                      const MenuView& view) {
  const auto open = menu_open_layout(menu, layout, view);
  if (!open.popup) return;
  Painter painter(window, {0, 0, window.width, window.height});
  draw_popup(painter, *open.popup_items, *open.popup, view.item);
  if (open.cascade) {
    draw_popup(painter, *open.cascade_items, *open.cascade,
               view.cascade_item);
  }
}

MenuTracker::MenuTracker(const OriginalMenuItems& menu,
                         const WindowLayout& layout)
    : menu_(menu),
      layout_(layout),
      bar_layout_(menu_bar_layout(menu, layout)) {}

const OriginalMenuItems* MenuTracker::popup_items() const {
  if (view_.bar < 0 || view_.title_only) return nullptr;
  return original_sub_menu(menu_, static_cast<std::size_t>(view_.bar));
}

const OriginalMenuItems* MenuTracker::cascade_items() const {
  const auto* popup = popup_items();
  if (!popup || !view_.cascade_open || view_.item < 0) return nullptr;
  return original_sub_menu(*popup, static_cast<std::size_t>(view_.item));
}

MenuStep MenuTracker::open(int bar, bool keyboard) {
  view_ = {};
  view_.bar = bar;
  view_.title_only = keyboard;
  button_down_ = !keyboard;
  return {true, false, {}};
}

MenuStep MenuTracker::select_bar(int bar, bool open_popup) {
  const int count = static_cast<int>(menu_.size());
  if (count == 0) return {};
  view_ = {};
  view_.bar = ((bar % count) + count) % count;
  view_.title_only = !open_popup;
  if (open_popup) {
    if (const auto* popup = popup_items()) {
      view_.item = next_menu_row(*popup, 0, 1);
    }
  }
  return {true, false, {}};
}

MenuStep MenuTracker::choose(const OriginalMenuItems& items, int index,
                             bool cascade) {
  if (index < 0 || index >= static_cast<int>(items.size())) return {};
  const auto& item = items[static_cast<std::size_t>(index)];
  if (item.separator() || item.grayed()) return {};
  if (item.popup()) {
    if (cascade) return {};
    view_.item = index;
    view_.cascade_open = true;
    view_.cascade_item = next_menu_row(item.children, 0, 1);
    return {true, false, {}};
  }
  return {true, true, item.id};
}

MenuStep MenuTracker::pointer(const InputEvent& event) {
  const bool down =
      event.kind == InputKind::down || event.kind == InputKind::double_click;
  if (event.kind == InputKind::right_down ||
      event.kind == InputKind::right_up) {
    return {};
  }
  if (down) button_down_ = true;
  if (event.kind == InputKind::up) button_down_ = false;

  const auto open = menu_open_layout(menu_, layout_, view_);
  const int title = menu_title_at(bar_layout_, event.x, event.y);
  const int cascade_row =
      open.cascade ? row_at(*open.cascade, event.x, event.y) : -1;
  const int popup_row =
      open.popup ? row_at(*open.popup, event.x, event.y) : -1;
  const bool in_cascade =
      open.cascade && open.cascade->outer.contains(event.x, event.y);
  const bool in_popup =
      open.popup && open.popup->outer.contains(event.x, event.y);
  const MenuView before = view_;

  if (title >= 0) {
    if (down && title == view_.bar && !view_.title_only) {
      // A press on the open title closes the menu.
      return {true, true, {}};
    }
    if (title != view_.bar || view_.title_only) {
      if (event.kind == InputKind::move && !button_down_ && view_.title_only) {
        return {};
      }
      view_ = {};
      view_.bar = title;
    }
    return {view_ != before, false, {}};
  }
  if (in_cascade) {
    view_.cascade_item = cascade_row;
    if (event.kind == InputKind::up && cascade_row >= 0) {
      auto step = choose(*open.cascade_items, cascade_row, true);
      step.redraw = step.redraw || view_ != before;
      return step;
    }
    return {view_ != before, false, {}};
  }
  if (in_popup) {
    if (popup_row >= 0) {
      const auto& item =
          (*open.popup_items)[static_cast<std::size_t>(popup_row)];
      view_.item = item.separator() ? -1 : popup_row;
      view_.cascade_open = item.popup();
      if (popup_row != before.item) view_.cascade_item = -1;
      if (event.kind == InputKind::up && !item.popup()) {
        auto step = choose(*open.popup_items, popup_row, false);
        step.redraw = step.redraw || view_ != before;
        return step;
      }
    }
    return {view_ != before, false, {}};
  }
  // Outside every menu part: a press closes the menu (and is consumed); a
  // release closes it after a drag away from the title; a move drops the
  // highlight unless a cascade is open.
  if (down) return {true, true, {}};
  if (event.kind == InputKind::up) {
    if (view_.item < 0 && !view_.cascade_open && !view_.title_only &&
        before.bar >= 0) {
      return {true, true, {}};
    }
    return {};
  }
  if (!view_.cascade_open) view_.item = -1;
  return {view_ != before, false, {}};
}

MenuStep MenuTracker::key(MenuKey key, char character) {
  const auto* popup = popup_items();
  const auto* cascade = cascade_items();
  switch (key) {
    case MenuKey::toggle:
      return {true, true, {}};
    case MenuKey::escape:
      if (view_.cascade_open) {
        view_.cascade_open = false;
        view_.cascade_item = -1;
        return {true, false, {}};
      }
      if (popup) {
        const int bar = view_.bar;
        view_ = {};
        view_.bar = bar;
        view_.title_only = true;
        return {true, false, {}};
      }
      return {true, true, {}};
    case MenuKey::left:
      if (view_.cascade_open) {
        view_.cascade_open = false;
        view_.cascade_item = -1;
        return {true, false, {}};
      }
      return select_bar(view_.bar - 1, popup != nullptr);
    case MenuKey::right:
      if (popup && !view_.cascade_open && view_.item >= 0 &&
          (*popup)[static_cast<std::size_t>(view_.item)].popup()) {
        return choose(*popup, view_.item, false);
      }
      return select_bar(view_.bar + 1, popup != nullptr || cascade != nullptr);
    case MenuKey::up:
    case MenuKey::down: {
      const int step = key == MenuKey::down ? 1 : -1;
      if (!popup) return select_bar(view_.bar, true);
      if (cascade) {
        view_.cascade_item = next_menu_row(
            *cascade, view_.cascade_item < 0 ? 0 : view_.cascade_item + step,
            step);
      } else {
        view_.item = next_menu_row(
            *popup, view_.item < 0 ? 0 : view_.item + step, step);
        view_.cascade_open = false;
        view_.cascade_item = -1;
      }
      return {true, false, {}};
    }
    case MenuKey::enter:
      if (!popup) return select_bar(view_.bar, true);
      if (cascade) return choose(*cascade, view_.cascade_item, true);
      return choose(*popup, view_.item, false);
    case MenuKey::character: {
      if (character >= 'A' && character <= 'Z') {
        character = static_cast<char>(character - 'A' + 'a');
      }
      const OriginalMenuItems& items =
          cascade ? *cascade : popup ? *popup : menu_;
      for (std::size_t index = 0; index < items.size(); ++index) {
        if (original_menu_mnemonic(items[index].text) != character) continue;
        if (!popup) return select_bar(static_cast<int>(index), true);
        return choose(items, static_cast<int>(index), cascade != nullptr);
      }
      return {};
    }
  }
  return {};
}

}  // namespace simtower::sdl
