// The shared Person Information controller
// (original_person_information_controller.hpp) against a frozen copy of
// the Win32 PEPLEINFODLOGFILTER branches in native_main.cpp as they were
// before the procedure shared it: WM_INITDIALOG's focus return and the
// whole WM_COMMAND (1 closes, 4 renames with a tower, everything else is
// not handled), plus the run_original_person_information_dialog exit
// latch. Each Win32 call the old code made is one log line; the controller
// runs over a recording OriginalDialogOps and a host that logs what
// Win32PersonInformationHost does. The logs, results, handled flags,
// information and saved towers must be identical for persons of the
// "busy" staged tower in all three DS:b3a6 contexts. Also: the portable
// painter on the canvas and its portrait call. Runs natively and under
// Wine; needs the pack (exit 77 without it).

#include "original_draw_canvas.hpp"
#include "original_find.hpp"
#include "original_information.hpp"
#include "original_information_paint.hpp"
#include "original_person_information_controller.hpp"
#include "original_resources.hpp"
#include "original_startup_data.hpp"
#include "original_tdt.hpp"
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
using simtower::OriginalPersonInformationContext;
using simtower::OriginalTdtDocument;
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

bool same_information(const simtower::OriginalPersonInformation& a,
                      const simtower::OriginalPersonInformation& b) {
  return a.valid == b.valid && a.person_index == b.person_index &&
         a.dialog_id == b.dialog_id && a.owner_floor == b.owner_floor &&
         a.owner_key == b.owner_key &&
         a.owner_tenant_index == b.owner_tenant_index &&
         a.owner_type == b.owner_type &&
         a.portrait_frame == b.portrait_frame &&
         a.portrait_variant == b.portrait_variant &&
         a.display_name == b.display_name &&
         a.origin_text == b.origin_text &&
         a.activity_text == b.activity_text &&
         a.evaluation == b.evaluation && a.stress == b.stress;
}

// The fake Rename Person both sides run: it saves a name (or cancels).
bool fake_rename(OriginalTdtDocument& document, std::size_t person,
                 bool saves, int& renames, Log& log) {
  log.push_back("rename " + std::to_string(person));
  if (!saves) return false;
  ++renames;
  return simtower::set_original_person_name(
             document, person, "Renamed " + std::to_string(renames))
      .changed;
}

// --- The frozen Win32 branches (nm 3790-3916 before the controller) ------

struct LegacyDialog {
  const simtower::OriginalResources& resources;
  const simtower::OriginalPartTable& part;
  OriginalTdtDocument* document;  // g_tower_document
  std::size_t person_index;
  OriginalPersonInformationContext source_context;
  simtower::OriginalPersonInformation information;
  bool saves;
  Log* log;
  bool changed{};
  int renames{};
  std::optional<int> ended{};

  bool init() {
    const auto focus_plan =
        simtower::original_painted_dialog_initialization_focus_plan();
    if (focus_plan.set_explicit_focus) log->push_back("SetFocus 1");
    return focus_plan.consume;
  }

  // WM_COMMAND: the procedure's return value.
  bool command(std::uint16_t id) {
    if (id == 1U) {
      log->push_back("DS31a4 0");
      if (simtower::original_person_information_capture_plan()
              .release_before_close) {
        log->push_back("ReleaseCapture");
      }
      ended = 1;
      return true;
    }
    if (id == 4U && document) {
      log->push_back("DS31a4 0");
      const bool renamed =
          fake_rename(*document, person_index, saves, renames, *log);
      log->push_back("DS31a4 dialog");
      log->push_back("TOPMOST");
      if (renamed) {
        changed = true;
        log->push_back("dirty");
        log->push_back("title");
      }
      information = simtower::original_person_information(
          resources, *document, part, person_index, source_context);
      log->push_back("InvalidateRect all");
      log->push_back("UpdateWindow");
      return true;
    }
    return false;
  }
};

// --- The shared side: Win32DialogOps and Win32PersonInformationHost as
// log lines ---------------------------------------------------------------

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

