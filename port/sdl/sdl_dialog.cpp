#include "sdl_dialog.hpp"

#include "original_dialog_paint.hpp"
#include "original_draw_canvas.hpp"
#include "original_dtmp_runtime.hpp"

#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace simtower::sdl {
namespace {

// MulDiv: (a * b) / c rounded half away from zero.
int mul_div(int a, int b, int c) {
  const long long product = static_cast<long long>(a) * b;
  const long long half = c / 2;
  return static_cast<int>(product >= 0 ? (product + half) / c
                                       : (product - half) / c);
}

// The index in the stripped text of the '&' mnemonic character, or npos.
std::size_t mnemonic_index(std::string_view text) {
  std::size_t stripped = 0;
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] != '&') {
      ++stripped;
      continue;
    }
    if (index + 1 >= text.size()) break;
    if (text[index + 1] == '&') {
      ++index;
      ++stripped;
      continue;
    }
    return stripped;
  }
  return std::string_view::npos;
}

}  // namespace

std::string event_dialog_label(std::uint16_t dialog_id) {
  return "event " + std::to_string(dialog_id);
}

std::string alert_dialog_label(int alert_id) {
  return "alert " + std::to_string(alert_id);
}

int dialog_line_height(OriginalCanvasFont& font, int pixel_height) {
  const auto metrics = font.metrics(pixel_height);
  return metrics.ascent + metrics.descent;
}

void draw_dialog_text_line(OriginalDrawContext& context,
                           OriginalCanvasFont& font, int pixel_height,
                           std::string_view text, int x, int y,
                           const OriginalRect& clip) {
  OriginalSavedDrawState saved(context);
  context.intersect_clip(clip);
  const int extent = font.extent(pixel_height, text);
  context.draw_text_single_line(
      text, {x, y, std::max(clip.right, x + extent),
             y + dialog_line_height(font, pixel_height)});
}

void draw_dialog_outline(OriginalDrawContext& context,
                         const OriginalRect& rect, OriginalRgb rgb) {
  context.fill_rect({rect.left, rect.top, rect.right, rect.top + 1}, rgb);
  context.fill_rect({rect.left, rect.bottom - 1, rect.right, rect.bottom},
                    rgb);
  context.fill_rect({rect.left, rect.top, rect.left + 1, rect.bottom}, rgb);
  context.fill_rect({rect.right - 1, rect.top, rect.right, rect.bottom}, rgb);
}

std::string strip_dialog_mnemonic(std::string_view text) {
  std::string result;
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] == '&') {
      if (index + 1 < text.size() && text[index + 1] == '&') {
        result.push_back('&');
        ++index;
      }
      continue;
    }
    result.push_back(text[index]);
  }
  return result;
}

char dialog_mnemonic(std::string_view text) {
  const auto index = mnemonic_index(text);
  if (index == std::string_view::npos) return '\0';
  const char next = strip_dialog_mnemonic(text)[index];
  return next >= 'A' && next <= 'Z' ? static_cast<char>(next - 'A' + 'a')
                                    : next;
}

PushButtonLabel push_button_label(OriginalCanvasFont& font,
                                  const DialogControl& control,
                                  int pixel_height, bool pressed,
                                  bool is_default) {
  PushButtonLabel label{};
  const auto& r = control.rect;
  label.text = strip_dialog_mnemonic(control.text);
  label.extent = font.extent(pixel_height, label.text);
  // The ink can pass the text cell (a glyph's side bearing is negative,
  // e.g. the final 'b' of "Find the Bomb", or a capital's top reaches the
  // cell's top row): the focus rectangle surrounds whichever is larger.
  label.height = dialog_line_height(font, pixel_height);
  const int ascent = font.metrics(pixel_height).ascent;
  int ink_left = 0;
  int ink_right = label.extent;
  int ink_top = 0;
  int ink_bottom = label.height;
  int pen = 0;
  for (const char character : label.text) {
    const auto& glyph =
        font.glyph(pixel_height, static_cast<unsigned char>(character));
    if (glyph.width > 0 && glyph.height > 0) {
      ink_left = std::min(ink_left, pen + glyph.left);
      ink_right = std::max(ink_right, pen + glyph.left + glyph.width);
      ink_top = std::min(ink_top, ascent + glyph.top);
      ink_bottom = std::max(ink_bottom, ascent + glyph.top + glyph.height);
    }
    pen += glyph.advance;
  }
  const int offset = pressed ? 1 : 0;
  label.x = r.left + (r.right - r.left - label.extent) / 2 + offset;
  label.y = r.top + (r.bottom - r.top - label.height) / 2 + offset;
  // DrawFocusRect around the text extent inflated by one pixel, and one
  // blank pixel between the dots and any ink on every side; only then
  // clamped inside the frame and bevel (two pixels, three for the default
  // button's doubled frame), which can bring the dots back onto the ink of
  // a label that does not fit.
  const int bevel = is_default ? 3 : 2;
  label.focus = {std::max(r.left + bevel,
                          std::min(label.x - 1, label.x + ink_left - 2)),
                 std::max(r.top + bevel,
                          std::min(label.y - 1, label.y + ink_top - 2)),
                 std::min(r.right - bevel,
                          std::max(label.x + label.extent + 1,
                                   label.x + ink_right + 2)),
                 std::min(r.bottom - bevel,
                          std::max(label.y + label.height + 1,
                                   label.y + ink_bottom + 2))};
  // TEMP: the mnemonic underline spans the character's advance one pixel
  // below the baseline (one blank row under the ink), as DrawText draws an
  // '&' prefix.
  if (const auto index = mnemonic_index(control.text);
      index != std::string_view::npos && index < label.text.size()) {
    const int left =
        label.x + font.extent(pixel_height,
                              std::string_view(label.text).substr(0, index));
    const int width = font.extent(
        pixel_height, std::string_view(label.text).substr(index, 1));
    const int row = label.y + font.metrics(pixel_height).ascent + 1;
    label.underline = {left, row, left + width, row + 1};
  }
  return label;
}

