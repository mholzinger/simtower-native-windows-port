// The SDL dialog manager's controls (sdl/sdl_dialog.hpp; no SDL in it):
// single-line edit, list box, drop-down-list combo box and SB_CTL scroll
// bar behavior (the Win32 messages and notifications the game's dialogs
// use), the IsDialogMessage group rules for the arrow keys, the dialog
// WM_PAINT event, and their Win3.1-style painting through the canvas.
// Synthetic controls only: no game assets, so this runs everywhere (also
// under Wine).

#include "sdl_dialog.hpp"
#include "sdl_focus_rect.hpp"
#include "sdl_style.hpp"

#include "original_canvas_font.hpp"
#include "original_dialog.hpp"
#include "original_dialog_model.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using simtower::OriginalDialogSignal;
using simtower::OriginalRect;
using simtower::sdl::DialogControl;
using simtower::sdl::DialogControlKind;
using simtower::sdl::DialogDescriptor;
using simtower::sdl::DialogKey;
using simtower::sdl::DialogPointer;
using simtower::sdl::DialogStep;
using simtower::sdl::DialogWindow;
using Kind = OriginalDialogSignal::Kind;

constexpr int kPixels = 13;
constexpr std::uint32_t kChild = simtower::kOriginalWindowStyleChild |
                                 simtower::kOriginalWindowStyleVisible;

OriginalDialogSignal command(std::uint16_t id, std::uint16_t code) {
  return {Kind::command, id, code};
}

OriginalDialogSignal scroll(std::uint16_t id, std::uint16_t code,
                            int value = 0) {
  OriginalDialogSignal signal{Kind::scroll, id, code};
  signal.value = value;
  return signal;
}

DialogControl make(std::uint16_t id, DialogControlKind kind,
                   std::uint32_t style, OriginalRect rect,
                   std::string text = {}) {
  DialogControl control{};
  control.id = id;
  control.kind = kind;
  control.style = kChild | style;
  control.rect = rect;
  control.text = std::move(text);
  control.visible = true;
  control.enabled = true;
  control.tab_stop = (style & simtower::kOriginalWindowStyleTabStop) != 0U;
  return control;
}

// A template whose items mirror the controls (for Enter's default id).
simtower::OriginalDialog template_of(const std::vector<DialogControl>& all) {
  simtower::OriginalDialog dialog{};
  for (const auto& control : all) {
    simtower::OriginalDialogItem item{};
    item.id = control.id;
    item.style = control.style;
    item.window_class.kind = simtower::OriginalDialogValue::Kind::ordinal;
    switch (control.kind) {
      case DialogControlKind::push_button:
      case DialogControlKind::default_push_button:
        item.window_class.ordinal = simtower::kOriginalDialogClassButton;
        break;
      case DialogControlKind::edit:
        item.window_class.ordinal = simtower::kOriginalDialogClassEdit;
        break;
      case DialogControlKind::list_box:
        item.window_class.ordinal = simtower::kOriginalDialogClassListBox;
        break;
      case DialogControlKind::combo_box:
        item.window_class.ordinal = simtower::kOriginalDialogClassComboBox;
        break;
      case DialogControlKind::scroll_bar:
        item.window_class.ordinal = simtower::kOriginalDialogClassScrollBar;
        break;
      default:
        item.window_class.ordinal = simtower::kOriginalDialogClassStatic;
        break;
    }
    dialog.items.push_back(item);
  }
  return dialog;
}

DialogWindow make_dialog(std::vector<DialogControl> controls,
                        std::uint16_t focus) {
  DialogDescriptor descriptor{};
  descriptor.label = "widgets";
  descriptor.font_pixels = kPixels;
  descriptor.static_background = 0xCCCCCCU;
  descriptor.edit_background = 0xCCCCCCU;
  auto dialog_template = template_of(controls);
  DialogWindow dialog(descriptor, dialog_template, std::move(controls), 400,
                     420, focus);
  dialog.place(400, 420);  // client at the window origin
  return dialog;
}

DialogStep press(DialogWindow& dialog, int x, int y, bool shift = false) {
  return dialog.pointer({DialogPointer::Kind::down, x, y, shift});
}

DialogStep release(DialogWindow& dialog, int x, int y) {
  return dialog.pointer({DialogPointer::Kind::up, x, y});
}

simtower::OriginalWorldRaster render(const DialogWindow& dialog) {
  simtower::OriginalWorldRaster raster{};
  raster.width = 400;
  raster.height = 420;
  raster.pixels.assign(static_cast<std::size_t>(raster.width) * raster.height,
                       0x00123456U);
  simtower::OriginalResources resources{};
  simtower::sdl::draw_dialog(raster, dialog, resources);
  return raster;
}

std::uint32_t at(const simtower::OriginalWorldRaster& raster, int x, int y) {
  return raster.pixels[static_cast<std::size_t>(y) * raster.width + x];
}

int count(const simtower::OriginalWorldRaster& raster,
          const OriginalRect& rect, std::uint32_t rgb) {
  int total = 0;
  for (int y = rect.top; y < rect.bottom; ++y) {
    for (int x = rect.left; x < rect.right; ++x) {
      if (at(raster, x, y) == rgb) ++total;
    }
  }
  return total;
}

