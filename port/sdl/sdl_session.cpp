#include "sdl_session.hpp"

#include "sdl_dialog_descriptors.hpp"

#include "original_about.hpp"
#include "original_alert.hpp"
#include "original_canvas_font.hpp"
#include "original_construction.hpp"
#include "original_dialog_model.hpp"
#include "original_draw_canvas.hpp"
#include "original_resources.generated.hpp"
#include "original_tables.hpp"
#include "original_startup_data.hpp"
#include "original_tdt_file.hpp"
#include "original_time.hpp"
#include "original_palette_frame.hpp"
#include "original_ui.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

namespace simtower::sdl {
namespace {

// Event-dialog default choices: the answer when no input answers the dialog
// (original_dialog_model.hpp explains each).
constexpr std::uint16_t kBombOfferChoice =
    kOriginalEventDialogBombOfferDefault;
constexpr std::uint16_t kFireCrewOfferChoice =
    kOriginalEventDialogFireCrewOfferDefault;
constexpr std::uint16_t kAcknowledgeChoice =
    kOriginalEventDialogAcknowledgeDefault;

void log_line(const std::string& text) {
  std::fprintf(stderr, "simtower_sdl: %s\n", text.c_str());
}

// find() bounds-checks each slice but never compares the pack with the
// compiled descriptor table, so a mismatched pack would yield wrong bytes.
void verify_pack_extent(std::span<const std::byte> pack) {
  std::size_t required = 0U;
  for (const auto& descriptor : generated::kResources) {
    required = std::max<std::size_t>(
        required, static_cast<std::size_t>(descriptor.offset) +
                      static_cast<std::size_t>(descriptor.size));
  }
  if (pack.size() != required) {
    throw std::runtime_error(
        "Resource pack size " + std::to_string(pack.size()) +
        " does not match the compiled descriptor table (" +
        std::to_string(required) + " bytes)");
  }
}

}  // namespace

// The SDL host of the shared world input (original_world_input.hpp). Win32
// window operations become session state: panel dirty flags, a plain
// viewport, a tracked capture owner, and logged dialog requests answered
// with fixed or scripted values. Every request it logs uses a RequestKind
// that only input reaches, so runs without input keep their digests.
class Session::WorldInputHost final : public OriginalWorldInputHost {
 public:
  explicit WorldInputHost(Session& session) : s_(session) {}

  // The clock, DS:b3ae, the notification line and the Command
  // invalidation are the session's idle-pass implementations.
  OriginalHostServices& services() override {
    return static_cast<OriginalIdlePassHost&>(s_);
  }
  // SIMTOWER.INI BeepOnly (DS:de28) has no SDL profile yet: off.
  bool beep_only() override { return false; }
  bool modal_input_locked() override { return s_.modal_depth_ != 0; }
  // DS:77c0 (only Person Information sets it, not ported yet).
  bool find_exit_latch_set() override { return s_.find_marker_.phase != 0U; }
  void reset_find_marker() override {
    reset_original_find_marker(s_.find_marker_);
  }
  bool primary_button_down() override { return s_.left_button_down_; }

  void set_capture(OriginalInputSurface surface) override {
    s_.capture_ = surface;
    s_.log_request(RequestKind::capture, {static_cast<int>(surface)});
  }
  void release_capture() override {
    s_.capture_.reset();
    s_.log_request(RequestKind::capture, {-1});
  }
  // TEMP: the custom CURSOR resources are not decoded yet; the window host
  // maps the id to an SDL system cursor.
  void set_cursor(std::uint16_t cursor_id) override {
    s_.cursor_id_ = cursor_id;
  }
  void refresh_cursor() override {
    // 1258:0505 with the panel under the last pointer.
    if (!s_.last_pointer_) return;
    const int x = s_.last_pointer_->x + s_.layout_.main_client.x;
    const int y = s_.last_pointer_->y + s_.layout_.main_client.y;
    const auto plan = original_main_cursor_point_plan(
        s_.layout_.map_outer.contains(x, y), s_.map_visible_,
        s_.layout_.info_outer.contains(x, y), s_.info_visible_,
        s_.layout_.command_outer.contains(x, y), s_.command_visible_,
        s_.layout_.main_client.contains(x, y), false,
        s_.world_input_.build_mode_enabled, s_.world_input_.command_mode,
        s_.capture_ == OriginalInputSurface::main);
    if (plan.set_cursor) s_.cursor_id_ = plan.selector;
  }

  OriginalWorldPoint main_view() override {
    return {s_.viewport_.x, s_.viewport_.y};
  }
  std::optional<OriginalMapRect> main_client() override {
    return OriginalMapRect{0, 0, s_.viewport_.width, s_.viewport_.height};
  }
  void set_main_view(int x, int y) override {
    // set_original_view: SetScrollInfo's clamp, then 1080:0000/0054's
    // camera refresh (synchronous 1080:0a1e(1), Map, Command, Map focus).
    s_.log_request(RequestKind::view, {x, y});
    s_.scroll_to(x, y);
  }
  void set_main_scroll_position(int x, int y) override {
    s_.log_request(RequestKind::view, {x, y, 0});
    s_.set_viewport_origin(x, y);
  }

  void mark_document_dirty() override { s_.document_dirty_ = true; }
  void invalidate_main() override {
    s_.main_backing_dirty_ = true;
    s_.present_requested_ = true;
  }
  void mark_main_backing_dirty() override { s_.main_backing_dirty_ = true; }
  // The panels are recomposed on every present, so the remaining
  // invalidations only request one.
  void invalidate_map() override { s_.present_requested_ = true; }
  // 1118:0000: invalidate and synchronously repaint the whole Info client.
  void present_info_status() override {
    s_.paint_info();
    s_.present_requested_ = true;
  }
  void present_command_now() override { s_.present_requested_ = true; }
  void resize_command(int outer_height) override {
    s_.command_outer_height_ = outer_height;
    s_.layout_ = make_window_layout(s_.viewport_.width, s_.viewport_.height,
                                    outer_height);
  }
  void request_main_pass(OriginalMainSurfacePass pass,
                         bool synchronous) override {
    // request_original_main_surface_pass: UpdateWindow runs the pending
    // pass at once; otherwise the next WM_PAINT does, before the next idle
    // pass.
    if (synchronous) {
      s_.rebuild_main(pass, true);
      s_.present_requested_ = true;
    } else {
      s_.pending_main_pass_ = pass;
      s_.main_backing_dirty_ = true;
      s_.present_requested_ = true;
    }
  }
  void hide_palette(OriginalAuxiliaryWindow kind) override {
    bool* visible = kind == OriginalAuxiliaryWindow::command
                        ? &s_.command_visible_
                        : kind == OriginalAuxiliaryWindow::info
                              ? &s_.info_visible_
                              : &s_.map_visible_;
    const auto plan = original_auxiliary_visibility_plan(
        OriginalAuxiliaryVisibilityTrigger::close_box, *visible);
    *visible = plan.visible;
    s_.log_request(RequestKind::panel, {static_cast<int>(kind), *visible});
    s_.present_requested_ = true;
  }

  void construction_status(std::uint16_t code) override {
    ++s_.host_requests_.status;
    const bool shown = set_original_info_construction_status(
        s_.resources_, s_.info_status_, code, s_.coarse_tick_now());
    if (shown) s_.paint_info();
    s_.log_request(RequestKind::construction_status, {code, shown});
    s_.tally("construction status");
  }
  void command_status(std::uint16_t code) override {
    ++s_.host_requests_.status;
    const bool shown = set_original_info_command_status(
        s_.resources_, s_.info_status_, code, s_.coarse_tick_now());
    if (shown) s_.paint_info();
    s_.log_request(RequestKind::command_status, {code, shown});
    s_.tally("command status");
  }
  void play_sound(std::int32_t resource,
                  std::uint16_t repeat,
                  std::uint16_t priority) override {
    s_.sound(resource, "input");
    s_.play_audio(resource, repeat, priority);
  }
  void play_reserved_if_idle(std::int32_t resource,
                             std::uint16_t repeat,
                             std::uint32_t) override {
    s_.sound(resource, "input reserved");
    s_.play_audio(resource, repeat, 0U, true);
  }
  void play_event_sounds(
      std::span<const OriginalEventSoundRequest> requests) override {
    s_.play_event_sounds(requests);
  }
  void beep() override {
    s_.log_request(RequestKind::beep, {});
    s_.tally("beep");
  }
  void audio_pump() override {
    if (s_.audio_) s_.audio_->pump(s_.clock_ms_());
  }
  void audio_transition(OriginalIdleAudioTransition transition) override {
    if (!s_.audio_) return;
    if (transition == OriginalIdleAudioTransition::activate) {
      s_.audio_->activate();
    } else if (transition == OriginalIdleAudioTransition::deactivate) {
      s_.audio_->deactivate();
    }
  }
  void sample_auxiliary_pointer_tick() override {
    s_.last_auxiliary_tick_ = s_.coarse_tick_now();
  }

  int alert(int resource_id, std::string_view text) override {
    // The Win32 host: show_original_alert(id, {text, {}, {}, {}}).
    return s_.show_alert(resource_id, {text, {}, {}, {}});
  }
  int event_dialog(const OriginalEventDialogRequest& request) override {
    return s_.run_event_dialog(request, kAcknowledgeChoice, "construction");
  }
  void open_elevator_control(std::size_t elevator_index,
                             std::int16_t pointer_x,
                             std::int16_t pointer_y) override {
    ++s_.host_requests_.dialogs;
    s_.log_request(RequestKind::elevator_control,
                   {static_cast<std::int64_t>(elevator_index), pointer_x,
                    pointer_y});
    s_.tally("elevator control");
    log_line("elevator control " + std::to_string(elevator_index));
  }
  void transport_information(const OriginalMagnifierTarget& target) override {
    ++s_.host_requests_.dialogs;
    s_.log_request(RequestKind::info_dialog,
                   {static_cast<int>(target.kind), target.dialog_id});
    s_.tally("transport information");
  }
  void facility_information(std::int16_t floor,
                            std::size_t tenant_index) override {
    ++s_.host_requests_.dialogs;
    s_.log_request(RequestKind::info_dialog,
                   {-1, floor, static_cast<std::int64_t>(tenant_index)});
    s_.tally("facility information");
    log_line("facility information floor " + std::to_string(floor) +
             " tenant " + std::to_string(tenant_index));
  }
  void person_information(std::size_t person_index) override {
    ++s_.host_requests_.dialogs;
    s_.log_request(RequestKind::info_dialog,
                   {-2, static_cast<std::int64_t>(person_index)});
    s_.tally("person information");
  }
  OriginalWorldPoint command_outer_origin() override {
    return {s_.layout_.command_outer.x, s_.layout_.command_outer.y};
  }
  bool run_command_selector(
      OriginalCommandSelectorSession& session) override {
    return s_.run_selector_modal(session);
  }

  void consume_person_family_dispatches(
      std::span<const OriginalPersonFamilyDispatchResult> dispatches)
      override {
    for (const auto& dispatch : dispatches) {
      consume_original_person_family_dispatch(
          static_cast<OriginalIdlePassHost&>(s_), dispatch);
    }
  }

 private:
  Session& s_;
};

// The SDL host of the shared File transactions (original_tower_file.hpp).
// Pickers come from the scripted queue or the window's native dialogs;
// prompts and alerts are message boxes of the dialog runtime when an input
// source runs them, and otherwise answered with the scripted alert answer
// (1 = Yes/OK, the default; 2 = No/Cancel). Either way they are logged.
class Session::TowerFileHost final : public OriginalTowerFileHost {
 public:
  explicit TowerFileHost(Session& session) : s_(session) {}

