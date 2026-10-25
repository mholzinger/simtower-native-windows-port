#include "sdl_dialog_descriptors.hpp"

#include "sdl_style.hpp"

#include "original_about.hpp"
#include "original_dialog_paint.hpp"
#include "original_dtmp_runtime.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace simtower::sdl {
namespace {

constexpr OriginalRgb kBlack = style::kBlack;
constexpr OriginalRgb kButtonFace = style::kButtonFace;
// TEMP: the drawn stand-in for the system exclamation icon (file comment).
constexpr OriginalRgb kIconYellow = 0xFFFF00U;

// TEMP message box metrics (USER computes these at run time).
constexpr int kBoxFrame = 4;
constexpr int kBoxCaption = 18;
constexpr int kBoxMargin = 12;
constexpr int kBoxIcon = 32;
constexpr int kBoxButtonHeight = 23;
constexpr int kBoxButtonMinWidth = 72;
constexpr int kBoxButtonGap = 8;
constexpr int kBoxTextMaxWidth = 360;

constexpr OriginalRgb gray(std::uint8_t level) {
  return (static_cast<OriginalRgb>(level) << 16U) |
         (static_cast<OriginalRgb>(level) << 8U) | level;
}

OriginalRect deflate(OriginalRect rect, int by) {
  return {rect.left + by, rect.top + by, rect.right - by, rect.bottom - by};
}

const char* message_box_label(int id) {
  switch (id) {
    case kOriginalAlertResultOk: return "OK";
    case kOriginalAlertResultCancel: return "Cancel";
    case kOriginalAlertResultAbort: return "&Abort";
    case kOriginalAlertResultRetry: return "&Retry";
    case kOriginalAlertResultIgnore: return "&Ignore";
    case kOriginalAlertResultYes: return "&Yes";
    case kOriginalAlertResultNo: return "&No";
    default: return "";
  }
}

// --- Event dialogs ------------------------------------------------------

void paint_event_client(const OriginalEventDialogModel& model,
                        OriginalDrawContext& context,
                        const ModalDialog& dialog,
                        const OriginalResources& resources) {
  // WM_PAINT's 1068:0567 chrome (GetDlgItem + IsWindow: any child, shown
  // or not), then the 1070:0231 bitmaps of WM_INITDIALOG. The manager
  // paints the children (their DTMP placement was applied at build time).
  paint_original_dtmp_chrome(
      context, {0, 0, dialog.client_width(), dialog.client_height()},
      model.dtmp,
      [&](std::uint16_t id) { return dialog.control(id) != nullptr; });
  for (const auto& op : original_dtmp_render_ops(model.dtmp)) {
    if (op.kind != OriginalDtmpRenderOp::Kind::bitmap) continue;
    context.draw_dib(resources.find("BITMAP", op.resource_id), op.x, op.y);
  }
}

// --- Message boxes ------------------------------------------------------

// TEMP: the message box frame and caption (DS_MODALFRAME + WS_CAPTION in
// Win3.1 style, without the system-menu box), in outer coordinates.
void paint_box_frame(const OriginalMessageBoxRequest& request,
                     const MessageBoxLayout& layout,
                     OriginalDrawContext& context, OriginalCanvasFont& font,
                     const ModalDialog& dialog) {
  const auto outer = dialog.outer();
  const OriginalRect all{0, 0, outer.width, outer.height};
  context.fill_rect(all, kButtonFace);
  draw_dialog_outline(context, all, kBlack);
  draw_dialog_outline(context, deflate(all, kBoxFrame - 1), kBlack);
  const auto caption = layout.caption_rect;
  context.fill_rect(caption, style::kCaption);
  context.fill_rect(
      {caption.left, caption.bottom - 1, caption.right, caption.bottom},
      kBlack);
  OriginalSavedDrawState saved(context);
  context.set_bk_transparent();
  context.set_text_color(style::kCaptionText);
  OriginalSelectedFont selected(context, kMessageBoxFontPixels);
  const int extent = font.extent(kMessageBoxFontPixels, request.caption);
  const int height = dialog_line_height(font, kMessageBoxFontPixels);
  draw_dialog_text_line(
      context, font, kMessageBoxFontPixels, request.caption,
      caption.left + (caption.right - caption.left - extent) / 2,
      caption.top + (caption.bottom - 1 - caption.top - height) / 2, caption);
}

void paint_box_client(const MessageBoxLayout& layout,
                      OriginalDrawContext& context, OriginalCanvasFont& font,
                      const ModalDialog& dialog) {
  context.fill_rect({0, 0, dialog.client_width(), dialog.client_height()},
                    style::kWindow);
  const auto icon = layout.icon_rect;
  if (icon.right > icon.left) {
    // TEMP, not the Win3.1 glyph (file comment): a yellow triangle with a
    // black '!'.
    const int center = (icon.left + icon.right) / 2;
    for (int row = 2; row < kBoxIcon - 2; ++row) {
      const int half = 1 + (row - 2) * 15 / (kBoxIcon - 5);
      context.fill_rect({center - half, icon.top + row, center + half,
                         icon.top + row + 1},
                        kIconYellow);
    }
    context.fill_rect({center - 2, icon.top + 10, center + 2, icon.top + 22},
                      kBlack);
    context.fill_rect({center - 2, icon.top + 24, center + 2, icon.top + 28},
                      kBlack);
  }
  OriginalSavedDrawState saved(context);
  context.set_bk_transparent();
  context.set_text_color(kBlack);
  OriginalSelectedFont selected(context, kMessageBoxFontPixels);
  const auto rect = layout.text_rect;
  const int height = dialog_line_height(font, kMessageBoxFontPixels);
  int y = rect.top;
  for (const auto& line : layout.text_lines) {
    draw_dialog_text_line(context, font, kMessageBoxFontPixels, line,
                          rect.left, y, rect);
    y += height;
  }
}

// MessageBox's Esc: IDCANCEL when the box has a Cancel button, IDOK on an
// MB_OK box, nothing otherwise (MB_YESNO, MB_ABORTRETRYIGNORE).
std::optional<std::uint16_t> message_box_escape(const ModalDialog& dialog) {
  if (dialog.control(kOriginalDialogIdCancel)) return kOriginalDialogIdCancel;
  const auto& controls = dialog.controls();
  if (controls.size() == 1U && controls[0].id == kOriginalDialogIdOk) {
    return kOriginalDialogIdOk;
  }
  return std::nullopt;
}

// --- About --------------------------------------------------------------

void paint_about_client(const OriginalAboutDialogModel& model,
                        OriginalDrawContext& context,
                        OriginalCanvasFont& font, const ModalDialog& dialog,
                        const OriginalResources& resources) {
  // paint_original_about_dialog: the chrome with the credits bevel, BITMAP/
  // 257, the RGB(230) credits fill, then the visible TEXT/128 lines centered
  // in the 12-pixel font (DrawText flags 0809: DT_BOTTOM has no effect
  // without DT_SINGLELINE, so each line is top-aligned in its row). The
  // credits position is the number of timer-9 ticks.
  const auto& layout = model.layout;
  paint_original_dtmp_chrome(
      context, {0, 0, dialog.client_width(), dialog.client_height()},
      model.chrome, [](std::uint16_t) { return false; });
  context.draw_dib(resources.find("BITMAP", 257), layout.title_bitmap.left,
                   layout.title_bitmap.top);
  const OriginalRect clip{layout.credits_inner.left, layout.credits_inner.top,
                          layout.credits_inner.right,
                          layout.credits_inner.bottom};
  const auto line_style = original_about_line_style();
  context.fill_rect(clip, gray(line_style.background));
  OriginalSavedDrawState saved(context);
  context.intersect_clip(clip);
  context.set_bk_transparent();
  context.set_text_color(kBlack);
  OriginalSelectedFont selected(context, line_style.font_pixels);
  for (const auto& line : original_about_visible_lines(
           model.lines, dialog.timer_ticks(), clip.bottom - clip.top)) {
    const int extent = font.extent(line_style.font_pixels, line.text);
    const int x = clip.left + (clip.right - clip.left - extent) / 2;
    const int top = clip.top + line.top;
    draw_dialog_text_line(
        context, font, line_style.font_pixels, line.text, x, top,
        {clip.left, std::max(clip.top, top), clip.right,
         std::min(clip.bottom, top + layout.line_height)});
  }
}

}  // namespace

