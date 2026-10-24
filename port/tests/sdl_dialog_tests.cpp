// The SDL host's portable dialog manager and its descriptors
// (sdl/sdl_dialog.hpp, sdl/sdl_dialog_descriptors.hpp; no SDL in them):
// a hand-made descriptor (hooks, Esc rule, timer, ops), dialog-unit layout
// against the templates, focus, the default button, Enter/Esc/Tab/
// mnemonics and their underline, pointer capture, timers, the answer of
// every event dialog through the shared AHOTTADLOGFILTER controller, the
// alert boxes (including the STRL/1001 three-button confirmation), About,
// and pinned frame hashes of five dialogs. The synthetic half needs no
// assets; the resource half reads the pack at run time and the test exits
// 77 (skipped) without it. No window is opened.

#include "sdl_dialog.hpp"
#include "sdl_dialog_descriptors.hpp"

#include "original_alert.hpp"
#include "original_canvas_font.hpp"
#include "original_dialog_model.hpp"
#include "original_dialog_paint.hpp"
#include "original_dtmp_runtime.hpp"
#include "original_resources.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using simtower::OriginalDialogOutcome;
using simtower::OriginalDialogSignal;
using simtower::OriginalRect;
using simtower::sdl::DialogControlKind;
using simtower::sdl::DialogKey;
using simtower::sdl::DialogPointer;
using simtower::sdl::DialogStep;
using simtower::sdl::ModalDialog;
using Kind = OriginalDialogSignal::Kind;

// frame_hash_tests pins (see there).
constexpr std::uint64_t kPinned3012 = 0x7e2856a39260c257ULL;
constexpr std::uint64_t kPinned3020 = 0xa7ef9191e9b37d29ULL;
constexpr std::uint64_t kPinned3030 = 0x1a417dd660c54068ULL;
constexpr std::uint64_t kPinned1001 = 0x057f9392f4e5c7b5ULL;
constexpr std::uint64_t kPinnedAbout = 0x9d3405fefe1d2751ULL;

constexpr std::array<std::uint16_t, 20> kEventDialogs{
    3000, 3001, 3002, 3003, 3010, 3011, 3012, 3013, 3014, 3020,
    3021, 3022, 3023, 3024, 3030, 3031, 3032, 3033, 3034, 3040};