  OriginalTdtDocument* document() override {
    return s_.document_ ? &*s_.document_ : nullptr;
  }
  std::filesystem::path tower_path() override { return s_.tower_path_; }
  std::filesystem::path tower_title() override { return s_.tower_title_; }
  void commit_tower_identity(const std::filesystem::path& path,
                             const std::filesystem::path& title) override {
    s_.tower_path_ = path;
    s_.tower_title_ = title;
    s_.document_dirty_ = false;
    s_.log_request(RequestKind::file, {1});
    log_line("tower is now " + path.string() + " (" + s_.window_title() +
             ")");
  }
  void store_view_in_header() override {
    s_.document_->header.view_x = static_cast<std::uint16_t>(s_.viewport_.x);
    s_.document_->header.view_y = static_cast<std::uint16_t>(s_.viewport_.y);
  }
  // TEMP: palettes are panels in the one window, so there is no
  // activation order to demote; only the Arrow cursor is restored.
  void begin_file_dialog() override { s_.cursor_id_ = 0U; }
  void end_file_dialog() override {}
  std::optional<OriginalPickedTowerFile> pick_open_file() override {
    auto path = s_.next_picked_file(false);
    if (!path) return std::nullopt;
    return OriginalPickedTowerFile{std::move(*path), {}};
  }
  std::optional<std::filesystem::path> pick_save_file() override {
    return s_.next_picked_file(true);
  }
  bool confirm_replace(const std::filesystem::path& path) override {
    // MessageBeep + MB_YESNO: Win32 replaces unless the result is IDNO.
    // TEMP: the MessageBeep is not reproduced (no beep request is logged).
    const auto raw = s_.run_message_box(
        original_replace_existing_message_box(), kReplaceBoxLabel);
    // The literal File box is MB_YESNO: no third answer.
    const int answer = s_.take_alert_answer(
        raw ? std::optional<int>(*raw == kOriginalAlertResultNo ? 2 : 1)
            : std::nullopt,
        false, "the replace prompt has two buttons");
    s_.log_request(RequestKind::alert, {-1, answer});
    s_.tally("replace prompt");
    log_line("replace " + path.string() + "? -> " +
             (answer == 2 ? "no" : "yes"));
    return answer != 2;
  }
  // The 8-character basename rule measured from the platform's directory
  // separator: '/' off Windows (where '\\' is an ordinary name character),
  // the original's backslash (and '/') on Windows.
  [[nodiscard]] bool basename_rule_uses_file_name_only() const override {
#ifdef _WIN32
    return false;
#else
    return true;
#endif
  }
  [[nodiscard]] std::wstring_view path_separators() const override {
#ifdef _WIN32
    return L"\\/";
#else
    return L"/";
#endif
  }
  void invalid_filename() override {
    (void)s_.run_message_box(original_invalid_filename_message_box(),
                             kInvalidFilenameBoxLabel);
    s_.log_request(RequestKind::alert, {-2, 1});
    s_.tally("invalid filename");
    log_line("That is not a valid filename.");
  }
  void file_error(std::uint16_t strl_index) override {
    (void)s_.world_host_->alert(
        1000, original_strl_entry(s_.resources_.find("STRL", 1002),
                                  strl_index));
  }
  int transition_alert(std::uint16_t action_index) override {
    if (s_.audio_) s_.audio_->stop_all(true);
    // The Win32 host: ALRT/1001 with ^0 = the file title and ^1 = the
    // STRL/1001 action ("Save the tower ... before closing?").
    const auto action = original_strl_entry(
        s_.resources_.find("STRL", 1001), action_index);
    const auto title = s_.tower_title_.string();
    return s_.show_alert(1001, {title, action, {}, {}});
  }
  void remove_recent_file_items() override { s_.remove_recent_file_items(); }
  void install_empty_document() override { s_.install_empty_document(); }
  void install_loaded_document(OriginalTdtDocument document) override {
    s_.install_document(std::move(document));
  }
  void begin_new_tower() override { s_.new_tower(); }

 private:
  Session& s_;
};

// The SDL host of the shared WM_COMMAND dispatcher (original_main_command.
// hpp). Dialogs other than the event dialogs, the alerts and About, help,
// and window z-order are logged requests until their runtimes exist. Every
// request it logs uses a RequestKind that only input reaches, except the
// hidden 9000/9002 event path, which --event drives with the calls
// trigger_event made before.
class Session::MainCommandHost final : public OriginalMainCommandHost {
 public:
  explicit MainCommandHost(Session& session) : s_(session) {}

  // Whether the last 9000/9002 offer started its event (trigger_event).
  [[nodiscard]] bool offer_started() const { return offer_started_; }
  void reset_offer() { offer_started_ = false; }

  bool modal_input_locked() override { return s_.modal_depth_ != 0; }
  int event_dialog(const OriginalEventDialogRequest& request) override {
    return s_.run_event_dialog(request, kAcknowledgeChoice, "command");
  }
  void exit_command() override {
    // apply_original_main_shutdown_message(exit_command).
    auto plan = original_main_shutdown_plan(
        OriginalMainShutdownMessage::exit_command, s_.main_closing_, false);
    if (plan.request_confirmation) {
      plan = original_main_shutdown_plan(
          OriginalMainShutdownMessage::exit_command, s_.main_closing_,
          confirm_original_tower_transition(*s_.file_host_, 4));
    }
    if (plan.set_closing_latch) s_.main_closing_ = true;
    if (plan.stop_audio_channels && s_.audio_) s_.audio_->stop_all(true);
    if (plan.destroy_main_window) s_.quit_requested_ = true;
    s_.log_request(RequestKind::shutdown, {plan.destroy_main_window});
    log_line(plan.destroy_main_window ? "exit: Main destroyed"
                                      : "exit: cancelled");
  }
  OriginalTowerFileHost& tower_file() override { return *s_.file_host_; }
  bool& command_flag(OriginalMainCommandFlag flag) override {
    switch (flag) {
      case OriginalMainCommandFlag::people_animation:
        return s_.flags_.people_animation_enabled;
      case OriginalMainCommandFlag::effects_animation:
        return s_.flags_.effects_animation_enabled;
      case OriginalMainCommandFlag::fast_mode:
        break;
    }
    return s_.flags_.fast_mode_enabled;
  }
  void apply_menu_op(const OriginalMenuOp& op) override {
    s_.apply_menu_ops({&op, 1U});
  }
  OriginalTdtDocument* document() override {
    return s_.document_ ? &*s_.document_ : nullptr;
  }
  const OriginalPartTable& part() override { return s_.part_; }
  bool consume_fire_crew_resolution(
      const OriginalFireCrewResolution& resolution) override {
    auto context = s_.idle_pass_context();
    return consume_original_fire_crew_resolution(context, s_, resolution);
  }
  void fire_crew_hired() override {
    // InvalidateRect on Main and Info.
    s_.main_backing_dirty_ = true;
    s_.present_requested_ = true;
    s_.paint_info();
  }
  // As Win32 (g_audio != nullptr): without a runtime the Sound toggles are
  // no-ops. They are also grayed then (original_sound_menu_ops), so only a
  // direct dispatch reaches this. The session keeps the category words
  // (digest, menu checks) and mirrors them into the runtime.
  bool audio_available() override { return s_.audio_ != nullptr; }
  bool audio_category_enabled(std::size_t category) override {
    return s_.sound_categories_[category];
  }
  void set_audio_category_enabled(std::size_t category,
                                  bool enabled) override {
    s_.sound_categories_[category] = enabled;
    if (s_.audio_) s_.audio_->set_category_enabled(category, enabled);
  }
  void stop_all_audio(bool force) override {
    if (s_.audio_) s_.audio_->stop_all(force);
  }
  bool& auxiliary_visible(OriginalAuxiliaryWindow window) override {
    switch (window) {
      case OriginalAuxiliaryWindow::command: return s_.command_visible_;
      case OriginalAuxiliaryWindow::info: return s_.info_visible_;
      case OriginalAuxiliaryWindow::map: break;
    }
    return s_.map_visible_;
  }
  // TEMP: the panel z-order is fixed (kPanelZOrder), so the HWND_TOP /
  // HWND_TOPMOST raise is not reproduced yet.
  void show_auxiliary(OriginalAuxiliaryWindow window,
                      OriginalAuxiliaryInsertAfter insert_after) override {
    // The shown Info window repaints its whole client (WM_PAINT).
    if (window == OriginalAuxiliaryWindow::info) s_.paint_info();
    s_.log_request(RequestKind::panel,
                   {static_cast<int>(window), 1,
                    static_cast<int>(insert_after)});
    s_.present_requested_ = true;
  }
  void hide_auxiliary(OriginalAuxiliaryWindow window) override {
    s_.log_request(RequestKind::panel, {static_cast<int>(window), 0});
    s_.present_requested_ = true;
  }
  void finance_dialog() override {
    // The same logged request as before, then the dialog when an input
    // source runs it.
    dialog(kOriginalCommandFinance, "finance");
    (void)s_.run_finance_dialog();
  }
  void about_dialog() override {
    // The About modal when an input source runs it, then the same logged
    // request as before, so runs without input keep their digests.
    s_.run_about_dialog();
    dialog(kOriginalCommandAbout, "about");
  }
  void find_dialog(OriginalFindMode mode) override {
    dialog(mode == OriginalFindMode::person ? kOriginalCommandFindPerson
                                            : kOriginalCommandFindTenant,
           mode == OriginalFindMode::person ? "find person" : "find tenant");
    (void)s_.run_find_dialog(mode);
  }
  void help_contents() override {
    // WinHelp(SIMTOWER.HLP, HELP_CONTENTS = 3). TEMP: a logged request
    // until the help viewer exists.
    s_.log_request(RequestKind::help, {3});
    s_.tally("help contents");
    log_line("help contents requested (SIMTOWER.HLP, WinHelp command 3)");
  }
  bool bomb_offer() override {
    auto context = s_.idle_pass_context();
    offer_started_ = run_original_bomb_offer_boundary(context, s_);
    return offer_started_;
  }
  bool fire_offer() override {
    auto context = s_.idle_pass_context();
    offer_started_ = run_original_fire_offer_boundary(context, s_);
    return offer_started_;
  }
  void world_interaction_changed() override {
    mark_original_world_interaction_changed(*s_.world_host_);
  }
  void mark_document_dirty() override { s_.document_dirty_ = true; }
  void present_info_status() override {
    s_.paint_info();
    s_.present_requested_ = true;
  }
  void present_main_direct() override { s_.present_main(); }

 private:
  // Finance, Find and About are logged here; the dialog runtime runs them
  // when an input source drives them.
  void dialog(std::uint16_t command, const char* name) {
    ++s_.host_requests_.dialogs;
    s_.log_request(RequestKind::command_dialog, {command});
    s_.tally(std::string("dialog ") + name);
    log_line(std::string("dialog ") + name + " requested (" +
             std::to_string(command) + ")");
  }