void type(DialogWindow& dialog, const std::string& text) {
  for (const char character : text) (void)dialog.character(character);
}

// --- Edit ---------------------------------------------------------------

void edit_tests() {
  const OriginalRect edit_rect{79, 40, 204, 69};  // Rename's DTMP rect
  auto ok = make(1, DialogControlKind::push_button,
                 simtower::kOriginalWindowStyleTabStop, {200, 90, 270, 116},
                 "OK");
  auto cancel = make(2, DialogControlKind::default_push_button,
                     simtower::kOriginalWindowStyleTabStop |
                         simtower::kOriginalButtonDefPushButton,
                     {101, 90, 181, 116}, "Cancel");
  auto edit = make(4, DialogControlKind::edit,
                   simtower::kOriginalWindowStyleBorder |
                       simtower::kOriginalWindowStyleTabStop,
                   edit_rect);
  auto dialog = make_dialog({ok, cancel, edit}, 1U);
  assert(dialog.take_paint() && !dialog.take_paint());

  // SetDlgItemText: caret at the start, nothing selected.
  dialog.set_item_text(4, "Bob");
  assert(dialog.take_invalidated());
  assert(dialog.item_text(4) == "Bob");
  assert(dialog.control(4)->caret == 0U && dialog.control(4)->anchor == 0U);
  // SetFocus (the Rename WM_PAINT tail) selects nothing.
  dialog.focus_item(4);
  assert(dialog.focus() == 4U && dialog.control(4)->anchor == 0U &&
         dialog.control(4)->caret == 0U);
  // Typing inserts at the caret and sends EN_CHANGE each time.
  assert(dialog.character('X').signal ==
         command(4, simtower::kOriginalEditChange));
  assert(dialog.item_text(4) == "XBob" && dialog.control(4)->caret == 1U);
  // Control characters are not text.
  assert(!dialog.character('\r').signal && dialog.item_text(4) == "XBob");
  // End, Shift+Home selects all; a character replaces the selection.
  (void)dialog.key(DialogKey::end);
  assert(dialog.control(4)->caret == 4U && dialog.control(4)->anchor == 4U);
  (void)dialog.key(DialogKey::home, '\0', true);
  assert(dialog.control(4)->caret == 0U && dialog.control(4)->anchor == 4U);
  assert(dialog.character('Z').signal ==
         command(4, simtower::kOriginalEditChange));
  assert(dialog.item_text(4) == "Z");
  // Backspace and Delete, with and without a selection.
  type(dialog, "ack");
  assert(dialog.item_text(4) == "Zack");
  assert(dialog.key(DialogKey::backspace).signal ==
         command(4, simtower::kOriginalEditChange));
  assert(dialog.item_text(4) == "Zac");
  (void)dialog.key(DialogKey::home);
  assert(dialog.key(DialogKey::delete_key).signal ==
         command(4, simtower::kOriginalEditChange));
  assert(dialog.item_text(4) == "ac");
  assert(!dialog.key(DialogKey::backspace).signal);  // caret at 0
  (void)dialog.key(DialogKey::right, '\0', true);
  (void)dialog.key(DialogKey::right, '\0', true);
  assert(dialog.control(4)->anchor == 0U && dialog.control(4)->caret == 2U);
  (void)dialog.key(DialogKey::delete_key);
  assert(dialog.item_text(4).empty());
  // The edit keeps the arrows, Space and letters (no mnemonic, no group
  // move); Up and Down do nothing in a single-line edit.
  assert(!dialog.key(DialogKey::up).signal && dialog.focus() == 4U);
  assert(!dialog.key(DialogKey::other, 'o').signal && dialog.focus() == 4U);
  assert(!dialog.key(DialogKey::space).signal && dialog.focus() == 4U);
  (void)dialog.character(' ');
  assert(dialog.item_text(4) == " ");
  (void)dialog.key(DialogKey::backspace);
  // Enter while the edit has the focus is the template default (Cancel).
  assert(dialog.key(DialogKey::enter).signal ==
         command(2, simtower::kOriginalNotifyClicked));

  // No ES_AUTOHSCROLL: typing stops at the formatting width with
  // EN_MAXTEXT, and more than 15 characters fit.
  auto& font = simtower::OriginalCanvasFont::bundled();
  const auto layout = simtower::sdl::dialog_edit_layout(*dialog.control(4));
  assert(layout.text_x ==
         edit_rect.left + 1 + simtower::sdl::kDialogEditInsetX);
  std::optional<OriginalDialogSignal> last{};
  for (int i = 0; i < 60; ++i) last = dialog.character('i').signal;
  assert(last == command(4, simtower::kOriginalEditMaxText));
  const auto full = dialog.item_text(4);
  assert(full.size() > 15U);
  assert(font.extent(kPixels, full) <= layout.width);
  assert(font.extent(kPixels, full + "i") > layout.width);
  // GetDlgItemText is capped at 0xfe - 1 bytes.
  dialog.set_item_text(4, std::string(300, 'w'));
  assert(dialog.item_text(4).size() == 0xfdU);

  // Tab into the edit selects all of it (DLGC_HASSETSEL).
  dialog.set_item_text(4, "Name");
  dialog.focus_item(1);
  (void)dialog.key(DialogKey::shift_tab);  // 1 -> 4 (template order wraps)
  assert(dialog.focus() == 4U);
  assert(dialog.control(4)->anchor == 0U && dialog.control(4)->caret == 4U);

  // Mouse: a press puts the caret at the nearest boundary, a drag selects,
  // Shift+press extends.
  const int y = (edit_rect.top + edit_rect.bottom) / 2;
  const int after_n = layout.text_x + font.extent(kPixels, "N");
  (void)press(dialog, after_n, y);
  assert(dialog.control(4)->caret == 1U && dialog.control(4)->anchor == 1U);
  (void)dialog.pointer({DialogPointer::Kind::move,
                        layout.text_x + font.extent(kPixels, "Nam"), y});
  assert(dialog.control(4)->anchor == 1U && dialog.control(4)->caret == 3U);
  assert(!release(dialog, layout.text_x, y).signal);
  (void)press(dialog, edit_rect.right - 3, y, true);
  assert(dialog.control(4)->anchor == 1U && dialog.control(4)->caret == 4U);
  (void)release(dialog, 0, 0);

  // Painting: the 0xCC WM_CTLCOLOREDIT brush, a one-pixel black border,
  // the navy selection with white text, and the blinking caret.
  dialog.set_clock(0U);
  (void)dialog.key(DialogKey::home);
  (void)dialog.key(DialogKey::end, '\0', true);  // all selected
  auto image = render(dialog);
  assert(at(image, edit_rect.left, edit_rect.top) == 0U);
  assert(at(image, edit_rect.right - 2, edit_rect.bottom - 2) == 0xCCCCCCU);
  const OriginalRect text_box{layout.text_x, layout.text_y,
                              layout.text_x + font.extent(kPixels, "Name"),
                              layout.text_y + 15};
  assert(count(image, text_box, simtower::sdl::style::kHighlight) > 20);
  assert(count(image, text_box, simtower::sdl::style::kHighlightText) > 10);
  (void)dialog.key(DialogKey::end);  // collapse: caret after "Name"
  const int caret_x = layout.text_x + font.extent(kPixels, "Name");
  dialog.set_clock(10U);
  image = render(dialog);
  assert(count(image, {caret_x, layout.text_y, caret_x + 1, layout.text_y + 15},
               0U) == 15);
  assert(count(image, text_box, simtower::sdl::style::kHighlight) == 0);
  dialog.set_clock(simtower::sdl::kDialogCaretBlinkMs + 10U);
  assert(!dialog.caret_visible());
  image = render(dialog);
  assert(count(image, {caret_x, layout.text_y, caret_x + 1, layout.text_y + 15},
               0U) < 15);
  // A change shows the caret at once.
  type(dialog, "s");
  assert(dialog.caret_visible());
}

