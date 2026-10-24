// Portable dialog models (original_dialog_model.hpp). The synthetic half
// needs no assets; the resource half reads the pack at run time and the test
// exits 77 (skipped) without it.

#include "original_alert.hpp"
#include "original_dialog_model.hpp"
#include "original_resources.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

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

const simtower::OriginalDialogItem& item(
    const simtower::OriginalDialog& dialog, std::uint16_t id) {
  for (const auto& entry : dialog.items) {
    if (entry.id == id) return entry;
  }
  assert(false && "dialog item missing");
  return dialog.items.front();
}

std::string text(const simtower::OriginalEventDialogModel& model,
                 std::uint16_t id) {
  return item(model.dialog, id).text.text;
}

// The portable procedures (original_dialog_model.hpp) the SDL runtime
// runs: AHOTTADLOGFILTER, ABOUTDLGPROC and the MessageBox result.
void controller_tests() {
  using simtower::OriginalDialogOutcome;
  using simtower::OriginalDialogSignal;
  using simtower::OriginalEventDialogController;
  using Kind = OriginalDialogSignal::Kind;
  const OriginalDialogSignal decline{Kind::command, 1U};
  const OriginalDialogSignal accept{Kind::command, 2U};
  const OriginalDialogSignal timer{Kind::timer, 10U};
  const OriginalDialogOutcome open{};

  // Button 1 declines, button 2 accepts when affordable (or a gain).
  {
    OriginalEventDialogController controller(-3000);
    assert(controller.signal(decline, 0) == OriginalDialogOutcome({1, false}));
    assert(controller.signal(accept, 5000) ==
           OriginalDialogOutcome({2, false}));
    assert(!controller.timer_fired());
  }
  // A manual unaffordable accept: ALRT/1004, then EndDialog(1).
  {
    OriginalEventDialogController controller(-3000);
    assert(controller.signal(accept, 2999) ==
           OriginalDialogOutcome({1, true}));
  }
  // The timer accepts silently: unaffordable -> 1 without the warning,
  // affordable -> 2. It sets the timer flag (1068:0380).
  {
    OriginalEventDialogController controller(-3000);
    assert(controller.signal(timer, 2999) ==
           OriginalDialogOutcome({1, false}));
    assert(controller.timer_fired());
    OriginalEventDialogController paid(-3000);
    assert(paid.signal(timer, 3000) == OriginalDialogOutcome({2, false}));
  }
  // Other commands, and the close signal, keep the dialog open.
  {
    OriginalEventDialogController controller(0);
    for (const std::uint16_t id : {0, 3, 4, 6, 7, 0xFFFF}) {
      assert(controller.signal({Kind::command, id}, 0) == open);
    }
    assert(controller.signal({Kind::close, 0U}, 0) == open);
    // A positive argument (a gain) is always affordable.
    assert(controller.signal(accept, std::numeric_limits<std::int32_t>::min())
               .result == 2);
  }
  // The controller agrees with the action table for every command.
  for (const std::int32_t argument : {0, 7, -150000, 123456}) {
    for (const std::int32_t balance : {0, 100, 150000, 2000000}) {
      for (const std::uint16_t command : {0, 1, 2, 3}) {
        OriginalEventDialogController controller(argument);
        const auto outcome =
            controller.signal({Kind::command, command}, balance);
        using Action = simtower::OriginalEventDialogAction;
        switch (simtower::original_event_dialog_action(command, argument,
                                                       balance, false)) {
          case Action::ignore:
            assert(outcome == open);
            break;
          case Action::close_decline:
            assert(outcome == OriginalDialogOutcome({1, false}));
            break;
          case Action::close_accept:
            assert(outcome == OriginalDialogOutcome({2, false}));
            break;
          case Action::warn_insufficient_funds_then_close_decline:
            assert(outcome == OriginalDialogOutcome({1, true}));
            break;
        }
      }
    }
  }
  // The headless defaults are button 1's decline result.
  {
    OriginalEventDialogController controller(-3000);
    const auto result = controller.signal(decline, 0).result;
    assert(result == simtower::kOriginalEventDialogBombOfferDefault);
    assert(result == simtower::kOriginalEventDialogFireCrewOfferDefault);
    assert(result == simtower::kOriginalEventDialogAcknowledgeDefault);
  }
  // The reserved signals keep an event dialog open.
  {
    OriginalEventDialogController controller(0);
    assert(controller.signal({Kind::draw_item, 1U}, 0) == open);
    assert(controller.signal({Kind::scroll, 2U}, 0) == open);
    // A signal defaults to BN_CLICKED.
    assert(OriginalDialogSignal({Kind::command, 2U}).notify_code ==
           simtower::kOriginalNotifyClicked);
  }
  // ABOUTDLGPROC: only the close signal ends it, with 1; its timer
  // repaints through the runtime ops.
  {
    struct Ops final : simtower::OriginalDialogOps {
      int invalidated{};
      void show_item(std::uint16_t, bool) override { assert(false); }
      void invalidate() override { ++invalidated; }
      void resize(int, int) override { assert(false); }
    } ops;
    const auto about = [&](const OriginalDialogSignal& signal) {
      return simtower::original_about_dialog_signal(signal, ops);
    };
    assert(about({Kind::close, 0U}) == OriginalDialogOutcome({1, false}));
    assert(about({Kind::command, 1U}) == open);
    assert(about({Kind::command, 2U}) == open);
    assert(ops.invalidated == 0);
    assert(about({Kind::timer, 9U}) == open);
    assert(ops.invalidated == 1);
  }
  // MessageBox: the button id.
  for (const std::uint16_t id : {1, 2, 3, 4, 5, 6, 7}) {
    assert(simtower::original_message_box_signal({Kind::command, id}) ==
           OriginalDialogOutcome({id, false}));
  }
  assert(simtower::original_message_box_signal({Kind::timer, 1U}) == open);
  // MulDiv(points, 96, 72).
  assert(simtower::original_dialog_font_pixels(8U) == 11);
  assert(simtower::original_dialog_font_pixels(10U) == 13);
  assert(simtower::original_dialog_font_pixels(12U) == 16);
}