class RecordingHost final : public simtower::OriginalPersonInformationHost {
 public:
  RecordingHost(const simtower::OriginalResources& resources,
                const simtower::OriginalPartTable& part,
                OriginalTdtDocument* document, bool saves, Log& log)
      : resources_(resources),
        part_(part),
        document_(document),
        saves_(saves),
        log_(log) {}
  OriginalTdtDocument* document() override { return document_; }
  const simtower::OriginalPartTable& part() override { return part_; }
  const simtower::OriginalResources& resources() override {
    return resources_;
  }
  bool rename_person(std::size_t person) override {
    log_.push_back("DS31a4 0");
    const bool renamed =
        fake_rename(*document_, person, saves_, renames_, log_);
    log_.push_back("DS31a4 dialog");
    log_.push_back("TOPMOST");
    return renamed;
  }
  void mark_document_dirty() override {
    log_.push_back("dirty");
    log_.push_back("title");
  }
  void set_cursor(std::uint16_t id) override {
    log_.push_back("SetCursor " + std::to_string(id));
  }

 private:
  const simtower::OriginalResources& resources_;
  const simtower::OriginalPartTable& part_;
  OriginalTdtDocument* document_;
  bool saves_;
  int renames_{};
  Log& log_;
};

// The inputs of one run: unknown ids, Esc's IDCANCEL, a notification on
// Rename, Rename twice, messages PEPLEINFODLOGFILTER has no handler for,
// then OK.
struct Step {
  Kind kind{Kind::command};
  std::uint16_t id{};
  std::uint16_t code{};
};
const std::vector<Step>& steps() {
  static const std::vector<Step> kSteps{
      {Kind::command, 2, 0},  {Kind::command, 99, 0},
      {Kind::command, 0, 0},  {Kind::command, 4, 0},
      {Kind::command, 4, 1},  {Kind::button_down, 0, 0},
      {Kind::mouse_move, 0, 0}, {Kind::key_up, 13, 0},
      {Kind::timer, 1, 0},    {Kind::command, 9, 0},
      {Kind::command, 1, 0},  {Kind::command, 4, 0},
  };
  return kSteps;
}

struct ParityCounts {
  int runs{};
  int changed{};
  int renames{};
  std::set<std::uint16_t> dialogs{};
};

void run_parity(const simtower::OriginalResources& resources,
                const simtower::OriginalPartTable& part,
                const OriginalTdtDocument& base, std::size_t person,
                OriginalPersonInformationContext context, bool saves,
                bool with_document, ParityCounts& counts) {
  auto legacy_document = std::make_unique<OriginalTdtDocument>(base);
  auto shared_document = std::make_unique<OriginalTdtDocument>(base);
  const auto information = simtower::original_person_information(
      resources, base, part, person, context);
  assert(information.valid);
  const auto dtmp = simtower::parse_original_dtmp(
      resources.find("DTMP", information.dialog_id));
  Log legacy_log;
  Log shared_log;
  LegacyDialog legacy{resources,
                      part,
                      with_document ? legacy_document.get() : nullptr,
                      person,
                      context,
                      information,
                      saves,
                      &legacy_log};
  simtower::OriginalPersonInformationController controller(
      person, context, information, dtmp);
  RecordingOps ops(shared_log);
  RecordingHost host(resources, part,
                     with_document ? shared_document.get() : nullptr, saves,
                     shared_log);
  assert(legacy.init() == controller.initial_focus(ops));
  assert(legacy_log == shared_log);
  std::optional<int> shared_ended{};
  for (const auto& step : steps()) {
    // The old procedure only reached its branches for WM_COMMAND; every
    // other message returned FALSE.
    bool legacy_handled = false;
    if (step.kind == Kind::command) legacy_handled = legacy.command(step.id);
    OriginalDialogSignal signal{step.kind, step.id, step.code};
    bool handled = !legacy_handled;
    const auto outcome = controller.signal(signal, ops, host, &handled);
    // The Win32 procedure's close tail (DS:31a4 cleared, ReleaseCapture,
    // EndDialog).
    if (outcome.result) {
      shared_log.push_back("DS31a4 0");
      shared_log.push_back("ReleaseCapture");
    }
    assert(handled == legacy_handled);
    assert(shared_log == legacy_log);
    shared_ended = outcome.result;
    assert(shared_ended == legacy.ended);
    assert(controller.changed() == legacy.changed);
    assert(same_information(controller.information(), legacy.information));
    if (shared_ended) break;
  }
  assert(shared_ended == std::optional<int>(1));
  assert(simtower::serialize_original_tdt_game_save(*shared_document) ==
         simtower::serialize_original_tdt_game_save(*legacy_document));
  ++counts.runs;
  if (controller.changed()) ++counts.changed;
  counts.renames += legacy.renames;
  counts.dialogs.insert(information.dialog_id);
}