// --- List box -----------------------------------------------------------

void list_tests() {
  const OriginalRect rect{10, 10, 290, 170};  // Find's DTMP rect
  auto ok = make(1, DialogControlKind::default_push_button,
                 simtower::kOriginalWindowStyleTabStop |
                     simtower::kOriginalButtonDefPushButton,
                 {222, 181, 293, 204}, "OK");
  auto list = make(5, DialogControlKind::list_box,
                   simtower::kOriginalWindowStyleBorder |
                       simtower::kOriginalWindowStyleVScroll |
                       simtower::kOriginalWindowStyleTabStop |
                       simtower::kOriginalListBoxNotify,
                   rect);
  auto dialog = make_dialog({ok, list}, 1U);
  const char* names[] = {"Zed", "Amy", "Bob", "Ann", "Cid", "Dee", "Eve",
                         "Fay", "Gus", "Hal", "Ivy", "Jon", "Kim", "Lou"};
  dialog.list_reset(5);
  for (const char* name : names) dialog.list_add(5, name);
  const auto& control = *dialog.control(5);
  // Unsorted: insertion order. Nothing selected.
  assert(control.items.size() == 14U && control.items[0] == "Zed");
  assert(dialog.list_selection(5) == -1);
  // 158 pixels inside the border: 10 full rows of 15 (a partial 11th).
  assert(dialog.list_rows(control) == 10);
  assert(dialog.list_has_scroll_bar(control));
  // A press selects the row (LBN_SELCHANGE) and focuses the list.
  auto point = dialog.list_item_point(5, 2);
  assert(point);
  dialog.set_clock(1000U);
  assert(press(dialog, point->first, point->second).signal ==
         command(5, simtower::kOriginalListBoxSelChange));
  assert(dialog.focus() == 5U && dialog.list_selection(5) == 2);
  assert(!release(dialog, point->first, point->second).signal);
  // A second press within 500 ms and 4 pixels is LBN_DBLCLK.
  dialog.set_clock(1400U);
  assert(press(dialog, point->first + 2, point->second).signal ==
         command(5, simtower::kOriginalListBoxDblClk));
  (void)release(dialog, point->first, point->second);
  // Too late: a plain press again (same row: no change, no notification).
  dialog.set_clock(3000U);
  assert(!press(dialog, point->first, point->second).signal);
  (void)release(dialog, point->first, point->second);
  dialog.set_clock(3600U);
  assert(!press(dialog, point->first, point->second).signal);
  (void)release(dialog, point->first, point->second);
  // A drag tracks the selection.
  dialog.set_clock(9000U);
  const auto row4 = dialog.list_item_point(5, 4);
  (void)press(dialog, row4->first, row4->second);
  const auto row6 = dialog.list_item_point(5, 6);
  assert(dialog.pointer({DialogPointer::Kind::move, row6->first,
                         row6->second})
             .signal == command(5, simtower::kOriginalListBoxSelChange));
  assert(dialog.list_selection(5) == 6);
  (void)release(dialog, row6->first, row6->second);
  // Keys: Up / Down by one, Home / End, PgDn by a page, letters search
  // after the caret (case-insensitive, wrapping); each scrolls into view.
  assert(dialog.key(DialogKey::down).signal ==
         command(5, simtower::kOriginalListBoxSelChange));
  assert(dialog.list_selection(5) == 7);
  (void)dialog.key(DialogKey::up);
  assert(dialog.list_selection(5) == 6);
  (void)dialog.key(DialogKey::end);
  assert(dialog.list_selection(5) == 13 && dialog.control(5)->top_index == 4);
  (void)dialog.key(DialogKey::home);
  assert(dialog.list_selection(5) == 0 && dialog.control(5)->top_index == 0);
  (void)dialog.key(DialogKey::page_down);
  assert(dialog.list_selection(5) == 9);
  (void)dialog.character('a');
  assert(dialog.list_selection(5) == 1);  // "Amy"
  (void)dialog.character('A');
  assert(dialog.list_selection(5) == 3);  // "Ann"
  (void)dialog.character('a');
  assert(dialog.list_selection(5) == 1);  // wraps
  // The list keeps letters (no mnemonic) and the arrows.
  assert(!dialog.key(DialogKey::other, 'o').signal && dialog.focus() == 5U);
  // Enter is the default push button.
  assert(dialog.key(DialogKey::enter).signal ==
         command(1, simtower::kOriginalNotifyClicked));
  // LB_DELETESTRING of the selection leaves none; an earlier row shifts it.
  dialog.list_set_selection(5, 3);
  dialog.list_delete(5, 1);
  assert(dialog.list_selection(5) == 2 && dialog.control(5)->items[1] == "Bob");
  dialog.list_delete(5, 2);
  assert(dialog.list_selection(5) == -1);
  // The scroll bar: the down arrow scrolls one row, the trough a page.
  const auto bar = dialog.scroll_track(*dialog.control(5));
  assert(bar.right == rect.right &&
         bar.right - bar.left == simtower::sdl::kDialogScrollBarWidth);
  (void)dialog.key(DialogKey::home);
  (void)press(dialog, bar.left + 5, bar.bottom - 5);
  (void)release(dialog, bar.left + 5, bar.bottom - 5);
  assert(dialog.control(5)->top_index == 1);
  (void)press(dialog, bar.left + 5, bar.top + 5);
  (void)release(dialog, bar.left + 5, bar.top + 5);
  assert(dialog.control(5)->top_index == 0);
  // Dragging the thumb to the bottom shows the last rows.
  const int thumb_top = bar.top + simtower::sdl::kDialogScrollBarWidth;
  (void)press(dialog, bar.left + 5, thumb_top + 3);
  (void)dialog.pointer({DialogPointer::Kind::move, bar.left + 5, 400});
  (void)release(dialog, bar.left + 5, 400);
  assert(dialog.control(5)->top_index ==
         static_cast<int>(dialog.control(5)->items.size()) - 10);

  // Painting: white WM_CTLCOLORLISTBOX background, the navy selected row
  // with white text, the gray scroll bar.
  dialog.list_set_selection(5, dialog.control(5)->top_index + 1);
  const auto selected = dialog.list_item_point(5, dialog.list_selection(5));
  const auto image = render(dialog);
  assert(at(image, rect.left, rect.top) == 0U);
  assert(at(image, rect.left + 1, rect.bottom - 2) == 0xFFFFFFU);
  assert(at(image, rect.left + 3, selected->second) ==
         simtower::sdl::style::kHighlight);
  assert(count(image, {rect.left + 1, selected->second - 6, bar.left,
                       selected->second + 6},
               0xFFFFFFU) > 10);
  assert(count(image, {bar.left + 2, bar.top + 20, bar.right - 2,
                       bar.bottom - 20},
               simtower::sdl::style::kButtonFace) > 50);

  // Without LBS_NOTIFY nothing is sent.
  auto quiet = make(6, DialogControlKind::list_box,
                    simtower::kOriginalWindowStyleBorder, rect);
  auto plain = make_dialog({quiet}, 6U);
  plain.list_add(6, "One");
  plain.list_add(6, "Two");
  const auto first = plain.list_item_point(6, 0);
  assert(!press(plain, first->first, first->second).signal);
  assert(plain.list_selection(6) == 0);
  assert(!plain.list_has_scroll_bar(*plain.control(6)));
}

