// The shared Transport Information controller
// (original_transport_information_controller.hpp) against a frozen copy of
// the Win32 ELVINFODLOGFILTER / ESCINFODLOGFILTER branches of
// event_dialog_proc in native_main.cpp as they were before the procedure
// shared it (transport_target set): WM_INITDIALOG's focus return,
// WM_LBUTTONDOWN (DC, palette, portrait hit, Person Information with
// DS:b3a6 = 1, the refreshed text, InvalidateRect + UpdateWindow,
// ReleaseDC, TOPMOST, DS:31a4), WM_MOUSEMOVE (the 1003 cursor over items 4
// and 9) and WM_COMMAND (only 1 closes). Each Win32 call the old code made
// is one log line; the controller runs over a recording OriginalDialogOps
// and a host that logs what Win32TransportInformationHost and the new
// procedure do. The logs, results, handled flags, texts and saved towers
// must be identical for every elevator car and stair / escalator of the
// "busy" staged tower with passengers written into it. Also: the portable
// painter's calls against the Win32 GDI painter's arguments, and its
// pixels on the canvas. Runs natively and under Wine; needs the pack (exit
// 77 without it).

#include "original_construction.hpp"
#include "original_dialog.hpp"
#include "original_draw_canvas.hpp"
#include "original_information.hpp"
#include "original_information_paint.hpp"
#include "original_resources.hpp"
#include "original_startup_data.hpp"
#include "original_tdt.hpp"
#include "original_transport_information_controller.hpp"
#include "original_ui.hpp"
#include "test_scenario.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

using simtower::OriginalDialogSignal;
using simtower::OriginalMagnifierTarget;
using simtower::OriginalMagnifierTargetKind;
using simtower::OriginalTdtDocument;
using simtower::OriginalTransportInformationText;
using Kind = OriginalDialogSignal::Kind;
using Log = std::vector<std::string>;