void parity_tests(const simtower::OriginalResources& resources,
                  const simtower::OriginalPartTable& part,
                  const OriginalTdtDocument& document) {
  ParityCounts counts{};
  std::set<std::string> kinds{};
  std::size_t valid = 0;
  for (std::size_t person = 0; person < document.people_count; ++person) {
    const auto information = simtower::original_person_information(
        resources, document, part, person);
    if (!information.valid) continue;
    ++valid;
    // Every kind of person (the default name) twice, and every 97th.
    const bool first_of_kind = kinds.insert(information.display_name).second;
    if (!first_of_kind && valid % 97U != 0U) continue;
    using Context = OriginalPersonInformationContext;
    for (const auto context : {Context::main_world, Context::transport_dialog,
                               Context::facility_dialog}) {
      for (const bool saves : {false, true}) {
        run_parity(resources, part, document, person, context, saves, true,
                   counts);
      }
      run_parity(resources, part, document, person, context, true, false,
                 counts);
    }
  }
  std::printf("person parity: %zu valid persons, %d runs, %d changed, %d "
              "renames, %zu kinds, %zu dialogs\n",
              valid, counts.runs, counts.changed, counts.renames, kinds.size(),
              counts.dialogs.size());
  assert(counts.runs > 0 && counts.changed > 0 && counts.renames > 0);
  assert(counts.dialogs.contains(763U) && counts.dialogs.contains(764U));
}

// The exit latch against the frozen run_original_person_information_dialog
// tail: the predicate, then phase 1.
void latch_tests() {
  for (const bool active : {false, true}) {
    for (const std::uint32_t selected : {5U, 6U}) {
      for (const std::uint16_t phase : {0U, 2U}) {
        simtower::OriginalFindMarkerState legacy{};
        if (active) {
          legacy.cell_x = 10;
          legacy.floor = 12;
        }
        legacy.selected_person = selected;
        legacy.phase = phase;
        auto shared = legacy;
        if (simtower::original_person_information_sets_find_exit_latch(
                legacy, 5U)) {
          legacy.phase = 1U;
        }
        const bool set =
            simtower::apply_original_person_information_exit(shared, 5U);
        assert(set == (active && selected == 5U));
        assert(shared.phase == legacy.phase &&
               shared.selected_person == legacy.selected_person &&
               shared.cell_x == legacy.cell_x && shared.floor == legacy.floor);
      }
    }
  }
}

// Records the scaled-DIB calls (the portrait) and the text, nothing else.
class PortraitRecorder final : public simtower::OriginalDrawContext {
 public:
  Log calls;
  int save() override { return 1; }
  void restore(int) override {}
  void fill_rect(const simtower::OriginalRect&, simtower::OriginalRgb rgb)
      override {
    calls.push_back("fill " + std::to_string(rgb));
  }
  void intersect_clip(const simtower::OriginalRect&) override {}
  void draw_dib(std::span<const std::byte>, int, int) override {}
  void push_solid_pen(simtower::OriginalRgb) override {}
  void pop_pen() override {}
  void move_to(int x, int y) override {
    calls.push_back("move " + std::to_string(x) + "," + std::to_string(y));
  }
  void line_to(int, int) override {}
  void push_font(std::int16_t height) override {
    calls.push_back("font " + std::to_string(height));
  }
  void pop_font() override {}
  void set_text_color(simtower::OriginalRgb) override {}
  void set_bk_transparent() override {}
  void set_text_align(std::uint16_t flags) override {
    calls.push_back("align " + std::to_string(flags));
  }
  void text_out(std::string_view text) override {
    calls.push_back("text " + std::string(text));
  }
  void draw_text_single_line(std::string_view,
                             const simtower::OriginalRect&) override {}
  int text_extent_cx(std::string_view) override { return 0; }
  void stretch_pixels(std::span<const std::uint32_t>, int, int,
                      const simtower::OriginalRect&,
                      const simtower::OriginalRect&) override {}
  void draw_dib_region_scaled(
      std::span<const std::byte> resource,
      const simtower::OriginalRect& source,
      const simtower::OriginalRect& destination) override {
    resource_ = resource.data();
    calls.push_back("scaled " + rect(source) + " " + rect(destination));
  }
  const std::byte* resource_{};