// --- Combo box ----------------------------------------------------------

void combo_tests() {
  const OriginalRect rect{150, 142, 250, 238};  // the rent combo's rect
  auto combo = make(13, DialogControlKind::combo_box,
                    simtower::kOriginalWindowStyleTabStop |
                        simtower::kOriginalComboBoxDropDownList,
                    rect);
  auto ok = make(1, DialogControlKind::default_push_button,
                 simtower::kOriginalWindowStyleTabStop |
                     simtower::kOriginalButtonDefPushButton,
                 {10, 10, 80, 33}, "OK");
  auto dialog = make_dialog({combo, ok}, 13U);
  for (const char* rent : {"$1,000", "$2,000", "$3,000", "$4,000"}) {
    dialog.list_add(13, rent);
  }
  // CB_SETCURSEL sends nothing.
  dialog.list_set_selection(13, 1);
  assert(dialog.list_selection(13) == 1);
  // Up / Down on the closed box change it with CBN_SELCHANGE each time.
  assert(dialog.key(DialogKey::down).signal ==
         command(13, simtower::kOriginalComboBoxSelChange));
  assert(dialog.list_selection(13) == 2);
  assert(dialog.key(DialogKey::up).signal ==
         command(13, simtower::kOriginalComboBoxSelChange));
  assert(dialog.list_selection(13) == 1);
  // A click on the field opens the list (CBN_DROPDOWN) ...
  auto& font = simtower::OriginalCanvasFont::bundled();
  const int line = simtower::sdl::dialog_line_height(font, kPixels);
  const auto field = simtower::sdl::dialog_combo_field_rect(
      *dialog.control(13), line);
  assert(field.bottom - field.top ==
         line + simtower::sdl::kDialogComboFieldExtra);
  assert(press(dialog, field.left + 5, field.top + 5).signal ==
         command(13, simtower::kOriginalComboBoxDropDown));
  (void)release(dialog, field.left + 5, field.top + 5);
  assert(dialog.control(13)->dropped);
  // ... drawn over the dialog ...
  const auto list =
      simtower::sdl::dialog_combo_list_rect(*dialog.control(13), line);
  assert(list.bottom - list.top == 4 * line + 2);
  const auto image = render(dialog);
  assert(at(image, list.left, list.bottom - 1) == 0U);
  // ... a click on an item selects it and closes the list.
  assert(press(dialog, list.left + 5, list.top + 1 + 3 * line + 3).signal ==
         command(13, simtower::kOriginalComboBoxSelChange));
  assert(!dialog.control(13)->dropped && dialog.list_selection(13) == 3);
  // A click outside closes it unchanged; Esc closes it without IDCANCEL.
  (void)dialog.key(DialogKey::f4);
  assert(dialog.control(13)->dropped);
  assert(!press(dialog, 380, 280).signal);
  assert(!dialog.control(13)->dropped && dialog.list_selection(13) == 3);
  (void)dialog.key(DialogKey::f4);
  assert(!dialog.key(DialogKey::escape).signal);
  assert(!dialog.control(13)->dropped);
  // Disabled: no input; drawn grayed.
  dialog.enable_item(13, false);
  assert(!press(dialog, field.left + 5, field.top + 5).signal);
  assert(!dialog.control(13)->dropped);

  // The closed combo window is only its field: a press below it, inside
  // the template rectangle, does not open the list and reaches the
  // control under it (a push button later in the Z order) or the dialog.
  auto below = make(9, DialogControlKind::push_button,
                    simtower::kOriginalWindowStyleTabStop,
                    {rect.left + 10, field.bottom + 10, rect.right - 10,
                     field.bottom + 33},
                    "Below");
  auto layered = make_dialog({combo, below}, 13U);
  for (const char* rent : {"$1,000", "$2,000"}) layered.list_add(13, rent);
  assert(layered.control(13)->rect.bottom > field.bottom + 33);
  const int under_x = rect.left + 20;
  const int under_y = field.bottom + 15;
  assert(!press(layered, under_x, under_y).signal);
  assert(!layered.control(13)->dropped && layered.captured() == 9U);
  assert(release(layered, under_x, under_y).signal ==
         command(9, simtower::kOriginalNotifyClicked));
  // Below the button, still inside the template rectangle: the dialog.
  assert(!press(layered, under_x, rect.bottom - 3).signal);
  assert(!layered.control(13)->dropped && layered.captured() == 0U);
  (void)release(layered, under_x, rect.bottom - 3);
  // Dropped, the list part takes the press (an item selects it).
  assert(press(layered, field.left + 5, field.top + 5).signal ==
         command(13, simtower::kOriginalComboBoxDropDown));
  (void)release(layered, field.left + 5, field.top + 5);
  const auto open_list =
      simtower::sdl::dialog_combo_list_rect(*layered.control(13), line);
  assert(press(layered, open_list.left + 5, open_list.top + 1 + line + 3)
             .signal == command(13, simtower::kOriginalComboBoxSelChange));
  assert(!layered.control(13)->dropped && layered.list_selection(13) == 1);
}