  Session& s_;
  bool offer_started_{};
};

Session::Session(std::vector<std::byte> pack,
                 std::function<std::uint32_t()> clock_ms,
                 SessionFlags flags,
                 OriginalAudioDevice* audio_device)
    : pack_(std::move(pack)),
      clock_ms_(std::move(clock_ms)),
      flags_(flags),
      world_host_(std::make_unique<WorldInputHost>(*this)),
      command_host_(std::make_unique<MainCommandHost>(*this)),
      file_host_(std::make_unique<TowerFileHost>(*this)) {
  verify_pack_extent(pack_);
  // WinMain: the portable span constructor replaces from_current_module.
  resources_ = OriginalResources(std::span<const std::byte>(pack_));

  // WinMain constructs the runtime right after the resources and runs
  // 11c8:006b (initialize_original_audio); a failure turns the master
  // switch off, which also stops the ambient RNG draw. There is no
  // SIMTOWER.INI here, so the three categories keep the shipped profile
  // (all on).
  if (flags_.sound_enabled && audio_device) {
    audio_ = std::make_unique<OriginalAudioRuntime>(resources_, *audio_device,
                                                    clock_ms_);
    audio_->set_sound_enabled(true);
    if (!audio_->initialize()) {
      log_line("audio: no PCM output device, sound off");
      audio_->set_sound_enabled(false);
      audio_.reset();
      flags_.sound_enabled = false;
    }
  }

  // validate_original_startup_data: the same portable loader the
  // Win32 host calls, so a truncated or mismatched pack fails here. The
  // command ratings feed the Command panel, which is not drawn yet.
  auto startup = load_original_startup_data(resources_);
  part_ = std::move(startup.part);
  construction_costs_ = std::move(startup.construction_costs);
  rent_income_ = std::move(startup.rent_income);
  maintenance_costs_ = std::move(startup.maintenance_costs);
  world_input_.command_ratings = std::move(startup.command_ratings);
  // DS:3218 starts from the host flag until the first mouse message
  // publishes the live modifier (1058:0028).
  world_input_.control_modifier = flags_.world_control_modifier;

  // WinMain's TOWER_MENU and accelerator table, then MAINWNDPROC
  // WM_CREATE (Fast Mode check, debug items deleted) and 1128:0ae7's Sound
  // profile after the scrollbars. None of this touches game state.
  menu_ = parse_original_menu(resources_.find("MENU", "TOWER_MENU"));
  accelerators_ = parse_original_accelerators(
      resources_.find("ACCELERATOR", "TOWER_MENU"));
  apply_menu_ops(original_main_create_menu_ops(flags_.fast_mode_enabled));
  apply_menu_ops(
      original_sound_menu_ops(sound_categories_, flags_.sound_enabled));

  // Deliberately omitted Win32 startup steps (WinMain), none of which
  // touches the document RNG or simulation state:
  // - SIMTOWER.INI profile, logical palette, font bank, cursors and icon;
  // - the BITMAP/128 -> 256 splash and its 180-tick wait (1128:0042-00a7);
  // - the bootstrap reset_original_command_state(true),
  //   refresh_original_rating_command(rating, 0) and Map-focus adjustment
  //   (10d0:0ac2): New/Open reset the command state again before any pass;
  // - WAVE/20000 (play_startup_sound, which only the interactive window
  //   calls). The bootstrap's fire-menu update is applied to the menu
  //   below without a logged request.
  // WinMain's base palette, then its never-stored bootstrap tower
  // (its frame 0x09e5 selects the CLUT 1002->1001 transition).
  reset_original_palette_runtime(resources_, nullptr, palette_runtime_,
                                 coarse_tick_now());
  const auto startup_bootstrap = make_original_new_tdt();
  reset_original_palette_runtime(resources_, &startup_bootstrap,
                                 palette_runtime_, coarse_tick_now());
  const auto fire = original_fire_menu_op(
      original_fire_crew_menu_enabled_after_rebuild(startup_bootstrap));
  apply_menu_ops({&fire, 1U});
  // The Info window's first WM_PAINT, before any tower: the painter's
  // no-document defaults.
  info_raster_.width = kOriginalInfoWidth;
  info_raster_.height = kOriginalInfoClientTop + kOriginalInfoBackingHeight;
  info_raster_.pixels.assign(
      static_cast<std::size_t>(info_raster_.width) * info_raster_.height,
      0x00ffffffU);
  paint_info();
}

void Session::paint_info() {
  // paint_known_original_surface's draw_original_info on a white client;
  // the frame row is composed per present.
  OriginalCanvasDrawContext context(
      info_raster_, OriginalCanvasFont::bundled(), 0, 0,
      {0, 0, info_raster_.width, info_raster_.height});
  draw_original_info(context, resources_, document(), info_status_.text);
}

Session::~Session() = default;

std::uint32_t Session::coarse_tick_now() {
  // 1208:05e6 through original_runtime_coarse_tick: every coarse-clock read
  // first enters 11e0:0e84's 48-ms audio pump, which reaps voices and calls
  // the device, so this is not a const read.
  const std::uint32_t now = clock_ms_();
  if (audio_) audio_->pump(now);
  return original_coarse_tick(now);
}

void Session::play_startup_sound() {
  play_audio(20000, 0U, 1U);
}

void Session::play_audio(std::int32_t resource,
                         std::uint16_t repeat,
                         std::uint16_t priority,
                         bool reserved_if_idle) {
  // The Win32 host passes a fresh GetTickCount to each request
  // (play_original_event_sounds, Win32IdlePassHost::play_sound). Results
  // are discarded there too; the line below is diagnostic only.
  if (!audio_) return;
  const std::uint32_t now = clock_ms_();
  const bool started =
      reserved_if_idle
          ? audio_->play_reserved_if_idle(resource, repeat, now)
          : audio_->play_resource(resource, repeat, priority, now);
  if (flags_.log_sounds) {
    log_line("audio " + std::to_string(resource) + " repeat " +
             std::to_string(repeat) + " priority " +
             (reserved_if_idle ? std::string("5 (reserved if idle)")
                               : std::to_string(priority)) +
             " at " + std::to_string(now) +
             (started ? " started" : " refused"));
  }
}

void Session::set_viewport_origin(int x, int y) {
  // SetScrollInfo's range clamp, as installed by 1158:05ef / 1080:00d7.
  viewport_.x = original_main_scrollbar_resize_state(
                    x, viewport_.width, kOriginalWorldWidth)
                    .position;
  viewport_.y = original_main_scrollbar_resize_state(
                    y, viewport_.height, kOriginalWorldHeight)
                    .position;
}

void Session::resize(int width, int height) {
  const bool first_size = viewport_.width == 0 && viewport_.height == 0;
  viewport_.width = std::max(0, width);
  viewport_.height = std::max(0, height);
  layout_ = make_window_layout(viewport_.width, viewport_.height,
                               command_outer_height_);
  // Open dialogs keep their placement rule in the new desktop (a Win32
  // dialog stays put, but here the window is the desktop and shrinking it
  // must not leave a dialog outside).
  for (auto* dialog : dialogs_) {
    dialog->place(layout_.window_width, layout_.window_height);
  }
  if (first_size) {
    // Window creation: 1128:08d6 (create_original_windows) installs the
    // initial view, centred horizontally with the bottom 324 px above the
    // world floor, while the DS:02a4 gate (g_main_message_runtime_initialized,
    // set late in WinMain) makes the creation-time WM_SIZE a no-op. So only
    // mark the backing dirty for the first paint.
    const auto initial =
        original_initial_view(viewport_.width, viewport_.height);
    set_viewport_origin(initial.x, initial.y);
    main_backing_dirty_ = true;
    present_requested_ = true;
    return;
  }
  // Live restore/maximize WM_SIZE (main_window_proc): 1158:05ef clamps the
  // saved origin to the new ranges, then completes through the synchronous
  // 1080:0a1e(1) pass, which advances the sky decorations and the visible
  // facility people (both consume the document RNG).
  set_viewport_origin(viewport_.x, viewport_.y);
  rebuild_main(OriginalMainSurfacePass::rebuild_with_sky, true);
  present_requested_ = true;
}

void Session::scroll_to(int x, int y) {
  set_viewport_origin(x, y);
  // 1058:05f8: synchronous 1080:0a1e(1), which advances the sky and the
  // visible facility people (both consume the document RNG). Like
  // scroll_original this runs for every accepted request,
  // including one that is clamped back to the current origin at a world edge.
  rebuild_main(OriginalMainSurfacePass::rebuild_with_sky, true);
  present_requested_ = true;
}

void Session::scroll_by_code(bool horizontal, std::uint16_t scroll_code) {
  const int current = horizontal ? viewport_.x : viewport_.y;
  const int extent = horizontal ? viewport_.width : viewport_.height;
  const int world = horizontal ? kOriginalWorldWidth : kOriginalWorldHeight;
  const auto position = original_main_scroll_request_position(
      scroll_code, current, 0U, extent, world - extent);
  if (!position) return;
  if (horizontal) {
    scroll_to(*position, viewport_.y);
  } else {
    scroll_to(viewport_.x, *position);
  }
}

void Session::reset_simulation_state() {
  // reset_original_simulation_state.
  transfer_visuals_.clear();
  main_backing_dirty_ = true;
  if (!document_) {
    simulation_state_ = {};
    palette_runtime_ = {};
    return;
  }
  reset_original_loaded_simulation_state(*document_);
  const auto now = clock_ms_();
  simulation_state_.frame_time = document_->header.frame_time;
  simulation_state_.current_day = document_->header.current_day;
  simulation_state_.day_phase =
      original_day_phase(simulation_state_.frame_time);
  simulation_state_.calendar_phase =
      original_calendar_phase(simulation_state_.current_day);
  simulation_state_.last_tick = original_coarse_tick(now);
  // The first Fast Mode frame is immediately eligible, as in
  // reset_original_simulation_state.
  fast_frame_ms_ = now - kNativeFastModeFramePeriodMs;
  reset_original_palette_runtime(resources_, &*document_, palette_runtime_,
                                 original_coarse_tick(now));
}

void Session::reset_command_state() {
  // reset_original_command_state: the shared edit/command part, the Info
  // status and the Find marker.
  reset_original_world_input_state(world_input_, false);
  info_status_ = {};
  reset_original_find_marker(find_marker_);
}

void Session::install_document(OriginalTdtDocument document) {
  carry_original_process_random_state(document_ ? &*document_ : nullptr,
                                      document);
  document_ = std::move(document);
  reset_simulation_state();
  reset_command_state();
  // 10d0:0ac2 -> 1140:010d(rating, 0): replaces the rating TABL, resizes
  // Command and dirties the backing (no RNG). restore_original_derived_
  // view_position then clamps the header view, and the document
  // transition's Main step is a synchronous 1080:0a1e(1)
  // (present_original_document_transition_synchronously).
  refresh_original_rating_command(world_input_context(), *world_host_,
                                  document_->header.rating, 0U);
  set_viewport_origin(document_->header.view_x, document_->header.view_y);
  rebuild_main(OriginalMainSurfacePass::rebuild_with_sky, true);
  present_requested_ = true;
  document_->header.view_x = static_cast<std::uint16_t>(viewport_.x);
  document_->header.view_y = static_cast<std::uint16_t>(viewport_.y);
  // The transition's fire_menu_update step, on the menu only: the logged
  // fire_menu request stays the idle pass's (digest unchanged).
  const auto fire = original_fire_menu_op(
      original_fire_crew_menu_enabled_after_rebuild(*document_));
  apply_menu_ops({&fire, 1U});
  // The transition's last step (info_repaint, 1118:0000).
  paint_info();
}

void Session::install_empty_document() {
  auto empty_document = make_original_new_tdt();
  carry_original_process_random_state(document_ ? &*document_ : nullptr,
                                      empty_document);
  document_ = std::move(empty_document);
  reset_simulation_state();
  reset_command_state();
}

void Session::remove_recent_file_items() {
  // 10d0:003e/0238: GetSubMenu(menu, 1) is Options; a no-op for TOWER_MENU.
  const auto* options = original_sub_menu(menu_, 1U);
  apply_menu_ops(original_recent_file_removal_ops(
      options ? options->size() : 0U));
}

std::optional<std::filesystem::path> Session::next_picked_file(bool save) {
  const char* sheet = save ? "save as" : "open";
  std::optional<std::filesystem::path> result;
  int source = 2;  // 0 scripted, 1 picker, 2 none (cancel)
  if (!picked_files_.empty()) {
    result = std::move(picked_files_.front());
    picked_files_.pop_front();
    source = 0;
  } else if (auto scripted =
                 input_source_ ? input_source_->next_file_pick()
                               : std::nullopt) {
    result = std::move(*scripted);
    source = 0;
  } else if (file_picker_) {
    result = file_picker_->pick(save);
    source = 1;
  }
  log_request(RequestKind::picker, {save, result.has_value(), source});
  tally(std::string("picker ") + sheet);
  log_line(std::string("picker ") + sheet + " (" +
           (source == 0 ? "scripted" : source == 1 ? "native" : "none") +
           ") -> " + (result ? result->string() : std::string("cancel")));
  return result;
}

bool Session::choose_menu_command(std::uint16_t command) {
  const auto* item = find_original_menu_command(menu_, command);
  const bool dispatched = item && !item->grayed();
  log_request(RequestKind::menu, {command, dispatched, 0});
  tally("menu " + std::to_string(command));
  log_line("menu " + std::to_string(command) +
           (item ? (dispatched ? "" : " (grayed: not sent)")
                 : " (not in the menu: not sent)"));
  if (dispatched) (void)dispatch_original_main_command(*command_host_, command);
  return dispatched;
}

bool Session::accelerator(std::uint16_t virtual_key) {
  for (const auto& entry : accelerators_) {
    if ((entry.flags & kOriginalAcceleratorVirtualKey) == 0U ||
        entry.key != virtual_key) {
      continue;
    }
    const auto* item = find_original_menu_command(menu_, entry.command);
    const bool dispatched = !item || !item->grayed();
    log_request(RequestKind::menu, {entry.command, dispatched, 1});
    tally("accelerator " + std::to_string(entry.command));
    if (dispatched) {
      (void)dispatch_original_main_command(*command_host_, entry.command);
    }
    return dispatched;
  }
  return false;
}

std::string Session::window_title() const {
  const auto title = original_tower_window_title(tower_title_.wstring());
  const auto utf8 = std::filesystem::path(title).u8string();
  return std::string(utf8.begin(), utf8.end());
}

void Session::new_tower() {
  // begin_original_new_tower: 10d0:001d -> 086c keeps the prior
  // view, so the fresh header's zero view words are replaced first.
  remove_recent_file_items();
  auto document = make_original_new_tdt();
  document.header.view_x = static_cast<std::uint16_t>(viewport_.x);
  document.header.view_y = static_cast<std::uint16_t>(viewport_.y);
  install_document(std::move(document));
  tower_path_.clear();
  tower_title_ = "untitled";
  document_dirty_ = false;
  log_line("new tower");
}

void Session::open_tower(const std::filesystem::path& path) {
  // load_original_tower_from_path: an empty tower is installed
  // and reset before disk I/O, carrying the process RNG through both.
  remove_recent_file_items();
  install_empty_document();
  install_document(load_original_tdt_file(path));
  tower_path_ = path;
  tower_title_ = path.filename();
  document_dirty_ = false;
  log_line("opened " + path.string());
}

void Session::rebuild_main(OriginalMainSurfacePass pass,
                           bool advance_state,
                           bool full_frame_surface_dirty) {
  // rebuild_original_main_backing without Elevator Control isolation,
  // with the Find marker and the construction preview.
  const auto plan = original_main_surface_pass_plan(pass);
  if (advance_state) (void)advance_main_surface_state(pass);
  if (plan.clear_elevator_transfer_visuals_before_rebuild) {
    // Every 1080:0a1e context first calls 10a8:0000.
    transfer_visuals_.clear();
  }
  if (viewport_.width <= 0 || viewport_.height <= 0) return;
  main_raster_ = render_original_world(
      resources_, document(), viewport_.x, viewport_.y, viewport_.width,
      viewport_.height, transfer_visuals_,
      palette_runtime_.initialized ? &palette_runtime_.colors : nullptr,
      &sky_decorations_, world_input_.map_mode, nullptr, {},
      full_frame_surface_dirty);
  if (find_marker_.active()) {
    // 10e0:055b: BITMAP/21256 over the found target.
    const auto derived = palette_runtime_.initialized
                             ? OriginalWorldPalette{}
                             : original_world_palette(resources_, document());
    composite_original_find_marker(
        resources_,
        palette_runtime_.initialized ? palette_runtime_.colors : derived,
        find_marker_.cell_x, find_marker_.floor, viewport_.x, viewport_.y,
        main_raster_);
  }
  // 1090:03df/05ee: the construction outline over the rebuilt backing, as
  // rebuild_original_main_backing composites it.
  const auto preview_rect = original_world_input_preview_rect(
      world_input_, {viewport_.x, viewport_.y});
  if (plan.update_construction_preview_rect) {
    world_input_.preview_rect = preview_rect;
  }
  if (preview_rect) {
    composite_original_construction_preview(
        world_input_.selected_build_type, world_input_.pointer.client_x,
        world_input_.pointer.client_y, viewport_.x, viewport_.y,
        main_raster_);
  }
  main_backing_dirty_ = false;
  if (plan.consume_elevator_transfer_visuals) transfer_visuals_.clear();
}

bool Session::advance_main_surface_state(OriginalMainSurfacePass pass) {
  // advance_original_main_surface_state.
  if (!document_) return false;
  const auto plan = original_main_surface_pass_plan(pass);
  if (plan.advance_sky_decorations) {
    // 1080:0a1e calls 1048:03a3 before 1038:050e; it draws RNG first.
    (void)step_original_sky_decorations(
        resources_, *document_, sky_decorations_, viewport_.x, viewport_.y,
        viewport_.width, viewport_.height);
  }
  if (plan.advance_visible_facility_people) {
    const auto people = step_original_visible_facility_people(
        *document_, viewport_.x, viewport_.y, viewport_.width,
        viewport_.height, flags_.people_animation_enabled,
        world_input_.control_modifier);
    return people.dispatched_tenants != 0U;
  }
  return false;
}

void Session::present() {
  // A pending WM_PAINT without an idle pass: presentation only.
  present_main();
  present_requested_ = false;
}

void Session::present_main() {
  // 1158:0a3c: rebuild only if a model change dirtied the backing.
  if (main_backing_dirty_) {
    rebuild_main(OriginalMainSurfacePass::window_paint, false);
  }
  present_requested_ = true;
}

void Session::focus_event_coordinate(std::int16_t floor, std::int16_t x) {
  // focus_original_event_coordinate -> set_original_view, whose
  // camera refresh is a synchronous 1080:0a1e(1). Not cosmetic: it advances
  // the sky decorations and visible facility people.
  const auto view = original_facility_focus_view(x, floor, viewport_.width,
                                                 viewport_.height);
  ++host_requests_.focus;
  log_request(RequestKind::focus, {floor, x});
  tally("focus");
  log_line("focus floor " + std::to_string(floor) + " x " +
           std::to_string(x));
  set_viewport_origin(view.x, view.y);
  rebuild_main(OriginalMainSurfacePass::rebuild_with_sky, true);
  present_requested_ = true;
}

std::uint16_t Session::log_dialog(const OriginalEventDialogRequest& dialog,
                                  std::uint16_t choice,
                                  std::string_view meaning,
                                  bool play_dialog_sound) {
  // run_original_event_dialog stand-in.
  ++host_requests_.dialogs;
  log_request(RequestKind::dialog, {dialog.dialog_id, dialog.argument,
                                    dialog.wave_resource, choice});
  tally("dialog " + std::to_string(dialog.dialog_id) + " (" +
        std::string(meaning) + ")");
  log_line("dialog " + std::to_string(dialog.dialog_id) + " argument " +
           std::to_string(dialog.argument) + " wave " +
           std::to_string(dialog.wave_resource) + " -> " +
           std::to_string(choice) + " (" + std::string(meaning) + ")");
  if (play_dialog_sound && audio_ && dialog.valid()) {
    // run_original_event_dialog: 1068:0000 forcibly stops both mixer
    // channels, then submits the dialog's WAVE resource at priority four
    // before opening it.
    audio_->stop_all(true);
    if (dialog.wave_resource != 0) play_audio(dialog.wave_resource, 0U, 4U);
  }
  return choice;
}

std::uint16_t Session::run_event_dialog(
    const OriginalEventDialogRequest& dialog,
    std::uint16_t default_choice,
    std::string_view meaning) {
  // Without a driving input source, or for a request
  // run_original_event_dialog would not open, the logged stand-in exactly
  // as before the runtime.
  if (!input_source_ || !dialog.valid()) {
    return log_dialog(dialog, default_choice, meaning);
  }
  // 1068:0000: stop both channels and submit the WAVE, then prepare and
  // open the dialog (run_original_event_dialog's order).
  if (audio_) {
    audio_->stop_all(true);
    if (dialog.wave_resource != 0) play_audio(dialog.wave_resource, 0U, 4U);
  }
  auto modal = make_event_dialog(
      resources_, prepare_original_event_dialog(resources_, dialog),
      event_dialog_label(dialog.dialog_id));
  if (!drives(modal)) {
    return log_dialog(dialog, default_choice, meaning, false);
  }
  OriginalEventDialogController controller(dialog.argument);
  const auto result = run_dialog_modal(
      modal, [&](const DialogSignal& signal,
                 OriginalDialogOps& /*ops*/) -> std::optional<int> {
        const auto outcome = controller.signal(
            signal, document_ ? document_->header.balance : 0);
        // close_original_event_dialog: ALRT/1004 owned by the dialog.
        if (outcome.warn_insufficient_funds) (void)show_alert(1004, {});
        return outcome.result;
      });
  if (!result) return log_dialog(dialog, default_choice, meaning, false);
  // "<context>: answered" (the default's action word dropped, e.g.
  // "bomb offer: search" -> "bomb offer: answered").
  return log_dialog(dialog, static_cast<std::uint16_t>(*result),
                    std::string(meaning.substr(0, meaning.find(':'))) +
                        ": answered",
                    false);
}

bool Session::drives(const DialogWindow& dialog) {
  return input_source_ != nullptr && input_source_->drives_dialog(dialog);
}

std::optional<int> Session::run_message_box(
    const OriginalMessageBoxRequest& request, std::string label) {
  if (!input_source_) return std::nullopt;
  // TEMP: beep_first's MessageBeep is not reproduced.
  auto modal = make_message_box(request, std::move(label));
  if (!drives(modal)) return std::nullopt;
  return run_dialog_modal(
      modal, [](const DialogSignal& signal, OriginalDialogOps& /*ops*/) {
        return original_message_box_signal(signal).result;
      });
}

int Session::show_alert(int alert_id,
                        const std::array<std::string_view, 4>& substitutions) {
  const auto presentation =
      prepare_original_alert(resources_, alert_id, substitutions);
  std::optional<int> answered{};
  if (const auto raw = run_message_box(build_original_message_box(presentation),
                                       alert_dialog_label(alert_id))) {
    answered = original_alert_result(presentation.button_mode, *raw);
  }
  // Only a Yes/No/Cancel box (STRL/1001) has a third answer.
  const int answer = take_alert_answer(
      answered,
      presentation.button_mode == kOriginalAlertButtonsYesNoCancel,
      "ALRT/" + std::to_string(alert_id) + " has " +
          std::to_string(
              original_message_box_buttons(presentation.style).size()) +
          " button(s)");
  log_request(RequestKind::alert, {alert_id, answer});
  tally("alert " + std::to_string(alert_id));
  log_line("alert " + std::to_string(alert_id) + " \"" +
           presentation.message + "\" -> " + std::to_string(answer));
  return answer;
}

int Session::take_alert_answer(std::optional<int> driven, bool three_buttons,
                               std::string_view box) {
  if (!driven && alert_answer_ == 3 && !three_buttons) {
    throw ScriptFailure(
        2, (alert_answer_where_.empty() ? std::string("input")
                                        : alert_answer_where_) +
               ": alert 3 answers only a three-button alert, but " +
               std::string(box));
  }
  const int answer = driven.value_or(alert_answer_);
  alert_answer_ = 1;
  ++host_requests_.dialogs;
  return answer;
}

void Session::run_about_dialog() {
  if (!input_source_) return;
  auto modal = make_about_dialog(prepare_original_about_dialog(resources_));
  if (!drives(modal)) return;
  // Reactivates the mixer backend if the modal step throws (a ScriptFailure
  // from a dialog-* line) after the plan deactivated it.
  struct MixerBackend {
    OriginalAudioRuntime* audio{};
    bool deactivated{};
    MixerBackend() = default;
    MixerBackend(const MixerBackend&) = delete;
    MixerBackend& operator=(const MixerBackend&) = delete;
    ~MixerBackend() {
      if (deactivated && audio) audio->activate_mixer_backend();
    }
  } mixer;
  mixer.audio = audio_.get();
  for (const auto step : original_about_launcher_plan()) {
    using Step = OriginalAboutLauncherStep;
    switch (step) {
      case Step::stop_audio_channels:
        if (audio_) audio_->stop_all(true);
        break;
      case Step::deactivate_mixer_backend:
        if (audio_) audio_->deactivate_mixer_backend();
        mixer.deactivated = true;
        break;
      case Step::compact_global_heap:
        break;
      case Step::run_modal_dialog:
        (void)run_dialog_modal(
            modal, [](const DialogSignal& signal, OriginalDialogOps& ops) {
              return original_about_dialog_signal(signal, ops).result;
            });
        break;
      case Step::activate_mixer_backend:
        if (audio_) audio_->activate_mixer_backend();
        mixer.deactivated = false;
        break;
    }
  }
}

// FINDDIALOGFILTER's world: the document, Main, the marker and the alerts.
class Session::FindHost final : public OriginalFindDialogHost {
 public:
  explicit FindHost(Session& session) : s_(session) {}
  OriginalTdtDocument* document() override {
    return s_.document_ ? &*s_.document_ : nullptr;
  }
  int main_client_width() override { return s_.viewport_.width; }
  int main_client_height() override { return s_.viewport_.height; }
  void reset_marker() override { reset_original_find_marker(s_.find_marker_); }
  void start_marker(const OriginalFindResolution& resolution,
                    std::uint32_t selected_person) override {
    start_original_find_marker(s_.find_marker_, resolution, selected_person,
                               s_.coarse_tick_now());
  }
  void alert(int alert_id,
             const std::array<std::string, 4>& substitutions) override {
    (void)s_.show_alert(alert_id, {substitutions[0], substitutions[1],
                                   substitutions[2], substitutions[3]});
  }
  void entry_removed() override {
    s_.document_dirty_ = true;
    s_.log_request(RequestKind::file, {2});
  }
  void invalidate_main() override {
    s_.main_backing_dirty_ = true;
    s_.present_requested_ = true;
  }
  void set_arrow_cursor() override { s_.cursor_id_ = 0U; }

