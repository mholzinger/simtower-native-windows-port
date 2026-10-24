#include "sdl_dialog.hpp"

#include "sdl_style.hpp"

#include "original_dialog_paint.hpp"
#include "original_draw_canvas.hpp"
#include "original_dtmp_runtime.hpp"

#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace simtower::sdl {
namespace {

constexpr OriginalRgb kBlack = style::kBlack;
constexpr OriginalRgb kWhite = style::kWhite;
constexpr OriginalRgb kButtonFace = style::kButtonFace;
constexpr OriginalRgb kButtonShadow = style::kButtonShadow;

bool contains(const OriginalRect& rect, int x, int y) {
  return x >= rect.left && y >= rect.top && x < rect.right && y < rect.bottom;
}

OriginalRect deflate(OriginalRect rect, int by) {
  return {rect.left + by, rect.top + by, rect.right - by, rect.bottom - by};
}

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
  auto inner = deflate(r, 1);
  if (is_default) {
    draw_dialog_outline(context, inner, kBlack);
    inner = deflate(inner, 1);
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
  const auto& focus = label.focus;
  const auto dot = [&](int px, int py) {
    if (((px + py) & 1) == 0) {
      context.fill_rect({px, py, px + 1, py + 1}, kBlack);
    }
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

void paint_controls(OriginalDrawContext& context, OriginalCanvasFont& font,
                    const ModalDialog& dialog) {
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
      case DialogControlKind::list_box:
      case DialogControlKind::combo_box:
      case DialogControlKind::scroll_bar:
      case DialogControlKind::other:
        // No painter yet (sdl_dialog.hpp, TEMP).
        break;
    }
  }
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
  // The ink can pass the advance sum (a glyph's right side bearing is
  // negative, e.g. the final 'b' of "Find the Bomb"): the focus rectangle
  // surrounds whichever is wider.
  int ink_right = label.extent;
  int pen = 0;
  for (const char character : label.text) {
    const auto& glyph =
        font.glyph(pixel_height, static_cast<unsigned char>(character));
    if (glyph.width > 0) {
      ink_right = std::max(ink_right, pen + glyph.left + glyph.width);
    }
    pen += glyph.advance;
  }
  label.height = dialog_line_height(font, pixel_height);
  const int offset = pressed ? 1 : 0;
  label.x = r.left + (r.right - r.left - label.extent) / 2 + offset;
  label.y = r.top + (r.bottom - r.top - label.height) / 2 + offset;
  // The text cell inflated by one pixel with one blank pixel between the
  // dots and the ink on both sides, kept inside the frame and bevel (two
  // pixels, three for the default button's doubled frame).
  const int bevel = is_default ? 3 : 2;
  label.focus = {std::max(r.left + bevel, label.x - 1),
                 std::max(r.top + bevel, label.y - 1),
                 std::min(r.right - bevel, label.x + ink_right + 2),
                 std::min(r.bottom - bevel, label.y + label.height + 1)};
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

ModalDialog::ModalDialog(DialogDescriptor descriptor,
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

void ModalDialog::place(int desktop_width, int desktop_height) {
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

const DialogControl* ModalDialog::control(std::uint16_t id) const {
  for (const auto& entry : controls_) {
    if (entry.id == id) return &entry;
  }
  return nullptr;
}

const DialogControl* ModalDialog::control_at(int x, int y) const {
  // The first template child is on top of the Z order.
  for (const auto& entry : controls_) {
    if (entry.visible && entry.enabled && contains(entry.rect, x, y)) {
      return &entry;
    }
  }
  return nullptr;
}

std::uint16_t ModalDialog::enter_command() const {
  return original_dialog_enter_command(dialog_, focus_);
}

std::uint16_t ModalDialog::default_button() const {
  const auto id = enter_command();
  const auto* entry = control(id);
  return entry && original_widget_is_push_button(entry->kind) ? id : 0U;
}

std::optional<std::pair<int, int>> ModalDialog::click_point(
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

void ModalDialog::move_focus(bool forward) {
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
  focus_ = stops[static_cast<std::size_t>(index)];
}

DialogStep ModalDialog::pointer(const DialogPointer& event) {
  DialogStep step{};
  const int x = event.x - client_x();
  const int y = event.y - client_y();
  // The procedure's share: a press or release no push button takes.
  const auto unhandled = [&]() {
    if (descriptor_.unhandled_pointer) {
      step.signal = descriptor_.unhandled_pointer(event);
    }
  };
  switch (event.kind) {
    case DialogPointer::Kind::down: {
      // BUTTON's WM_LBUTTONDOWN: capture, focus, drawn pressed. A static
      // without SS_NOTIFY is transparent to the click.
      const auto* hit = control_at(x, y);
      if (!hit || !original_widget_is_push_button(hit->kind)) {
        unhandled();
        break;
      }
      captured_ = hit->id;
      pressed_ = true;
      focus_ = hit->id;
      step.redraw = true;
      break;
    }
    case DialogPointer::Kind::move: {
      if (captured_ == 0U) break;
      const auto* entry = control(captured_);
      const bool inside = entry && contains(entry->rect, x, y);
      if (inside != pressed_) {
        pressed_ = inside;
        step.redraw = true;
      }
      break;
    }
    case DialogPointer::Kind::up: {
      if (captured_ == 0U) {
        unhandled();
        break;
      }
      const auto* entry = control(captured_);
      const bool inside = entry && contains(entry->rect, x, y);
      const auto id = captured_;
      captured_ = 0U;
      pressed_ = false;
      step.redraw = true;
      // BN_CLICKED: WM_COMMAND(id) only for a release inside the button.
      if (inside) {
        step.signal = DialogSignal{DialogSignal::Kind::command, id,
                                   kOriginalNotifyClicked};
      }
      break;
    }
    case DialogPointer::Kind::right_down:
      unhandled();
      break;
  }
  return step;
}

DialogStep ModalDialog::key(DialogKey key, char character) {
  DialogStep step{};
  const auto command = [](std::uint16_t id) {
    return DialogSignal{DialogSignal::Kind::command, id,
                        kOriginalNotifyClicked};
  };
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
      // TEMP: the press and release in one step.
      if (const auto* entry = control(focus_);
          entry && original_widget_is_push_button(entry->kind)) {
        step.signal = command(focus_);
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
          focus_ = entry.id;
          step.redraw = true;
          step.signal = command(entry.id);
          consumed = true;
          break;
        }
      }
      break;
    case DialogKey::arrow:
      break;
  }
  if (!consumed && descriptor_.unhandled_key) {
    step.signal = descriptor_.unhandled_key(key, character);
  }
  return step;
}

DialogStep ModalDialog::timer() {
  DialogStep step{};
  if (descriptor_.timer.id == 0U) return step;
  ++timer_ticks_;
  step.signal = DialogSignal{DialogSignal::Kind::timer, descriptor_.timer.id};
  return step;
}

void ModalDialog::show_item(std::uint16_t id, bool shown) {
  for (auto& entry : controls_) {
    if (entry.id == id) entry.visible = shown;
  }
  invalidated_ = true;
}

void ModalDialog::invalidate() { invalidated_ = true; }

void ModalDialog::resize(int client_width, int client_height) {
  client_width_ = std::max(0, client_width);
  client_height_ = std::max(0, client_height);
  if (desktop_width_ != 0 || desktop_height_ != 0) {
    place(desktop_width_, desktop_height_);
  }
  invalidated_ = true;
}

bool ModalDialog::take_invalidated() {
  return std::exchange(invalidated_, false);
}

void draw_dialog(OriginalWorldRaster& raster, const ModalDialog& dialog,
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
  paint_controls(context, font, dialog);
}

}  // namespace simtower::sdl