// --- Scroll bar ---------------------------------------------------------

void scroll_bar_tests() {
  const OriginalRect rect{135, 195, 151, 391};  // Elevator Control's bar
  auto bar = make(7, DialogControlKind::scroll_bar,
                  simtower::kOriginalScrollBarVertical, rect);
  auto dialog = make_dialog({bar}, 0U);
  dialog.set_scroll_range(7, 0, 100);
  dialog.set_scroll_pos(7, 50);
  assert(dialog.control(7)->scroll_pos == 50);
  dialog.set_scroll_pos(7, 200);
  assert(dialog.control(7)->scroll_pos == 100);
  dialog.set_scroll_pos(7, 50);
  const int width = rect.right - rect.left;
  // Arrows: SB_LINEUP / SB_LINEDOWN on the press, SB_ENDSCROLL on the
  // release; the control never moves its own thumb.
  assert(press(dialog, rect.left + 3, rect.top + 3).signal ==
         scroll(7, simtower::kOriginalScrollLineUp));
  assert(release(dialog, rect.left + 3, rect.top + 3).signal ==
         scroll(7, simtower::kOriginalScrollEnd));
  assert(press(dialog, rect.left + 3, rect.bottom - 3).signal ==
         scroll(7, simtower::kOriginalScrollLineDown));
  (void)release(dialog, rect.left + 3, rect.bottom - 3);
  assert(dialog.control(7)->scroll_pos == 50);
  // The trough: page codes on either side of the thumb.
  assert(press(dialog, rect.left + 3, rect.top + width + 2).signal ==
         scroll(7, simtower::kOriginalScrollPageUp));
  (void)release(dialog, 0, 0);
  assert(press(dialog, rect.left + 3, rect.bottom - width - 2).signal ==
         scroll(7, simtower::kOriginalScrollPageDown));
  (void)release(dialog, 0, 0);
  // The thumb: SB_THUMBTRACK while dragged, SB_THUMBPOSITION and
  // SB_ENDSCROLL on the release.
  const int room = (rect.bottom - rect.top) - 3 * width;
  const int thumb_top = rect.top + width + room / 2;
  assert(!press(dialog, rect.left + 3, thumb_top + 2).signal);
  const auto tracked = dialog.pointer(
      {DialogPointer::Kind::move, rect.left + 3, rect.bottom - width + 2});
  assert(tracked.signal == scroll(7, simtower::kOriginalScrollThumbTrack, 100));
  assert(dialog.scroll_tracking(7) == 100);
  const auto dropped = release(dialog, rect.left + 3, rect.bottom);
  assert(dropped.signal ==
         scroll(7, simtower::kOriginalScrollThumbPosition, 100));
  assert(dropped.then == scroll(7, simtower::kOriginalScrollEnd));
  assert(!dialog.scroll_tracking(7));
  assert(dialog.control(7)->scroll_pos == 50);
  // WM_CANCELMODE (a modal opened mid-drag): the track ends, the capture
  // is released and the later release and moves send nothing.
  assert(!press(dialog, rect.left + 3, thumb_top + 2).signal);
  assert(dialog.scroll_tracking_active() && dialog.has_pointer_capture());
  assert(dialog.cancel_scroll_tracking());
  assert(!dialog.scroll_tracking_active() && !dialog.has_pointer_capture());
  assert(!dialog.cancel_scroll_tracking());
  assert(!dialog.pointer({DialogPointer::Kind::move, rect.left + 3,
                          rect.bottom - width + 2})
              .signal);
  assert(!release(dialog, rect.left + 3, rect.bottom).signal);
  assert(dialog.control(7)->scroll_pos == 50);
  // Keys while focused.
  assert(dialog.focus() == 7U);
  assert(dialog.key(DialogKey::down).signal ==
         scroll(7, simtower::kOriginalScrollLineDown));
  assert(dialog.key(DialogKey::page_up).signal ==
         scroll(7, simtower::kOriginalScrollPageUp));
  assert(dialog.key(DialogKey::home).signal ==
         scroll(7, simtower::kOriginalScrollTop));
  const auto bottom = dialog.key(DialogKey::end);
  assert(bottom.signal == scroll(7, simtower::kOriginalScrollBottom));
  assert(bottom.then == scroll(7, simtower::kOriginalScrollEnd));
  // Painting: gray trough and face, black frame.
  const auto image = render(dialog);
  assert(at(image, rect.left, rect.top + 40) == 0U);
  assert(count(image, rect, simtower::sdl::style::kButtonFace) > 500);
}