std::optional<std::vector<std::byte>> read_file(
    const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::nullopt;
  const std::vector<char> chars((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(chars.size());
  for (std::size_t index = 0; index < chars.size(); ++index) {
    bytes[index] =
        static_cast<std::byte>(static_cast<unsigned char>(chars[index]));
  }
  return bytes;
}

std::optional<OriginalDialogSignal> command(std::uint16_t id) {
  return OriginalDialogSignal{Kind::command, id};
}

// Press and release at a control's script click point.
DialogStep click(ModalDialog& dialog, std::uint16_t id) {
  const auto point = dialog.click_point(id);
  assert(point);
  const auto down =
      dialog.pointer({DialogPointer::Kind::down, point->first, point->second});
  assert(!down.signal);
  return dialog.pointer(
      {DialogPointer::Kind::up, point->first, point->second});
}

std::vector<std::uint16_t> tab_stops(const ModalDialog& dialog) {
  std::vector<std::uint16_t> stops;
  for (const auto& control : dialog.controls()) {
    if (control.visible && control.enabled && control.tab_stop) {
      stops.push_back(control.id);
    }
  }
  return stops;
}

// Tab walks the stops in template order and wraps; Shift-Tab reverses.
void check_tab_cycle(ModalDialog& dialog) {
  const auto stops = tab_stops(dialog);
  if (stops.empty()) return;
  const auto start = dialog.focus();
  auto at = std::ranges::find(stops, start);
  assert(at != stops.end());
  for (std::size_t step = 1; step <= stops.size(); ++step) {
    assert(dialog.key(DialogKey::tab).redraw);
    const auto expected =
        stops[(static_cast<std::size_t>(at - stops.begin()) + step) %
              stops.size()];
    assert(dialog.focus() == expected);
  }
  assert(dialog.focus() == start);
  (void)dialog.key(DialogKey::shift_tab);
  assert(dialog.focus() ==
         stops[(static_cast<std::size_t>(at - stops.begin()) +
                stops.size() - 1) %
               stops.size()]);
  (void)dialog.key(DialogKey::tab);
  assert(dialog.focus() == start);
}

// The focus rectangle surrounds the label with a blank pixel when the
// button is wide enough, and never leaves the bevel.
void check_label(const ModalDialog& dialog,
                 const simtower::sdl::DialogControl& control) {
  auto& font = simtower::OriginalCanvasFont::bundled();
  const bool is_default = dialog.default_button() == control.id;
  const auto label = simtower::sdl::push_button_label(
      font, control, dialog.font_pixels(), false, is_default);
  const auto& r = control.rect;
  const int bevel = is_default ? 3 : 2;
  assert(label.focus.left >= r.left + bevel &&
         label.focus.right <= r.right - bevel);
  assert(label.text.find('&') == std::string::npos ||
         control.text.find("&&") != std::string::npos);
  if (label.extent + 2 * bevel + 4 <= r.right - r.left) {
    assert(label.focus.left < label.x);
    assert(label.focus.right > label.x + label.extent);
  }
}

// Every pixel outside the dialog's outer rectangle is left alone; some
// inside change.
void check_draw(const ModalDialog& dialog,
                const simtower::OriginalResources& resources) {
  simtower::OriginalWorldRaster raster{};
  raster.width = 1100;
  raster.height = 700;
  constexpr std::uint32_t kSentinel = 0x00123456U;
  raster.pixels.assign(
      static_cast<std::size_t>(raster.width) * raster.height, kSentinel);
  simtower::sdl::draw_dialog(raster, dialog, resources);
  const auto outer = dialog.outer();
  bool changed = false;
  for (int y = 0; y < raster.height; ++y) {
    for (int x = 0; x < raster.width; ++x) {
      const auto pixel =
          raster.pixels[static_cast<std::size_t>(y) * raster.width + x];
      if (!outer.contains(x, y)) {
        assert(pixel == kSentinel);
      } else if (pixel != kSentinel) {
        changed = true;
      }
    }
  }
  assert(changed);
}

// FNV-1a over the dialog's outer rectangle of a 1019x628 window raster
// (0x00RRGGBB pixels, little-endian bytes), drawn at its placement.
std::uint64_t dialog_hash(const ModalDialog& dialog,
                          const simtower::OriginalResources& resources) {
  simtower::OriginalWorldRaster raster{};
  raster.width = 1019;
  raster.height = 628;
  raster.pixels.assign(
      static_cast<std::size_t>(raster.width) * raster.height, 0x00123456U);
  simtower::sdl::draw_dialog(raster, dialog, resources);
  std::uint64_t hash = 0xcbf29ce484222325ULL;
  const auto outer = dialog.outer();
  for (int y = outer.y; y < outer.y + outer.height; ++y) {
    for (int x = outer.x; x < outer.x + outer.width; ++x) {
      const auto pixel =
          raster.pixels[static_cast<std::size_t>(y) * raster.width + x];
      for (int shift = 0; shift < 32; shift += 8) {
        hash ^= (pixel >> shift) & 0xFFU;
        hash *= 0x100000001b3ULL;
      }
    }
  }
  return hash;
}

// Prints every mismatch (so one run shows all of them); the caller
// asserts.
bool check_hash(const char* name, std::uint64_t actual,
                std::uint64_t expected) {
  if (actual != expected) {
    std::printf("%s: frame hash 0x%016llxULL, pinned 0x%016llxULL\n", name,
                static_cast<unsigned long long>(actual),
                static_cast<unsigned long long>(expected));
  }
  return actual == expected;
}

// The '&' mnemonic of every MessageBox push button is underlined in the
// text colour one pixel below the baseline, under that character only;
// buttons without a mnemonic ("Cancel", "OK") have none.
void check_mnemonic_underlines(const ModalDialog& dialog,
                               const simtower::OriginalResources& resources) {
  auto& font = simtower::OriginalCanvasFont::bundled();
  simtower::OriginalWorldRaster raster{};
  raster.width = 1019;
  raster.height = 628;
  constexpr std::uint32_t kSentinel = 0x00123456U;
  raster.pixels.assign(
      static_cast<std::size_t>(raster.width) * raster.height, kSentinel);
  simtower::sdl::draw_dialog(raster, dialog, resources);
  const auto at = [&](int x, int y) {
    return raster.pixels[static_cast<std::size_t>(y + dialog.client_y()) *
                             raster.width +
                         x + dialog.client_x()];
  };
  int underlined = 0;
  for (const auto& control : dialog.controls()) {
    const bool is_default = dialog.default_button() == control.id;
    const auto label = simtower::sdl::push_button_label(
        font, control, dialog.font_pixels(), false, is_default);
    const auto mnemonic = simtower::sdl::dialog_mnemonic(control.text);
    if (mnemonic == '\0') {
      assert(label.underline.right <= label.underline.left);
      continue;
    }
    ++underlined;
    const auto& u = label.underline;
    // One row, as wide as the mnemonic character's advance, starting at
    // the character's pen position (the first character here).
    assert(u.bottom == u.top + 1 && u.left == label.x);
    const auto first = static_cast<unsigned char>(label.text[0]);
    assert(u.right - u.left == font.glyph(dialog.font_pixels(), first).advance);
    assert(u.top == label.y + font.metrics(dialog.font_pixels()).ascent + 1);
    for (int x = u.left; x < u.right; ++x) assert(at(x, u.top) == 0U);
    // Nothing else of the label on that row: the rest of the text has no
    // descender there ("es", "o").
    for (int x = u.right + 1; x < label.x + label.extent; ++x) {
      assert(at(x, u.top) != 0U);
    }
  }
  assert(underlined == 2);  // &Yes, &No
}

// The manager is generic: a dialog built from a hand-made descriptor gets
// its placement, timer, Esc rule, unhandled-input hooks and the ops.
void descriptor_tests() {
  using simtower::sdl::DialogDescriptor;
  using simtower::sdl::DialogControl;
  DialogDescriptor descriptor{};
  descriptor.label = "custom";
  descriptor.placement = simtower::sdl::DialogPlacement::centered;
  descriptor.timer = {7U, 250U, true};
  descriptor.font_pixels = 11;
  descriptor.frame = {2, 10, 2, 2};
  int escapes = 0;
  descriptor.escape_command =
      [&](const ModalDialog&) -> std::optional<std::uint16_t> {
    ++escapes;
    return 42U;
  };
  descriptor.unhandled_key = [](DialogKey key, char character)
      -> std::optional<OriginalDialogSignal> {
    if (key == DialogKey::other && character == 'q') {
      return OriginalDialogSignal{Kind::close, 0U};
    }
    return std::nullopt;
  };
  DialogControl button{};
  button.id = 5U;
  button.kind = DialogControlKind::push_button;
  button.rect = {10, 10, 60, 30};
  button.text = "&Go";
  button.visible = button.enabled = button.tab_stop = true;
  DialogControl hidden = button;
  hidden.id = 6U;
  hidden.rect = {70, 10, 120, 30};
  hidden.text = "Later";
  hidden.visible = false;
  simtower::OriginalDialog dialog_template{};
  ModalDialog dialog(descriptor, dialog_template, {button, hidden}, 200, 100,
                     5U);
  dialog.place(1000, 500);
  assert(dialog.label() == "custom" && !dialog.counts_modal_lock());
  assert(dialog.outer_width() == 204 && dialog.outer_height() == 112);
  assert(dialog.outer().x == (1000 - 204) / 2 &&
         dialog.client_x() == dialog.outer().x + 2 &&
         dialog.client_y() == dialog.outer().y + 10);
  assert(dialog.timer_id() == 7U && dialog.timer_repeats());
  assert(dialog.timer().signal == OriginalDialogSignal({Kind::timer, 7U}));
  assert(dialog.timer_ticks() == 1U);
  assert(dialog.key(DialogKey::escape).signal == command(42U) &&
         escapes == 1);
  assert(dialog.key(DialogKey::other, 'g').signal == command(5U));
  assert(dialog.key(DialogKey::other, 'q').signal ==
         OriginalDialogSignal({Kind::close, 0U}));
  assert(!dialog.key(DialogKey::other, 'z').signal);
  // No unhandled_pointer hook: a click on nothing does nothing.
  assert(!dialog.pointer({DialogPointer::Kind::up, 0, 0}).signal);
  // Ops: show_item reveals a control (now clickable), resize re-places,
  // each asks for a repaint once.
  assert(!dialog.click_point(6U) && !dialog.take_invalidated());
  dialog.show_item(6U, true);
  assert(dialog.take_invalidated() && !dialog.take_invalidated());
  assert(dialog.click_point(6U));
  dialog.resize(300, 120);
  assert(dialog.take_invalidated());
  assert(dialog.client_width() == 300 &&
         dialog.outer().x == (1000 - 304) / 2);
  dialog.invalidate();
  assert(dialog.take_invalidated());
  // A message box hides nothing behind its own hooks either: it is a
  // descriptor too (Esc rule, frame, no lock, no timer).
  auto box = simtower::sdl::make_message_box(
      simtower::original_invalid_filename_message_box(), "box");
  assert(box.descriptor().escape_command && box.descriptor().paint_frame &&
         !box.descriptor().unhandled_key && box.timer_id() == 0U);
  assert(!box.timer().signal && box.timer_ticks() == 0U);
}

void synthetic_tests() {
  // MapDialogRect: MulDiv rounding (half away from zero).
  const simtower::sdl::DialogBaseUnits units{6, 13};
  assert(simtower::sdl::map_dialog_rect(units, 4, 8, 40, 14) ==
         OriginalRect({6, 13, 66, 36}));
  assert(simtower::sdl::map_dialog_rect(units, 1, 1, 1, 1) ==
         OriginalRect({2, 2, 4, 4}));
  auto& font = simtower::OriginalCanvasFont::bundled();
  const auto base = simtower::sdl::dialog_base_units(font, 11);
  const auto metrics = font.metrics(11);
  assert(base.x >= 4 && base.x <= 7);
  assert(base.y == metrics.ascent + metrics.descent);

  // DrawText(DT_WORDBREAK) breaking.
  using Lines = std::vector<std::string>;
  const int narrow = font.extent(11, "aaaa");
  assert(simtower::sdl::wrap_dialog_text(font, 11, "aaa bbb", narrow) ==
         Lines({"aaa", "bbb"}));
  assert(simtower::sdl::wrap_dialog_text(font, 11, "a\r\nb", 1000) ==
         Lines({"a", "b"}));
  assert(simtower::sdl::wrap_dialog_text(font, 11, "x y", 1000) ==
         Lines({"x y"}));

  // Mnemonics.
  assert(simtower::sdl::dialog_mnemonic("&Yes") == 'y');
  assert(simtower::sdl::dialog_mnemonic("Find the &Bomb") == 'b');
  assert(simtower::sdl::dialog_mnemonic("A&&B") == '\0');
  assert(simtower::sdl::dialog_mnemonic("OK") == '\0');

  assert(simtower::sdl::strip_dialog_mnemonic("&Yes") == "Yes");
  assert(simtower::sdl::strip_dialog_mnemonic("A&&B") == "A&B");

  descriptor_tests();

  // Labels.
  assert(simtower::sdl::event_dialog_label(3020) == "event 3020");
  assert(simtower::sdl::alert_dialog_label(1001) == "alert 1001");

  // A Yes/No/Cancel box (MB_DEFBUTTON1).
  const simtower::OriginalMessageBoxRequest request{
      "SimTower", "Save?", 0x1033U, 3U, false};
  auto box = simtower::sdl::make_message_box(request, "alert 1001");
  box.place(1019, 628);
  assert(box.label() == "alert 1001");
  assert(!box.counts_modal_lock());
  assert(box.controls().size() == 3U);
  assert(box.controls()[0].id == 6U && box.controls()[1].id == 7U &&
         box.controls()[2].id == 2U);
  assert(box.focus() == 6U && box.default_button() == 6U);
  assert(box.key(DialogKey::enter).signal == command(6));
  assert(box.key(DialogKey::escape).signal == command(2));
  assert(box.key(DialogKey::other, 'n').signal == command(7));
  assert(box.focus() == 7U && box.default_button() == 7U);
  assert(box.key(DialogKey::other, 'y').signal == command(6));
  assert(!box.key(DialogKey::other, 'q').signal);
  assert(!box.key(DialogKey::arrow).signal);
  check_tab_cycle(box);
  assert(box.key(DialogKey::space).signal == command(box.focus()));
  assert(box.timer_id() == 0U && box.timer_interval_ms() == 0U);
  // Message box placement: centered (TEMP), inside the desktop.
  const auto outer = box.outer();
  assert(outer.x == (1019 - outer.width) / 2 &&
         outer.y == (628 - outer.height) / 2);
  assert(box.client_x() > outer.x && box.client_y() > outer.y);

  // Pointer: a release inside the pressed button is BN_CLICKED; leaving it
  // draws it raised, and a release outside sends nothing.
  {
    const auto step = click(box, 7);
    assert(step.signal == command(7) && step.redraw);
    const auto point = *box.click_point(2);
    auto down = box.pointer({DialogPointer::Kind::down, point.first,
                             point.second});
    assert(down.redraw && box.captured() == 2U && box.pressed() &&
           box.focus() == 2U);
    auto away = box.pointer({DialogPointer::Kind::move, 0, 0});
    assert(away.redraw && !box.pressed());
    auto back = box.pointer({DialogPointer::Kind::move, point.first,
                             point.second});
    assert(back.redraw && box.pressed());
    (void)box.pointer({DialogPointer::Kind::move, 0, 0});
    const auto up = box.pointer({DialogPointer::Kind::up, 0, 0});
    assert(!up.signal && box.captured() == 0U);
    // A press outside every button does nothing.
    assert(!box.pointer({DialogPointer::Kind::down, 0, 0}).redraw);
  }
  // Script click points exist for push buttons only.
  assert(box.click_point(6) && !box.click_point(99) && !box.click_point(0));
  for (const auto& control : box.controls()) check_label(box, control);

  // MB_OK: Esc answers IDOK. MB_YESNO: Esc does nothing.
  auto ok = simtower::sdl::make_message_box(
      simtower::original_invalid_filename_message_box(),
      simtower::sdl::kInvalidFilenameBoxLabel);
  assert(ok.key(DialogKey::escape).signal == command(1));
  auto yes_no = simtower::sdl::make_message_box(
      simtower::original_replace_existing_message_box(),
      simtower::sdl::kReplaceBoxLabel);
  assert(!yes_no.key(DialogKey::escape).signal);
  assert(yes_no.key(DialogKey::other, 'n').signal == command(7));
  const auto yes_no_layout = simtower::sdl::layout_message_box(
      simtower::original_replace_existing_message_box());
  assert(yes_no_layout.icon_rect.right > yes_no_layout.icon_rect.left);
}

void event_tests(const simtower::OriginalResources& resources) {
  auto& font = simtower::OriginalCanvasFont::bundled();
  for (const auto id : kEventDialogs) {
    for (const std::int32_t argument : {0, -3000, 150000}) {
      const simtower::OriginalEventDialogRequest request{id, argument, 0};
      auto model = simtower::prepare_original_event_dialog(resources, request);
      const auto copy = model;
      auto dialog = simtower::sdl::make_event_dialog(
          resources, std::move(model),
          simtower::sdl::event_dialog_label(id));
      dialog.place(1019, 628);
      assert(dialog.label() == "event " + std::to_string(id));
      // The template's 8-point font (owner decision: statics too).
      assert(dialog.font_pixels() == 11);
      // The DTMP sizes the client; there is no frame or caption.
      const auto size = simtower::original_dtmp_window_size(copy.dtmp,
                                                            resources);
      if (size.width != 0) {
        assert(dialog.client_width() == size.width &&
               dialog.client_height() == size.height);
      }
      assert(dialog.outer().width == dialog.client_width());
      // 11e0:0b52 placement inside the desktop.
      const auto position = simtower::original_dialog_screen_position(
          {0, 0, 1019, 628},
          {0, 0, dialog.client_width(), dialog.client_height()}, 0);
      assert(dialog.outer().x == position.left &&
             dialog.outer().y == position.top);
      // Controls: template order, MapDialogRect geometry unless the DTMP
      // render pass moved (and showed) them.
      const auto units = simtower::sdl::dialog_base_units(font, 11);
      const auto ops = simtower::original_dtmp_render_ops(copy.dtmp);
      assert(dialog.controls().size() == copy.dialog.items.size());
      for (std::size_t index = 0; index < copy.dialog.items.size();
           ++index) {
        const auto& item = copy.dialog.items[index];
        const auto& control = dialog.controls()[index];
        assert(control.id == item.id && control.style == item.style);
        const auto placed = std::ranges::find_if(ops, [&](const auto& op) {
          return op.kind ==
                     simtower::OriginalDtmpRenderOp::Kind::place_child &&
                 op.child_id == item.id;
        });
        if (placed != ops.end()) {
          assert(control.rect == placed->rect && control.visible);
        } else {
          assert(control.rect ==
                 simtower::sdl::map_dialog_rect(units, item.x, item.y,
                                                item.width, item.height));
          assert(control.visible ==
                 ((item.style & simtower::kOriginalWindowStyleVisible) !=
                  0U));
        }
        assert(control.kind ==
               simtower::original_dialog_item_widget_kind(item));
        assert(simtower::original_widget_is_push_button(control.kind) ==
               simtower::original_dialog_item_is_push_button(item));
        if (simtower::original_widget_is_push_button(control.kind)) {
          check_label(dialog, control);
        }
      }
      // Focus and the default button (Enter).
      assert(dialog.focus() == copy.initial_focus);
      assert(dialog.enter_command() ==
             simtower::original_dialog_enter_command(copy.dialog,
                                                     dialog.focus()));
      assert(dialog.key(DialogKey::enter).signal ==
             command(dialog.enter_command()));
      // Esc: IDCANCEL = 2 whether or not a control 2 exists.
      assert(dialog.key(DialogKey::escape).signal == command(2));
      check_tab_cycle(dialog);
      // Timer 10, one-shot, 0x93e0 ms.
      assert(dialog.timer_id() == 10U &&
             dialog.timer_interval_ms() == 0x93e0U &&
             !dialog.timer_repeats());
      assert(dialog.timer().signal ==
             OriginalDialogSignal({Kind::timer, 10U}));
      // Every visible, enabled push button answers through the controller.
      for (const auto& control : dialog.controls()) {
        if (!simtower::original_widget_is_push_button(control.kind) ||
            !control.visible || !control.enabled) {
          continue;
        }
        auto fresh =
            simtower::sdl::make_event_dialog(resources, copy, dialog.label());
        fresh.place(1019, 628);
        const auto step = click(fresh, control.id);
        assert(step.signal == command(control.id));
        for (const std::int32_t balance : {0, 2999, 1000000}) {
          simtower::OriginalEventDialogController controller(argument);
          const auto outcome = controller.signal(*step.signal, balance);
          if (control.id == 1U) {
            assert(outcome == OriginalDialogOutcome({1, false}));
          } else if (control.id == 2U) {
            const bool short_of_money =
                argument < 0 && balance + argument < 0;
            assert(outcome == (short_of_money
                                   ? OriginalDialogOutcome({1, true})
                                   : OriginalDialogOutcome({2, false})));
          } else {
            assert(!outcome.result);
          }
        }
      }
      // A static does not take a click.
      for (const auto& control : dialog.controls()) {
        if (control.kind == DialogControlKind::static_text) {
          assert(!dialog.click_point(control.id));
        }
      }
      check_draw(dialog, resources);
    }
  }

  // The dialogs the owner decisions name.
  {
    const auto model =
        simtower::prepare_original_event_dialog(resources, {3020, -3000, 0});
    auto bomb =
        simtower::sdl::make_event_dialog(resources, model, "event 3020");
    assert(bomb.focus() == 1U);  // Find the Bomb
    // Esc pays the ransom (IDCANCEL = 2 = Pay Them), faithfully.
    simtower::OriginalEventDialogController controller(-3000);
    assert(controller.signal(*bomb.key(DialogKey::escape).signal, 1000000) ==
           OriginalDialogOutcome({2, false}));
    // The timer pays too, silently declining when short of money.
    simtower::OriginalEventDialogController timed(-3000);
    assert(timed.signal(*bomb.timer().signal, 100) ==
           OriginalDialogOutcome({1, false}));
  }
  {
    const auto model =
        simtower::prepare_original_event_dialog(resources, {3012, -150000, 0});
    auto fire =
        simtower::sdl::make_event_dialog(resources, model, "event 3012");
    assert(fire.focus() == 2U);  // Yes
    // Esc hires the crew (2 = Yes).
    assert(fire.key(DialogKey::escape).signal == command(2));
  }
}

void alert_tests(const simtower::OriginalResources& resources) {
  // STRL/1001 confirmation: ALRT/1001 formatted with the file title and
  // action, Yes / No / Cancel, normalized by 1208:0369.
  const std::array<std::string_view, 4> subs{"untitled", "closing", {}, {}};
  const auto presentation =
      simtower::prepare_original_alert(resources, 1001, subs);
  auto box = simtower::sdl::make_message_box(
      simtower::build_original_message_box(presentation), "alert 1001");
  box.place(1019, 628);
  const auto request = simtower::build_original_message_box(presentation);
  assert(request.text == "Save the tower \"untitled\" before closing?");
  assert(request.caption == "SimTower");
  const auto layout = simtower::sdl::layout_message_box(request);
  std::string joined;
  for (const auto& line : layout.text_lines) joined += line + " ";
  assert(joined.find("Save the tower") != std::string::npos);
  assert(joined.find('^') == std::string::npos);
  assert(layout.icon_rect.right > layout.icon_rect.left);
  check_mnemonic_underlines(box, resources);
  const auto mode = presentation.button_mode;
  assert(simtower::original_alert_result(
             mode, box.key(DialogKey::enter).signal->id) == 1);
  assert(simtower::original_alert_result(
             mode, box.key(DialogKey::other, 'n').signal->id) == 2);
  assert(simtower::original_alert_result(
             mode, box.key(DialogKey::escape).signal->id) == 3);
  assert(simtower::original_alert_result(mode, click(box, 2).signal->id) ==
         3);
  check_draw(box, resources);

  // Every ALRT the game shows formats without a leftover caret and gets
  // its button row.
  for (const int id : {1000, 1001, 1002, 1003, 1004, 1005}) {
    const auto alert =
        simtower::prepare_original_alert(resources, id, {"x", "y", {}, {}});
    assert(alert.message.find('^') == std::string::npos);
    auto dialog = simtower::sdl::make_message_box(
        simtower::build_original_message_box(alert),
        simtower::sdl::alert_dialog_label(id));
    dialog.place(1019, 628);
    assert(dialog.controls().size() ==
           simtower::original_message_box_buttons(alert.style).size());
    assert(dialog.key(DialogKey::enter).signal ==
           command(static_cast<std::uint16_t>(
               simtower::original_message_box_buttons(alert.style)[0])));
    check_draw(dialog, resources);
  }
}

void about_tests(const simtower::OriginalResources& resources) {
  auto about =
      simtower::sdl::make_about_dialog(
          simtower::prepare_original_about_dialog(resources));
  about.place(1019, 628);
  assert(about.label() == "about");
  assert(about.client_width() == 604 && about.client_height() == 290);
  assert(about.controls().empty());
  assert(about.timer_id() == 9U && about.timer_repeats() &&
         about.timer_interval_ms() != 0U);
  // Enter / Esc reach ABOUTDLGPROC as WM_COMMAND, which it ignores.
  assert(simtower::original_about_dialog_signal(
             *about.key(DialogKey::enter).signal, about) ==
         OriginalDialogOutcome{});
  assert(simtower::original_about_dialog_signal(
             *about.key(DialogKey::escape).signal, about) ==
         OriginalDialogOutcome{});
  assert(!about.key(DialogKey::tab).signal);
  // Any other key, and a button release, closes it with 1.
  for (const auto key : {DialogKey::other, DialogKey::space}) {
    assert(simtower::original_about_dialog_signal(*about.key(key).signal,
                                                  about) ==
           OriginalDialogOutcome({1, false}));
  }
  assert(about.click_point(0));
  assert(!about.pointer({DialogPointer::Kind::down, 10, 10}).signal);
  assert(about.pointer({DialogPointer::Kind::up, 10, 10}).signal ==
         OriginalDialogSignal({Kind::close, 0U}));
  assert(about.pointer({DialogPointer::Kind::right_down, 10, 10}).signal ==
         OriginalDialogSignal({Kind::close, 0U}));
  // Timer 9 scrolls the credits: each tick reaches ABOUTDLGPROC, which
  // repaints through the ops.
  for (int tick = 1; tick <= 40; ++tick) {
    const auto step = about.timer();
    assert(step.signal == OriginalDialogSignal({Kind::timer, 9U}));
    assert(!about.take_invalidated());
    assert(!simtower::original_about_dialog_signal(*step.signal, about)
                .result);
    assert(about.take_invalidated());
    assert(about.timer_ticks() == static_cast<std::uint64_t>(tick));
  }
  check_draw(about, resources);
}

// Pinned frames: FNV-1a of the dialog's pixels (dialog_hash) in its
// opening state, for the bomb offer, the fire-crew offer, the rating
// promotion, the STRL/1001 confirmation and About after 40 credit steps.
// A change to the chrome, bitmaps, buttons, static text, focus rectangle,
// mnemonic underline or message-box layout changes one of them; update the
// pins only together with a reviewed frame. Only hashes are kept here, no
// asset bytes.
void frame_hash_tests(const simtower::OriginalResources& resources) {
  const auto event = [&](std::uint16_t id, std::int32_t argument) {
    auto dialog = simtower::sdl::make_event_dialog(
        resources,
        simtower::prepare_original_event_dialog(resources, {id, argument, 0}),
        simtower::sdl::event_dialog_label(id));
    dialog.place(1019, 628);
    return dialog_hash(dialog, resources);
  };
  bool ok = check_hash("event 3012", event(3012, -150000), kPinned3012);
  ok = check_hash("event 3020", event(3020, -3000), kPinned3020) && ok;
  ok = check_hash("event 3030", event(3030, 0), kPinned3030) && ok;
  {
    const std::array<std::string_view, 4> subs{"untitled", "closing", {}, {}};
    auto box = simtower::sdl::make_message_box(
        simtower::build_original_message_box(
            simtower::prepare_original_alert(resources, 1001, subs)),
        "alert 1001");
    box.place(1019, 628);
    ok = check_hash("alert 1001", dialog_hash(box, resources), kPinned1001) &&
         ok;
  }
  {
    auto about = simtower::sdl::make_about_dialog(
        simtower::prepare_original_about_dialog(resources));
    about.place(1019, 628);
    for (int tick = 0; tick < 40; ++tick) (void)about.timer();
    ok = check_hash("about tick 40", dialog_hash(about, resources),
                    kPinnedAbout) &&
         ok;
  }
  assert(ok);
}

}  // namespace

int main(int argc, char** argv) {
  synthetic_tests();
  if (argc < 2) return 77;
  const auto pack = read_file(argv[1]);
  if (!pack) {
    std::printf("skipped: no resource pack at %s\n", argv[1]);
    return 77;
  }
  const simtower::OriginalResources resources(*pack);
  event_tests(resources);
  alert_tests(resources);
  about_tests(resources);
  frame_hash_tests(resources);
  std::printf("sdl dialog tests passed\n");
  return 0;
}