std::optional<std::vector<std::byte>> read_file(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::nullopt;
  const std::vector<char> chars((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(chars.size());
  for (std::size_t i = 0; i < chars.size(); ++i) {
    bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(chars[i]));
  }
  return bytes;
}

bool same_text(const OriginalTransportInformationText& a,
               const OriginalTransportInformationText& b) {
  if (a.valid != b.valid || a.primary != b.primary ||
      a.secondary != b.secondary ||
      a.person_sprites.size() != b.person_sprites.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.person_sprites.size(); ++i) {
    const auto& x = a.person_sprites[i];
    const auto& y = b.person_sprites[i];
    if (x.person_index != y.person_index || x.bitmap_id != y.bitmap_id ||
        x.frame != y.frame || x.destination_x != y.destination_x ||
        x.destination_y != y.destination_y || x.width != y.width ||
        x.height != y.height) {
      return false;
    }
  }
  return true;
}

// The fake Person Information both sides run: it names the person (a
// named portrait is BITMAP 702, so the refreshed strip differs) or not.
bool fake_person_information(OriginalTdtDocument& document,
                             std::size_t person, bool renames, int& count,
                             Log& log) {
  log.push_back("person " + std::to_string(person) + " owner dialog b3a6 1");
  if (!renames) return false;
  ++count;
  return simtower::set_original_person_name(
             document, person, "Rider " + std::to_string(count))
      .changed;
}

// --- The frozen Win32 branches (event_dialog_proc with transport_target,
// nm 2580-2794 before the controller), with g_logical_palette set ---------

struct LegacyDialog {
  const simtower::OriginalResources& resources;
  OriginalTdtDocument* document;  // g_tower_document
  OriginalMagnifierTarget target;
  OriginalTransportInformationText text;  // text_overlays + person_sprites
  simtower::OriginalDtmp dtmp;
  bool renames;
  Log* log;
  int person_dialogs{};
  int named{};
  std::optional<int> ended{};

  bool init() {
    const auto focus_plan =
        simtower::original_painted_dialog_initialization_focus_plan();
    if (focus_plan.set_explicit_focus) log->push_back("SetFocus 1");
    return focus_plan.consume;
  }

  // The procedure's return value for one message (false: break / FALSE).
  bool message(Kind kind, std::uint16_t id, int x, int y) {
    switch (kind) {
      case Kind::button_down: {
        const auto click_plan =
            simtower::original_transport_information_click_plan(target.kind);
        if (!click_plan.consume) return false;
        log->push_back("GetDC");
        if (click_plan.select_palette) log->push_back("SelectPalette");
        if (click_plan.realize_palette) log->push_back("RealizePalette");
        std::optional<std::size_t> person{};
        if (document) {
          person = simtower::original_information_person_sprite_hit(
              text.person_sprites, static_cast<std::int16_t>(x),
              static_cast<std::int16_t>(y));
        }
        if (person) {
          ++person_dialogs;
          (void)fake_person_information(*document, *person, renames, named,
                                        *log);
          const auto refreshed = simtower::original_transport_information_text(
              resources, *document, target);
          text.primary = refreshed.primary;
          text.secondary = refreshed.secondary;
          text.person_sprites = refreshed.person_sprites;
          log->push_back("InvalidateRect all");
          log->push_back("UpdateWindow");
        }
        log->push_back("ReleaseDC");
        if (click_plan.restore_topmost) log->push_back("TOPMOST");
        if (click_plan.restore_modal_target) log->push_back("DS31a4 dialog");
        return true;
      }
      case Kind::mouse_move: {
        const bool portrait_panel =
            simtower::original_information_portrait_panel_hit(
                dtmp, static_cast<std::int16_t>(x),
                static_cast<std::int16_t>(y));
        log->push_back("SetCursor " +
                       std::to_string(portrait_panel ? 1003U : 0U));
        return true;
      }
      case Kind::command:
        if (id == 1U) {
          log->push_back("DS31a4 0");
          ended = 1;
          return true;
        }
        return false;
      default:
        // WM_TIMER / WM_KEYUP / WM_LBUTTONUP: no transport branch.
        return false;
    }
  }
};

// --- The shared side: Win32DialogOps and Win32TransportInformationHost
// as log lines ------------------------------------------------------------

class RecordingOps final : public simtower::OriginalDialogOps {
 public:
  explicit RecordingOps(Log& log) : log_(log) {}
  void show_item(std::uint16_t, bool) override { log_.push_back("show"); }
  void invalidate() override { log_.push_back("InvalidateRect all"); }
  void invalidate_rect(const simtower::OriginalRect&) override {
    log_.push_back("InvalidateRect part");
  }
  void update_window() override { log_.push_back("UpdateWindow"); }
  void resize(int, int) override { log_.push_back("resize"); }
  void enable_item(std::uint16_t id, bool) override {
    log_.push_back("EnableWindow " + std::to_string(id));
  }
  void focus_item(std::uint16_t id) override {
    log_.push_back("SetFocus " + std::to_string(id));
  }
  void list_add(std::uint16_t id, std::string_view) override {
    log_.push_back("LB_ADDSTRING " + std::to_string(id));
  }

 private:
  Log& log_;
};

class RecordingHost final
    : public simtower::OriginalTransportInformationHost {
 public:
  RecordingHost(const simtower::OriginalResources& resources,
                OriginalTdtDocument* document, bool renames, Log& log)
      : resources_(resources),
        document_(document),
        renames_(renames),
        log_(log) {}
  OriginalTdtDocument* document() override { return document_; }
  const simtower::OriginalPartTable& part() override { return part_; }
  const simtower::OriginalResources& resources() override {
    return resources_;
  }
  void mark_document_dirty() override { log_.push_back("dirty"); }
  bool person_information(std::size_t person) override {
    return fake_person_information(*document_, person, renames_, named_,
                                   log_);
  }
  void set_cursor(std::uint16_t cursor_id) override {
    log_.push_back("SetCursor " + std::to_string(cursor_id));
  }

 private:
  const simtower::OriginalResources& resources_;
  simtower::OriginalPartTable part_{};
  OriginalTdtDocument* document_;
  bool renames_;
  int named_{};
  Log& log_;
};

// transport_information_dialog_proc around the controller: the procedure's
// return value for one message.
bool shared_message(simtower::OriginalTransportInformationController& c,
                    Kind kind, std::uint16_t id, std::uint16_t code, int x,
                    int y, RecordingOps& ops, RecordingHost& host, Log& log,
                    std::optional<int>& ended) {
  OriginalDialogSignal signal{kind, id, code};
  signal.x = static_cast<std::int16_t>(x);
  signal.y = static_cast<std::int16_t>(y);
  if (kind == Kind::button_down) {
    const auto click_plan = c.click_plan();
    if (!click_plan.consume) return false;
    log.push_back("GetDC");
    if (click_plan.select_palette) log.push_back("SelectPalette");
    if (click_plan.realize_palette) log.push_back("RealizePalette");
    (void)c.signal(signal, ops, host);
    log.push_back("ReleaseDC");
    if (click_plan.restore_topmost) log.push_back("TOPMOST");
    if (click_plan.restore_modal_target) log.push_back("DS31a4 dialog");
    return true;
  }
  if (kind == Kind::mouse_move) {
    (void)c.signal(signal, ops, host);
    return true;
  }
  if (kind == Kind::command) {
    bool handled = false;
    const auto outcome = c.signal(signal, ops, host, &handled);
    if (outcome.result) {
      log.push_back("DS31a4 0");
      ended = outcome.result;
    }
    return handled;
  }
  // WM_TIMER / WM_KEYUP / WM_LBUTTONUP: the procedure has no case.
  return false;
}

struct Step {
  Kind kind{Kind::command};
  std::uint16_t id{};
  std::uint16_t code{};
  int x{};
  int y{};
};

// Unknown commands, Esc's IDCANCEL, a notification, messages the filters
// have no branch for, moves over every DTMP rectangle's corners (and
// beside them), presses on every portrait (twice for the first), on empty
// panel space and outside, then OK.
std::vector<Step> steps(const OriginalTransportInformationText& text,
                        const simtower::OriginalDtmp& dtmp,
                        std::uint16_t close_code) {
  std::vector<Step> out{
      {Kind::command, 2, 0},       {Kind::command, 99, 0},
      {Kind::command, 0, 0},       {Kind::command, 2, 1},
      {Kind::timer, 1, 0},         {Kind::key_up, 13, 0},
      {Kind::button_up, 0, 0, 5, 5}, {Kind::right_button_up, 0, 0, 5, 5},
  };
  for (const auto& r : dtmp.rectangles) {
    const int left = r.left;
    const int top = r.top;
    const int right = r.right;
    const int bottom = r.bottom;
    for (const auto& [x, y] :
         {std::pair{left, top}, std::pair{right - 1, bottom - 1},
          std::pair{right, bottom}, std::pair{left - 1, top}}) {
      out.push_back({Kind::mouse_move, 0, 0, x, y});
    }
  }
  bool first = true;
  for (const auto& sprite : text.person_sprites) {
    const Step press{Kind::button_down, 0, 0,
                     sprite.destination_x + sprite.width / 2,
                     sprite.destination_y + 12};
    out.push_back(press);
    if (first) out.push_back(press);
    first = false;
  }
  out.push_back({Kind::button_down, 0, 0, 1, 1});
  out.push_back({Kind::button_down, 0, 0, -3, 4000});
  if (dtmp.rectangles.size() >= 4U) {
    const auto& item4 = dtmp.rectangles[3];
    out.push_back({Kind::button_down, 0, 0, item4.right - 3,
                   item4.bottom - 2});
  }
  out.push_back({Kind::command, 1, close_code});
  out.push_back({Kind::command, 1, 0});
  return out;
}

struct ParityCounts {
  int runs{};
  int person_dialogs{};
  int named{};
  int sprites{};
  std::set<std::uint16_t> dialogs{};
  std::set<OriginalMagnifierTargetKind> kinds{};
};

void run_parity(const simtower::OriginalResources& resources,
                const OriginalTdtDocument& base,
                const OriginalMagnifierTarget& target, bool renames,
                bool with_document, std::uint16_t close_code,
                ParityCounts& counts) {
  auto legacy_document = std::make_unique<OriginalTdtDocument>(base);
  auto shared_document = std::make_unique<OriginalTdtDocument>(base);
  const auto text =
      simtower::original_transport_information_text(resources, base, target);
  assert(text.valid);
  const auto dtmp = simtower::parse_original_dtmp(
      resources.find("DTMP", target.dialog_id));
  Log legacy_log;
  Log shared_log;
  LegacyDialog legacy{resources,
                      with_document ? legacy_document.get() : nullptr,
                      target,
                      text,
                      dtmp,
                      renames,
                      &legacy_log};
  simtower::OriginalTransportInformationController controller(target, text,
                                                              dtmp);
  RecordingOps ops(shared_log);
  RecordingHost host(resources,
                     with_document ? shared_document.get() : nullptr,
                     renames, shared_log);
  assert(legacy.init() == controller.initial_focus(ops));
  assert(legacy_log == shared_log);
  std::optional<int> shared_ended{};
  for (const auto& step : steps(text, dtmp, close_code)) {
    const bool legacy_handled =
        legacy.message(step.kind, step.id, step.x, step.y);
    const bool shared_handled =
        shared_message(controller, step.kind, step.id, step.code, step.x,
                       step.y, ops, host, shared_log, shared_ended);
    assert(shared_handled == legacy_handled);
    assert(shared_log == legacy_log);
    assert(shared_ended == legacy.ended);
    assert(same_text(controller.text(), legacy.text));
    if (shared_ended) break;
  }
  assert(shared_ended == std::optional<int>(1));
  assert(simtower::serialize_original_tdt_game_save(*shared_document) ==
         simtower::serialize_original_tdt_game_save(*legacy_document));
  ++counts.runs;
  counts.person_dialogs += legacy.person_dialogs;
  counts.named += legacy.named;
  counts.sprites += static_cast<int>(text.person_sprites.size());
  counts.dialogs.insert(target.dialog_id);
  counts.kinds.insert(target.kind);
}

void store_u32(std::span<std::byte> bytes, std::size_t offset,
               std::uint32_t value, bool byte_swapped) {
  for (std::size_t i = 0; i < 4U; ++i) {
    const auto shift = byte_swapped ? (3U - i) * 8U : i * 8U;
    bytes[offset + i] = static_cast<std::byte>((value >> shift) & 0xffU);
  }
}

// Passengers for every car of every used shaft (slots 184+ live, person
// indices at 16 + slot * 4, the count at byte 3) and riders for every used
// stair / escalator (people of the scanned families at state 0x40 on it,
// word_6 the count). Returns the targets.
std::vector<OriginalMagnifierTarget> stage_transport(
    OriginalTdtDocument& document) {
  std::vector<OriginalMagnifierTarget> targets;
  std::size_t next_person = 0U;
  for (std::size_t e = 0; e < document.elevators.size(); ++e) {
    auto& elevator = document.elevators[e];
    if (!elevator.used) continue;
    for (std::size_t car = 0;
         car < elevator.cars && car < elevator.car_records.size(); ++car) {
      auto& bytes = elevator.car_records[car].exact_bytes;
      // 0, a few, and more than one row's worth (the item 9 spill).
      const std::size_t riders =
          std::min<std::size_t>(elevator.capacity,
                                car == 0U ? 0U : (car == 1U ? 5U : 30U));
      for (std::size_t slot = 0; slot < 42U; ++slot) {
        const bool live = slot < riders;
        bytes[184U + slot] = live ? std::byte{0} : std::byte{0xff};
        if (live) {
          store_u32(bytes, 16U + slot * 4U,
                    static_cast<std::uint32_t>(next_person),
                    document.header.byte_swapped);
          next_person = (next_person + 37U) % document.people_count;
        }
      }
      bytes[3] = static_cast<std::byte>(riders);
      OriginalMagnifierTarget target{};
      target.kind = OriginalMagnifierTargetKind::elevator_car_information;
      target.elevator_index = e;
      target.elevator_car_index = static_cast<std::int16_t>(car);
      target.dialog_id =
          simtower::original_elevator_information_dialog_id(elevator.type);
      targets.push_back(target);
    }
  }
  std::size_t rider = 0U;
  for (std::size_t s = 0; s < document.post_elevator.stairs_bd70.size();
       ++s) {
    auto& stair = document.post_elevator.stairs_bd70[s];
    if (!stair.used) continue;
    const std::size_t wanted = s % 3U == 0U ? 0U : (s % 3U == 1U ? 4U : 40U);
    std::size_t placed = 0U;
    for (; rider < document.people_count && placed < wanted; ++rider) {
      auto& person = document.people[rider].exact_bytes;
      const auto type = std::to_integer<int>(person[4]);
      if (type != 3 && type != 7 && type != 9 && type != 15) continue;
      person[5] = static_cast<std::byte>(0x40);
      person[8] = static_cast<std::byte>(s);
      ++placed;
    }
    stair.word_6 = static_cast<std::uint16_t>(placed);
    stair.word_8 = 0U;
    OriginalMagnifierTarget target{};
    target.kind = OriginalMagnifierTargetKind::vertical_transport_information;
    target.vertical_transport_index = s;
    target.dialog_id =
        simtower::original_vertical_transport_information_dialog_id();
    targets.push_back(target);
  }
  return targets;
}

void parity_tests(const simtower::OriginalResources& resources,
                  const OriginalTdtDocument& document,
                  const std::vector<OriginalMagnifierTarget>& targets) {
  ParityCounts counts{};
  for (const auto& target : targets) {
    for (const bool renames : {false, true}) {
      run_parity(resources, document, target, renames, true, 0U, counts);
    }
    run_parity(resources, document, target, true, false, 1U, counts);
  }
  std::printf("transport parity: %zu targets, %d runs, %d portrait hits, %d "
              "named, %d sprites, %zu dialogs, %zu kinds\n",
              targets.size(), counts.runs, counts.person_dialogs,
              counts.named, counts.sprites, counts.dialogs.size(),
              counts.kinds.size());
  assert(counts.runs > 0 && counts.person_dialogs > 0 && counts.named > 0);
  assert(counts.kinds.size() == 2U);
  assert(counts.dialogs.contains(761U));
}

// Records every drawing call.
class Recorder final : public simtower::OriginalDrawContext {
 public:
  Log calls;
  int save() override {
    calls.push_back("save");
    return 1;
  }
  void restore(int) override { calls.push_back("restore"); }
  void fill_rect(const simtower::OriginalRect& r,
                 simtower::OriginalRgb rgb) override {
    calls.push_back("fill " + rect(r) + " " + std::to_string(rgb));
  }
  void intersect_clip(const simtower::OriginalRect& r) override {
    calls.push_back("clip " + rect(r));
  }
  void draw_dib(std::span<const std::byte> resource, int x, int y) override {
    calls.push_back("dib " + std::to_string(resource.size()) + " " +
                    std::to_string(x) + "," + std::to_string(y));
  }
  void push_solid_pen(simtower::OriginalRgb rgb) override {
    calls.push_back("pen " + std::to_string(rgb));
  }
  void pop_pen() override { calls.push_back("pop pen"); }
  void move_to(int x, int y) override {
    calls.push_back("move " + std::to_string(x) + "," + std::to_string(y));
  }
  void line_to(int x, int y) override {
    calls.push_back("line " + std::to_string(x) + "," + std::to_string(y));
  }
  void push_font(std::int16_t height) override {
    calls.push_back("font " + std::to_string(height));
  }
  void pop_font() override { calls.push_back("pop font"); }
  void set_text_color(simtower::OriginalRgb rgb) override {
    calls.push_back("color " + std::to_string(rgb));
  }
  void set_bk_transparent() override { calls.push_back("transparent"); }
  void set_text_align(std::uint16_t flags) override {
    calls.push_back("align " + std::to_string(flags));
  }
  void text_out(std::string_view text) override {
    calls.push_back("text " + std::string(text));
  }
  void draw_text_single_line(std::string_view text,
                             const simtower::OriginalRect& r) override {
    calls.push_back("line text " + std::string(text) + " " + rect(r));
  }
  int text_extent_cx(std::string_view) override { return 0; }
  void stretch_pixels(std::span<const std::uint32_t>, int, int,
                      const simtower::OriginalRect&,
                      const simtower::OriginalRect&) override {
    calls.push_back("stretch");
  }
  void draw_dib_region_scaled(
      std::span<const std::byte> resource,
      const simtower::OriginalRect& source,
      const simtower::OriginalRect& destination) override {
    calls.push_back("scaled " + std::to_string(resource.size()) + " " +
                    rect(source) + " " + rect(destination));
  }

  static std::string rect(const simtower::OriginalRect& r) {
    return std::to_string(r.left) + "," + std::to_string(r.top) + "," +
           std::to_string(r.right) + "," + std::to_string(r.bottom);
  }
};

// The portable painter: the chrome, then the Win32 GDI painter's text
// origins (rectangle 3 / 6 + (8, 1) under TA_UPDATECP in the 13-pixel font)
// and its draw_original_dib_region_scaled arguments for each portrait; on
// the canvas the client is fully painted, the first line and every
// portrait leave black pixels.
void painter_tests(const simtower::OriginalResources& resources,
                   const OriginalTdtDocument& document,
                   const std::vector<OriginalMagnifierTarget>& targets) {
  auto& font = simtower::OriginalCanvasFont::bundled();
  int painted = 0;
  int sprites = 0;
  for (const auto& target : targets) {
    const auto text = simtower::original_transport_information_text(
        resources, document, target);
    if (!text.valid) continue;
    const auto dtmp = simtower::parse_original_dtmp(
        resources.find("DTMP", target.dialog_id));
    const simtower::OriginalRect client{0, 0, dtmp.width_or_header,
                                        dtmp.height_or_header};
    const auto has_child = [](std::uint16_t id) { return id == 1U; };
    Recorder chrome;
    simtower::paint_original_dtmp_chrome(chrome, client, dtmp, has_child);
    Recorder full;
    simtower::paint_original_transport_information(full, resources, dtmp,
                                                   text, client, has_child);
    Log expected = chrome.calls;
    expected.push_back("save");
    expected.push_back("align " +
                       std::to_string(simtower::kOriginalTaUpdateCp));
    expected.push_back("transparent");
    expected.push_back("color 0");
    expected.push_back("font 13");
    for (const auto& [index, line] :
         {std::pair{2U, &text.primary}, std::pair{5U, &text.secondary}}) {
      if (index >= dtmp.rectangles.size()) continue;
      const auto& r = dtmp.rectangles[index];
      const auto origin = simtower::original_relative_gdi_position(
          static_cast<std::int16_t>(r.left),
          static_cast<std::int16_t>(r.top), 8, 1);
      expected.push_back("move " + std::to_string(origin.x) + "," +
                         std::to_string(origin.y));
      expected.push_back("text " + *line);
    }
    expected.push_back("pop font");
    expected.push_back("restore");
    for (const auto& sprite : text.person_sprites) {
      // draw_original_dib_region_scaled(dc, bitmap, x, y, w, h,
      // frame * 8, 0, w, 24).
      const simtower::OriginalRect source{sprite.frame * 8, 0,
                                          sprite.frame * 8 + sprite.width,
                                          24};
      const simtower::OriginalRect destination{
          sprite.destination_x, sprite.destination_y,
          sprite.destination_x + sprite.width,
          sprite.destination_y + sprite.height};
      expected.push_back(
          "scaled " +
          std::to_string(resources.find("BITMAP", sprite.bitmap_id).size()) +
          " " + Recorder::rect(source) + " " + Recorder::rect(destination));
    }
    if (full.calls != expected) {
      for (std::size_t i = 0; i < std::max(full.calls.size(),
                                            expected.size()); ++i) {
        std::printf("%-40s | %s\n",
                    i < full.calls.size() ? full.calls[i].c_str() : "",
                    i < expected.size() ? expected[i].c_str() : "");
      }
    }
    assert(full.calls == expected);

    simtower::OriginalWorldRaster raster{};
    raster.width = client.right;
    raster.height = client.bottom;
    raster.pixels.assign(
        static_cast<std::size_t>(raster.width) * raster.height, 0x123456U);
    simtower::OriginalCanvasDrawContext context(raster, font);
    simtower::paint_original_transport_information(context, resources, dtmp,
                                                   text, client, has_child);
    for (int y = 6; y < raster.height - 6; ++y) {
      for (int x = 6; x < raster.width - 6; ++x) {
        assert(raster.at(x, y) != 0x123456U);
      }
    }
    const auto black_in = [&](int left, int top, int right, int bottom) {
      int found = 0;
      for (int y = std::max(0, top); y < std::min(raster.height, bottom);
           ++y) {
        for (int x = std::max(0, left); x < std::min(raster.width, right);
             ++x) {
          if (raster.at(x, y) == 0U) ++found;
        }
      }
      return found;
    };
    const auto& item3 = dtmp.rectangles[2];
    assert(black_in(item3.left, item3.top, item3.right, item3.bottom) > 0);
    for (const auto& sprite : text.person_sprites) {
      assert(black_in(sprite.destination_x, sprite.destination_y,
                      sprite.destination_x + sprite.width,
                      sprite.destination_y + sprite.height) > 0);
      ++sprites;
    }
    ++painted;
  }
  std::printf("transport painter: %d dialogs painted, %d portraits\n",
              painted, sprites);
  assert(painted > 0 && sprites > 0);
}

// The controller alone: OK closes, IDCANCEL does not, a press without a
// tower is consumed and opens nothing, the cursor over items 4 and 9.
void controller_tests(const simtower::OriginalResources& resources,
                      const OriginalTdtDocument& document,
                      const OriginalMagnifierTarget& target) {
  const auto text =
      simtower::original_transport_information_text(resources, document,
                                                    target);
  const auto dtmp = simtower::parse_original_dtmp(
      resources.find("DTMP", target.dialog_id));
  simtower::OriginalTransportInformationController controller(target, text,
                                                              dtmp);
  Log log;
  RecordingOps ops(log);
  RecordingHost host(resources, nullptr, false, log);
  assert(controller.initial_focus(ops) && log.empty());
  bool handled = true;
  assert(!controller.signal({Kind::command, 2, 0}, ops, host, &handled)
              .result);
  assert(!handled);
  OriginalDialogSignal press{Kind::button_down};
  if (!text.person_sprites.empty()) {
    press.x = text.person_sprites[0].destination_x;
    press.y = text.person_sprites[0].destination_y;
  }
  assert(!controller.signal(press, ops, host, &handled).result);
  assert(handled && log.empty() && !controller.changed());
  OriginalDialogSignal move{Kind::mouse_move};
  move.x = dtmp.rectangles[3].left;
  move.y = dtmp.rectangles[3].top;
  (void)controller.signal(move, ops, host);
  if (dtmp.rectangles.size() >= 9U && dtmp.rectangles[8].right > 0) {
    move.x = dtmp.rectangles[8].left;
    move.y = dtmp.rectangles[8].top;
    (void)controller.signal(move, ops, host);
  }
  move.x = -1;
  move.y = -1;
  (void)controller.signal(move, ops, host);
  assert(log.front() == "SetCursor 1003" && log.back() == "SetCursor 0");
  handled = false;
  const auto outcome =
      controller.signal({Kind::command, 1, 1}, ops, host, &handled);
  assert(outcome.result == std::optional<int>(1) && handled);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 77;
  const auto pack = read_file(argv[1]);
  if (!pack) {
    std::printf("skipped: no resource pack at %s\n", argv[1]);
    return 77;
  }
  const simtower::OriginalResources resources{
      std::span<const std::byte>(*pack)};
  const auto startup = simtower::load_original_startup_data(resources);
  auto document =
      std::make_unique<OriginalTdtDocument>(simtower::make_original_new_tdt());
  (void)simtower::build_scenario("busy", *document, startup.part,
                                 startup.construction_costs);
  // The busy tower has only standard shafts (type 1, DIALOG 762): add one
  // express shaft (raw command 42, type 0) on the lobby floor so the
  // elevator path to DIALOG 761 is covered too.
  bool express = false;
  for (std::uint16_t x = 20U; x < 360U && !express; x += 10U) {
    express = simtower::build_original_elevator(*document, 42U, 10, x,
                                                startup.construction_costs,
                                                startup.part)
                  .succeeded();
  }
  assert(express);
  const auto targets = stage_transport(*document);
  bool express_target = false;
  for (const auto& target : targets) {
    express_target =
        express_target ||
        (target.kind ==
             OriginalMagnifierTargetKind::elevator_car_information &&
         target.dialog_id == 761U);
  }
  assert(express_target);
  controller_tests(resources, *document, targets.front());
  parity_tests(resources, *document, targets);
  painter_tests(resources, *document, targets);
  std::printf("transport information controller tests passed\n");
  return 0;
}