DialogBaseUnits dialog_base_units(OriginalCanvasFont& font,
                                  int pixel_height) {
  const int total = font.extent(
      pixel_height, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
  return {(total / 26 + 1) / 2, dialog_line_height(font, pixel_height)};
}

OriginalRect map_dialog_rect(const DialogBaseUnits& units, int x, int y,
                             int width, int height) {
  const int left = mul_div(x, units.x, 4);
  const int top = mul_div(y, units.y, 8);
  return {left, top, left + mul_div(width, units.x, 4),
          top + mul_div(height, units.y, 8)};
}

std::vector<DialogControl> dialog_template_controls(
    const OriginalDialog& dialog, const DialogBaseUnits& units) {
  std::vector<DialogControl> controls;
  controls.reserve(dialog.items.size());
  for (const auto& item : dialog.items) {
    DialogControl control{};
    control.id = item.id;
    control.kind = original_dialog_item_widget_kind(item);
    control.style = item.style;
    control.text = item.text.kind == OriginalDialogValue::Kind::text
                       ? item.text.text
                       : std::string{};
    control.rect =
        map_dialog_rect(units, item.x, item.y, item.width, item.height);
    control.visible = (item.style & kOriginalWindowStyleVisible) != 0U;
    control.enabled = (item.style & kOriginalWindowStyleDisabled) == 0U;
    control.tab_stop = (item.style & kOriginalWindowStyleTabStop) != 0U;
    controls.push_back(std::move(control));
  }
  return controls;
}

void apply_dtmp_child_placement(std::vector<DialogControl>& controls,
                                const OriginalDtmp& dtmp) {
  for (const auto& op : original_dtmp_render_ops(dtmp)) {
    if (op.kind != OriginalDtmpRenderOp::Kind::place_child) continue;
    for (auto& control : controls) {
      if (control.id != op.child_id) continue;
      control.rect = op.rect;
      control.visible = true;
    }
  }
}

std::vector<std::string> wrap_dialog_text(OriginalCanvasFont& font,
                                          int pixel_height,
                                          std::string_view text, int width) {
  std::vector<std::string> lines;
  const auto paragraph = [&](std::string_view part) {
    std::string current;
    bool started = false;
    std::size_t start = 0;
    while (start <= part.size()) {
      const auto end = std::min(part.find(' ', start), part.size());
      const auto word = part.substr(start, end - start);
      std::string candidate =
          started ? current + " " + std::string(word) : std::string(word);
      if (!started || font.extent(pixel_height, candidate) <= width) {
        current = std::move(candidate);
        started = true;
      } else {
        lines.push_back(std::move(current));
        current = std::string(word);
      }
      start = end + 1;
    }
    lines.push_back(std::move(current));
  };
  std::string cleaned;
  for (const char character : text) {
    if (character != '\r') cleaned.push_back(character);
  }
  std::size_t start = 0;
  while (true) {
    const auto end = cleaned.find('\n', start);
    if (end == std::string::npos) {
      paragraph(std::string_view(cleaned).substr(start));
      break;
    }
    paragraph(std::string_view(cleaned).substr(start, end - start));
    start = end + 1;
  }
  return lines;
}


std::uint16_t dialog_key_virtual_key(DialogKey key, char character) {
  switch (key) {
    case DialogKey::enter: return 0x0DU;
    case DialogKey::escape: return 0x1BU;
    case DialogKey::tab:
    case DialogKey::shift_tab: return 0x09U;
    case DialogKey::space: return 0x20U;
    case DialogKey::left: return 0x25U;
    case DialogKey::up: return 0x26U;
    case DialogKey::right: return 0x27U;
    case DialogKey::down: return 0x28U;
    case DialogKey::home: return 0x24U;
    case DialogKey::end: return 0x23U;
    case DialogKey::page_up: return 0x21U;
    case DialogKey::page_down: return 0x22U;
    case DialogKey::backspace: return 0x08U;
    case DialogKey::delete_key: return 0x2EU;
    case DialogKey::f4: return 0x73U;
    case DialogKey::menu: return 0x12U;
    case DialogKey::other:
      if (character >= 'a' && character <= 'z') {
        return static_cast<std::uint16_t>(character - 'a' + 'A');
      }
      return 0U;
  }
  return 0U;
}

// --- Widget geometry --------------------------------------------------

DialogEditLayout dialog_edit_layout(const DialogControl& edit) {
  const int border =
      (edit.style & kOriginalWindowStyleBorder) != 0U ? 1 : 0;
  return {edit.rect.left + border + kDialogEditInsetX,
          edit.rect.top + border + kDialogEditInsetY,
          std::max(0, edit.rect.right - edit.rect.left - 2 * border -
                          2 * kDialogEditInsetX)};
}

std::size_t dialog_edit_hit(OriginalCanvasFont& font, int pixel_height,
                            const DialogControl& edit, int x) {
  const auto layout = dialog_edit_layout(edit);
  std::size_t best = 0;
  int best_distance = std::abs(x - layout.text_x);
  int pen = layout.text_x;
  for (std::size_t index = 0; index < edit.text.size(); ++index) {
    pen += font.glyph(pixel_height,
                      static_cast<unsigned char>(edit.text[index]))
               .advance;
    const int distance = std::abs(x - pen);
    if (distance < best_distance) {
      best = index + 1;
      best_distance = distance;
    }
  }
  return best;
}

OriginalRect dialog_list_rows_rect(const DialogControl& list) {
  const int border =
      (list.style & kOriginalWindowStyleBorder) != 0U ? 1 : 0;
  return original_deflate_rect(list.rect, border);
}

OriginalRect dialog_combo_field_rect(const DialogControl& combo,
                                     int line_height) {
  return {combo.rect.left, combo.rect.top, combo.rect.right,
          std::min(combo.rect.bottom,
                   combo.rect.top + line_height + kDialogComboFieldExtra)};
}

OriginalRect dialog_combo_list_rect(const DialogControl& combo,
                                    int line_height) {
  // min(items * line + 2, template height - field height), under the field.
  const auto field = dialog_combo_field_rect(combo, line_height);
  const int wanted =
      static_cast<int>(combo.items.size()) * line_height + 2;
  const int room = combo.rect.bottom - field.bottom;
  return {combo.rect.left, field.bottom - 1, combo.rect.right,
          field.bottom - 1 + std::max(line_height + 2, std::min(wanted, room))};
}

DialogScrollParts dialog_scroll_parts(const OriginalRect& bar, int min,
                                      int max, int position) {
  DialogScrollParts parts{};
  const int width = bar.right - bar.left;
  const int arrow = std::min(width, (bar.bottom - bar.top) / 2);
  parts.up = {bar.left, bar.top, bar.right, bar.top + arrow};
  parts.down = {bar.left, bar.bottom - arrow, bar.right, bar.bottom};
  parts.track_top = parts.up.bottom;
  parts.track_bottom = parts.down.top;
  const int room = parts.track_bottom - parts.track_top - width;
  if (max > min && room >= 0) {
    const int clamped = std::clamp(position, min, max);
    const int top = parts.track_top +
                    static_cast<int>(static_cast<long long>(clamped - min) *
                                     room / (max - min));
    parts.thumb = {bar.left, top, bar.right, top + width};
  }
  return parts;
}

namespace {

int scroll_position_at(const DialogScrollParts& parts, int width, int min,
                       int max, int thumb_top) {
  const int room = parts.track_bottom - parts.track_top - width;
  if (room <= 0 || max <= min) return min;
  const int offset = std::clamp(thumb_top - parts.track_top, 0, room);
  return min + static_cast<int>(
                   (static_cast<long long>(offset) * (max - min) + room / 2) /
                   room);
}

DialogSignal notify(std::uint16_t id, std::uint16_t code) {
  return DialogSignal{DialogSignal::Kind::command, id, code};
}

DialogSignal scroll_signal(std::uint16_t id, std::uint16_t code,
                           int position = 0) {
  DialogSignal signal{DialogSignal::Kind::scroll, id, code};
  signal.value = position;
  return signal;
}

char lower(char character) {
  return character >= 'A' && character <= 'Z'
             ? static_cast<char>(character - 'A' + 'a')
             : character;
}

}  // namespace

// --- DialogWindow --------------------------------------------------------

DialogWindow::DialogWindow(DialogDescriptor descriptor,
                         OriginalDialog dialog_template,
                         std::vector<DialogControl> controls,
                         int client_width, int client_height,
                         std::uint16_t initial_focus)
    : descriptor_(std::move(descriptor)),
      dialog_(std::move(dialog_template)),
      controls_(std::move(controls)),
      client_width_(client_width),
      client_height_(client_height),
      focus_(initial_focus) {}

void DialogWindow::place(int desktop_width, int desktop_height) {
  desktop_width_ = desktop_width;
  desktop_height_ = desktop_height;
  const OriginalDialogScreenRect desktop{0, 0, desktop_width, desktop_height};
  const OriginalDialogScreenRect window{0, 0, outer_width(), outer_height()};
  const auto position =
      descriptor_.placement == DialogPlacement::screen_position
          ? original_dialog_screen_position(desktop, window, 0)
          : original_dialog_center_position(desktop, window);
  x_ = position.left;
  y_ = position.top;
}

const DialogControl* DialogWindow::control(std::uint16_t id) const {
  for (const auto& entry : controls_) {
    if (entry.id == id) return &entry;
  }
  return nullptr;
}

DialogControl* DialogWindow::mutable_control(std::uint16_t id) {
  for (auto& entry : controls_) {
    if (entry.id == id) return &entry;
  }
  return nullptr;
}

OriginalRect DialogWindow::hit_rect(const DialogControl& control) const {
  if (control.kind != DialogControlKind::combo_box) return control.rect;
  // CBS_DROPDOWNLIST: the closed combo window is only its field; the list
  // below it exists only while dropped (the template height includes it).
  auto& font = OriginalCanvasFont::bundled();
  const int line = dialog_line_height(font, font_pixels());
  auto rect = dialog_combo_field_rect(control, line);
  if (control.dropped) {
    rect.bottom =
        std::max(rect.bottom, dialog_combo_list_rect(control, line).bottom);
  }
  return rect;
}

const DialogControl* DialogWindow::control_at(int x, int y) const {
  // The first template child is on top of the Z order. A static (without
  // SS_NOTIFY) is transparent to the mouse; a disabled control takes none.
  for (const auto& entry : controls_) {
    if (entry.visible && entry.enabled &&
        entry.kind != DialogControlKind::static_text &&
        entry.kind != DialogControlKind::other &&
        original_rect_contains(hit_rect(entry), x, y)) {
      return &entry;
    }
  }
  return nullptr;
}

std::uint16_t DialogWindow::enter_command() const {
  return original_dialog_enter_command(dialog_, focus_);
}

std::uint16_t DialogWindow::default_button() const {
  const auto id = enter_command();
  const auto* entry = control(id);
  return entry && original_widget_is_push_button(entry->kind) ? id : 0U;
}

std::optional<std::pair<int, int>> DialogWindow::click_point(
    std::uint16_t id) const {
  if (id == 0U && controls_.empty()) {
    return std::pair{client_x() + client_width_ / 2,
                     client_y() + client_height_ / 2};
  }
  const auto* entry = control(id);
  if (!entry || !entry->visible || !entry->enabled ||
      !original_widget_is_push_button(entry->kind)) {
    return std::nullopt;
  }
  return std::pair{client_x() + (entry->rect.left + entry->rect.right) / 2,
                   client_y() + (entry->rect.top + entry->rect.bottom) / 2};
}

int DialogWindow::list_rows(const DialogControl& control) const {
  auto& font = OriginalCanvasFont::bundled();
  const int line = dialog_line_height(font, font_pixels());
  const auto rows = dialog_list_rows_rect(control);
  return std::max(1, (rows.bottom - rows.top) / std::max(1, line));
}

bool DialogWindow::list_has_scroll_bar(const DialogControl& control) const {
  return control.kind == DialogControlKind::list_box &&
         (control.style & kOriginalWindowStyleVScroll) != 0U &&
         static_cast<int>(control.items.size()) > list_rows(control);
}

OriginalRect DialogWindow::scroll_track(const DialogControl& control) const {
  if (control.kind == DialogControlKind::list_box) {
    return {control.rect.right - kDialogScrollBarWidth, control.rect.top,
            control.rect.right, control.rect.bottom};
  }
  return control.rect;
}

std::optional<std::pair<int, int>> DialogWindow::list_item_point(
    std::uint16_t id, int index) const {
  const auto* entry = control(id);
  if (!entry || !entry->visible || !entry->enabled ||
      entry->kind != DialogControlKind::list_box || index < 0 ||
      index >= static_cast<int>(entry->items.size())) {
    return std::nullopt;
  }
  auto& font = OriginalCanvasFont::bundled();
  const int line = dialog_line_height(font, font_pixels());
  const auto rows = dialog_list_rows_rect(*entry);
  const int row = index - entry->top_index;
  const int top = rows.top + row * line;
  if (row < 0 || top + line > rows.bottom) return std::nullopt;
  return std::pair{client_x() + rows.left + 8, client_y() + top + line / 2};
}

bool DialogWindow::caret_visible() const {
  const auto* entry = control(focus_);
  if (!entry || entry->kind != DialogControlKind::edit) return false;
  const auto elapsed =
      clock_ms_ >= caret_epoch_ms_ ? clock_ms_ - caret_epoch_ms_ : 0U;
  return (elapsed / kDialogCaretBlinkMs) % 2U == 0U;
}

std::optional<std::uint64_t> DialogWindow::next_caret_blink() const {
  const auto* entry = control(focus_);
  if (!entry || entry->kind != DialogControlKind::edit) return std::nullopt;
  const auto elapsed =
      clock_ms_ >= caret_epoch_ms_ ? clock_ms_ - caret_epoch_ms_ : 0U;
  return caret_epoch_ms_ +
         (elapsed / kDialogCaretBlinkMs + 1U) * kDialogCaretBlinkMs;
}

void DialogWindow::set_focus(std::uint16_t id, bool select_edit_text) {
  if (focus_ != id) close_drop_downs();
  focus_ = id;
  caret_epoch_ms_ = clock_ms_;
  auto* entry = mutable_control(id);
  if (entry && entry->kind == DialogControlKind::edit && select_edit_text) {
    // DLGC_HASSETSEL: the dialog manager selects the whole text.
    entry->anchor = 0U;
    entry->caret = entry->text.size();
  }
}

void DialogWindow::focus_first_tab_stop() {
  focus_ = 0U;
  for (const auto& entry : controls_) {
    if (entry.visible && entry.enabled && entry.tab_stop) {
      set_focus(entry.id, false);
      return;
    }
  }
}

void DialogWindow::close_drop_downs() {
  for (auto& entry : controls_) entry.dropped = false;
}

void DialogWindow::move_focus(bool forward) {
  std::vector<std::uint16_t> stops;
  for (const auto& entry : controls_) {
    if (entry.visible && entry.enabled && entry.tab_stop) {
      stops.push_back(entry.id);
    }
  }
  if (stops.empty()) return;
  const auto current = std::ranges::find(stops, focus_);
  const auto size = static_cast<std::ptrdiff_t>(stops.size());
  std::ptrdiff_t index =
      current == stops.end() ? (forward ? -1 : 0) : current - stops.begin();
  index = (index + (forward ? 1 : size - 1)) % size;
  set_focus(stops[static_cast<std::size_t>(index)], true);
}

void DialogWindow::move_group_focus(bool forward) {
  // GetNextDlgGroupItem over the focused control's group (from the
  // preceding WS_GROUP control up to the next one), wrapping and skipping
  // hidden and disabled controls; IsDialogMessage also skips statics
  // (DLGC_STATIC) when the arrows move through a group.
  const auto count = static_cast<std::ptrdiff_t>(controls_.size());
  std::ptrdiff_t at = -1;
  for (std::ptrdiff_t index = 0; index < count; ++index) {
    if (controls_[static_cast<std::size_t>(index)].id == focus_) at = index;
  }
  if (at < 0) return;
  const auto grouped = [&](std::ptrdiff_t index) {
    return (controls_[static_cast<std::size_t>(index)].style &
            kOriginalWindowStyleGroup) != 0U;
  };
  std::ptrdiff_t first = at;
  while (first > 0 && !grouped(first)) --first;
  std::ptrdiff_t last = at + 1;
  while (last < count && !grouped(last)) ++last;
  const auto size = last - first;
  for (std::ptrdiff_t step = 1; step < size; ++step) {
    const auto offset = forward ? step : size - step;
    const auto& entry = controls_[static_cast<std::size_t>(
        first + (at - first + offset) % size)];
    if (!entry.visible || !entry.enabled ||
        entry.kind == DialogControlKind::static_text) {
      continue;
    }
    set_focus(entry.id, true);
    return;
  }
}

void DialogWindow::ensure_visible(DialogControl& list) {
  if (list.caret_index < list.top_index) {
    list_scroll_to(list, list.caret_index);
  } else if (list.caret_index >= list.top_index + list_rows(list)) {
    list_scroll_to(list, list.caret_index - list_rows(list) + 1);
  }
}

void DialogWindow::list_scroll_to(DialogControl& list, int top) {
  const int most =
      std::max(0, static_cast<int>(list.items.size()) - list_rows(list));
  list.top_index = std::clamp(top, 0, most);
}

DialogStep DialogWindow::list_select(DialogControl& list, int index,
                                    bool notify_change) {
  DialogStep step{};
  if (list.items.empty()) return step;
  index = std::clamp(index, 0, static_cast<int>(list.items.size()) - 1);
  const bool changed = list.selection != index;
  list.selection = index;
  list.caret_index = index;
  ensure_visible(list);
  step.redraw = true;
  if (changed && notify_change) {
    if (list.kind == DialogControlKind::combo_box) {
      step.signal = notify(list.id, kOriginalComboBoxSelChange);
    } else if ((list.style & kOriginalListBoxNotify) != 0U) {
      step.signal = notify(list.id, kOriginalListBoxSelChange);
    }
  }
  return step;
}

DialogStep DialogWindow::edit_key(DialogControl& edit, DialogKey key,
                                 bool shift) {
  DialogStep step{};
  const auto size = edit.text.size();
  const auto low = std::min(edit.anchor, edit.caret);
  const auto high = std::max(edit.anchor, edit.caret);
  const auto move = [&](std::size_t caret) {
    edit.caret = std::min(caret, size);
    if (!shift) edit.anchor = edit.caret;
    step.redraw = true;
  };
  const auto erase = [&](std::size_t from, std::size_t to) {
    edit.text.erase(from, to - from);
    edit.caret = edit.anchor = from;
    step.redraw = true;
    step.signal = notify(edit.id, kOriginalEditChange);
  };
  switch (key) {
    case DialogKey::left:
      move(edit.caret == 0U ? 0U : edit.caret - 1U);
      break;
    case DialogKey::right:
      move(edit.caret + 1U);
      break;
    case DialogKey::home:
      move(0U);
      break;
    case DialogKey::end:
      move(size);
      break;
    case DialogKey::backspace:
      if (low != high) {
        erase(low, high);
      } else if (edit.caret > 0U) {
        erase(edit.caret - 1U, edit.caret);
      }
      break;
    case DialogKey::delete_key:
      if (low != high) {
        erase(low, high);
      } else if (edit.caret < size) {
        erase(edit.caret, edit.caret + 1U);
      }
      break;
    default:
      break;
  }
  caret_epoch_ms_ = clock_ms_;
  return step;
}

DialogStep DialogWindow::edit_character(DialogControl& edit, char cp1252) {
  DialogStep step{};
  if (static_cast<unsigned char>(cp1252) < 0x20U) return step;
  const auto low = std::min(edit.anchor, edit.caret);
  const auto high = std::max(edit.anchor, edit.caret);
  auto text = edit.text;
  text.replace(low, high - low, 1U, cp1252);
  // Without ES_AUTOHSCROLL a single-line edit refuses a character that
  // would run past its formatting rectangle (EN_MAXTEXT, nothing typed).
  if ((edit.style & kOriginalEditAutoHScroll) == 0U) {
    auto& font = OriginalCanvasFont::bundled();
    if (font.extent(font_pixels(), text) > dialog_edit_layout(edit).width) {
      step.signal = notify(edit.id, kOriginalEditMaxText);
      return step;
    }
  }
  edit.text = std::move(text);
  edit.caret = edit.anchor = low + 1U;
  caret_epoch_ms_ = clock_ms_;
  step.redraw = true;
  step.signal = notify(edit.id, kOriginalEditChange);
  return step;
}

DialogStep DialogWindow::list_key(DialogControl& list, DialogKey key) {
  if (list.items.empty()) return {};
  const int rows = list_rows(list);
  const int last = static_cast<int>(list.items.size()) - 1;
  // The caret row moves (a fresh list's caret is row 0, so Down selects
  // row 1, as the list box's focus item does).
  int target = list.caret_index;
  switch (key) {
    case DialogKey::up: target = list.caret_index - 1; break;
    case DialogKey::down: target = list.caret_index + 1; break;
    case DialogKey::page_up: target = list.caret_index - (rows - 1); break;
    case DialogKey::page_down: target = list.caret_index + (rows - 1); break;
    case DialogKey::home: target = 0; break;
    case DialogKey::end: target = last; break;
    default: return {};
  }
  return list_select(list, std::clamp(target, 0, last), true);
}

DialogStep DialogWindow::combo_key(DialogControl& combo, DialogKey key) {
  DialogStep step{};
  switch (key) {
    case DialogKey::f4:
      combo.dropped = !combo.dropped;
      step.redraw = true;
      if (combo.dropped) {
        step.signal = notify(combo.id, kOriginalComboBoxDropDown);
      }
      return step;
    case DialogKey::up:
    case DialogKey::down: {
      if (combo.items.empty()) return step;
      const int from = combo.selection < 0 ? (key == DialogKey::down ? -1 : 1)
                                           : combo.selection;
      return list_select(combo, from + (key == DialogKey::down ? 1 : -1),
                         true);
    }
    case DialogKey::home:
      return list_select(combo, 0, true);
    case DialogKey::end:
      return list_select(combo, static_cast<int>(combo.items.size()) - 1,
                         true);
    default:
      return step;
  }
}

DialogStep DialogWindow::scroll_key(DialogControl& bar, DialogKey key) {
  std::uint16_t code = 0U;
  switch (key) {
    case DialogKey::up:
    case DialogKey::left: code = kOriginalScrollLineUp; break;
    case DialogKey::down:
    case DialogKey::right: code = kOriginalScrollLineDown; break;
    case DialogKey::page_up: code = kOriginalScrollPageUp; break;
    case DialogKey::page_down: code = kOriginalScrollPageDown; break;
    case DialogKey::home: code = kOriginalScrollTop; break;
    case DialogKey::end: code = kOriginalScrollBottom; break;
    default: return {};
  }
  DialogStep step{};
  step.signal = scroll_signal(bar.id, code);
  step.then = scroll_signal(bar.id, kOriginalScrollEnd);
  return step;
}

DialogStep DialogWindow::scroll_bar_down(DialogControl& bar,
                                        const OriginalRect& area,
                                        bool for_list, int x, int y) {
  (void)x;
  DialogStep step{};
  const int min = for_list ? 0 : bar.scroll_min;
  const int max =
      for_list ? std::max(0, static_cast<int>(bar.items.size()) -
                                 list_rows(bar))
               : bar.scroll_max;
  const int position = for_list ? bar.top_index : bar.scroll_pos;
  const auto parts = dialog_scroll_parts(area, min, max, position);
  std::uint16_t code = 0U;
  if (original_rect_contains(parts.up, x, y)) {
    code = kOriginalScrollLineUp;
  } else if (original_rect_contains(parts.down, x, y)) {
    code = kOriginalScrollLineDown;
  } else if (parts.thumb.bottom > parts.thumb.top &&
             original_rect_contains(parts.thumb, x, y)) {
    scroll_drag_ = ScrollDrag{bar.id, for_list, y - parts.thumb.top,
                              position};
    step.redraw = true;
    return step;
  } else if (parts.thumb.bottom > parts.thumb.top) {
    code = y < parts.thumb.top ? kOriginalScrollPageUp
                               : kOriginalScrollPageDown;
  } else {
    return step;
  }
  // A click (TEMP: no auto-repeat while the button stays down); the release
  // sends SB_ENDSCROLL.
  scroll_drag_ = ScrollDrag{bar.id, for_list, -1, position};
  if (for_list) {
    const int rows = list_rows(bar);
    const int delta = code == kOriginalScrollLineUp     ? -1
                      : code == kOriginalScrollLineDown ? 1
                      : code == kOriginalScrollPageUp   ? -rows
                                                        : rows;
    list_scroll_to(bar, bar.top_index + delta);
    step.redraw = true;
    return step;
  }
  step.signal = scroll_signal(bar.id, code);
  return step;
}

DialogStep DialogWindow::pointer_down(const DialogPointer& event, int x,
                                     int y) {
  DialogStep step{};
  auto& font = OriginalCanvasFont::bundled();
  const int line = dialog_line_height(font, font_pixels());
  // An open combo box list takes the press: an item selects and closes,
  // anything else only closes it.
  for (auto& entry : controls_) {
    if (!entry.dropped) continue;
    const auto list = dialog_combo_list_rect(entry, line);
    entry.dropped = false;
    step.redraw = true;
    if (original_rect_contains(list, x, y)) {
      const int index = entry.top_index + (y - list.top - 1) / line;
      if (index >= 0 && index < static_cast<int>(entry.items.size())) {
        auto selected = list_select(entry, index, true);
        selected.redraw = true;
        return selected;
      }
    }
    return step;
  }
  // The double-click test, on the dialog clock.
  const bool double_click =
      last_press_ms_ && clock_ms_ - *last_press_ms_ <= kDialogDoubleClickMs &&
      std::abs(x - last_press_x_) <= kDialogDoubleClickDistance &&
      std::abs(y - last_press_y_) <= kDialogDoubleClickDistance;
  const auto* hit = control_at(x, y);
  const std::uint16_t hit_id = hit ? hit->id : 0U;
  const bool same = double_click && last_press_id_ == hit_id;
  last_press_ms_ = same ? std::nullopt : std::optional(clock_ms_);
  last_press_x_ = x;
  last_press_y_ = y;
  last_press_id_ = hit_id;
  if (!hit) {
    if (descriptor_.unhandled_pointer) {
      step.signal = descriptor_.unhandled_pointer(event, x, y);
    }
    return step;
  }
  auto& entry = *mutable_control(hit_id);
  switch (entry.kind) {
    case DialogControlKind::push_button:
    case DialogControlKind::default_push_button:
      // BUTTON's WM_LBUTTONDOWN: capture, focus, drawn pressed.
      set_focus(entry.id, false);
      captured_ = entry.id;
      pressed_ = true;
      drag_ = Drag::button;
      step.redraw = true;
      return step;
    case DialogControlKind::edit: {
      set_focus(entry.id, false);
      entry.caret = dialog_edit_hit(font, font_pixels(), entry, x);
      if (!event.shift) entry.anchor = entry.caret;
      captured_ = entry.id;
      drag_ = Drag::edit;
      step.redraw = true;
      return step;
    }
    case DialogControlKind::list_box: {
      if (list_has_scroll_bar(entry) &&
          original_rect_contains(scroll_track(entry), x, y)) {
        set_focus(entry.id, false);
        auto scrolled =
            scroll_bar_down(entry, scroll_track(entry), true, x, y);
        scrolled.redraw = true;
        return scrolled;
      }
      if (same && (entry.style & kOriginalListBoxNotify) != 0U) {
        // WM_LBUTTONDBLCLK (LISTBOX has CS_DBLCLKS): LBN_DBLCLK only.
        step.signal = notify(entry.id, kOriginalListBoxDblClk);
        return step;
      }
      set_focus(entry.id, false);
      const auto rows = dialog_list_rows_rect(entry);
      const int index = entry.top_index + (y - rows.top) / line;
      captured_ = entry.id;
      drag_ = Drag::list;
      if (index < static_cast<int>(entry.items.size())) {
        step = list_select(entry, index, true);
      }
      step.redraw = true;
      return step;
    }
    case DialogControlKind::combo_box:
      set_focus(entry.id, false);
      entry.dropped = true;
      step.redraw = true;
      step.signal = notify(entry.id, kOriginalComboBoxDropDown);
      return step;
    case DialogControlKind::scroll_bar: {
      set_focus(entry.id, false);
      auto scrolled = scroll_bar_down(entry, entry.rect, false, x, y);
      scrolled.redraw = true;
      return scrolled;
    }
    case DialogControlKind::static_text:
    case DialogControlKind::other:
      break;
  }
  return step;
}

DialogStep DialogWindow::pointer(const DialogPointer& event) {
  DialogStep step{};
  const int x = event.x - client_x();
  const int y = event.y - client_y();
  // The procedure's share: a press or release no control takes.
  const auto unhandled = [&]() {
    if (descriptor_.unhandled_pointer) {
      step.signal = descriptor_.unhandled_pointer(event, x, y);
    }
  };
  switch (event.kind) {
    case DialogPointer::Kind::down:
      return pointer_down(event, x, y);
    case DialogPointer::Kind::move: {
      if (scroll_drag_ && scroll_drag_->grab >= 0) {
        auto* bar = mutable_control(scroll_drag_->id);
        if (!bar) break;
        const bool for_list = scroll_drag_->for_list;
        const auto area = for_list ? scroll_track(*bar) : bar->rect;
        const int min = for_list ? 0 : bar->scroll_min;
        const int max =
            for_list ? std::max(0, static_cast<int>(bar->items.size()) -
                                       list_rows(*bar))
                     : bar->scroll_max;
        const auto parts = dialog_scroll_parts(area, min, max, min);
        const int position = scroll_position_at(
            parts, area.right - area.left, min, max, y - scroll_drag_->grab);
        if (position == scroll_drag_->position) break;
        scroll_drag_->position = position;
        step.redraw = true;
        if (for_list) {
          list_scroll_to(*bar, position);
        } else {
          step.signal = scroll_signal(bar->id, kOriginalScrollThumbTrack,
                                      position);
        }
        break;
      }
      if (captured_ == 0U) break;
      auto* entry = mutable_control(captured_);
      if (!entry) break;
      if (drag_ == Drag::button) {
        const bool inside = original_rect_contains(entry->rect, x, y);
        if (inside != pressed_) {
          pressed_ = inside;
          step.redraw = true;
        }
      } else if (drag_ == Drag::edit) {
        auto& font = OriginalCanvasFont::bundled();
        const auto caret = dialog_edit_hit(font, font_pixels(), *entry, x);
        if (caret != entry->caret) {
          entry->caret = caret;
          step.redraw = true;
        }
      } else if (drag_ == Drag::list && !entry->items.empty()) {
        auto& font = OriginalCanvasFont::bundled();
        const int line = dialog_line_height(font, font_pixels());
        const auto rows = dialog_list_rows_rect(*entry);
        const int visible_last = std::min(
            static_cast<int>(entry->items.size()) - 1,
            entry->top_index + list_rows(*entry) - 1);
        // TEMP: no auto-scroll while dragging outside the rows.
        const int row = y < rows.top ? -1 : (y - rows.top) / line;
        const int index = std::clamp(entry->top_index + row,
                                     entry->top_index, visible_last);
        if (index != entry->selection) step = list_select(*entry, index, true);
      }
      break;
    }
    case DialogPointer::Kind::up: {
      if (scroll_drag_) {
        const auto drag = *scroll_drag_;
        scroll_drag_.reset();
        step.redraw = true;
        if (drag.for_list) break;
        if (drag.grab >= 0) {
          step.signal = scroll_signal(drag.id, kOriginalScrollThumbPosition,
                                      drag.position);
          step.then = scroll_signal(drag.id, kOriginalScrollEnd);
        } else {
          step.signal = scroll_signal(drag.id, kOriginalScrollEnd);
        }
        break;
      }
      if (captured_ == 0U) {
        unhandled();
        break;
      }
      const auto* entry = control(captured_);
      const bool inside = entry && original_rect_contains(entry->rect, x, y);
      const auto id = captured_;
      const auto drag = drag_;
      captured_ = 0U;
      pressed_ = false;
      drag_ = Drag::none;
      step.redraw = true;
      // BN_CLICKED: WM_COMMAND(id) only for a release inside the button.
      if (drag == Drag::button && inside) {
        step.signal = notify(id, kOriginalNotifyClicked);
      }
      break;
    }
    case DialogPointer::Kind::right_down:
    case DialogPointer::Kind::right_up:
      unhandled();
      break;
  }
  return step;
}

DialogStep DialogWindow::key(DialogKey key, char character, bool shift) {
  DialogStep step{};
  const auto command = [](std::uint16_t id) {
    return notify(id, kOriginalNotifyClicked);
  };
  // Alt or F10 alone: WM_SYSKEYDOWN, which no dialog procedure here
  // handles (DefDlgProc finds no menu to activate).
  if (key == DialogKey::menu) return step;
  // An open combo box list keeps the keys: the arrows move its selection,
  // Enter, Esc and F4 close it.
  for (auto& entry : controls_) {
    if (!entry.dropped) continue;
    if (key == DialogKey::enter || key == DialogKey::escape ||
        key == DialogKey::f4 || key == DialogKey::tab ||
        key == DialogKey::shift_tab) {
      entry.dropped = false;
      step.redraw = true;
      return step;
    }
    return combo_key(entry, key);
  }
  auto* focused = mutable_control(focus_);
  if (focused && focused->visible && focused->enabled) {
    // DLGC_WANTARROWS / DLGC_WANTCHARS: the focused control keeps these.
    switch (focused->kind) {
      case DialogControlKind::edit:
        switch (key) {
          case DialogKey::left:
          case DialogKey::right:
          case DialogKey::up:
          case DialogKey::down:
          case DialogKey::home:
          case DialogKey::end:
          case DialogKey::backspace:
          case DialogKey::delete_key:
            // Up and Down do nothing in a single-line edit.
            if (key == DialogKey::up || key == DialogKey::down) return step;
            return edit_key(*focused, key, shift);
          case DialogKey::space:
          case DialogKey::other:
            return step;  // typed through character() (WM_CHAR)
          default:
            break;
        }
        break;
      case DialogControlKind::list_box:
        switch (key) {
          case DialogKey::space:
          case DialogKey::other:
            return step;  // first-letter search through character()
          case DialogKey::left:
          case DialogKey::right:
            return step;  // a one-column list ignores them
          case DialogKey::up:
          case DialogKey::down:
          case DialogKey::home:
          case DialogKey::end:
          case DialogKey::page_up:
          case DialogKey::page_down:
            return list_key(*focused, key);
          default:
            break;
        }
        break;
      case DialogControlKind::combo_box:
        if (key == DialogKey::space || key == DialogKey::other) return step;
        if (key == DialogKey::up || key == DialogKey::down ||
            key == DialogKey::left || key == DialogKey::right ||
            key == DialogKey::home || key == DialogKey::end ||
            key == DialogKey::f4) {
          return combo_key(*focused, key);
        }
        break;
      case DialogControlKind::scroll_bar:
        if (key == DialogKey::up || key == DialogKey::down ||
            key == DialogKey::left || key == DialogKey::right ||
            key == DialogKey::home || key == DialogKey::end ||
            key == DialogKey::page_up || key == DialogKey::page_down) {
          return scroll_key(*focused, key);
        }
        break;
      default:
        break;
    }
  }
  // IsDialogMessage consumes Tab, the arrows, Enter and Esc; Space and a
  // letter only when a push button takes them. The rest reaches the
  // procedure as WM_KEYDOWN.
  bool consumed = true;
  switch (key) {
    case DialogKey::tab:
    case DialogKey::shift_tab:
      move_focus(key == DialogKey::tab);
      step.redraw = true;
      break;
    case DialogKey::left:
    case DialogKey::up:
    case DialogKey::right:
    case DialogKey::down:
      move_group_focus(key == DialogKey::right || key == DialogKey::down);
      step.redraw = true;
      break;
    case DialogKey::enter:
      step.signal = command(enter_command());
      break;
    case DialogKey::escape:
      if (descriptor_.escape_command) {
        if (const auto id = descriptor_.escape_command(*this)) {
          step.signal = command(*id);
        }
      } else {
        // IsDialogMessage sends WM_COMMAND(IDCANCEL) whether or not a
        // control 2 exists.
        step.signal = command(kOriginalDialogIdCancel);
      }
      break;
    case DialogKey::space:
      // BUTTON: pressed on the key down, clicked on the key up.
      if (focused && focused->visible && focused->enabled &&
          original_widget_is_push_button(focused->kind)) {
        captured_ = focused->id;
        pressed_ = true;
        space_pressed_ = true;
        drag_ = Drag::none;
        step.redraw = true;
      } else {
        consumed = false;
      }
      break;
    case DialogKey::other:
      // A character key: the visible, enabled push button whose mnemonic
      // it is (DLGC_BUTTON: clicked without Alt).
      consumed = false;
      if (character == '\0') break;
      for (const auto& entry : controls_) {
        if (entry.visible && entry.enabled &&
            original_widget_is_push_button(entry.kind) &&
            dialog_mnemonic(entry.text) == character) {
          set_focus(entry.id, false);
          step.redraw = true;
          step.signal = command(entry.id);
          consumed = true;
          break;
        }
      }
      break;
    default:
      consumed = false;
      break;
  }
  if (!consumed && descriptor_.unhandled_key) {
    step.signal = descriptor_.unhandled_key(key, character);
  }
  return step;
}

DialogStep DialogWindow::key_up(DialogKey key, char character) {
  DialogStep step{};
  if (key == DialogKey::space && space_pressed_) {
    const auto id = captured_;
    const bool inside = pressed_;
    space_pressed_ = false;
    captured_ = 0U;
    pressed_ = false;
    step.redraw = true;
    if (inside && id != 0U) step.signal = notify(id, kOriginalNotifyClicked);
    return step;
  }
  // WM_KEYUP goes to the focus window: the procedure sees it only while the
  // dialog itself has the focus (no control focused).
  if (focus_ == 0U && descriptor_.unhandled_key_up) {
    step.signal = descriptor_.unhandled_key_up(key, character);
  }
  return step;
}

DialogStep DialogWindow::character(char cp1252) {
  auto* focused = mutable_control(focus_);
  if (!focused || !focused->visible || !focused->enabled) return {};
  if (focused->kind == DialogControlKind::edit) {
    return edit_character(*focused, cp1252);
  }
  if ((focused->kind == DialogControlKind::list_box ||
       focused->kind == DialogControlKind::combo_box) &&
      !focused->items.empty() && static_cast<unsigned char>(cp1252) >= 0x20U) {
    // The next item (after the caret, wrapping) starting with the letter.
    const int count = static_cast<int>(focused->items.size());
    const int from = focused->kind == DialogControlKind::combo_box
                         ? focused->selection
                         : focused->caret_index;
    for (int step = 1; step <= count; ++step) {
      const int index = ((from < 0 ? -1 : from) + step + count) % count;
      const auto& item = focused->items[static_cast<std::size_t>(index)];
      if (!item.empty() && lower(item[0]) == lower(cp1252)) {
        return list_select(*focused, index, true);
      }
    }
  }
  return {};
}

DialogStep DialogWindow::timer() {
  DialogStep step{};
  if (descriptor_.timer.id == 0U) return step;
  ++timer_ticks_;
  step.signal = DialogSignal{DialogSignal::Kind::timer, descriptor_.timer.id};
  return step;
}

bool DialogWindow::take_paint() { return std::exchange(paint_pending_, false); }

void DialogWindow::show_item(std::uint16_t id, bool shown) {
  for (auto& entry : controls_) {
    if (entry.id == id) entry.visible = shown;
  }
  invalidated_ = true;
}

void DialogWindow::invalidate() {
  invalidated_ = true;
  paint_pending_ = true;
}

void DialogWindow::resize(int client_width, int client_height) {
  client_width_ = std::max(0, client_width);
  client_height_ = std::max(0, client_height);
  if (desktop_width_ != 0 || desktop_height_ != 0) {
    place(desktop_width_, desktop_height_);
  }
  invalidate();
}

void DialogWindow::enable_item(std::uint16_t id, bool enabled) {
  if (auto* entry = mutable_control(id)) {
    entry->enabled = enabled;
    if (!enabled) entry->dropped = false;
  }
  invalidated_ = true;
}

void DialogWindow::focus_item(std::uint16_t id) {
  if (control(id)) set_focus(id, false);
  invalidated_ = true;
}

void DialogWindow::set_item_text(std::uint16_t id, std::string_view cp1252) {
  if (auto* entry = mutable_control(id)) {
    entry->text = std::string(cp1252);
    // WM_SETTEXT on an edit leaves the caret at the start, nothing
    // selected.
    entry->caret = entry->anchor = 0U;
  }
  invalidated_ = true;
}

std::string DialogWindow::item_text(std::uint16_t id) const {
  const auto* entry = control(id);
  if (!entry) return {};
  // GetDlgItemText into the 11e0:0000 buffer: at most buffer - 1 bytes.
  const auto limit =
      static_cast<std::size_t>(
          original_dialog_item_text_contract().get_buffer_characters) -
      1U;
  return entry->text.substr(0, limit);
}

void DialogWindow::list_reset(std::uint16_t id) {
  if (auto* entry = mutable_control(id)) {
    entry->items.clear();
    entry->selection = -1;
    entry->top_index = entry->caret_index = 0;
  }
  invalidated_ = true;
}

void DialogWindow::list_add(std::uint16_t id, std::string_view cp1252) {
  if (auto* entry = mutable_control(id)) {
    entry->items.emplace_back(cp1252);
  }
  invalidated_ = true;
}

void DialogWindow::list_delete(std::uint16_t id, int index) {
  auto* entry = mutable_control(id);
  if (!entry || index < 0 ||
      index >= static_cast<int>(entry->items.size())) {
    return;
  }
  entry->items.erase(entry->items.begin() + index);
  // Deleting the selected string leaves no selection.
  if (entry->selection == index) {
    entry->selection = -1;
  } else if (entry->selection > index) {
    --entry->selection;
  }
  const int last = static_cast<int>(entry->items.size()) - 1;
  entry->caret_index = std::clamp(entry->caret_index, 0, std::max(0, last));
  list_scroll_to(*entry, entry->top_index);
  invalidated_ = true;
}

int DialogWindow::list_selection(std::uint16_t id) const {
  const auto* entry = control(id);
  return entry ? entry->selection : -1;
}

void DialogWindow::list_set_selection(std::uint16_t id, int index) {
  auto* entry = mutable_control(id);
  if (!entry) return;
  if (index < 0 || index >= static_cast<int>(entry->items.size())) {
    entry->selection = -1;
  } else {
    entry->selection = entry->caret_index = index;
    ensure_visible(*entry);
  }
  invalidated_ = true;
}

void DialogWindow::set_scroll_range(std::uint16_t id, int min, int max) {
  if (auto* entry = mutable_control(id)) {
    entry->scroll_min = min;
    entry->scroll_max = max;
    entry->scroll_pos = std::clamp(entry->scroll_pos, min, std::max(min, max));
  }
  invalidated_ = true;
}

void DialogWindow::set_scroll_pos(std::uint16_t id, int position) {
  if (auto* entry = mutable_control(id)) {
    entry->scroll_pos = std::clamp(position, entry->scroll_min,
                                   std::max(entry->scroll_min,
                                            entry->scroll_max));
  }
  invalidated_ = true;
}

void DialogWindow::update_window() {
  // The window is presented now; the procedure's own WM_PAINT signal still
  // follows when it returns (run_dialog_modal), as no procedure re-enters.
  if (update_window_) update_window_();
}

bool DialogWindow::take_invalidated() {
  return std::exchange(invalidated_, false);
}

void draw_dialog(OriginalWorldRaster& raster, const DialogWindow& dialog,
                 const OriginalResources& resources) {
  auto& font = OriginalCanvasFont::bundled();
  const auto outer = dialog.outer();
  const auto& descriptor = dialog.descriptor();
  if (descriptor.paint_frame) {
    OriginalCanvasDrawContext frame(raster, font, outer.x, outer.y,
                                    {0, 0, outer.width, outer.height});
    descriptor.paint_frame(frame, font, dialog, resources);
  }
  OriginalCanvasDrawContext context(
      raster, font, dialog.client_x(), dialog.client_y(),
      {0, 0, dialog.client_width(), dialog.client_height()});
  if (descriptor.paint_client) {
    descriptor.paint_client(context, font, dialog, resources);
  }
  paint_dialog_controls(context, font, dialog);
}

}  // namespace simtower::sdl