ModalDialog make_event_dialog(const OriginalResources& resources,
                              OriginalEventDialogModel model,
                              std::string label) {
  // The template font (8 points after the 1008:0085 patch): the dialog
  // base units and every control's text.
  const int font_pixels =
      original_dialog_font_pixels(model.dialog.font_point_size);
  auto& font = OriginalCanvasFont::bundled();
  const auto units = dialog_base_units(font, font_pixels);
  // configure_original_dtmp_window: the DTMP (or its bitmap) sizes the
  // window; the template has no frame or caption (0x92000042), so that is
  // the client. A DTMP without a size keeps the template's.
  int client_width = 0;
  int client_height = 0;
  if (const auto size = original_dtmp_window_size(model.dtmp, resources);
      size.width != 0) {
    client_width = size.width;
    client_height = size.height;
  } else {
    const auto rect = map_dialog_rect(units, 0, 0, model.dialog.width,
                                      model.dialog.height);
    client_width = rect.right;
    client_height = rect.bottom;
  }
  auto controls = dialog_template_controls(model.dialog, units);
  apply_dtmp_child_placement(controls, model.dtmp);

  DialogDescriptor descriptor{};
  descriptor.label = std::move(label);
  descriptor.placement = DialogPlacement::screen_position;
  // SetTimer(dialog, 10, 0x93e0); 1068:0380 kills it when it fires.
  descriptor.timer = {model.timer_id, model.timer_ms, false};
  descriptor.counts_modal_lock = true;
  descriptor.font_pixels = font_pixels;
  descriptor.static_background = model.static_background_rgb;
  // Esc: the dialog manager's IDCANCEL (descriptor.escape_command null).
  const auto shared =
      std::make_shared<const OriginalEventDialogModel>(std::move(model));
  descriptor.paint_client = [shared](OriginalDrawContext& context,
                                     OriginalCanvasFont& /*font*/,
                                     const ModalDialog& dialog,
                                     const OriginalResources& resources) {
    paint_event_client(*shared, context, dialog, resources);
  };
  return ModalDialog(std::move(descriptor), shared->dialog,
                     std::move(controls), client_width, client_height,
                     shared->initial_focus);
}