// Template items to widget kinds: the predefined class ordinals and names.
void widget_kind_tests() {
  using simtower::OriginalWidgetKind;
  using Value = simtower::OriginalDialogValue;
  const auto kind = [](Value window_class, std::uint32_t style) {
    simtower::OriginalDialogItem item{};
    item.window_class = std::move(window_class);
    item.style = style;
    return simtower::original_dialog_item_widget_kind(item);
  };
  const auto ordinal = [](std::uint16_t value) {
    return Value{Value::Kind::ordinal, {}, value};
  };
  const auto name = [](const char* text) {
    return Value{Value::Kind::text, text, 0U};
  };
  assert(kind(ordinal(0x80), 0x0U) == OriginalWidgetKind::push_button);
  assert(kind(ordinal(0x80), 0x1U) ==
         OriginalWidgetKind::default_push_button);
  assert(kind(ordinal(0x80), 0x3U) == OriginalWidgetKind::other);  // check
  assert(kind(ordinal(0x80), 0x7U) == OriginalWidgetKind::other);  // group
  assert(kind(ordinal(0x81), 0x0U) == OriginalWidgetKind::edit);
  assert(kind(ordinal(0x82), 0x0U) == OriginalWidgetKind::static_text);
  assert(kind(ordinal(0x82), 0x2U) == OriginalWidgetKind::static_text);
  assert(kind(ordinal(0x82), 0x3U) == OriginalWidgetKind::other);  // icon
  assert(kind(ordinal(0x83), 0x0U) == OriginalWidgetKind::list_box);
  assert(kind(ordinal(0x84), 0x0U) == OriginalWidgetKind::scroll_bar);
  assert(kind(ordinal(0x85), 0x0U) == OriginalWidgetKind::combo_box);
  assert(kind(ordinal(0x99), 0x0U) == OriginalWidgetKind::other);
  assert(kind(name("Button"), 0x1U) ==
         OriginalWidgetKind::default_push_button);
  assert(kind(name("LISTBOX"), 0x0U) == OriginalWidgetKind::list_box);
  assert(kind(name("TowerView"), 0x0U) == OriginalWidgetKind::other);
  assert(kind(Value{}, 0x0U) == OriginalWidgetKind::other);
  assert(simtower::original_widget_is_push_button(
      OriginalWidgetKind::default_push_button));
  assert(!simtower::original_widget_is_push_button(
      OriginalWidgetKind::static_text));
}