 private:
  static std::string rect(const simtower::OriginalRect& r) {
    return std::to_string(r.left) + "," + std::to_string(r.top) + "," +
           std::to_string(r.right) + "," + std::to_string(r.bottom);
  }
};

// The portable painter: the portrait call carries the Win32 painter's
// draw_original_dib_region_scaled arguments, the texts their positions, and
// on the canvas the client is fully painted, the portrait and name are
// drawn and 764 has no meters.
void painter_tests(const simtower::OriginalResources& resources,
                   const simtower::OriginalPartTable& part,
                   const OriginalTdtDocument& document) {
  auto& font = simtower::OriginalCanvasFont::bundled();
  std::set<std::string> painted{};
  int wide_frames = 0;
  for (std::size_t person = 0; person < document.people_count; ++person) {
    const auto information = simtower::original_person_information(
        resources, document, part, person);
    if (!information.valid ||
        !painted.insert(information.display_name).second) {
      continue;
    }
    const auto dtmp = simtower::parse_original_dtmp(
        resources.find("DTMP", information.dialog_id));
    // The Win32 painter's portrait arguments
    // (paint_original_person_dialog in native_main.cpp).
    PortraitRecorder recorder;
    simtower::paint_original_person_portrait(recorder, resources, dtmp,
                                             information);
    const auto& item2 = dtmp.rectangles[1];
    if (information.portrait_frame >= 0) {
      const int width = information.portrait_frame >= 6 ? 16 : 8;
      if (width == 16) ++wide_frames;
      const int x = item2.left + 20 - (width == 16 ? 8 : 0);
      const int y = item2.top + 2;
      const int source_x = information.portrait_frame * 8;
      assert(recorder.calls.size() == 1U);
      assert(recorder.calls[0] ==
             "scaled " + std::to_string(source_x) + ",0," +
                 std::to_string(source_x + width) + ",24 " +
                 std::to_string(x) + "," + std::to_string(y) + "," +
                 std::to_string(x + width * 2) + "," + std::to_string(y + 47));
      const int bitmap =
          information.portrait_variant ==
                  simtower::OriginalPersonPortraitVariant::named
              ? 702
          : information.portrait_variant ==
                  simtower::OriginalPersonPortraitVariant::vip
              ? 703
              : 700;
      assert(recorder.resource_ == resources.find("BITMAP", bitmap).data());
    } else {
      assert(recorder.calls.empty());
    }

    simtower::OriginalWorldRaster raster{};
    raster.width = dtmp.width_or_header;
    raster.height = dtmp.height_or_header;
    raster.pixels.assign(
        static_cast<std::size_t>(raster.width) * raster.height, 0x123456U);
    simtower::OriginalCanvasDrawContext context(raster, font);
    simtower::paint_original_person_information(
        context, resources, dtmp, information,
        {0, 0, raster.width, raster.height},
        [](std::uint16_t id) { return id == 1U || id == 4U; });
    for (int y = 6; y < raster.height - 6; ++y) {
      for (int x = 6; x < raster.width - 6; ++x) {
        assert(raster.at(x, y) != 0x123456U);
      }
    }
    const auto count = [&](const simtower::OriginalDtmpRect& r,
                           auto&& predicate) {
      int found = 0;
      for (int y = r.top; y < r.bottom; ++y) {
        for (int x = r.left; x < r.right; ++x) {
          if (predicate(raster.at(x, y))) ++found;
        }
      }
      return found;
    };
    const auto black = [](std::uint32_t c) { return c == 0U; };
    assert(count(dtmp.rectangles[2], black) > 0);  // the name
    if (information.portrait_frame >= 0) {
      assert(count(item2, black) > 0);  // the portrait's figure
    }
    // The meters: item 6 holds the evaluation's band colours for 763; 764
    // zeroes items 6 and 8.
    const auto meter_colour = [](std::uint32_t c) {
      return c == 0x0000FFU || c == 0xFFFF00U || c == 0xFF0000U;
    };
    if (information.dialog_id == 763U) {
      assert(information.evaluation.visible ==
             (count(dtmp.rectangles[5], meter_colour) > 0));
    } else {
      assert(dtmp.rectangles[5].left == 0 && dtmp.rectangles[7].left == 0);
    }
  }
  std::printf("person painter: %zu kinds painted, %d wide frames\n",
              painted.size(), wide_frames);
  assert(painted.size() >= 6U);
}