MessageBoxLayout layout_message_box(const OriginalMessageBoxRequest& request) {
  MessageBoxLayout layout{};
  auto& font = OriginalCanvasFont::bundled();
  constexpr int px = kMessageBoxFontPixels;
  const int height = dialog_line_height(font, px);
  const auto buttons = original_message_box_buttons(request.style);
  const bool icon = (request.style & kOriginalAlertIconMask) != 0U;

  layout.text_lines = wrap_dialog_text(font, px, request.text,
                                       kBoxTextMaxWidth);
  int text_width = 0;
  for (const auto& line : layout.text_lines) {
    text_width = std::max(text_width, font.extent(px, line));
  }
  const int text_height =
      static_cast<int>(layout.text_lines.size()) * height;
  int button_width = kBoxButtonMinWidth;
  for (const int id : buttons) {
    button_width = std::max(
        button_width,
        font.extent(px, strip_dialog_mnemonic(message_box_label(id))) + 16);
  }
  const int count = static_cast<int>(buttons.size());
  const int row_width = count * button_width + (count - 1) * kBoxButtonGap;
  const int text_left = icon ? kBoxMargin + kBoxIcon + kBoxMargin : kBoxMargin;
  const int content_height = std::max(text_height, icon ? kBoxIcon : 0);
  const int text_top =
      kBoxMargin + (icon && text_height < kBoxIcon
                        ? (kBoxIcon - text_height) / 2
                        : 0);
  layout.client_width = std::max(
      {text_left + text_width + kBoxMargin, row_width + 2 * kBoxMargin,
       font.extent(px, request.caption) + 2 * kBoxMargin});
  const int button_top = kBoxMargin + content_height + kBoxMargin;
  layout.client_height = button_top + kBoxButtonHeight + kBoxMargin;
  layout.text_rect = {text_left, text_top, text_left + text_width,
                      text_top + text_height};
  if (icon) {
    layout.icon_rect = {kBoxMargin, kBoxMargin, kBoxMargin + kBoxIcon,
                        kBoxMargin + kBoxIcon};
  }
  int left = (layout.client_width - row_width) / 2;
  for (int index = 0; index < count; ++index) {
    DialogControl control{};
    control.id = static_cast<std::uint16_t>(buttons[index]);
    control.kind = index == 0 ? DialogControlKind::default_push_button
                              : DialogControlKind::push_button;
    control.style = kOriginalWindowStyleChild | kOriginalWindowStyleVisible |
                    kOriginalWindowStyleTabStop |
                    (index == 0 ? kOriginalButtonDefPushButton : 0U);
    control.rect = {left, button_top, left + button_width,
                    button_top + kBoxButtonHeight};
    control.text = message_box_label(buttons[index]);
    control.visible = true;
    control.enabled = true;
    control.tab_stop = true;
    layout.buttons.push_back(std::move(control));
    left += button_width + kBoxButtonGap;
  }
  layout.frame = {kBoxFrame, kBoxFrame + kBoxCaption, kBoxFrame, kBoxFrame};
  const int outer_width = layout.client_width + 2 * kBoxFrame;
  layout.caption_rect = {kBoxFrame, kBoxFrame, outer_width - kBoxFrame,
                         kBoxFrame + kBoxCaption};
  return layout;
}