void synthetic_tests() {
  // Initial focus: the first visible, enabled WS_TABSTOP control in template
  // order, as GetNextDlgTabItem(dialog, nullptr, FALSE) picks it.
  simtower::OriginalDialog dialog{};
  dialog.items.push_back({0, 0, 1, 1, 3U, 0x40020000U, {}, {}});  // static
  dialog.items.push_back({0, 0, 1, 1, 5U, 0x58010000U, {}, {}});  // disabled
  dialog.items.push_back({0, 0, 1, 1, 6U, 0x40010000U, {}, {}});  // hidden
  dialog.items.push_back({0, 0, 1, 1, 2U, 0x50010000U, {}, {}});
  dialog.items.push_back({0, 0, 1, 1, 1U, 0x50010001U, {}, {}});
  assert(simtower::original_dialog_first_tab_stop(dialog) == 2U);
  dialog.items.resize(3);
  assert(simtower::original_dialog_first_tab_stop(dialog) == 0U);

  // MessageBox button rows (MB_TYPEMASK), left to right.
  using V = std::vector<int>;
  assert(simtower::original_message_box_buttons(0x30U) == V({1}));
  assert(simtower::original_message_box_buttons(0x31U) == V({1, 2}));
  assert(simtower::original_message_box_buttons(0x1032U) == V({3, 4, 5}));
  assert(simtower::original_message_box_buttons(0x1033U) == V({6, 7, 2}));
  assert(simtower::original_message_box_buttons(0x34U) == V({6, 7}));
  assert(simtower::original_message_box_buttons(0x35U) == V({4, 2}));

  // The literal boxes keep the Win32 host's strings and styles.
  const auto replace = simtower::original_replace_existing_message_box();
  assert(replace.caption == "Save SimTower data file");
  assert(replace.text ==
         "This file name already exists. Do you want to replace it?");
  assert(replace.style == 0x34U && replace.button_mode == 4U &&
         replace.beep_first);
  const auto invalid = simtower::original_invalid_filename_message_box();
  assert(invalid.caption == "SimTower for Windows");
  assert(invalid.text == "That is not a valid filename.");
  assert(invalid.style == 0x30U && invalid.button_mode == 0U &&
         !invalid.beep_first);
  const auto timer = simtower::original_about_timer_failed_message_box();
  assert(timer.caption == "SimTower - About" &&
         timer.text == "SetTimer failed" && timer.style == 0x30U &&
         timer.beep_first);

  // 11e0:0c9e: the ALRT presentation under the "SimTower" caption.
  const simtower::OriginalAlertPresentation presentation{
      3U, "Save?", simtower::original_alert_message_box_style(3U)};
  const auto box = simtower::build_original_message_box(presentation);
  assert(box.caption == "SimTower" && box.text == "Save?" &&
         box.style == 0x1033U && box.button_mode == 3U && !box.beep_first);

  controller_tests();
  widget_kind_tests();
}

