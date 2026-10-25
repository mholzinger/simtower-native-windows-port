#include "sdl_dialog_descriptors.hpp"

#include "sdl_style.hpp"

#include "original_about.hpp"
#include "original_dialog.hpp"
#include "original_dialog_paint.hpp"
#include "original_dtmp_runtime.hpp"
#include "original_elevator_control.hpp"
#include "original_elevator_control_controller.hpp"
#include "original_information.hpp"
#include "original_startup.hpp"

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
                        const DialogWindow& dialog,
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
                     const DialogWindow& dialog, bool active) {
  const auto outer = dialog.outer();
  const OriginalRect all{0, 0, outer.width, outer.height};
  context.fill_rect(all, kButtonFace);
  draw_dialog_outline(context, all, kBlack);
  draw_dialog_outline(context, original_deflate_rect(all, kBoxFrame - 1),
                      kBlack);
  const auto caption = layout.caption_rect;
  context.fill_rect(caption,
                    active ? style::kCaption : style::kInactiveCaption);
  context.fill_rect(
      {caption.left, caption.bottom - 1, caption.right, caption.bottom},
      kBlack);
  OriginalSavedDrawState saved(context);
  context.set_bk_transparent();
  context.set_text_color(active ? style::kCaptionText
                                : style::kInactiveCaptionText);
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
                      const DialogWindow& dialog) {
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
std::optional<std::uint16_t> message_box_escape(const DialogWindow& dialog) {
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
                        OriginalCanvasFont& font, const DialogWindow& dialog,
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
  context.fill_rect(clip, original_gray(line_style.background));
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

// --- Template + DTMP dialogs (Find, Rename, New Movie, Finance) ---------

// TEMP: the Win3.1 WS_CAPTION frame (sdl_dialog_descriptors.hpp), in outer
// coordinates: border, navy caption with its bottom line, centered title.
void paint_caption_frame(const std::string& caption,
                         OriginalDrawContext& context, OriginalCanvasFont& font,
                         const DialogWindow& dialog, bool active) {
  const auto outer = dialog.outer();
  const OriginalRect all{0, 0, outer.width, outer.height};
  draw_dialog_outline(context, all, kBlack);
  const OriginalRect bar{kCaptionFrameBorder, kCaptionFrameBorder,
                         outer.width - kCaptionFrameBorder,
                         kCaptionFrameBorder + kCaptionFrameHeight};
  context.fill_rect(bar, active ? style::kCaption : style::kInactiveCaption);
  context.fill_rect({bar.left, bar.bottom - 1, bar.right, bar.bottom}, kBlack);
  OriginalSavedDrawState saved(context);
  context.set_bk_transparent();
  context.set_text_color(active ? style::kCaptionText
                                : style::kInactiveCaptionText);
  OriginalSelectedFont selected(context, kMessageBoxFontPixels);
  const int extent = font.extent(kMessageBoxFontPixels, caption);
  const int height = dialog_line_height(font, kMessageBoxFontPixels);
  draw_dialog_text_line(context, font, kMessageBoxFontPixels, caption,
                        bar.left + (bar.right - bar.left - extent) / 2,
                        bar.top + (bar.bottom - 1 - bar.top - height) / 2, bar);
}

// The 1070:0231 bitmaps of a DTMP (the header bitmap and bottom == 0
// rectangles).
void draw_dtmp_bitmaps(OriginalDrawContext& context, const OriginalDtmp& dtmp,
                       const OriginalResources& resources) {
  for (const auto& op : original_dtmp_render_ops(dtmp)) {
    if (op.kind != OriginalDtmpRenderOp::Kind::bitmap) continue;
    context.draw_dib(resources.find("BITMAP", op.resource_id), op.x, op.y);
  }
}

struct DtmpDialogParts {
  std::vector<DialogControl> controls{};
  int client_width{};
  int client_height{};
  DialogFrameInsets frame{};
};

// configure_original_dtmp_window + render_original_dtmp's child pass: the
// window gets the DTMP (or its bitmap's) size, which for a WS_CAPTION
// template includes the TEMP frame; the DTMP rectangles place and show the
// children.
DtmpDialogParts dtmp_dialog_parts(const OriginalResources& resources,
                                  const OriginalDialog& dialog,
                                  const OriginalDtmp& dtmp, int font_pixels) {
  auto& font = OriginalCanvasFont::bundled();
  const auto units = dialog_base_units(font, font_pixels);
  DtmpDialogParts parts{};
  int width = 0;
  int height = 0;
  if (const auto size = original_dtmp_window_size(dtmp, resources);
      size.width != 0) {
    width = size.width;
    height = size.height;
  } else {
    const auto rect =
        map_dialog_rect(units, 0, 0, dialog.width, dialog.height);
    width = rect.right;
    height = rect.bottom;
  }
  if ((dialog.style & kOriginalWindowStyleCaption) ==
      kOriginalWindowStyleCaption) {
    parts.frame = {kCaptionFrameBorder,
                   kCaptionFrameBorder + kCaptionFrameHeight,
                   kCaptionFrameBorder, kCaptionFrameBorder};
  }
  parts.client_width =
      std::max(0, width - parts.frame.left - parts.frame.right);
  parts.client_height =
      std::max(0, height - parts.frame.top - parts.frame.bottom);
  parts.controls = dialog_template_controls(dialog, units);
  apply_dtmp_child_placement(parts.controls, dtmp);
  return parts;
}

// A template + DTMP dialog with the template font, the screen-position
// placement and the modal lock (run_original_modal_dialog); chrome selects
// the 1068:0567 generic painter (Rename, New Movie) before the bitmaps. A
// descriptor that brings its own paint_client (Finance) keeps it.
DialogWindow make_dtmp_dialog(
    const OriginalResources& resources, std::string label,
    OriginalDialog dialog, OriginalDtmp dtmp, DialogDescriptor descriptor,
    bool chrome,
    DialogPlacement placement = DialogPlacement::screen_position) {
  const int font_pixels = original_dialog_font_pixels(dialog.font_point_size);
  auto parts = dtmp_dialog_parts(resources, dialog, dtmp, font_pixels);
  descriptor.label = std::move(label);
  descriptor.placement = placement;
  descriptor.counts_modal_lock = true;
  descriptor.font_pixels = font_pixels;
  descriptor.frame = parts.frame;
  if (parts.frame.top != 0) {
    const auto caption = dialog.caption.kind == OriginalDialogValue::Kind::text
                             ? dialog.caption.text
                             : std::string{};
    descriptor.paint_frame = [caption](OriginalDrawContext& context,
                                       OriginalCanvasFont& font,
                                       const DialogWindow& shown,
                                       const OriginalResources&,
                                       bool active) {
      paint_caption_frame(caption, context, font, shown, active);
    };
  }
  const auto shared = std::make_shared<const OriginalDtmp>(std::move(dtmp));
  if (!descriptor.paint_client) descriptor.paint_client = [shared, chrome](
                                OriginalDrawContext& context,
                                OriginalCanvasFont& /*font*/,
                                const DialogWindow& shown,
                                const OriginalResources& pack) {
    if (chrome) {
      paint_original_dtmp_chrome(
          context, {0, 0, shown.client_width(), shown.client_height()},
          *shared,
          [&](std::uint16_t id) { return shown.control(id) != nullptr; });
    }
    draw_dtmp_bitmaps(context, *shared, pack);
  };
  return DialogWindow(std::move(descriptor), std::move(dialog),
                     std::move(parts.controls), parts.client_width,
                     parts.client_height, 0U);
}

}  // namespace

DialogWindow make_find_dialog(const OriginalResources& resources,
                             OriginalFindMode mode) {
  const auto launcher = original_find_launcher_contract(mode);
  DialogDescriptor descriptor{};
  // WM_CTLCOLORLISTBOX: the white stock brush; buttons and statics NULL.
  descriptor.list_background = style::kWindow;
  descriptor.static_background = style::kButtonFace;
  // original_find_presentation_plan: the positive DTMP renders BITMAP/510
  // and the children, without the generic chrome.
  return make_dtmp_dialog(
      resources,
      mode == OriginalFindMode::person ? kFindPersonDialogLabel
                                       : kFindTenantDialogLabel,
      parse_original_dialog(
          resources.find("DIALOG", launcher.dialog_resource_id)),
      parse_original_dtmp(resources.find("DTMP", launcher.dialog_resource_id)),
      descriptor, false);
}

// Person Rename's SetCapture(dialog) (nm 3941) is not modelled. What is
// known: NAMEPEPLEDIALOGFILTER calls USER.18 SetCapture(hDlg) first thing
// in WM_INITDIALOG (1100:3a6c) and USER.19 ReleaseCapture only in the shared
// close tail (1100:3d00), and its message table (cs:3d4b) handles only
// WM_PAINT, WM_CTLCOLOR, WM_INITDIALOG and WM_COMMAND, so no WM_LBUTTON*
// handler would forward a click. NAMETENANTDIALOGFILTER (732) never takes
// the capture. Open (no reference capture exists): whether Win16 USER then
// keeps clicks on 730's buttons and edit from those children (Win16 sends
// every mouse message to the capture window), which would leave 730
// keyboard-only, and whether Win32 / Wine route them the same way. Until a
// reference decides it, 730 takes clicks like 732.
DialogWindow make_rename_dialog(const OriginalResources& resources,
                               bool person) {
  const auto launcher = original_information_launcher_contract(
      person ? OriginalInformationLauncherKind::person_rename
             : OriginalInformationLauncherKind::tenant_rename);
  DialogDescriptor descriptor{};
  // WM_CTLCOLOREDIT / WM_CTLCOLORSTATIC: the palette-nearest 0xCCCCCC brush.
  descriptor.edit_background = kOriginalDialogStaticBackgroundRgb;
  descriptor.static_background = kOriginalDialogStaticBackgroundRgb;
  return make_dtmp_dialog(
      resources, person ? kRenamePersonDialogLabel : kRenameTenantDialogLabel,
      parse_original_dialog(
          resources.find("DIALOG", launcher.dialog_resource_id)),
      parse_original_dtmp(resources.find("DTMP", launcher.dialog_resource_id)),
      descriptor, true);
}

namespace {

// Presses on no control reach the procedure as WM_LBUTTONDOWN, moves as
// WM_MOUSEMOVE (the Facility and Transport Information filters).
void forward_pointer_signals(DialogDescriptor& descriptor) {
  descriptor.unhandled_pointer =
      [](const DialogPointer& pointer, int x,
         int y) -> std::optional<DialogSignal> {
    if (pointer.kind != DialogPointer::Kind::down) return std::nullopt;
    DialogSignal signal{DialogSignal::Kind::button_down};
    signal.x = x;
    signal.y = y;
    return signal;
  };
  descriptor.mouse_move = [](const DialogPointer& /*pointer*/, int x,
                             int y) -> std::optional<DialogSignal> {
    DialogSignal signal{DialogSignal::Kind::mouse_move};
    signal.x = x;
    signal.y = y;
    return signal;
  };
}

}  // namespace

DialogWindow make_information_dialog(const OriginalResources& resources,
                                     InformationDialogKind kind,
                                     std::uint16_t dialog_id,
                                     const OriginalDtmp& dtmp,
                                     DialogDescriptor::Painter client) {
  DialogDescriptor descriptor{};
  // WM_CTLCOLORSTATIC: the palette-nearest 0xCCCCCC brush (Facility
  // 1100:09ad, Person 1100:020b, Transport 1100:0fde / 1316; DS:31ae);
  // every other subtype NULL_BRUSH, which leaves the buttons' face. The
  // Facility combo's CTLCOLOREDIT / CTLCOLORLISTBOX NULL_BRUSH is not
  // reproduced: the Win32 port leaves them white too.
  descriptor.static_background = kOriginalDialogStaticBackgroundRgb;
  descriptor.paint_client = std::move(client);
  // WM_LBUTTONDOWN and WM_MOUSEMOVE (Facility 1100:0d5a / 0e54, Transport
  // 1100:113a / 13fc and 1071 / 13a9); PEPLEINFODLOGFILTER handles neither.
  if (kind != InformationDialogKind::person) {
    forward_pointer_signals(descriptor);
  }
  return make_dtmp_dialog(
      resources, information_dialog_label(kind, dialog_id),
      parse_original_dialog(resources.find("DIALOG", dialog_id)), dtmp,
      std::move(descriptor), true);
}

DialogWindow make_movie_dialog(const OriginalResources& resources) {
  const auto launcher = original_information_launcher_contract(
      OriginalInformationLauncherKind::movie_choice);
  DialogDescriptor descriptor{};
  const auto style = original_movie_choice_dialog_style();
  descriptor.static_background =
      (static_cast<OriginalRgb>(style.static_red) << 16U) |
      (static_cast<OriginalRgb>(style.static_green) << 8U) | style.static_blue;
  return make_dtmp_dialog(
      resources, kMovieDialogLabel,
      parse_original_dialog(
          resources.find("DIALOG", launcher.dialog_resource_id)),
      parse_original_dtmp(resources.find("DTMP", launcher.dialog_resource_id)),
      descriptor, true);
}

DialogWindow make_startup_dialog(const OriginalResources& resources) {
  // 1018:0000 loads DIALOG/10000; WM_INITDIALOG's 1070:0005 sizes it from
  // DTMP/10000 and 1070:0231 shows the buttons at its rectangles.
  const auto launcher = original_new_load_launcher_contract();
  DialogDescriptor descriptor{};
  // WM_CTLCOLOR returns NULL_BRUSH (1018:0152): the buttons keep their
  // face; there are no statics.
  return make_dtmp_dialog(
      resources, kStartupDialogLabel,
      parse_original_dialog(
          resources.find("DIALOG", launcher.dialog_resource_id)),
      parse_original_dtmp(resources.find("DTMP", launcher.dialog_resource_id)),
      descriptor, true, DialogPlacement::centered);
}

DialogWindow make_finance_dialog(const OriginalResources& resources,
                                OriginalFinanceView view) {
  const auto launcher = original_finance_launcher_contract();
  auto dtmp =
      parse_original_dtmp(resources.find("DTMP", launcher.dialog_resource_id));
  DialogDescriptor descriptor{};
  // COUNTDLOGMAIN has the focus itself (SetFocus(dialog)): its WM_KEYUP and
  // button messages reach it.
  descriptor.unhandled_key_up =
      [](DialogKey key, char character) -> std::optional<DialogSignal> {
    return DialogSignal{DialogSignal::Kind::key_up,
                        dialog_key_virtual_key(key, character)};
  };
  descriptor.unhandled_pointer =
      [](const DialogPointer& pointer, int x,
         int y) -> std::optional<DialogSignal> {
    DialogSignal signal{};
    if (pointer.kind == DialogPointer::Kind::down) {
      signal.kind = DialogSignal::Kind::button_down;
    } else if (pointer.kind == DialogPointer::Kind::up) {
      signal.kind = DialogSignal::Kind::button_up;
    } else {
      return std::nullopt;
    }
    signal.x = x;
    signal.y = y;
    return signal;
  };
  // The procedure's pressed state arrives through paint_state (no
  // controller is captured here).
  descriptor.paint_client =
      [dtmp, view](OriginalDrawContext& context, OriginalCanvasFont& /*font*/,
                   const DialogWindow& shown, const OriginalResources& pack) {
        paint_original_finance(context, pack, dtmp, view,
                               shown.paint_state() != 0U);
      };
  return make_dtmp_dialog(
      resources, kFinanceDialogLabel,
      parse_original_dialog(
          resources.find("DIALOG", launcher.dialog_resource_id)),
      std::move(dtmp), std::move(descriptor), false);
}

DialogWindow make_event_dialog(const OriginalResources& resources,
                              OriginalEventDialogModel model,
                              std::string label) {
  // The template font (8 points after the 1008:0085 patch): the dialog
  // base units and every control's text.
  const int font_pixels =
      original_dialog_font_pixels(model.dialog.font_point_size);
  // configure_original_dtmp_window: the DTMP (or its bitmap) sizes the
  // window; the template has no frame or caption (0x92000042), so that is
  // the client. A DTMP without a size keeps the template's.
  auto parts =
      dtmp_dialog_parts(resources, model.dialog, model.dtmp, font_pixels);

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
                                     const DialogWindow& dialog,
                                     const OriginalResources& resources) {
    paint_event_client(*shared, context, dialog, resources);
  };
  return DialogWindow(std::move(descriptor), shared->dialog,
                     std::move(parts.controls), parts.client_width,
                     parts.client_height, shared->initial_focus);
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

DialogWindow make_message_box(OriginalMessageBoxRequest request,
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
                               const DialogWindow& dialog,
                               const OriginalResources& /*resources*/,
                               bool active) {
    paint_box_frame(*shared_request, *shared_layout, context, font, dialog,
                    active);
  };
  descriptor.paint_client = [shared_layout](
                                OriginalDrawContext& context,
                                OriginalCanvasFont& font,
                                const DialogWindow& dialog,
                                const OriginalResources& /*resources*/) {
    paint_box_client(*shared_layout, context, font, dialog);
  };
  return DialogWindow(std::move(descriptor), std::move(dialog_template),
                     std::move(controls), client_width, client_height,
                     focus);
}