// --- Modeless additions (Elevator Control) ------------------------------

std::optional<OriginalDialogSignal> move_signal(const DialogPointer&, int x,
                                                int y) {
  OriginalDialogSignal signal{Kind::mouse_move};
  signal.x = x;
  signal.y = y;
  return signal;
}

void modeless_tests() {
  // WM_MOUSEMOVE reaches the procedure over the client and no control,
  // never over the scroll bar or outside, and not while a control drags.
  const OriginalRect rect{135, 195, 151, 391};
  auto bar = make(7, DialogControlKind::scroll_bar,
                  simtower::kOriginalScrollBarVertical, rect);
  {
    DialogDescriptor descriptor{};
    descriptor.font_pixels = kPixels;
    descriptor.mouse_move = move_signal;
    descriptor.scroll_bar_background = 0xFFFFFFU;
    DialogWindow dialog(descriptor, template_of({bar}), {bar}, 400, 420, 0U);
    dialog.place(400, 420);
    dialog.set_scroll_range(7, 1, 10);
    dialog.set_scroll_pos(7, 10);
    const auto moved =
        dialog.pointer({DialogPointer::Kind::move, 20, 30}).signal;
    assert(moved && moved->kind == Kind::mouse_move && moved->x == 20 &&
           moved->y == 30);
    assert(!dialog.pointer({DialogPointer::Kind::move, 140, 300}).signal);
    assert(!dialog.pointer({DialogPointer::Kind::move, 500, 30}).signal);
    assert(!dialog.has_pointer_capture());
    // A scroll-bar click holds the capture until the release (USER's
    // tracking), and moves then reach no procedure.
    (void)press(dialog, rect.left + 3, rect.top + 3);
    assert(dialog.has_pointer_capture());
    assert(!dialog.pointer({DialogPointer::Kind::move, 20, 30}).signal);
    (void)release(dialog, 700, 700);
    assert(!dialog.has_pointer_capture());
    // WM_CTLCOLORSCROLLBAR: the trough takes the dialog's brush.
    const auto image = render(dialog);
    assert(count(image, rect, 0xFFFFFFU) > 500);
    assert(count(image, {rect.left + 1, rect.top + 20, rect.right - 1,
                         rect.bottom - 40},
                 simtower::sdl::style::kButtonFace) == 0);
  }
  {
    // SetCapture + ClipCursor: every move reaches the procedure, clamped
    // into the client.
    DialogDescriptor descriptor{};
    descriptor.font_pixels = kPixels;
    descriptor.mouse_move = move_signal;
    descriptor.capture_pointer = true;
    descriptor.clip_pointer = true;
    descriptor.placement = simtower::sdl::DialogPlacement::at;
    descriptor.origin_x = 50;
    descriptor.origin_y = 60;
    DialogWindow popup(descriptor, {}, {}, 82, 67, 0U);
    popup.place(1000, 1000);
    assert(popup.client_x() == 50 && popup.client_y() == 60);
    assert(popup.has_pointer_capture());
    const auto far = popup.pointer({DialogPointer::Kind::move, 0, 900}).signal;
    assert(far && far->x == 0 && far->y == 66);
    const auto right =
        popup.pointer({DialogPointer::Kind::move, 400, 70}).signal;
    assert(right && right->x == 81 && right->y == 10);
  }
  {
    // 11e0:0b52 with requested left 8 on the fixed 1024x768 desktop: the
    // 202x448 Elevator Control outer rectangle lands at (8, 160) while it
    // fits the window; a shorter window (TEMP) moves it up to keep the
    // bottom edge inside, a window shorter than the panel too.
    DialogDescriptor descriptor{};
    descriptor.placement = simtower::sdl::DialogPlacement::fixed_desktop;
    descriptor.requested_left = 8;
    descriptor.frame = {1, 19, 1, 1};
    DialogWindow panel(descriptor, {}, {}, 200, 428, 0U);
    for (const auto& [w, h] : {std::pair{1019, 628}, std::pair{1024, 608}}) {
      panel.place(w, h);
      assert(panel.outer() == (simtower::sdl::PanelRect{8, 160, 202, 448}));
      assert(panel.client_x() == 9 && panel.client_y() == 179);
    }
    panel.place(640, 480);
    assert(panel.outer() == (simtower::sdl::PanelRect{8, 32, 202, 448}));
    panel.place(400, 300);
    assert(panel.outer() == (simtower::sdl::PanelRect{8, -148, 202, 448}));
    panel.place(200, 300);
    assert(panel.outer() == (simtower::sdl::PanelRect{-2, -148, 202, 448}));
  }
}