ModalDialog make_message_box(OriginalMessageBoxRequest request,
                             std::string label) {
  auto layout = layout_message_box(request);
  // The template the dialog-manager rules read: the button row as
  // BUTTON items (Enter's default id).
  OriginalDialog dialog_template{};
  for (const auto& button : layout.buttons) {
    OriginalDialogItem item{};
    item.id = button.id;
    item.style = button.style;
    item.window_class.kind = OriginalDialogValue::Kind::ordinal;
    item.window_class.ordinal = kOriginalDialogClassButton;
    item.text.kind = OriginalDialogValue::Kind::text;
    item.text.text = button.text;
    dialog_template.items.push_back(std::move(item));
  }
  const std::uint16_t focus =
      layout.buttons.empty() ? std::uint16_t{0} : layout.buttons.front().id;
  auto controls = layout.buttons;
  const int client_width = layout.client_width;
  const int client_height = layout.client_height;

  DialogDescriptor descriptor{};
  descriptor.label = std::move(label);
  // TEMP: centered (USER places a MessageBox itself).
  descriptor.placement = DialogPlacement::centered;
  descriptor.counts_modal_lock = false;
  descriptor.font_pixels = kMessageBoxFontPixels;
  descriptor.static_background = style::kWindow;
  descriptor.frame = layout.frame;
  descriptor.escape_command = message_box_escape;
  const auto shared_request =
      std::make_shared<const OriginalMessageBoxRequest>(std::move(request));
  const auto shared_layout =
      std::make_shared<const MessageBoxLayout>(std::move(layout));
  descriptor.paint_frame = [shared_request, shared_layout](
                               OriginalDrawContext& context,
                               OriginalCanvasFont& font,
                               const ModalDialog& dialog,
                               const OriginalResources& /*resources*/) {
    paint_box_frame(*shared_request, *shared_layout, context, font, dialog);
  };
  descriptor.paint_client = [shared_layout](
                                OriginalDrawContext& context,
                                OriginalCanvasFont& font,
                                const ModalDialog& dialog,
                                const OriginalResources& /*resources*/) {
    paint_box_client(*shared_layout, context, font, dialog);
  };
  return ModalDialog(std::move(descriptor), std::move(dialog_template),
                     std::move(controls), client_width, client_height,
                     focus);
}

ModalDialog make_about_dialog(OriginalAboutDialogModel model) {
  DialogDescriptor descriptor{};
  descriptor.label = kAboutDialogLabel;
  descriptor.placement = DialogPlacement::centered;
  // The first WM_PAINT's SetTimer(9); it repeats until EndDialog.
  descriptor.timer = {model.timer_id, model.layout.timer_interval_ms, true};
  descriptor.counts_modal_lock = true;
  // ABOUTDLGPROC's WM_KEYDOWN: IsDialogMessage consumes Tab, the arrows,
  // Enter (WM_COMMAND IDOK) and Esc (WM_COMMAND IDCANCEL), which it
  // ignores; every other key reaches it and closes the dialog.
  descriptor.unhandled_key = [](DialogKey,
                                 char) -> std::optional<DialogSignal> {
    return DialogSignal{DialogSignal::Kind::close, 0U};
  };
  // WM_LBUTTONUP and WM_RBUTTONUP close it. TEMP: the session reports no
  // right-button release, so the press stands in.
  descriptor.unhandled_pointer =
      [](const DialogPointer& pointer) -> std::optional<DialogSignal> {
    if (pointer.kind == DialogPointer::Kind::up ||
        pointer.kind == DialogPointer::Kind::right_down) {
      return DialogSignal{DialogSignal::Kind::close, 0U};
    }
    return std::nullopt;
  };
  // WM_INITDIALOG's MoveWindow to the BITMAP/257 layout; TOWER_TITLE
  // (0x90000042) has no frame, caption or children.
  const int client_width = model.layout.window_width;
  const int client_height = model.layout.window_height;
  const auto shared =
      std::make_shared<const OriginalAboutDialogModel>(std::move(model));
  descriptor.paint_client = [shared](OriginalDrawContext& context,
                                     OriginalCanvasFont& font,
                                     const ModalDialog& dialog,
                                     const OriginalResources& resources) {
    paint_about_client(*shared, context, font, dialog, resources);
  };
  return ModalDialog(std::move(descriptor), shared->dialog, {}, client_width,
                     client_height, 0U);
}

}  // namespace simtower::sdl