// The canvas scaled DIB: nearest-neighbour doubling of a source region.
void scaled_dib_tests(const simtower::OriginalResources& resources) {
  auto& font = simtower::OriginalCanvasFont::bundled();
  const auto bitmap = resources.find("BITMAP", 700);
  simtower::OriginalWorldRaster unscaled{};
  unscaled.width = 8;
  unscaled.height = 24;
  unscaled.pixels.assign(8U * 24U, 0x123456U);
  {
    simtower::OriginalCanvasDrawContext context(unscaled, font);
    context.draw_dib(bitmap, -16, 0);  // frame 2's columns at x 0
  }
  simtower::OriginalWorldRaster scaled{};
  scaled.width = 16;
  scaled.height = 48;
  scaled.pixels.assign(16U * 48U, 0x123456U);
  {
    simtower::OriginalCanvasDrawContext context(scaled, font);
    context.draw_dib_region_scaled(bitmap, {16, 0, 24, 24}, {0, 0, 16, 48});
  }
  for (int y = 0; y < 48; ++y) {
    for (int x = 0; x < 16; ++x) {
      assert(scaled.at(x, y) == unscaled.at(x / 2, y / 2));
    }
  }
  bool threw = false;
  try {
    simtower::OriginalCanvasDrawContext context(scaled, font);
    context.draw_dib_region_scaled(bitmap, {0, 0, 8, 999}, {0, 0, 16, 48});
  } catch (const std::exception&) {
    threw = true;
  }
  assert(threw);
}

}  // namespace

// A WM_COMMAND that reaches the Person dialog while Rename Person runs
// (Rename is owned by Main, so the Person dialog stays enabled) re-enters
// signal(); the outer Rename must still report handled (TRUE).
void nested_signal_test(const simtower::OriginalResources& resources,
                        const simtower::OriginalPartTable& part,
                        const OriginalTdtDocument& base) {
  std::size_t person = base.people_count;
  for (std::size_t index = 0; index < base.people_count; ++index) {
    if (simtower::original_person_information(resources, base, part, index)
            .valid) {
      person = index;
      break;
    }
  }
  assert(person < base.people_count);
  auto document = std::make_unique<OriginalTdtDocument>(base);
  const auto information = simtower::original_person_information(
      resources, base, part, person);
  simtower::OriginalPersonInformationController controller(
      person, OriginalPersonInformationContext::main_world, information,
      simtower::parse_original_dtmp(
          resources.find("DTMP", information.dialog_id)));
  Log log;
  RecordingOps ops(log);
  class NestingHost final : public simtower::OriginalPersonInformationHost {
   public:
    NestingHost(const simtower::OriginalResources& resources,
                const simtower::OriginalPartTable& part,
                OriginalTdtDocument* document,
                simtower::OriginalPersonInformationController& controller,
                RecordingOps& ops)
        : resources_(resources),
          part_(part),
          document_(document),
          controller_(controller),
          ops_(ops) {}
    OriginalTdtDocument* document() override { return document_; }
    const simtower::OriginalPartTable& part() override { return part_; }
    const simtower::OriginalResources& resources() override {
      return resources_;
    }
    bool rename_person(std::size_t) override {
      bool inner = true;
      (void)controller_.signal({Kind::command, 2, 0}, ops_, *this, &inner);
      nested_unhandled = !inner;
      return false;
    }
    void mark_document_dirty() override {}
    void set_cursor(std::uint16_t) override {}
    bool nested_unhandled{};

   private:
    const simtower::OriginalResources& resources_;
    const simtower::OriginalPartTable& part_;
    OriginalTdtDocument* document_;
    simtower::OriginalPersonInformationController& controller_;
    RecordingOps& ops_;
  };
  NestingHost host(resources, part, document.get(), controller, ops);
  bool handled = false;
  const auto outcome = controller.signal(
      {Kind::command, simtower::kOriginalPersonInfoRenameId, 0}, ops, host,
      &handled);
  assert(!outcome.result && handled && host.nested_unhandled);
}

int main(int argc, char** argv) {
  latch_tests();
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
  scaled_dib_tests(resources);
  parity_tests(resources, startup.part, *document);
  nested_signal_test(resources, startup.part, *document);
  painter_tests(resources, startup.part, *document);
  std::printf("person information controller tests passed\n");
  return 0;
}