void resource_tests(const simtower::OriginalResources& resources) {
  const auto event = [&](std::uint16_t id, std::int32_t argument) {
    return simtower::prepare_original_event_dialog(resources,
                                                   {id, argument, 0});
  };

  // Every event model: point size 8 (1008:0085), the AHOTTADLOGFILTER timer
  // and control colours.
  for (const std::uint16_t id :
       {3000, 3001, 3002, 3003, 3010, 3011, 3012, 3013, 3014, 3020, 3021,
        3022, 3023, 3024, 3030, 3031, 3032, 3033, 3034, 3040}) {
    const auto model = event(id, 0);
    assert(model.dialog.font_point_size == 8U);
    assert((model.dialog.style & simtower::kOriginalDialogStyleSetFont) != 0U);
    assert(model.timer_id == 10U && model.timer_ms == 37856U);
    assert(model.ctlcolor_font_pixels == 14);
    assert(model.static_background_rgb == 0x00CCCCCCU);
    assert(!model.dtmp.rectangles.empty());
  }
  // No template for the gaps (3004-3009, ...): the launcher fails.
  bool threw = false;
  try {
    (void)event(3004, 0);
  } catch (const std::exception&) {
    threw = true;
  }
  assert(threw);

  // Texts after the caret and item-3 passes.
  assert(text(event(3000, 7), 3) ==
         "A VIP has made reservations for the Hotel Suite on floor 7.");
  assert(text(event(3000, -150000), 3) ==
         "A VIP has made reservations for the Hotel Suite on floor -150000.");
  assert(text(event(3012, 7), 3) ==
         "Would you like to call an emergency fire crew?\nIt will cost "
         "$700.");
  assert(text(event(3020, -200000), 3) ==
         "Blackmail from Terrorists!\nThey demand $20000000 or a hidden "
         "bomb will explode at 3 o'clock.");
  // 3040: the caret pass prints the raw %ld before the literal "00".
  assert(text(event(3040, 123456), 3) ==
         "During construction, workers discovered ancient buried treasure! "
         "\nIt is worth $12345600");
  assert(text(event(3040, std::numeric_limits<std::int32_t>::min()), 3) ==
         "During construction, workers discovered ancient buried treasure! "
         "\nIt is worth $-214748364800");
  // Buttons keep their resource captions.
  assert(text(event(3020, 0), 1) == "Find the Bomb");
  assert(text(event(3020, 0), 2) == "Pay Them");
  assert(text(event(3030, 0), 1) == "OK");

  // Initial focus (first WS_TABSTOP in template order).
  assert(event(3012, 0).initial_focus == 2U);  // Yes
  assert(event(3020, 0).initial_focus == 1U);  // Find the Bomb
  assert(event(3000, 0).initial_focus == 1U);
  assert(event(3030, 0).initial_focus == 1U);

  // About: TOWER_TITLE, the BITMAP/257 layout and TEXT/128.
  const auto about = simtower::prepare_original_about_dialog(resources);
  assert(about.layout.window_width == 604 && about.layout.window_height == 290);
  assert(about.dialog.items.empty());
  assert(!about.lines.empty());
  assert(about.chrome.rectangles.size() == 1U &&
         about.chrome.rectangles[0] == about.layout.credits_outer);
  assert(about.background_rgb == 0x00E6E6E6U && about.timer_id == 9U);

  // ALRT/1001 through the shared presentation: three buttons, Yes first.
  const std::array<std::string_view, 4> subs{"untitled", "quitting", {}, {}};
  const auto alert = simtower::build_original_message_box(
      simtower::prepare_original_alert(resources, 1001, subs));
  assert(alert.caption == "SimTower");
  assert(alert.text == "Save the tower \"untitled\" before quitting?");
  assert(alert.button_mode == 3U);
  assert(simtower::original_message_box_buttons(alert.style) ==
         std::vector<int>({6, 7, 2}));
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
  resource_tests(resources);
  std::printf("dialog model tests passed\n");
  return 0;
}