 private:
  Session& s_;
};

class Session::RenameHost final : public OriginalRenameDialogHost {
 public:
  explicit RenameHost(Session& session) : s_(session) {}
  OriginalTdtDocument* document() override {
    return s_.document_ ? &*s_.document_ : nullptr;
  }
  void alert_strl(std::uint16_t strl_index) override {
    const auto text =
        original_strl_entry(s_.resources_.find("STRL", 1005), strl_index);
    (void)s_.show_alert(1000, {text, {}, {}, {}});
  }

 private:
  Session& s_;
};

bool Session::run_find_dialog(OriginalFindMode mode) {
  if (!document_ || !input_source_) return false;
  auto modal = make_find_dialog(resources_, mode);
  if (!drives(modal)) return false;
  OriginalFindController controller(mode,
                                    original_find_entries(*document_, mode));
  // WM_INITDIALOG; its TRUE return lets the dialog manager focus OK.
  controller.initialize(modal);
  if (controller.initial_focus(modal)) modal.focus_first_tab_stop();
  FindHost host(*this);
  (void)run_dialog_modal(
      modal, [&](const DialogSignal& signal, OriginalDialogOps& ops) {
        return controller.signal(signal, ops, host).result;
      });
  log_line(std::string("dialog find ") +
           (mode == OriginalFindMode::person ? "person" : "tenant") +
           (controller.resolution().focused() ? ": focused floor " +
                                                    std::to_string(
                                                        controller.resolution()
                                                            .floor) +
                                                    " x " +
                                                    std::to_string(
                                                        controller.resolution()
                                                            .x)
                                              : std::string(": closed")));
  if (!controller.resolution().focused()) return true;
  // 10e0:0cea: construction off, edit mode two, then Command, Map, the
  // preview scratch and the camera transform.
  end_original_lobby_drag(world_input_context(), *world_host_);
  world_input_.build_mode_enabled = false;
  world_input_.command_mode = 2U;
  for (const auto step : original_find_focus_refresh_order()) {
    switch (step) {
      case OriginalFindFocusRefreshStep::command_repaint:
      case OriginalFindFocusRefreshStep::map_repaint:
        present_requested_ = true;
        break;
      case OriginalFindFocusRefreshStep::restore_preview_scratch:
        // No Elevator Control isolation exists on SDL yet.
        if (world_input_.preview_rect) {
          world_input_.preview_rect.reset();
          main_backing_dirty_ = true;
        }
        break;
      case OriginalFindFocusRefreshStep::camera_transform:
        world_host_->set_main_view(controller.resolution().view.x,
                                   controller.resolution().view.y);
        break;
    }
  }
  return true;
}

bool Session::run_finance_dialog() {
  if (!document_ || !input_source_) return false;
  const auto dtmp = parse_original_dtmp(resources_.find(
      "DTMP", original_finance_launcher_contract().dialog_resource_id));
  if (dtmp.rectangles.empty()) return false;
  const auto& rect = dtmp.rectangles[kOriginalFinanceButtonRectangle - 1U];
  OriginalFinanceController controller(
      OriginalRect{rect.left, rect.top, rect.right, rect.bottom});
  auto modal =
      make_finance_dialog(resources_, derive_original_finance_view(*document_));
  if (!drives(modal)) return false;
  // The painter draws the controller's pressed surface.
  modal.set_paint_state(
      [&controller]() { return controller.pressed() ? 1U : 0U; });
  // WM_INITDIALOG: SetFocus(dialog), no control focused.
  (void)run_dialog_modal(
      modal, [&](const DialogSignal& signal, OriginalDialogOps& ops) {
        return controller.signal(signal, ops).result;
      });
  return true;
}

bool Session::open_rename_dialog_for_test(const OriginalRenameTarget& target) {
  const std::uint16_t id = target.person ? 730U : 732U;
  ++host_requests_.dialogs;
  log_request(RequestKind::command_dialog, {id});
  tally(target.person ? "dialog rename person" : "dialog rename tenant");
  if (!document_) return false;
  OriginalRenameController controller(target);
  bool driven = false;
  if (input_source_) {
    auto modal = make_rename_dialog(resources_, target.person);
    if (drives(modal)) {
      driven = true;
      controller.initialize(modal, *document_);
      modal.focus_first_tab_stop();
      RenameHost host(*this);
      (void)run_dialog_modal(
          modal, [&](const DialogSignal& signal, OriginalDialogOps& ops) {
            return controller.signal(signal, ops, host).result;
          });
    }
  }
  // The Information callers: a change dirties the document.
  if (controller.changed()) {
    document_dirty_ = true;
    present_requested_ = true;
  }
  log_line(std::string("dialog rename ") +
           (target.person ? "person" : "tenant") + " -> " +
           (controller.changed() ? "changed" : "unchanged") +
           (driven ? "" : " (not driven: cancel)") + ", name \"" +
           original_rename_initial_text(*document_, target) + "\"");
  return controller.changed();
}

bool Session::open_movie_dialog_for_test(std::size_t linked_record_index) {
  ++host_requests_.dialogs;
  log_request(RequestKind::command_dialog,
              {731, static_cast<std::int64_t>(linked_record_index)});
  tally("dialog movie");
  if (!document_) return false;
  OriginalMovieChoiceController controller(linked_record_index);
  std::optional<int> result{};
  if (input_source_) {
    auto modal = make_movie_dialog(resources_);
    if (drives(modal)) {
      controller.initialize(modal, *document_, part_);
      modal.focus_first_tab_stop();
      result = run_dialog_modal(
          modal, [&](const DialogSignal& signal, OriginalDialogOps&) {
            return controller.signal(signal, &*document_, part_).result;
          });
    }
  }
  // The Facility caller: any result but Cancel (0) dirties the document.
  const bool chosen = result.value_or(0) != 0;
  if (chosen) {
    document_dirty_ = true;
    present_requested_ = true;
    paint_info();
  }
  log_line("dialog movie record " + std::to_string(linked_record_index) +
           " -> " + std::to_string(result.value_or(0)) +
           (controller.changed() ? " (changed)" : ""));
  return chosen;
}

std::optional<int> Session::run_dialog_modal(DialogWindow& dialog,
                                             const DialogProcedure& procedure) {
  // DialogBoxIndirectParamW's own loop: no idle pass runs until the dialog
  // ends. Event dialogs and About hold DS:24b8; MessageBox does not. The
  // open dialog is drawn by compose_window.
  auto* source = input_source_;
  const bool lock = dialog.counts_modal_lock();
  dialog.place(layout_.window_width, layout_.window_height);
  // Undone on every exit, including a ScriptFailure thrown by the source or
  // an exception from the procedure (a nested alert). The dialog it covered
  // owes its procedure a WM_PAINT.
  struct Open {
    Session& session;
    InputSource* source;
    bool lock;
    DialogWindow& dialog;
    Open(Session& s, InputSource* src, bool l, DialogWindow& d)
        : session(s), source(src), lock(l), dialog(d) {
      session.dialogs_.push_back(&d);
      if (lock) ++session.modal_depth_;
      // UpdateWindow from the procedure (ops.update_window): present the
      // invalid dialog now (COUNTDLOGMAIN's pressed then released frame).
      d.set_update_window([&d, src]() {
        if (d.take_invalidated()) src->modal_changed();
      });
    }
    Open(const Open&) = delete;
    Open& operator=(const Open&) = delete;
    ~Open() {
      dialog.set_update_window({});
      session.dialogs_.pop_back();
      if (lock) --session.modal_depth_;
      if (!session.dialogs_.empty()) session.dialogs_.back()->uncovered();
      try {
        source->dialog_closed();
        source->modal_changed();
      } catch (...) {
        // Unwinding already; the next present repaints anyway.
      }
    }
  } open(*this, source, lock, dialog);
  // The dialog clock: milliseconds since it opened, advanced only by the
  // inputs' elapsed_ms. SetTimer at WM_INITDIALOG (event dialogs) or the
  // first WM_PAINT (About) arms the first deadline.
  std::uint64_t now = 0U;
  dialog.set_clock(now);
  std::optional<std::uint64_t> deadline{};
  if (dialog.timer_interval_ms() != 0U) deadline = dialog.timer_interval_ms();
  std::optional<int> result{};
  const auto signal = [&](const DialogSignal& sent) {
    if (!result) result = procedure(sent, dialog);
  };
  // WM_PAINT: when it opens, after the procedure invalidated it and after a
  // dialog that covered it closed; never once per frame.
  const auto paint = [&]() {
    if (!result && dialog.take_paint()) {
      signal(DialogSignal{DialogSignal::Kind::paint, 0U});
    }
  };
  const auto deliver = [&](const DialogStep& step) {
    if (step.redraw) source->modal_changed();
    if (step.signal) signal(*step.signal);
    if (step.then) signal(*step.then);
    paint();
    // The procedure's ops repaint an open dialog.
    if (dialog.take_invalidated() && !result) source->modal_changed();
    return result.has_value();
  };
  paint();
  (void)dialog.take_invalidated();
  source->modal_changed();
  if (result) return result;
  // WM_TIMER: the event timer is one-shot (1068:0380 kills it); About's
  // repeats every interval.
  const auto fire_timer = [&](std::uint64_t at) {
    deadline.reset();
    if (dialog.timer_repeats()) deadline = at + dialog.timer_interval_ms();
    return deliver(dialog.timer());
  };
  while (true) {
    // The source wakes for the timer and for the caret's next blink (a
    // focused edit); the blink only repaints.
    std::optional<std::uint64_t> wake = deadline;
    if (const auto blink = dialog.next_caret_blink()) {
      wake = wake ? std::min(*wake, *blink) : *blink;
    }
    std::optional<std::uint32_t> wait{};
    if (wake) {
      wait = static_cast<std::uint32_t>(std::min<std::uint64_t>(
          *wake > now ? *wake - now : 0U, 0xFFFFFFFFU));
    }
    const bool caret_shown = dialog.caret_visible();
    const auto input = source->next_dialog_input(wait);
    if (!input) break;
    now += input->elapsed_ms;
    dialog.set_clock(now);
    if (dialog.caret_visible() != caret_shown) source->modal_changed();
    bool done = false;
    switch (input->kind) {
      case DialogInput::Kind::pointer: {
        DialogPointer pointer{DialogPointer::Kind::move, input->pointer.x,
                              input->pointer.y,
                              (input->pointer.keys & kOriginalKeyShift) != 0U};
        switch (input->pointer.kind) {
          case InputKind::move: break;
          // The dialog manager's own double-click test (on the dialog
          // clock) stands in for CS_DBLCLKS: a double click is a down.
          case InputKind::down:
          case InputKind::double_click:
            pointer.kind = DialogPointer::Kind::down;
            break;
          case InputKind::up: pointer.kind = DialogPointer::Kind::up; break;
          case InputKind::right_down:
            pointer.kind = DialogPointer::Kind::right_down;
            break;
          case InputKind::right_up:
            pointer.kind = DialogPointer::Kind::right_up;
            break;
        }
        done = deliver(dialog.pointer(pointer));
        break;
      }
      case DialogInput::Kind::key:
        done = deliver(dialog.key(input->key, input->character, input->shift));
        break;
      case DialogInput::Kind::key_up:
        done = deliver(dialog.key_up(input->key, input->character));
        break;
      case DialogInput::Kind::character:
        done = deliver(dialog.character(input->character));
        break;
      case DialogInput::Kind::timer:
        // A scripted WM_TIMER now (nothing without a live timer).
        if (deadline) done = fire_timer(now);
        break;
      case DialogInput::Kind::wait_elapsed:
        break;
    }
    // WM_TIMER has the lowest priority: due timers fire after the input,
    // one per elapsed interval.
    while (!done && deadline && now >= *deadline) done = fire_timer(*deadline);
    if (done) break;
  }
  // The uncovered windows repaint. Only an answered dialog requests it.
  if (result) present_requested_ = true;
  return result;
}

void Session::sound(std::int32_t resource, std::string_view source) {
  ++host_requests_.sounds;
  log_request(RequestKind::sound, {resource});
  tally("sound " + std::string(source));
  if (flags_.log_sounds) {
    log_line("sound " + std::to_string(resource) + " (" +
             std::string(source) + ")");
  }
}

void Session::notification_status(std::uint16_t code) {
  ++host_requests_.status;
  const bool shown = set_original_info_notification_status(
      resources_, info_status_, code, coarse_tick_now());
  if (shown) paint_info();
  log_request(RequestKind::notification_status, {code, shown});
  tally("notification status");
  if (shown) {
    log_line("info notification " + std::to_string(code) + ": " +
             info_status_.text);
  }
}

void Session::income_status(std::uint16_t code) {
  ++host_requests_.status;
  const bool shown = set_original_info_income_status(resources_, info_status_,
                                                     code, coarse_tick_now());
  if (shown) paint_info();
  log_request(RequestKind::income_status, {code, shown});
  tally("income status");
}

void Session::log_fire_menu(bool enabled) {
  // set_original_fire_menu_enabled: EnableMenuItem(0x9c48) on the menu.
  const auto op = original_fire_menu_op(enabled);
  apply_menu_ops({&op, 1U});
  log_request(RequestKind::fire_menu, {enabled});
  tally("fire menu");
  log_line(std::string("fire menu ") + (enabled ? "enabled" : "disabled"));
}

OriginalIdlePassContext Session::idle_pass_context() {
  return {document_ ? &*document_ : nullptr, simulation_state_,
          fast_frame_ms_,                    transfer_visuals_,
          part_,                             rent_income_,
          maintenance_costs_};
}

bool Session::run_idle_pass() {
  // run_original_idle_simulation. Of the 1258:0195-023a prelude only the
  // WAVMIX activation reconcile applies (TOPMOST window repair has no
  // single-window equivalent), and of the 1258:0285-02d9 epilogue only the
  // 11c8:0135(0) audio finish (no Elevator Control yet). The shared pass
  // runs 1258:023a through 1090:03ab(1), calling back into the hooks below.
  if (pending_main_pass_) {
    // The pending WM_PAINT of a non-synchronous request_main_pass.
    const auto pass = *pending_main_pass_;
    pending_main_pass_.reset();
    rebuild_main(pass, true);
  }
  ++idle_passes_;
  log_request(RequestKind::pass_begin,
              {static_cast<std::int64_t>(idle_passes_)});
  if (audio_) {
    // 1258:0195-01c3: losing activation or minimizing stops every channel;
    // regaining it retries activation while the latch is clear.
    switch (original_idle_audio_transition(window_active_, window_iconic_,
                                           audio_->active())) {
      case OriginalIdleAudioTransition::activate:
        audio_->activate();
        break;
      case OriginalIdleAudioTransition::deactivate:
        audio_->deactivate();
        break;
      case OriginalIdleAudioTransition::none:
        break;
    }
  }
  auto context = idle_pass_context();
  (void)run_original_idle_pass(context, *this);
  // 1258:02d9 always ends through 11c8:0135(0); the zero force word makes
  // both channel stops deliberate no-ops.
  if (audio_) audio_->stop_all(false);
  // A pending WM_PAINT is processed before the next empty-queue pass.
  if (present_requested_) present_main();
  const bool present = present_requested_;
  present_requested_ = false;
  log_request(RequestKind::pass_end, {present});
  return present;
}

std::uint32_t Session::coarse_tick(std::uint32_t now_ms) {
  // 1208:05e6 with the Win32 wrapper's 11e0:0e84 pump first.
  if (audio_) audio_->pump(now_ms);
  return original_coarse_tick(now_ms);
}

void Session::expire_find_marker() {
  // 1258:0186 Find-marker timeout (10e0:051d), then the 1058:0352
  // construction toggle. Only Find sets the marker; an inactive one never
  // expires, so no clock is read for it.
  if (!find_marker_.active()) return;
  auto expired = find_marker_;
  if (expire_original_find_marker(expired, coarse_tick_now())) {
    find_marker_ = expired;
    apply_original_construction_toggle(world_input_context(), *world_host_);
  }
}

OriginalIdleView Session::main_view() {
  return {viewport_.x, viewport_.y, viewport_.width, viewport_.height};
}

bool Session::step_effect_palette() {
  // 1020:00cb. No AnimatePalette sync: colours apply while composing.
  return document_ &&
         step_original_effect_palette(*document_, palette_runtime_,
                                      flags_.effects_animation_enabled,
                                      coarse_tick_now());
}

bool Session::refresh_time_palette() {
  return refresh_original_time_palette(resources_, *document_,
                                       palette_runtime_);
}

bool Session::advance_visible_people() {
  return advance_main_surface_state(OriginalMainSurfacePass::simulation_frame);
}

std::uint16_t Session::event_dialog(const OriginalEventDialogRequest& dialog,
                                    OriginalIdleDialog kind) {
  switch (kind) {
    case OriginalIdleDialog::bomb_offer:
      return run_event_dialog(dialog, kBombOfferChoice, "bomb offer: search");
    case OriginalIdleDialog::bomb_followup:
      return run_event_dialog(dialog, kAcknowledgeChoice, "bomb follow-up");
    case OriginalIdleDialog::bomb_event:
      return run_event_dialog(dialog, kAcknowledgeChoice, "bomb");
    case OriginalIdleDialog::fire_offer:
    case OriginalIdleDialog::fire_event:
      return run_event_dialog(dialog, kAcknowledgeChoice, "fire");
    case OriginalIdleDialog::fire_crew_offer:
      return run_event_dialog(dialog, kFireCrewOfferChoice,
                              "fire crew offer: no");
    case OriginalIdleDialog::fire_crew_followup:
      return run_event_dialog(dialog, kAcknowledgeChoice,
                              "fire crew follow-up");
    case OriginalIdleDialog::hotel:
      return run_event_dialog(dialog, kAcknowledgeChoice, "hotel");
    case OriginalIdleDialog::rating:
      return run_event_dialog(dialog, kAcknowledgeChoice, "rating");
  }
  return run_event_dialog(dialog, kAcknowledgeChoice, "event");
}

void Session::focus(std::int16_t floor, std::int16_t x) {
  focus_event_coordinate(floor, x);
}

void Session::expire_info_status() {
  if (expire_original_info_status(info_status_, coarse_tick_now())) {
    paint_info();
  }
}

void Session::begin_full_frame(bool day_changed) {
  // 1200:04d4-04fe synchronously repaints 1118:045d right after the day
  // update, before the ordinary 1090 frame pass.
  if (day_changed) paint_info();
}

void Session::present_info_direct(OriginalIdleInfoLatches& latches) {
  // present_original_info_surface_direct: the retained painter first, then
  // the Hotel latch at the 1090:0691 checkpoint. The Win32 audio pumps
  // around it read the clock; this host pumps only through its own hooks,
  // as before this step, so the digest is unchanged.
  paint_info();
  latches.consume_hotel_checkout();
}

void Session::play_sound(std::int32_t resource,
                         std::uint16_t repeat,
                         std::uint16_t priority,
                         OriginalIdleSound source) {
  // Logged first (digest), then played (Win32IdlePassHost::play_sound).
  const char* label = "other";
  switch (source) {
    case OriginalIdleSound::scheduler: label = "scheduler"; break;
    case OriginalIdleSound::ambient: label = "ambient"; break;
    case OriginalIdleSound::recycling: label = "recycling"; break;
    case OriginalIdleSound::metro: label = "metro"; break;
    case OriginalIdleSound::bomb_payment: label = "bomb payment"; break;
    case OriginalIdleSound::cathedral: label = "cathedral"; break;
    case OriginalIdleSound::elevator: label = "elevator"; break;
    case OriginalIdleSound::hotel_checkout: label = "hotel checkout"; break;
  }
  sound(resource, label);
  play_audio(resource, repeat, priority);
}

void Session::play_event_sounds(
    std::span<const OriginalEventSoundRequest> requests) {
  // play_original_event_sounds: 10e8:0450's requests go through 11c8:0100
  // (reserved channel, only while idle); the others through 11c8:0167.
  for (const auto& request : requests) {
    sound(request.resource, "event");
    play_audio(request.resource, request.repeat, request.priority,
               request.reserved_if_idle);
  }
}

void Session::stop_all_audio(bool force) {
  // 11c8:0135 (the Cathedral ceremony passes force).
  if (audio_) audio_->stop_all(force);
}

void Session::checkpoint(OriginalIdleCheckpoint) {
  // Direct 11e0:0e84 pump inside 1090:03ab(1), on a fresh clock read.
  if (audio_) audio_->pump(clock_ms_());
}

void Session::idle_palette_changed() {
  main_backing_dirty_ = true;
  present_requested_ = true;
}

void Session::rating_promoted_before_dialog(std::uint16_t rating) {
  // InvalidateRect+UpdateWindow on Info before the rating dialog.
  paint_info();
  log_request(RequestKind::rating_promoted, {rating});
  tally("rating promoted");
  log_line("rating promoted to " + std::to_string(rating));
}

void Session::rating_promoted_after_dialog(std::uint16_t /*rating*/) {
  // 1140:010d(1) resets DS:783c to edit mode two.
  if (original_rating_command_refresh_plan(1U, false, false)
          .force_command_mode_two) {
    world_input_.command_mode = 2U;
    log_request(RequestKind::command_mode, {world_input_.command_mode});
  }
}

void Session::before_scheduler_step(bool sample_cursor) {
  // 1258:0256 samples the cursor into the Main client (even outside it;
  // 11f8:3c13 intersects later). The SDL host samples the last pointer
  // event; before the first one there is nothing to sample.
  if (sample_cursor && last_pointer_) {
    world_input_.pointer = {true, last_pointer_->x, last_pointer_->y};
  }
  // 1090:03ab saves DS:77ac before the preview-only or full-frame pass.
  previous_preview_rect_ = world_input_.preview_rect;
}

void Session::preview_only_pass() {
  // 1090:03bf-0427 through the shared plan, as Win32IdlePassHost runs it.
  const auto plan = original_preview_only_pass_plan(
      world_input_, false, previous_preview_rect_,
      original_world_input_preview_rect(world_input_,
                                        {viewport_.x, viewport_.y}));
  for (std::uint8_t index = 0; index < plan.checkpoints_before; ++index) {
    checkpoint(OriginalIdleCheckpoint::construction);
  }
  if (plan.clear_previous) world_input_.preview_rect.reset();
  if (plan.present_main_direct) {
    rebuild_main(OriginalMainSurfacePass::preview_repaint, false);
  } else {
    world_input_.preview_rect = plan.new_rect;
  }
  for (std::uint8_t index = 0; index < plan.checkpoints_after; ++index) {
    checkpoint(OriginalIdleCheckpoint::construction);
  }
  if (plan.present_main_direct) present_main();
}

void Session::restore_preview() {
  // 1090:04a7 11f8:3b94.
  const auto restore = original_construction_preview_restore_plan(
      world_input_.preview_scratch, false, previous_preview_rect_.has_value());
  for (std::uint8_t index = 0; index < restore.audio_checkpoints; ++index) {
    checkpoint(OriginalIdleCheckpoint::construction);
  }
  if (restore.clear_previous_rectangle) world_input_.preview_rect.reset();
}

void Session::cathedral_rating_promoted() {
  // 1140:00a8 after its modal; 1140:010d(1) as in the ordinary promotion.
  const auto rating = document_->header.rating;
  log_request(RequestKind::rating_promoted, {rating});
  tally("rating promoted");
  log_line("rating promoted to " + std::to_string(rating));
  rating_promoted_after_dialog(rating);
}

bool Session::render_full_frame(bool renderer_surface_dirty) {
  // 1090:0580 renderer and the 1090:05ee outline.
  current_preview_rect_ = original_world_input_preview_rect(
      world_input_, {viewport_.x, viewport_.y});
  const bool preview_changed = previous_preview_rect_ != current_preview_rect_;
  rebuild_main(OriginalMainSurfacePass::simulation_frame, false,
               renderer_surface_dirty);
  const auto draw = original_construction_preview_draw_plan(
      world_input_.preview_scratch, world_input_.build_mode_enabled,
      world_input_.command_mode);
  for (std::uint8_t index = 0; index < draw.audio_checkpoints; ++index) {
    checkpoint(OriginalIdleCheckpoint::construction);
  }
  return preview_changed;
}

void Session::commit_full_frame_preview() {
  world_input_.preview_rect = current_preview_rect_;
}

void Session::palette_recolor_rebuild() {
  rebuild_main(OriginalMainSurfacePass::palette_repaint, false);
}

void Session::final_effect_palette_changed() {
  // Fourth 1020:00cb (1090:06dc).
  rebuild_main(OriginalMainSurfacePass::palette_repaint, false);
  present_main();
}

OriginalWorldInputContext Session::world_input_context() {
  return {document_ ? &*document_ : nullptr,
          resources_,
          part_,
          construction_costs_,
          rent_income_,
          world_input_};
}

bool Session::run_selector_modal(OriginalCommandSelectorSession& selector) {
  // DialogBoxIndirectParamW's own loop: DS:24b8 is held, the selector has
  // the capture, and no idle pass runs until it ends.
  log_request(RequestKind::selector,
              {selector.group().tabm_number, selector.choice()});
  ++modal_depth_;
  selector_ = &selector;
  if (input_source_) input_source_->modal_changed();
  bool accepted = false;
  while (input_source_) {
    const auto event = input_source_->next_modal_event();
    if (!event) break;
    if (event->kind == InputKind::down ||
        event->kind == InputKind::double_click) {
      left_button_down_ = true;
    } else if (event->kind == InputKind::up) {
      left_button_down_ = false;
    }
    last_pointer_ = OriginalWorldPoint{event->x - layout_.main_client.x,
                                       event->y - layout_.main_client.y};
    // The desktop bottom of 1050:05d6 is the window bottom.
    const auto row = OriginalCommandSelectorSession::row_at(
        event->x - selector.anchor_left(),
        event->y - selector.top(layout_.window_height), selector.width(),
        selector.height());
    OriginalCommandSelectorStep step{};
    switch (event->kind) {
      case InputKind::move: step = selector.on_move(row); break;
      // The dialog class has no CS_DBLCLKS: a double click is a down.
      case InputKind::down:
      case InputKind::double_click: step = selector.on_lbutton_down(row);
        break;
      case InputKind::up: step = selector.on_lbutton_up(row); break;
      case InputKind::right_down: step = selector.on_rbutton_down(); break;
      case InputKind::right_up: break;
    }
    if (step.redraw) input_source_->modal_changed();
    if (step.end) {
      accepted = step.accepted;
      break;
    }
  }
  selector_ = nullptr;
  --modal_depth_;
  if (input_source_) input_source_->modal_changed();
  log_request(RequestKind::selector, {accepted, selector.choice()});
  present_requested_ = true;
  return accepted;
}

void Session::pointer_event(const InputEvent& event) {
  // MK_LBUTTON from the tracked button state.
  switch (event.kind) {
    case InputKind::down:
    case InputKind::double_click:
      left_button_down_ = true;
      break;
    case InputKind::up:
      left_button_down_ = false;
      break;
    case InputKind::move:
    case InputKind::right_down:
    case InputKind::right_up:
      break;
  }
  const std::uint16_t keys = static_cast<std::uint16_t>(
      (event.keys & (kOriginalKeyShift | kOriginalKeyControl)) |
      (left_button_down_ ? kOriginalKeyLButton : 0U));
  // GetCursorPos converted to the Main client on every motion, even outside
  // Main (1258:0256 never clears "inside").
  last_pointer_ = OriginalWorldPoint{event.x - layout_.main_client.x,
                                     event.y - layout_.main_client.y};
  if (event.kind == InputKind::right_down ||
      event.kind == InputKind::right_up) {
    return;
  }

  const bool down =
      event.kind == InputKind::down || event.kind == InputKind::double_click;
  // A press on a palette's HTCAPTION area enters DefWindowProc's SC_MOVE
  // loop, which captures the mouse until the release: no procedure sees
  // the moves or the release. TEMP: panels do not move yet, so the loop
  // only swallows the gesture.
  if (nonclient_press_) {
    if (event.kind == InputKind::up) nonclient_press_ = false;
    return;
  }

  // SetCapture routes every mouse message to its owner, without
  // WM_NCHITTEST.
  Panel panel = Panel::none;
  if (capture_ == OriginalInputSurface::main) {
    panel = Panel::main;
  } else if (capture_ == OriginalInputSurface::map) {
    panel = Panel::map;
  } else {
    // Topmost first: the drawing order of compose_window, reversed. Main
    // takes only its client; a palette takes its whole outer rectangle.
    for (auto it = kPanelZOrder.rbegin(); it != kPanelZOrder.rend(); ++it) {
      const auto& area = *it == Panel::main      ? layout_.main_client
                         : *it == Panel::map     ? layout_.map_outer
                         : *it == Panel::info    ? layout_.info_outer
                                                 : layout_.command_outer;
      if (panel_visible(*it) && area.contains(event.x, event.y)) {
        panel = *it;
        break;
      }
    }
    if (panel == Panel::none) return;
    if (panel != Panel::main) {
      const auto hit = palette_hit(panel, event.x, event.y);
      if (hit != PaletteHit::client) {
        // Nonclient: a press still activates the palette, and HTCAPTION
        // starts the move loop. HTBORDER does nothing more.
        if (down) {
          active_panel_ = panel;
          nonclient_press_ = hit == PaletteHit::caption;
        }
        return;
      }
    }
  }
  if (down) active_panel_ = panel;
  route_pointer(panel, event, keys);
}

Session::PaletteHit Session::palette_hit(Panel panel, int x, int y) const {
  // The CMDBTNWNDPROC / INFOWNDPROC / MAPWNDPROC WM_NCHITTEST split
  // (original_palette_frame_hit_test) runs on every point of the window,
  // its one-pixel WS_BORDER included: the close box is HTCLIENT, every
  // other point above client y 8 (the top border too) HTCAPTION. The
  // remaining border pixels fall through to DefWindowProc's HTBORDER.
  const PanelRect& client = panel == Panel::command ? layout_.command_client
                            : panel == Panel::map   ? layout_.map_client
                                                    : layout_.info_client;
  const auto hit =
      original_palette_frame_hit_test(x - client.x, y - client.y);
  if (hit == OriginalPaletteFrameHit::drag) return PaletteHit::caption;
  return client.contains(x, y) ? PaletteHit::client : PaletteHit::border;
}

void Session::route_pointer(Panel panel,
                            const InputEvent& event,
                            std::uint16_t keys) {
  const auto ctx = world_input_context();
  auto& host = *world_host_;
  const auto client_x = [&](const PanelRect& rect) { return event.x - rect.x; };
  const auto client_y = [&](const PanelRect& rect) { return event.y - rect.y; };
  // Only Main's class has CS_DBLCLKS; elsewhere the second press is a down.
  const bool down =
      event.kind == InputKind::down || event.kind == InputKind::double_click;
  switch (panel) {
    case Panel::main: {
      const OriginalWorldInputPoint point{client_x(layout_.main_client),
                                          client_y(layout_.main_client), keys};
      if (down) {
        original_main_pointer_down(ctx, host, point,
                                   event.kind == InputKind::double_click);
      } else if (event.kind == InputKind::move) {
        original_main_pointer_move(ctx, host, point);
      } else if (event.kind == InputKind::up) {
        original_main_pointer_up(ctx, host, point);
      }
      break;
    }
    case Panel::command: {
      // CMDBTNWNDPROC handles no WM_MOUSEMOVE.
      const int x = client_x(layout_.command_client);
      const int y = client_y(layout_.command_client);
      if (down) {
        original_command_pointer_down(ctx, host, x, y);
      } else if (event.kind == InputKind::up) {
        original_command_pointer_up(ctx, host, x, y);
      }
      break;
    }
    case Panel::map: {
      const int x = client_x(layout_.map_client);
      const int y = client_y(layout_.map_client);
      if (down) {
        original_map_pointer_down(ctx, host, x, y);
      } else if (event.kind == InputKind::move) {
        (void)original_map_pointer_move(ctx, host, x, y, keys);
      } else if (event.kind == InputKind::up) {
        (void)original_map_pointer_up(ctx, host);
      }
      break;
    }
    case Panel::info:
      if (down) {
        (void)original_info_pointer_down(ctx, host,
                                         client_x(layout_.info_client),
                                         client_y(layout_.info_client));
      }
      break;
    case Panel::none:
      break;
  }
}

void Session::select_tool(std::optional<std::uint16_t> edit_mode,
                          std::optional<std::uint16_t> build_type) {
  if (edit_mode) {
    world_input_.command_mode = *edit_mode;
  }
  if (build_type) {
    world_input_.selected_build_type = *build_type;
    world_input_.command_mode = 3U;
    const auto& rating = active_original_command_rating(
        world_input_context(), world_input_.active_command_rating);
    const auto catalog = original_command_catalog(resources_, rating);
    for (std::size_t index = 0; index < catalog.size(); ++index) {
      if (original_command_build_type(resources_, catalog[index]) ==
          *build_type) {
        world_input_.command_mode = static_cast<std::uint16_t>(index + 3U);
        break;
      }
    }
  }
  present_requested_ = true;
}

bool Session::panel_visible(Panel panel) const {
  switch (panel) {
    case Panel::map: return map_visible_;
    case Panel::info: return info_visible_;
    case Panel::command: return command_visible_;
    case Panel::main: return true;
    case Panel::none: return false;
  }
  return false;
}

const OriginalCommandRatingState& Session::active_rating_state() const {
  const auto bounded = std::clamp<std::uint16_t>(
      world_input_.active_command_rating, 1U, 6U);
  return world_input_.command_ratings[bounded - 1U];
}

std::uint64_t Session::input_digest() const {
  const auto& state = world_input_;
  Fnv64 hash;
  hash.boolean(state.build_mode_enabled);
  hash.boolean(state.command_toggle_pressed);
  hash.unsigned_integer(state.command_mode);
  hash.unsigned_integer(state.selected_build_type);
  hash.unsigned_integer(state.map_mode);
  hash.unsigned_integer(state.active_command_rating);
  for (const auto& rating : state.command_ratings) {
    hash.unsigned_integer(rating.rating);
    hash.unsigned_integer(rating.encoded_entries.size());
    for (const auto entry : rating.encoded_entries) {
      hash.unsigned_integer(entry);
    }
  }
  hash.unsigned_integer(state.office_variant);
  for (const auto variant : state.facility_variants) {
    hash.unsigned_integer(variant);
  }
  hash.boolean(state.pointer_armed);
  hash.boolean(state.control_modifier);
  hash.boolean(state.shift_modifier);
  hash.boolean(state.capture_held);
  for (const auto& span : {state.lobby_drag, state.floor_drag}) {
    hash.boolean(span.active);
    hash.integer(span.floor);
    hash.integer(span.anchor_left);
    hash.integer(span.anchor_right);
  }
  hash.unsigned_integer(static_cast<std::uint8_t>(state.parking_drag.kind));
  hash.integer(state.parking_drag.press_floor);
  hash.integer(state.parking_drag.snapped_left);
  hash.integer(state.parking_drag.snapped_right);
  hash.boolean(state.completion.active);
  hash.unsigned_integer(state.completion.type);
  hash.unsigned_integer(state.completion.successful_steps);
  hash.integer(state.completion.balance_at_press);
  hash.boolean(state.completion.priority_sound_latch_armed);
  hash.boolean(state.shaft_drag.capture_active);
  hash.unsigned_integer(static_cast<std::uint8_t>(state.shaft_drag.direction));
  hash.unsigned_integer(state.shaft_drag.elevator_index);
  hash.boolean(state.pointer.inside);
  hash.integer(state.pointer.client_x);
  hash.integer(state.pointer.client_y);
  hash.boolean(state.preview_rect.has_value());
  if (state.preview_rect) {
    hash.integer(state.preview_rect->left);
    hash.integer(state.preview_rect->top);
    hash.integer(state.preview_rect->right);
    hash.integer(state.preview_rect->bottom);
  }
  hash.boolean(state.preview_scratch.backing_saved);
  hash.boolean(state.map_drag_active);
  hash.boolean(state.map_pointer_down);
  hash.boolean(state.map_focus.has_value());
  hash.boolean(last_pointer_.has_value());
  if (last_pointer_) {
    hash.integer(last_pointer_->x);
    hash.integer(last_pointer_->y);
  }
  hash.boolean(map_visible_);
  hash.boolean(info_visible_);
  hash.boolean(command_visible_);
  hash.integer(command_outer_height_);
  hash.boolean(document_dirty_);
  hash.boolean(flags_.fast_mode_enabled);
  hash.boolean(flags_.people_animation_enabled);
  hash.boolean(flags_.effects_animation_enabled);
  for (const bool category : sound_categories_) hash.boolean(category);
  hash.text(tower_path_.string());
  hash.text(tower_title_.string());
  hash.boolean(main_closing_);
  return hash.value();
}

bool Session::direct_build(const DirectBuild& build) {
  if (!document_) throw std::runtime_error("direct build needs a tower");
  const bool built = apply_direct_build(*document_, part_, construction_costs_,
                                        rent_income_, build);
  // As apply_original_construction_mutation_presentation: no synchronous
  // rebuild (no RNG draw) here.
  main_backing_dirty_ = true;
  present_requested_ = true;
  return built;
}

void Session::log_request(RequestKind kind,
                          std::initializer_list<std::int64_t> fields) {
  request_log_.byte(static_cast<std::uint8_t>(kind));
  request_log_.unsigned_integer(fields.size());
  for (const auto field : fields) request_log_.integer(field);
  ++request_log_entries_;
  if (flags_.trace_requests) {
    std::string line = "request " + std::to_string(static_cast<int>(kind));
    for (const auto field : fields) line += " " + std::to_string(field);
    std::printf("%s\n", line.c_str());
  }
}

ScenarioSummary Session::apply_scenario(std::string_view name) {
  if (!document_) throw std::runtime_error("scenario needs a tower");
  auto summary =
      build_scenario(name, *document_, part_, construction_costs_);
  // As apply_original_construction_mutation_presentation: the model changed,
  // so the backing is stale. No synchronous rebuild (no RNG draw) here.
  main_backing_dirty_ = true;
  present_requested_ = true;
  return summary;
}

bool Session::trigger_event(std::string_view name) {
  if (!document_) throw std::runtime_error("event needs a tower");
  std::uint16_t command{};
  if (name == "bomb") {
    command = kOriginalCommandBomb;
  } else if (name == "fire") {
    command = kOriginalCommandFire;
  } else {
    throw std::runtime_error("unknown event: " + std::string(name));
  }
  // WM_COMMAND 9000/9002 (not a menu pick: the items were deleted at
  // WM_CREATE), so nothing is logged beyond what the event itself logs.
  command_host_->reset_offer();
  (void)dispatch_original_main_command(*command_host_, command);
  return command_host_->offer_started();
}

SessionDigest Session::state_digest() const {
  SessionDigest digest{};
  if (document_) {
    Fnv64 tower;
    tower.bytes(serialize_original_tdt(*document_));
    digest.tower = tower.value();

    // OriginalTdtDocument members that serialize_original_tdt omits.
    Fnv64 process;
    process.unsigned_integer(document_->random_state);
    process.unsigned_integer(document_->restaurant_service_variant);
    process.unsigned_integer(document_->retail_service_variant);
    process.unsigned_integer(document_->fast_food_service_variant);
    process.boolean(document_->security_event_accelerated);
    process.unsigned_integer(document_->hotel_checkout_count);
    process.boolean(document_->hotel_checkout_effect_cadence);
    process.boolean(document_->balance_changed_effect_active);
    process.bytes(document_->medical_route_index);
    digest.process = process.value();
  }

  Fnv64 host;
  host.boolean(document_.has_value());
  host.unsigned_integer(simulation_state_.frame_time);
  host.integer(simulation_state_.current_day);
  host.integer(simulation_state_.day_phase);
  host.unsigned_integer(simulation_state_.calendar_phase);
  host.unsigned_integer(simulation_state_.last_tick);
  host.unsigned_integer(fast_frame_ms_);
  host.boolean(palette_runtime_.initialized);
  host.unsigned_integer(palette_runtime_.last_effect_tick);
  host.unsigned_integer(palette_runtime_.effect_counter);
  host.unsigned_integer(palette_runtime_.colors.size());
  for (const auto color : palette_runtime_.colors) {
    host.unsigned_integer(color);
  }
  for (const auto& placement : sky_decorations_.placements) {
    host.integer(placement.bitmap_index);
    host.integer(placement.left);
    host.integer(placement.top);
    host.integer(placement.right);
    host.integer(placement.bottom);
  }
  host.text(info_status_.text);
  host.unsigned_integer(info_status_.started_tick);
  host.integer(info_status_.priority);
  host.unsigned_integer(transfer_visuals_.size());
  for (const auto& visual : transfer_visuals_) {
    host.unsigned_integer(visual.elevator_index);
    host.integer(visual.floor);
    host.boolean(visual.boarding);
    host.boolean(visual.direction_up);
    host.unsigned_integer(visual.person_index);
  }
  host.boolean(world_input_.build_mode_enabled);
  host.unsigned_integer(world_input_.command_mode);
  host.unsigned_integer(world_input_.map_mode);
  host.integer(viewport_.x);
  host.integer(viewport_.y);
  host.integer(viewport_.width);
  host.integer(viewport_.height);
  host.boolean(main_backing_dirty_);
  host.boolean(present_requested_);
  host.unsigned_integer(host_requests_.dialogs);
  host.unsigned_integer(host_requests_.sounds);
  host.unsigned_integer(host_requests_.focus);
  host.unsigned_integer(host_requests_.status);
  digest.host = host.value();

  Fnv64 requests = request_log_;
  requests.unsigned_integer(request_log_entries_);
  requests.unsigned_integer(idle_passes_);
  digest.requests = requests.value();

  Fnv64 combined;
  combined.unsigned_integer(digest.tower);
  combined.unsigned_integer(digest.process);
  combined.unsigned_integer(digest.host);
  combined.unsigned_integer(digest.requests);
  digest.combined = combined.value();
  return digest;
}

std::uint32_t Session::milliseconds_until_due() const {
  const std::uint32_t now = clock_ms_();
  const auto remaining = [now](std::uint32_t due_ms) -> std::uint32_t {
    const auto delta = static_cast<std::int32_t>(due_ms - now);
    return delta > 0 ? static_cast<std::uint32_t>(delta) : 0U;
  };
  std::uint32_t wait = 100U;
  if (document_ && world_input_.build_mode_enabled) {
    wait = std::min(
        wait, flags_.fast_mode_enabled
                  ? remaining(fast_frame_ms_ + kNativeFastModeFramePeriodMs)
                  : remaining((simulation_state_.last_tick +
                               kOriginalSimulationGateTicks)
                              << 4U));
  }
  if (document_ && flags_.effects_animation_enabled &&
      palette_runtime_.initialized) {
    wait = std::min(wait, remaining((palette_runtime_.last_effect_tick + 15U)
                                    << 4U));
  }
  return wait;
}

}  // namespace simtower::sdl