// --- Dialog manager -----------------------------------------------------

void group_tests() {
  // Two groups: a static that starts the first (as in 3012), two buttons;
  // an edit that starts the second, then a button.
  auto label = make(10, DialogControlKind::static_text,
                    simtower::kOriginalWindowStyleGroup, {0, 0, 50, 10},
                    "Label");
  auto yes = make(11, DialogControlKind::push_button,
                  simtower::kOriginalWindowStyleTabStop, {0, 20, 50, 40},
                  "&Yes");
  auto no = make(12, DialogControlKind::push_button,
                 simtower::kOriginalWindowStyleTabStop, {60, 20, 110, 40},
                 "&No");
  auto edit = make(13, DialogControlKind::edit,
                   simtower::kOriginalWindowStyleGroup |
                       simtower::kOriginalWindowStyleTabStop |
                       simtower::kOriginalWindowStyleBorder,
                   {0, 50, 100, 70});
  auto later = make(14, DialogControlKind::push_button,
                    simtower::kOriginalWindowStyleTabStop, {0, 80, 50, 100},
                    "Later");
  auto dialog = make_dialog({label, yes, no, edit, later}, 11U);
  // The arrows wrap inside the group and skip the static.
  (void)dialog.key(DialogKey::right);
  assert(dialog.focus() == 12U && dialog.default_button() == 12U);
  (void)dialog.key(DialogKey::right);
  assert(dialog.focus() == 11U);
  (void)dialog.key(DialogKey::left);
  assert(dialog.focus() == 12U);
  // Hidden or disabled buttons are skipped.
  dialog.enable_item(11, false);
  (void)dialog.key(DialogKey::down);
  assert(dialog.focus() == 12U);
  dialog.enable_item(11, true);
  // The second group: Later -> edit (its text all selected), and the edit
  // keeps the arrows.
  dialog.set_item_text(13, "abc");
  dialog.focus_item(14);
  (void)dialog.key(DialogKey::up);
  assert(dialog.focus() == 13U);
  assert(dialog.control(13)->anchor == 0U && dialog.control(13)->caret == 3U);
  (void)dialog.key(DialogKey::down);
  assert(dialog.focus() == 13U);
  // A mnemonic is not a character for a button group... unless an edit has
  // the focus.
  assert(!dialog.key(DialogKey::other, 'y').signal);
  dialog.focus_item(12);
  assert(dialog.key(DialogKey::other, 'y').signal ==
         command(11, simtower::kOriginalNotifyClicked));
  // Space: press on the key down, click on the key up; Alt does nothing.
  assert(!dialog.key(DialogKey::space).signal && dialog.pressed());
  assert(dialog.key_up(DialogKey::space).signal ==
         command(11, simtower::kOriginalNotifyClicked));
  assert(!dialog.key(DialogKey::menu).signal);
  // WM_PAINT: owed once at open, after invalidate and after a covering
  // dialog closed; show_item and other ops only repaint.
  assert(dialog.take_paint() && !dialog.take_paint());
  dialog.show_item(14, false);
  assert(!dialog.take_paint() && dialog.take_invalidated());
  dialog.invalidate();
  assert(dialog.take_paint());
  dialog.uncovered();
  assert(dialog.take_paint() && dialog.take_invalidated());
  // A WM_KEYUP reaches the procedure only while the dialog itself has the
  // focus (Finance).
  DialogDescriptor descriptor{};
  descriptor.font_pixels = kPixels;
  descriptor.unhandled_key_up = [](DialogKey key, char character)
      -> std::optional<OriginalDialogSignal> {
    return OriginalDialogSignal{
        Kind::key_up, simtower::sdl::dialog_key_virtual_key(key, character)};
  };
  descriptor.unhandled_pointer =
      [](const DialogPointer& pointer, int x,
         int y) -> std::optional<OriginalDialogSignal> {
    OriginalDialogSignal signal{pointer.kind == DialogPointer::Kind::down
                                    ? Kind::button_down
                                    : Kind::button_up};
    signal.x = x;
    signal.y = y;
    return signal;
  };
  DialogWindow bare(descriptor, {}, {}, 300, 200, 0U);
  bare.place(300, 200);
  assert(bare.key_up(DialogKey::enter).signal ==
         OriginalDialogSignal({Kind::key_up, 0x0DU}));
  assert(bare.key_up(DialogKey::space).signal ==
         OriginalDialogSignal({Kind::key_up, 0x20U}));
  const auto down = press(bare, 140, 330 - 300);
  assert(down.signal && down.signal->kind == Kind::button_down &&
         down.signal->x == 140 && down.signal->y == 30);
  assert(release(bare, 1, 2).signal->kind == Kind::button_up);
  assert(dialog.key_up(DialogKey::enter).signal == std::nullopt);
}