DialogWindow make_about_dialog(OriginalAboutDialogModel model) {
  DialogDescriptor descriptor{};
  descriptor.label = kAboutDialogLabel;
  descriptor.placement = DialogPlacement::centered;
  // The first WM_PAINT's SetTimer(9); it repeats until EndDialog.
  descriptor.timer = {model.timer_id, model.layout.timer_interval_ms, true};
  descriptor.counts_modal_lock = true;
  // ABOUTDLGPROC's WM_KEYDOWN: IsDialogMessage consumes Tab, the arrows,
  // Enter (WM_COMMAND IDOK) and Esc (WM_COMMAND IDCANCEL), which it
  // ignores; every other key reaches it and closes the dialog. Alt and F10
  // are WM_SYSKEYDOWN, which the manager keeps from it.
  descriptor.unhandled_key = [](DialogKey,
                                 char) -> std::optional<DialogSignal> {
    return DialogSignal{DialogSignal::Kind::close, 0U};
  };
  // WM_LBUTTONUP and WM_RBUTTONUP close it; the presses do nothing.
  descriptor.unhandled_pointer =
      [](const DialogPointer& pointer, int,
         int) -> std::optional<DialogSignal> {
    if (pointer.kind == DialogPointer::Kind::up ||
        pointer.kind == DialogPointer::Kind::right_up) {
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
                                     const DialogWindow& dialog,
                                     const OriginalResources& resources) {
    paint_about_client(*shared, context, font, dialog, resources);
  };
  return DialogWindow(std::move(descriptor), shared->dialog, {}, client_width,
                     client_height, 0U);
}

namespace {

// The pointer messages a procedure reads: presses and releases on no
// control (WM_LBUTTONDOWN / WM_LBUTTONUP, WM_RBUTTONUP when right) and
// moves (WM_MOUSEMOVE), in client coordinates.
std::optional<DialogSignal> pointer_signal(const DialogPointer& pointer, int x,
                                           int y, bool left_down,
                                           bool right_up) {
  DialogSignal signal{};
  switch (pointer.kind) {
    case DialogPointer::Kind::down:
      if (!left_down) return std::nullopt;
      signal.kind = DialogSignal::Kind::button_down;
      break;
    case DialogPointer::Kind::up:
      signal.kind = DialogSignal::Kind::button_up;
      break;
    case DialogPointer::Kind::right_up:
      if (!right_up) return std::nullopt;
      signal.kind = DialogSignal::Kind::right_button_up;
      break;
    case DialogPointer::Kind::move:
      signal.kind = DialogSignal::Kind::mouse_move;
      break;
    case DialogPointer::Kind::right_down:
      return std::nullopt;
  }
  signal.x = x;
  signal.y = y;
  return signal;
}

}  // namespace

DialogWindow make_elevator_control_dialog(const OriginalResources& resources,
                                         int requested_left,
                                         std::string caption,
                                         DialogDescriptor::Painter client) {
  auto dialog = parse_original_dialog(
      resources.find("DIALOG", kOriginalElevatorControlDialogId));
  const auto dtmp = parse_original_dtmp(
      resources.find("DTMP", kOriginalElevatorControlDialogId));
  const int font_pixels = original_dialog_font_pixels(dialog.font_point_size);
  auto& font = OriginalCanvasFont::bundled();
  // render_original_dtmp moves and shows scroll bar 7 at DTMP rectangle 7.
  auto controls =
      dialog_template_controls(dialog, dialog_base_units(font, font_pixels));
  apply_dtmp_child_placement(controls, dtmp);

  DialogDescriptor descriptor{};
  descriptor.label = kElevatorControlDialogLabel;
  // 1098:0773: 11e0:0b52(dialog, 8), on the fixed desktop of the panels.
  descriptor.placement = DialogPlacement::fixed_desktop;
  descriptor.requested_left = requested_left;
  descriptor.font_pixels = font_pixels;
  // 0x92c00040: WS_CAPTION without WS_SYSMENU (no close box).
  descriptor.frame = {kCaptionFrameBorder,
                      kCaptionFrameBorder + kCaptionFrameHeight,
                      kCaptionFrameBorder, kCaptionFrameBorder};
  descriptor.paint_frame = [caption = std::move(caption)](
                               OriginalDrawContext& context,
                               OriginalCanvasFont& frame_font,
                               const DialogWindow& shown,
                               const OriginalResources&, bool active) {
    paint_caption_frame(caption, context, frame_font, shown, active);
  };
  descriptor.paint_client = std::move(client);
  // 1098:091e: WHITE_BRUSH for CTLCOLOR_SCROLLBAR; NULL_BRUSH (nothing
  // erased) for the dialog itself, which BITMAP/400 covers.
  if (original_elevator_control_stock_brush(true) ==
      OriginalElevatorControlStockBrush::white) {
    descriptor.scroll_bar_background = 0xFFFFFFU;
  }
  descriptor.unhandled_pointer = [](const DialogPointer& pointer, int x,
                                    int y) {
    return pointer_signal(pointer, x, y, true, false);
  };
  descriptor.mouse_move = [](const DialogPointer& pointer, int x, int y) {
    return pointer_signal(pointer, x, y, true, false);
  };
  return DialogWindow(std::move(descriptor), std::move(dialog),
                     std::move(controls), kOriginalElevatorControlClientWidth,
                     kOriginalElevatorControlClientHeight, 0U);
}

DialogWindow make_elevator_popup_dialog(
    const OriginalResources& resources, int x, int y,
    std::function<std::int16_t()> hover_mode) {
  auto dialog = parse_original_dialog(
      resources.find("DIALOG", kOriginalElevatorPopupDialogId));
  DialogDescriptor descriptor{};
  descriptor.label = kElevatorPopupDialogLabel;
  descriptor.placement = DialogPlacement::at;
  descriptor.origin_x = x;
  descriptor.origin_y = y;
  // run_original_modal_dialog holds DS:24b8.
  descriptor.counts_modal_lock = true;
  descriptor.font_pixels = original_dialog_font_pixels(dialog.font_point_size);
  descriptor.capture_pointer = true;
  descriptor.clip_pointer = true;
  // ELVPOPUP has no WM_LBUTTONDOWN or WM_COMMAND: Enter and Esc do nothing.
  descriptor.escape_command = [](const DialogWindow&) {
    return std::optional<std::uint16_t>{};
  };
  descriptor.unhandled_pointer = [](const DialogPointer& pointer, int px,
                                    int py) {
    return pointer_signal(pointer, px, py, false, true);
  };
  descriptor.mouse_move = [](const DialogPointer& pointer, int px, int py) {
    return pointer_signal(pointer, px, py, false, true);
  };
  // The procedure's hover band (-1: none) through hover_mode, as the
  // panel's painter reads its controller.
  descriptor.paint_client = [hover_mode = std::move(hover_mode)](
                                OriginalDrawContext& context,
                                OriginalCanvasFont&, const DialogWindow&,
                                const OriginalResources& pack) {
    paint_original_elevator_popup(context, pack,
                                  hover_mode ? hover_mode()
                                             : std::int16_t{-1});
  };
  return DialogWindow(std::move(descriptor), std::move(dialog), {},
                     kOriginalElevatorControlPopupWidth,
                     kOriginalElevatorControlPopupHeight, 0U);
}

}  // namespace simtower::sdl