// The one DrawFocusRect pattern (sdl_focus_rect.hpp) the widgets and the
// panels share: every other perimeter pixel, each visited once (an XOR
// must not undo itself on a one-pixel-wide or one-pixel-high rectangle).
void focus_rect_tests() {
  const auto visit = [](int left, int top, int right, int bottom) {
    std::vector<std::pair<int, int>> order{};
    simtower::sdl::for_each_focus_rect_pixel(
        left, top, right, bottom,
        [&](int x, int y) { order.emplace_back(x, y); });
    const std::set<std::pair<int, int>> unique(order.begin(), order.end());
    assert(unique.size() == order.size());
    return unique;
  };
  using Pixels = std::set<std::pair<int, int>>;
  // 4x3 at (0,0): the even (x + y) perimeter pixels.
  assert(visit(0, 0, 4, 3) ==
         (Pixels{{0, 0}, {2, 0}, {0, 2}, {2, 2}, {3, 1}}));
  // Shifted by one: the other phase.
  assert(visit(1, 0, 5, 3) ==
         (Pixels{{2, 0}, {4, 0}, {2, 2}, {4, 2}, {1, 1}}));
  assert(visit(0, 0, 5, 1) == (Pixels{{0, 0}, {2, 0}, {4, 0}}));
  assert(visit(3, 0, 4, 4) == (Pixels{{3, 1}, {3, 3}}));
  assert(visit(0, 0, 0, 5).empty());
  assert(visit(0, 0, 5, 0).empty());
}

}  // namespace

int main() {
  focus_rect_tests();
  edit_tests();
  list_tests();
  combo_tests();
  scroll_bar_tests();
  group_tests();
  modeless_tests();
  std::printf("sdl widget tests passed\n");
  return 0;
}
