// SDL3 frontend: one window holding Main and the Map, Info and Command
// panels (sdl_layout.hpp), driven by the same portable simulation,
// renderer and world input module as the Win32 host. TOWER_MENU is drawn
// in the window (sdl_menu.hpp) and dispatched through the shared WM_COMMAND
// dispatcher; New/Open/Save/Save As use the native file pickers. The event
// dialogs, alerts and About run in the window through the dialog runtime
// (sdl_dialog.hpp); the other dialogs are logged requests until their
// runtimes exist. Help opens the HTML converted from SIMTOWER.HLP in the
// browser (sdl_help.hpp).
//
// Usage:
//   simtower_sdl [--pack FILE] [--open FILE.TDT] [--size WxH] [--slow]
//                [--log-sounds] [--sound | --no-sound]
//                [--audio-capture FILE.wav]
//                [--headless-frame FILE.bmp [--passes N] [--view X,Y]]
//                [--scenario none|busy|full] [--state-digest]
//                [--digest-every K] [--trace-requests]
//                [--event bomb|fire@PASS ...]
//                [--input-script FILE] [--headless-window FILE.bmp]
//                [--direct-build TYPE,FLOOR,LEFT[,VARIANT][,shift]@PASS ...]
//                [--help-dir DIR] [--startup]
//
// Help: Help > Contents and F1 open DIR/contents.html (default DIR: "help"
// next to the executable, which the build fills from SIMTOWER.HLP) with
// SDL_OpenURL. Headless runs and the dummy or offscreen video driver never
// open it: the request and its file URL are logged ("expect-help" checks
// it).
//
// Startup: the window starts like WinMain (Session::run_startup): the
// BITMAP/128 then BITMAP/256 splash, WAVE/20000, then New / Load (DIALOG
// 10000: New Tower, Quit, Load Saved Tower with the native Open picker),
// or with --open that tower instead of the dialog. Headless runs skip it
// and start on New (or --open), unless --startup is given: the splash
// wait then moves the virtual clock, and an --input-script drives New /
// Load with "expect-dialog startup" as its first line (undriven, it is
// New Tower). --startup does not select headless mode by itself and
// cannot be combined with --scenario.
//
// Sound: the interactive window plays sound by default (--no-sound turns
// it off); it also plays the startup WAVE/20000. Headless runs keep sound
// OFF by default, so the regression oracle never changes. --sound turns it
// on and plays through the SDL audio device (use SDL_AUDIO_DRIVER=dummy for
// a silent run; device completion then follows wall time, so such runs are
// not reproducible). --audio-capture FILE.wav selects headless mode, turns
// sound on and replaces the SDL device with the deterministic capture
// device: every voice the runtime starts is mixed on the virtual clock into
// a 44100 Hz 16-bit mono WAV. Sound on changes the simulation (11c8:03ab's
// ambient RNG draw), so digests then differ from sound-off runs by design.
//
// Regression oracle (headless only): --state-digest prints one stdout line
// "digest <hex> ..." after the passes, --digest-every K one line every K
// passes. --scenario stages a deterministic test tower after New. --event
// runs the hidden Bomb or Fire menu command before pass PASS (0-based, must
// be below --passes); it may repeat.
//
// Input: --input-script FILE plays scripted mouse, menu and file-picker
// input (commands in sdl_input_script.hpp) through the same panel router
// and WM_COMMAND dispatcher as the window; the digest line then gains an
// "input <hex>" field. Exit 2 on a script syntax, range or pass-order
// error, 3 on a failed expect-*; a script that ends inside the selector
// modal cancels it. A scripted File > Exit ends the run early. Headless
// runs never open a native picker: an Open / Save As sheet takes the next
// "pick-file" result, and with none it is cancelled. --direct-build
// (test-only) runs one constructor plus the common success tail before
// pass PASS, as the reference for a scripted placement. --headless-window
// FILE.bmp writes the composed window (panels included) after the passes,
// unless a script's "dialog-hold" already wrote it with a dialog open.
//
// Window keys: a lone Alt or F10 enters keyboard menu mode (arrows, Enter,
// Esc, mnemonics), Alt+letter opens that title's drop-down, and F1 is the
// TOWER_MENU accelerator (Help Contents).
//
// Headless mode (no window) is selected by any of --headless-frame,
// --headless-window, --state-digest, --digest-every, --event,
// --input-script, --direct-build, --audio-capture, or --scenario other than
// none; --headless-frame is then optional. No other flag selects it:
// without those, --trace-requests, --log-sounds or --sound alone still open
// a window.

#include "sdl_audio.hpp"
#include "sdl_input_script.hpp"
#include "sdl_layout.hpp"
#include "sdl_menu.hpp"
#include "sdl_panels.hpp"
#include "sdl_session.hpp"

#include "original_audio_capture.hpp"
#include "original_canvas_font.hpp"
#include "original_menu.hpp"
#include "original_resources.generated.hpp"
#include "original_simulation.hpp"
#include "original_tdt.hpp"
#include "original_tdt_file.hpp"
#include "original_time.hpp"
#include "original_ui.hpp"
#include "original_wave.hpp"
#include "original_world_input.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// Main client-size rules. Win32 sizes Main from 1128:02aa (desktop-derived
// client caps, at most 816x576), 1128:08d6 (startup rectangle) and
// 1158:0334 (MINMAXINFO tracking limits), all in
// original_main_window_geometry. This frontend has no Win16 nonclient area,
// so it evaluates that helper once with fixed inputs and subtracts the same
// nonclient extents to get client sizes:
// - desktop 1024x768, at or above which the 816x576 caps apply;
// - the classic metrics used by resource_tests.cpp (frame 4, scrollbars 16,
//   menu 19, caption 19, border 1).
// The client = outer - frame/scrollbar/menu/caption subtraction is an
// approximation of Win16 GetClientRect for WS_OVERLAPPEDWINDOW plus a menu.
// Viewport width and height feed step_original_visible_facility_people,
// step_original_sky_decorations and select_original_ambient_sound, so the
// view must stay inside the same range as the Win32 client.
struct MainClientLimits {
  int initial_width{};
  int initial_height{};
  int minimum_width{};
  int minimum_height{};
  int maximum_width{};
  int maximum_height{};
};

MainClientLimits main_client_limits() {
  // One definition with the panel layout (sdl_layout.hpp).
  constexpr auto& metrics = simtower::sdl::kMainNonclientMetrics;
  const auto geometry = simtower::original_main_window_geometry(
      simtower::sdl::kDesktopWidth, simtower::sdl::kDesktopHeight, metrics);
  const int nonclient_width =
      2 * metrics.frame_width + metrics.vertical_scroll_width;
  const int nonclient_height = 2 * metrics.frame_height +
                               metrics.caption_height + metrics.menu_height +
                               metrics.horizontal_scroll_height;
  return {
      .initial_width = geometry.initial_width - nonclient_width,
      .initial_height = geometry.initial_height - nonclient_height,
      .minimum_width = geometry.minimum_track_width - nonclient_width,
      .minimum_height = geometry.minimum_track_height - nonclient_height,
      .maximum_width = geometry.maximum_track_width - nonclient_width,
      .maximum_height = geometry.maximum_track_height - nonclient_height,
  };
}

// The MINMAXINFO clamp (1158:0334) applied to every size the session sees.
std::pair<int, int> clamp_client(int width, int height) {
  const auto limits = main_client_limits();
  return {std::clamp(width, limits.minimum_width, limits.maximum_width),
          std::clamp(height, limits.minimum_height, limits.maximum_height)};
}

struct Options {
  std::filesystem::path pack{};
  std::optional<std::filesystem::path> open{};
  int width{main_client_limits().initial_width};
  int height{main_client_limits().initial_height};
  bool fast_mode{true};
  bool log_sounds{false};
  // --sound / --no-sound; unset means the mode default (window on,
  // headless off).
  std::optional<bool> sound{};
  std::optional<std::filesystem::path> audio_capture{};
  bool trace_requests{false};
  std::optional<std::filesystem::path> headless_frame{};
  int passes{200};
  std::optional<simtower::OriginalWorldPoint> view{};
  std::string scenario{"none"};
  bool state_digest{false};
  int digest_every{0};
  // --event NAME@PASS: run Session::trigger_event before that pass.
  std::vector<std::pair<std::string, int>> events{};
  std::optional<std::filesystem::path> input_script{};
  std::optional<std::filesystem::path> headless_window{};
  // --direct-build SPEC@PASS: Session::direct_build before that pass.
  std::vector<std::pair<simtower::DirectBuild, int>> direct_builds{};
  std::filesystem::path help_dir{};
  // --startup: a headless run starts through Session::run_startup too.
  bool startup{false};

  [[nodiscard]] bool headless() const {
    return headless_frame || headless_window || state_digest ||
           digest_every > 0 || scenario != "none" || !events.empty() ||
           audio_capture || input_script || !direct_builds.empty();
  }
  [[nodiscard]] bool sound_enabled() const {
    if (audio_capture) return true;
    return sound.value_or(!headless());
  }
};

[[noreturn]] void usage_error(const std::string& message) {
  throw std::invalid_argument(
      message +
      "\nusage: simtower_sdl [--pack FILE] [--open FILE.TDT] [--size WxH]"
      " [--slow] [--log-sounds] [--sound|--no-sound]"
      " [--audio-capture FILE.wav]"
      " [--headless-frame FILE.bmp [--passes N] [--view X,Y]]"
      " [--scenario none|busy|full] [--state-digest] [--digest-every K]"
      " [--trace-requests] [--event bomb|fire@PASS ...]"
      " [--input-script FILE] [--headless-window FILE.bmp]"
      " [--direct-build TYPE,FLOOR,LEFT[,VARIANT][,shift]@PASS ...]"
      " [--help-dir DIR]"
      " [--startup]"
      "\n--input-script exits 2 on a script error, 3 on a failed expect-*"
      " (see sdl_input_script.hpp)");
}

// Parses a whole decimal int; trailing junk ("12x", "3 ") is an error.
int parse_int(std::string_view text, const std::string& error) {
  try {
    std::size_t used = 0U;
    const int value = std::stoi(std::string(text), &used);
    if (used != text.size()) usage_error(error);
    return value;
  } catch (const std::exception&) {
    usage_error(error);
  }
}

// Parses "AxB" or "A,B" into two integers.
std::pair<int, int> parse_pair(std::string_view text, char separator) {
  const std::string error = "bad pair: " + std::string(text);
  const auto split = text.find(separator);
  if (split == std::string_view::npos) usage_error(error);
  return {parse_int(text.substr(0, split), error),
          parse_int(text.substr(split + 1), error)};
}

// TYPE,FLOOR,LEFT[,VARIANT][,shift]
simtower::DirectBuild parse_direct_build(const std::string& text) {
  std::vector<std::string> fields;
  std::size_t start = 0U;
  while (true) {
    const auto comma = text.find(',', start);
    fields.push_back(text.substr(start, comma - start));
    if (comma == std::string::npos) break;
    start = comma + 1U;
  }
  simtower::DirectBuild build{};
  if (!fields.empty() && fields.back() == "shift") {
    build.shift = true;
    fields.pop_back();
  }
  if (fields.size() < 3U || fields.size() > 4U) {
    usage_error("bad --direct-build: " + text);
  }
  const std::string error = "bad --direct-build: " + text;
  build.type = static_cast<std::uint16_t>(parse_int(fields[0], error));
  build.floor = static_cast<std::int16_t>(parse_int(fields[1], error));
  build.left = static_cast<std::uint16_t>(parse_int(fields[2], error));
  if (fields.size() == 4U) {
    build.variant = static_cast<std::uint8_t>(parse_int(fields[3], error));
  }
  return build;
}

int parse_count(std::string_view text) {
  return parse_int(text, "bad passes: " + std::string(text));
}

Options parse_options(int argc, char** argv) {
  Options options{};
  const auto value = [&](int& index) -> std::string_view {
    if (index + 1 >= argc) usage_error(std::string(argv[index]) +
                                       " needs a value");
    return argv[++index];
  };
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--pack") {
      options.pack = value(index);
    } else if (argument == "--open") {
      options.open = value(index);
    } else if (argument == "--size") {
      const auto [width, height] = parse_pair(value(index), 'x');
      options.width = width;
      options.height = height;
    } else if (argument == "--slow") {
      options.fast_mode = false;
    } else if (argument == "--log-sounds") {
      options.log_sounds = true;
    } else if (argument == "--sound") {
      options.sound = true;
    } else if (argument == "--no-sound") {
      options.sound = false;
    } else if (argument == "--audio-capture") {
      options.audio_capture = value(index);
    } else if (argument == "--headless-frame") {
      options.headless_frame = value(index);
    } else if (argument == "--passes") {
      options.passes = parse_count(value(index));
    } else if (argument == "--scenario") {
      options.scenario = value(index);
    } else if (argument == "--trace-requests") {
      options.trace_requests = true;
    } else if (argument == "--state-digest") {
      options.state_digest = true;
    } else if (argument == "--digest-every") {
      options.digest_every = parse_count(value(index));
      if (options.digest_every <= 0) usage_error("--digest-every must be > 0");
    } else if (argument == "--event") {
      const std::string text(value(index));
      const auto at = text.find('@');
      if (at == std::string::npos) usage_error("bad event: " + text);
      options.events.emplace_back(text.substr(0, at),
                                  parse_count(text.substr(at + 1)));
    } else if (argument == "--input-script") {
      options.input_script = value(index);
    } else if (argument == "--headless-window") {
      options.headless_window = value(index);
    } else if (argument == "--direct-build") {
      const std::string text(value(index));
      const auto at = text.find('@');
      if (at == std::string::npos) usage_error("bad --direct-build: " + text);
      options.direct_builds.emplace_back(parse_direct_build(text.substr(0, at)),
                                         parse_count(text.substr(at + 1)));
    } else if (argument == "--help-dir") {
      options.help_dir = value(index);
    } else if (argument == "--startup") {
      options.startup = true;
    } else if (argument == "--view") {
      const auto [x, y] = parse_pair(value(index), ',');
      options.view = simtower::OriginalWorldPoint{x, y};
    } else {
      usage_error("unknown option: " + std::string(argument));
    }
  }
  if (options.startup && options.scenario != "none") {
    usage_error("--startup starts on New / Load, not on a --scenario");
  }
  if (options.audio_capture && options.sound == false) {
    usage_error("--audio-capture needs sound; drop --no-sound");
  }
  if (options.width <= 0 || options.height <= 0 || options.passes < 0) {
    usage_error("size and passes must be positive");
  }
  for (const auto& [build, at_pass] : options.direct_builds) {
    if (at_pass < 0 || at_pass >= options.passes) {
      usage_error("--direct-build at pass " + std::to_string(at_pass) +
                  " is outside --passes " + std::to_string(options.passes));
    }
  }
  for (const auto& [name, at_pass] : options.events) {
    if (name != "bomb" && name != "fire") {
      usage_error("unknown event: " + name + " (expected bomb or fire)");
    }
    if (at_pass < 0 || at_pass >= options.passes) {
      usage_error("event " + name + "@" + std::to_string(at_pass) +
                  " is outside --passes " + std::to_string(options.passes));
    }
  }
  const auto [width, height] = clamp_client(options.width, options.height);
  if (width != options.width || height != options.height) {
    std::fprintf(stderr,
                 "simtower_sdl: --size %dx%d clamped to the Main client"
                 " range: %dx%d\n",
                 options.width, options.height, width, height);
    options.width = width;
    options.height = height;
  }
  if (options.pack.empty()) {
    // Default: the pack next to the executable, replacing the PE RCDATA.
    const char* base = SDL_GetBasePath();
    options.pack = std::filesystem::path(base ? base : "") /
                   "original_resources.pack";
  }
  if (options.help_dir.empty()) {
    // The build converts SIMTOWER.HLP into "help" next to the executable.
    const char* base = SDL_GetBasePath();
    options.help_dir = std::filesystem::path(base ? base : "") / "help";
  }
  return options;
}

std::vector<std::byte> read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("Could not open resource pack " + path.string());
  }
  const std::vector<char> chars((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(chars.size());
  for (std::size_t index = 0; index < chars.size(); ++index) {
    bytes[index] =
        static_cast<std::byte>(static_cast<unsigned char>(chars[index]));
  }
  return bytes;
}

// A headless run without --startup: New, or Open with the Win32 host's
// fall-back to New on a load failure (load_original_tower_from_path).
void start_tower(simtower::sdl::Session& session, const Options& options) {
  if (options.view) session.restore_view(options.view->x, options.view->y);
  if (options.open) {
    try {
      session.open_tower(*options.open);
      return;
    } catch (const simtower::OriginalTdtError& error) {
      std::fprintf(stderr, "simtower_sdl: open failed: %s\n", error.what());
    } catch (const simtower::OriginalTdtFileError& error) {
      std::fprintf(stderr, "simtower_sdl: open failed: %s\n", error.what());
    }
  }
  session.new_tower();
}

// Raster pixels are 0x00RRGGBB, which is SDL_PIXELFORMAT_XRGB8888.
bool write_bmp(const simtower::OriginalWorldRaster& raster,
               const std::filesystem::path& path) {
  std::vector<std::uint32_t> pixels = raster.pixels;
  SDL_Surface* surface = SDL_CreateSurfaceFrom(
      raster.width, raster.height, SDL_PIXELFORMAT_XRGB8888, pixels.data(),
      raster.width * 4);
  if (!surface) return false;
  const bool saved = SDL_SaveBMP(surface, path.string().c_str());
  SDL_DestroySurface(surface);
  return saved;
}

bool write_bytes(const std::filesystem::path& path,
                 const std::vector<std::byte>& bytes) {
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(stream);
}

// The WAVE resource ids whose PCM equals a captured buffer, in descriptor
// order (several ids can share identical samples). Read from the pack,
// never stored.
std::vector<std::int32_t> matching_wave_ids(
    const simtower::OriginalResources& resources,
    const std::vector<std::byte>& samples) {
  std::vector<std::int32_t> ids;
  for (const auto& descriptor : simtower::generated::kResources) {
    if (descriptor.type != "WAVE") continue;
    const auto wave = simtower::parse_original_wave(
        resources.find("WAVE", descriptor.numeric_id));
    if (wave.logical_size != 0U &&
        std::equal(wave.samples.begin(), wave.samples.end(), samples.begin(),
                   samples.end())) {
      ids.push_back(descriptor.numeric_id);
    }
  }
  return ids;
}

std::string join_wave_ids(const std::vector<std::int32_t>& ids) {
  if (ids.empty()) return "none";
  std::string out;
  for (const auto id : ids) {
    out += (out.empty() ? "" : "/") + std::to_string(id);
  }
  return out;
}

// Writes --audio-capture and reports what was started. The first voice's
// region is checked against its resource's PCM from the pack: every output
// sample must be the zero-order-hold source frame as (u - 128) << 8 plus
// whatever other voices overlap it.
bool write_audio_capture(const Options& options,
                         const simtower::sdl::Session& session,
                         const simtower::OriginalAudioCaptureDevice& capture) {
  const auto render = capture.render(44100U);
  const auto& path = *options.audio_capture;
  if (!write_bytes(path, simtower::OriginalAudioCaptureDevice::wav_file(render))) {
    std::fprintf(stderr, "simtower_sdl: could not write %s\n",
                 path.string().c_str());
    return false;
  }
  const auto& submissions = capture.submissions();
  std::map<std::string, std::size_t> by_resource;
  std::size_t per_voice[2]{};
  for (std::size_t index = 0; index < submissions.size(); ++index) {
    const auto& submission = submissions[index];
    const auto ids = join_wave_ids(
        matching_wave_ids(session.resources(), submission.samples));
    ++by_resource[ids];
    ++per_voice[submission.voice];
    if (!options.log_sounds) continue;
    const auto [first, end] = render.regions[index];
    std::string line =
        "capture voice " + std::to_string(index) + " channel " +
        std::to_string(submission.voice) + " resource " + ids + " at +" +
        std::to_string(submission.start_ms - render.origin_ms) + " ms passes " +
        std::to_string(submission.passes) + " samples [" +
        std::to_string(first) + "," + std::to_string(end) + ")";
    if (submission.stop_ms) {
      line += " stop +" + std::to_string(*submission.stop_ms - render.origin_ms) +
              " ms";
    }
    std::fprintf(stderr, "simtower_sdl: %s\n", line.c_str());
  }
  std::string tally;
  for (const auto& [ids, count] : by_resource) {
    tally += " " + ids + "=" + std::to_string(count);
  }
  std::fprintf(stderr,
               "simtower_sdl: audio capture %s: voices started %zu (channel 0"
               " %zu, channel 1 %zu); resources%s; origin %u ms; duration"
               " %.3f s (%zu samples at %u Hz)\n",
               path.string().c_str(), submissions.size(), per_voice[0],
               per_voice[1], tally.c_str(), render.origin_ms,
               static_cast<double>(render.samples.size()) / render.sample_rate,
               render.samples.size(), render.sample_rate);
  if (submissions.empty()) return true;

  // First started voice versus its pack PCM.
  const auto& first = submissions.front();
  const auto [begin, end] = render.regions.front();
  std::size_t overlapped = 0U;
  for (std::size_t index = 1; index < submissions.size(); ++index) {
    const auto [other_begin, other_end] = render.regions[index];
    if (other_begin < end && other_end > begin) {
      overlapped = std::max(overlapped, end - std::max(begin, other_begin));
    }
  }
  const std::size_t clean_end = end - overlapped;
  const auto first_ids = matching_wave_ids(session.resources(), first.samples);
  if (first_ids.empty()) {
    std::fprintf(stderr,
                 "simtower_sdl: capture check: first voice (%zu bytes) matches"
                 " no WAVE resource in the pack\n",
                 first.samples.size());
    return false;
  }
  const std::int32_t resource_id = first_ids.front();
  const auto wave = simtower::parse_original_wave(
      session.resources().find("WAVE", resource_id));
  const simtower::OriginalPcmFormat format{
      wave.format_tag, wave.channels, wave.samples_per_second,
      wave.average_bytes_per_second, wave.block_align, wave.bits_per_sample};
  const std::uint64_t frames =
      wave.block_align == 0U ? 0U : wave.samples.size() / wave.block_align;
  if (frames == 0U) {
    std::fprintf(stderr,
                 "simtower_sdl: capture check: first voice WAVE/%d has no"
                 " whole frame (block_align %u, %zu bytes)\n",
                 static_cast<int>(resource_id), wave.block_align,
                 wave.samples.size());
    return false;
  }
  std::size_t mismatches = 0U;
  for (std::size_t at = begin; at < clean_end; ++at) {
    const std::uint64_t source =
        static_cast<std::uint64_t>(at - begin) * wave.samples_per_second /
        render.sample_rate;
    if (render.samples[at] !=
        simtower::original_pcm_frame_s16(format, wave.samples,
                                         static_cast<std::size_t>(source % frames))) {
      ++mismatches;
    }
  }
  std::fprintf(stderr,
               "simtower_sdl: capture check: first voice WAVE/%d (%u Hz,"
               " %zu bytes, %u passes) samples [%zu,%zu), %zu overlapped by"
               " later voices, %zu compared, %zu mismatches\n",
               static_cast<int>(resource_id), wave.samples_per_second,
               wave.samples.size(), first.passes, begin, end, overlapped,
               clean_end - begin, mismatches);
  return mismatches == 0U;
}

int run_headless(const Options& options) {
  // A virtual clock makes every pass one full simulation frame: it advances
  // by the fast-mode host period (or the nominal six-tick gate when slow)
  // before each pass, so --passes N runs exactly N frames in both modes.
  std::uint32_t clock_ms = 1'000'000U;
  const std::uint32_t pass_ms =
      options.fast_mode ? simtower::kNativeFastModeFramePeriodMs
                        : simtower::kOriginalSimulationGateNominalMs;
  // Sound is off unless asked for. The devices outlive the session, whose
  // runtime stops them on destruction.
  std::optional<simtower::OriginalAudioCaptureDevice> capture;
  std::optional<simtower::sdl::SdlAudioDevice> sdl_audio;
  simtower::OriginalAudioDevice* audio_device = nullptr;
  if (options.audio_capture) {
    audio_device = &capture.emplace([&clock_ms] { return clock_ms; });
  } else if (options.sound_enabled()) {
    audio_device = &sdl_audio.emplace();
  }
  simtower::sdl::Session session(read_file(options.pack),
                                 [&clock_ms] { return clock_ms; },
                                 {.fast_mode_enabled = options.fast_mode,
                                  .sound_enabled = options.sound_enabled(),
                                  .log_sounds = options.log_sounds,
                                  .trace_requests = options.trace_requests},
                                 audio_device);
  session.resize(options.width, options.height);
  // No help viewer: a help request is only logged with its URL.
  session.set_help_directory(options.help_dir);
  // The "input" field is added only with a script, so the regression oracle
  // (no script) prints exactly what it printed before input existed.
  const bool scripted = options.input_script.has_value();
  const auto print_digest = [&session, scripted](int pass) {
    const auto digest = session.state_digest();
    std::printf("digest %016llx pass %d tower %016llx process %016llx"
                " host %016llx requests %016llx",
                static_cast<unsigned long long>(digest.combined), pass,
                static_cast<unsigned long long>(digest.tower),
                static_cast<unsigned long long>(digest.process),
                static_cast<unsigned long long>(digest.host),
                static_cast<unsigned long long>(digest.requests));
    if (scripted) {
      std::printf(" input %016llx", static_cast<unsigned long long>(
                                         session.input_digest()));
    }
    std::printf("\n");
  };
  int completed = 0;
  // A "dialog-hold" without FILE wrote --headless-window already.
  bool window_held = false;
  const auto hold_dialog = [&](const std::string& path,
                               const std::string& where) {
    std::filesystem::path target = path;
    if (target.empty()) {
      if (!options.headless_window) {
        throw simtower::sdl::ScriptFailure(
            2, where + ": dialog-hold needs FILE or --headless-window");
      }
      target = *options.headless_window;
      window_held = true;
    }
    // No session.present() here: it may rebuild the backing, which is
    // digest state. The frame shows the backing as the dialog opened.
    const auto raster = simtower::sdl::compose_window(session);
    if (!write_bmp(raster, target)) {
      throw std::runtime_error("could not write " + target.string() + ": " +
                               SDL_GetError());
    }
    std::fprintf(stderr, "simtower_sdl: wrote %s (%dx%d, dialog held)\n",
                 target.string().c_str(), raster.width, raster.height);
  };
  // Each "pass N" runs passes until N have run; the commands after it run
  // between pass N and pass N+1 on a fixed virtual clock.
  // validate_pass_order makes every target reachable in order.
  std::optional<simtower::sdl::ScriptPlayer> player;
  const auto make_player = [&]() {
    auto commands = simtower::sdl::parse_input_script(*options.input_script);
    simtower::sdl::validate_pass_order(commands, options.passes);
    player.emplace(std::move(commands), session,
                   [&] { print_digest(completed); }, hold_dialog);
  };
  if (options.startup) {
    // WinMain's startup on the virtual clock: the splash wait advances it
    // by one coarse tick (1208:05e6) per step. The script, made here, can
    // drive New / Load before its first pass.
    if (options.view) session.restore_view(options.view->x, options.view->y);
    if (scripted) {
      make_player();
      session.set_input_source(&*player);
    }
    constexpr std::uint32_t kSplashWaitStepMs = 16U;
    const bool started = session.run_startup(
        {.present = [] {},
         .wait =
             [&clock_ms] {
               clock_ms += kSplashWaitStepMs;
               return true;
             }},
        options.open);
    session.set_input_source(nullptr);
    // Quit (logged by the session): 1128:0005 fails, no tower exists and
    // no pass runs.
    if (!started) return 0;
  } else {
    start_tower(session, options);
  }
  if (options.scenario != "none") {
    if (options.open) usage_error("--scenario builds on New, not --open");
    const auto summary = session.apply_scenario(options.scenario);
    std::string built;
    for (const auto& [type, count] : summary.built_by_type) {
      built += " " + simtower::scenario_type_name(type) + "=" +
               std::to_string(count);
    }
    std::fprintf(stderr,
                 "simtower_sdl: scenario %s built:%s; extra cars %zu, shaft"
                 " extensions %zu, rating dialogs %zu, balance %d\n",
                 options.scenario.c_str(), built.c_str(),
                 summary.elevator_cars, summary.shaft_extensions,
                 summary.rating_dialogs,
                 static_cast<int>(summary.balance_after));
  }
  const auto run_pass = [&]() {
    const int pass = completed;
    for (const auto& [build, at_pass] : options.direct_builds) {
      if (at_pass != pass) continue;
      const bool built = session.direct_build(build);
      std::fprintf(stderr,
                   "simtower_sdl: direct build type %u floor %d left %u"
                   " before pass %d: %s\n",
                   build.type, build.floor, build.left, pass,
                   built ? "built" : "rejected");
    }
    for (const auto& [name, at_pass] : options.events) {
      if (at_pass != pass) continue;
      const bool started = session.trigger_event(name);
      std::fprintf(stderr, "simtower_sdl: event %s before pass %d: %s\n",
                   name.c_str(), pass, started ? "started" : "not offered");
    }
    clock_ms += pass_ms;
    (void)session.run_idle_pass();
    ++completed;
    if (options.digest_every > 0 && completed % options.digest_every == 0) {
      print_digest(completed);
    }
  };
  if (scripted) {
    if (!player) make_player();
    session.set_input_source(&*player);
    while (const auto* pass = player->run_until_pass()) {
      while (completed < static_cast<int>(pass->value) &&
             !session.quit_requested()) {
        run_pass();
      }
    }
    session.set_input_source(nullptr);
    // After File > Exit the rest of the script never ran, so its unused
    // pick-file results are expected.
    if (!session.quit_requested() && session.queued_picked_files() != 0U) {
      throw simtower::sdl::ScriptFailure(
          2, options.input_script->string() + ": " +
                 std::to_string(session.queued_picked_files()) +
                 " pick-file result(s) never used by an Open or Save As");
    }
  }
  // File > Exit destroys Main (WM_DESTROY posts WM_QUIT): no further pass.
  while (completed < options.passes && !session.quit_requested()) run_pass();
  if (session.quit_requested()) {
    std::fprintf(stderr,
                 "simtower_sdl: Exit after %d of %d passes; the rest of the"
                 " run is skipped\n",
                 completed, options.passes);
  }
  // Taken before the presentation step below, which may rebuild the backing.
  if (options.state_digest) print_digest(completed);
  std::fflush(stdout);

  const auto& header = session.document()->header;
  const auto& requests = session.host_requests();
  std::string tally;
  for (const auto& [key, count] : session.request_tally()) {
    tally += "; " + key + " " + std::to_string(count);
  }
  std::fprintf(stderr,
               "simtower_sdl: after %d passes: frame_time %u day %d rating %u"
               " balance %d people records %u population %d; logged dialogs"
               " %zu sounds %zu focus %zu status %zu%s\n",
               completed, static_cast<unsigned>(header.frame_time),
               static_cast<int>(header.current_day),
               static_cast<unsigned>(header.rating),
               static_cast<int>(header.balance),
               static_cast<unsigned>(session.document()->people_count),
               static_cast<int>(
                   session.document()->post_elevator.finance.total_population),
               requests.dialogs, requests.sounds, requests.focus,
               requests.status, tally.c_str());
  if (capture && !write_audio_capture(options, session, *capture)) return 1;
  if (!options.headless_frame && !options.headless_window) return 0;

  // Presentation only: no scheduler, palette or clock step.
  session.present();
  const auto write = [](const simtower::OriginalWorldRaster& raster,
                        const std::filesystem::path& path) {
    if (!write_bmp(raster, path)) {
      std::fprintf(stderr, "simtower_sdl: could not write %s: %s\n",
                   path.string().c_str(), SDL_GetError());
      return false;
    }
    std::fprintf(stderr, "simtower_sdl: wrote %s (%dx%d)\n",
                 path.string().c_str(), raster.width, raster.height);
    return true;
  };
  if (options.headless_frame &&
      !write(session.main_raster(), *options.headless_frame)) {
    return 1;
  }
  if (options.headless_window && !window_held &&
      !write(simtower::sdl::compose_window(session),
             *options.headless_window)) {
    return 1;
  }
  const auto& view = session.viewport();
  std::fprintf(stderr, "simtower_sdl: view %d,%d\n", view.x, view.y);
  return 0;
}

class Window {
 public:
  explicit Window(const Options& options) {
    if (!SDL_Init(SDL_INIT_VIDEO)) fail("SDL_Init");
    // One window holding Main and the panels (sdl_layout.hpp). Window
    // coordinates are the Win16 desktop coordinates; the texture is scaled
    // to the drawable on HiDPI, so mouse coordinates (window points) map
    // one to one onto the composed raster.
    if (!SDL_CreateWindowAndRenderer(
            "SimTower", simtower::sdl::window_width_for(options.width),
            simtower::sdl::window_height_for(options.height),
            SDL_WINDOW_RESIZABLE, &window_, &renderer_)) {
      const std::string error = SDL_GetError();
      SDL_Quit();
      throw std::runtime_error("SDL_CreateWindowAndRenderer: " + error);
    }
    // MINMAXINFO tracking limits (1158:0334) on the Main client.
    const auto limits = main_client_limits();
    SDL_SetWindowMinimumSize(
        window_, simtower::sdl::window_width_for(limits.minimum_width),
        simtower::sdl::window_height_for(limits.minimum_height));
    SDL_SetWindowMaximumSize(
        window_, simtower::sdl::window_width_for(limits.maximum_width),
        simtower::sdl::window_height_for(limits.maximum_height));
  }
  ~Window() {
    for (auto* cursor : cursors_) {
      if (cursor) SDL_DestroyCursor(cursor);
    }
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
  }
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  // The Main client size for the current window size, clamped as well, in
  // case a window manager ignores the size limits.
  [[nodiscard]] std::pair<int, int> client_size() const {
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window_, &width, &height);
    return clamp_client(simtower::sdl::main_client_width_for(width),
                        simtower::sdl::main_client_height_for(height));
  }

  [[nodiscard]] SDL_WindowFlags flags() const {
    return SDL_GetWindowFlags(window_);
  }

  // Replaces each window's SetDIBitsToDevice with one composed raster.
  void present(const simtower::OriginalWorldRaster& raster) {
    if (raster.width <= 0 || raster.height <= 0) return;
    if (!texture_ || texture_width_ != raster.width ||
        texture_height_ != raster.height) {
      if (texture_) SDL_DestroyTexture(texture_);
      texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, raster.width,
                                   raster.height);
      if (!texture_) fail("SDL_CreateTexture");
      SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
      texture_width_ = raster.width;
      texture_height_ = raster.height;
    }
    SDL_UpdateTexture(texture_, nullptr, raster.pixels.data(),
                      raster.width * 4);
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
  }

  [[nodiscard]] SDL_Window* handle() const { return window_; }

  // The caption follows the document title (update_original_tower_title).
  void set_title(const std::string& title) {
    if (title == title_) return;
    title_ = title;
    SDL_SetWindowTitle(window_, title.c_str());
  }

  // TEMP: the original CURSOR resources (1002..1005) are not decoded yet,
  // so each resolve_original_cursor id maps to an SDL system cursor.
  void set_cursor(std::uint16_t cursor_id) {
    if (cursor_id == cursor_id_) return;
    cursor_id_ = cursor_id;
    SDL_SystemCursor system = SDL_SYSTEM_CURSOR_DEFAULT;
    switch (cursor_id) {
      case 2U: system = SDL_SYSTEM_CURSOR_CROSSHAIR; break;
      case 4U: system = SDL_SYSTEM_CURSOR_WAIT; break;
      case 1000U: system = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
      case 1002U: system = SDL_SYSTEM_CURSOR_CROSSHAIR; break;  // build
      case 1003U: system = SDL_SYSTEM_CURSOR_POINTER; break;    // inspect
      case 1004U: system = SDL_SYSTEM_CURSOR_POINTER; break;    // finger
      case 1005U: system = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;  // bulldoze
      default: break;
    }
    auto*& cursor = cursors_[static_cast<std::size_t>(system)];
    if (!cursor) cursor = SDL_CreateSystemCursor(system);
    if (cursor) SDL_SetCursor(cursor);
  }

 private:
  [[noreturn]] static void fail(const char* what) {
    throw std::runtime_error(std::string(what) + ": " + SDL_GetError());
  }

  SDL_Window* window_{};
  SDL_Renderer* renderer_{};
  SDL_Texture* texture_{};
  int texture_width_{};
  int texture_height_{};
  std::uint16_t cursor_id_{0xFFFFU};
  std::array<SDL_Cursor*, SDL_SYSTEM_CURSOR_COUNT> cursors_{};
  std::string title_{};
};

// Main activation word and IsIconic for the idle-pass audio prelude.
struct WindowActivation {
  bool active{true};
  bool iconic{false};
};

// The state the window opened in: one launched in the background has no
// input focus and never receives FOCUS_LOST, so it must start inactive.
WindowActivation initial_activation(SDL_WindowFlags flags) {
  return {.active = (flags & SDL_WINDOW_INPUT_FOCUS) != 0U,
          .iconic = (flags & SDL_WINDOW_MINIMIZED) != 0U};
}

// SDL mouse events as session input: window points are the logical
// coordinates, Shift/Control come from the keyboard state, and a left
// press with clicks >= 2 is WM_LBUTTONDBLCLK (Main's CS_DBLCLKS).
std::optional<simtower::sdl::InputEvent> pointer_input(const SDL_Event& event) {
  using simtower::sdl::InputKind;
  const auto modifiers = SDL_GetModState();
  const std::uint16_t keys = static_cast<std::uint16_t>(
      ((modifiers & SDL_KMOD_SHIFT) != 0 ? simtower::kOriginalKeyShift : 0U) |
      ((modifiers & SDL_KMOD_CTRL) != 0 ? simtower::kOriginalKeyControl : 0U));
  const auto at = [&](InputKind kind, float x, float y) {
    return simtower::sdl::InputEvent{kind, static_cast<int>(x),
                                     static_cast<int>(y), keys};
  };
  switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
      return at(InputKind::move, event.motion.x, event.motion.y);
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      if (event.button.button == SDL_BUTTON_LEFT) {
        return at(event.button.clicks >= 2 ? InputKind::double_click
                                            : InputKind::down,
                  event.button.x, event.button.y);
      }
      if (event.button.button == SDL_BUTTON_RIGHT) {
        return at(InputKind::right_down, event.button.x, event.button.y);
      }
      return std::nullopt;
    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (event.button.button == SDL_BUTTON_LEFT) {
        return at(InputKind::up, event.button.x, event.button.y);
      }
      if (event.button.button == SDL_BUTTON_RIGHT) {
        return at(InputKind::right_up, event.button.x, event.button.y);
      }
      return std::nullopt;
    default:
      return std::nullopt;
  }
}

// WM_SIZE: a resize changes only the Main client; the panels stay put.
void resize_session(const SDL_Event& event, simtower::sdl::Session& session) {
  const auto [width, height] =
      clamp_client(simtower::sdl::main_client_width_for(event.window.data1),
                   simtower::sdl::main_client_height_for(event.window.data2));
  session.resize(width, height);
}

// A WM_KEYDOWN that TranslateAccelerator matches against TOWER_MENU's
// table (its one entry, VK_F1 -> 40021). The same test for Main and for the
// Elevator Control path. TEMP: auto-repeat downs are not offered to it (the
// existing Main behaviour; whether Win16 TranslateAccelerator acts on
// repeats is not established).
bool accelerator_key_down(const SDL_Event& event) {
  return event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1 &&
         !event.key.repeat;
}

// WM_KEYDOWN / WM_KEYUP for a dialog manager (an open modal dialog, or the
// active Elevator Control): the key, Shift, and a letter's lower-case
// character (a button mnemonic); nullopt for any other event.
std::optional<simtower::sdl::DialogInput> dialog_key_input(
    const SDL_Event& event) {
  using simtower::sdl::DialogInput;
  using simtower::sdl::DialogKey;
  if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) {
    return std::nullopt;
  }
  DialogInput input{event.type == SDL_EVENT_KEY_DOWN
                        ? DialogInput::Kind::key
                        : DialogInput::Kind::key_up,
                    {},
                    DialogKey::other};
  const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
  input.shift = shift;
  switch (event.key.key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER: input.key = DialogKey::enter; break;
    case SDLK_ESCAPE: input.key = DialogKey::escape; break;
    case SDLK_TAB:
      input.key = shift ? DialogKey::shift_tab : DialogKey::tab;
      break;
    case SDLK_SPACE: input.key = DialogKey::space; break;
    case SDLK_LEFT: input.key = DialogKey::left; break;
    case SDLK_RIGHT: input.key = DialogKey::right; break;
    case SDLK_UP: input.key = DialogKey::up; break;
    case SDLK_DOWN: input.key = DialogKey::down; break;
    case SDLK_HOME: input.key = DialogKey::home; break;
    case SDLK_END: input.key = DialogKey::end; break;
    case SDLK_PAGEUP: input.key = DialogKey::page_up; break;
    case SDLK_PAGEDOWN: input.key = DialogKey::page_down; break;
    case SDLK_BACKSPACE: input.key = DialogKey::backspace; break;
    case SDLK_DELETE: input.key = DialogKey::delete_key; break;
    case SDLK_F4: input.key = DialogKey::f4; break;
    // WM_SYSKEYDOWN: Alt or F10 alone.
    case SDLK_LALT:
    case SDLK_RALT:
    case SDLK_F10: input.key = DialogKey::menu; break;
    default:
      // A letter is a button mnemonic (SDL keycodes of letters are
      // their lower-case ASCII).
      if (event.key.key >= SDLK_A && event.key.key <= SDLK_Z) {
        input.character = static_cast<char>(event.key.key);
      }
      break;
  }
  return input;
}

// The 1050:05a7 selector modal in the window: a nested SDL_WaitEvent loop
// that re-presents the window (with the selector overlay) and runs no idle
// pass, as DialogBoxIndirectParamW's own loop does. Only pointer input,
// QUIT, EXPOSED and RESIZED are handled inside it; other events (focus,
// keys, wheel) are dropped while it runs.
class WindowModalSource final : public simtower::sdl::InputSource {
 public:
  WindowModalSource(Window& window, simtower::sdl::Session& session)
      : window_(window), session_(session) {}

  [[nodiscard]] bool quit_requested() const { return quit_requested_; }

  std::optional<simtower::sdl::InputEvent> next_modal_event() override {
    SDL_Event event{};
    while (!quit_requested_ && SDL_WaitEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        quit_requested_ = true;
        break;
      }
      if (event.type == SDL_EVENT_WINDOW_EXPOSED) {
        modal_changed();
        continue;
      }
      if (event.type == SDL_EVENT_WINDOW_RESIZED) {
        // A Win32 modal loop still dispatches WM_SIZE to Main.
        resize_session(event, session_);
        modal_changed();
        continue;
      }
      if (const auto input = pointer_input(event)) return input;
    }
    return std::nullopt;
  }

  void modal_changed() override {
    window_.present(simtower::sdl::compose_window(session_));
  }

  // The window answers every dialog.
  bool drives_dialog(const simtower::sdl::DialogWindow& /*dialog*/) override {
    return true;
  }

  // GetAsyncKeyState(VK_LBUTTON) < 0: SDL's button state after a pump,
  // which already counts the events queued behind the one being routed
  // (a fast click's release reads as up). The button is SDL_BUTTON_LEFT,
  // the one pointer_input routes as the press.
  std::optional<bool> primary_button_held() override {
    SDL_PumpEvents();
    return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0U;
  }

  // The open dialog's loop (sdl_dialog.hpp): pointer input, the keys the
  // dialog manager and procedures see, and the timer wait. Other events
  // (focus, wheel, key releases) only end a timed wait early, so the
  // session can check the deadline again. Every input carries the
  // milliseconds this call waited (the dialog clock).
  std::optional<simtower::sdl::DialogInput> next_dialog_input(
      std::optional<std::uint32_t> wait_ms) override {
    using simtower::sdl::DialogInput;
    using simtower::sdl::DialogKey;
    // 1068:00fb / 1010:057b select the stock arrow.
    window_.set_cursor(0U);
    if (!text_input_) {
      // Typed characters (WM_CHAR) for edits and list searches.
      text_input_ = SDL_StartTextInput(window_.handle());
    }
    if (!pending_.empty()) {
      auto input = pending_.front();
      pending_.pop_front();
      return input;
    }
    const Uint64 started = SDL_GetTicks();
    const auto with_elapsed = [&](DialogInput input) {
      input.elapsed_ms = static_cast<std::uint32_t>(std::min<Uint64>(
          SDL_GetTicks() - started, 0xFFFFFFFFU));
      return input;
    };
    SDL_Event event{};
    while (!quit_requested_) {
      std::optional<std::uint32_t> remaining{};
      if (wait_ms) {
        const auto spent = SDL_GetTicks() - started;
        remaining = spent >= *wait_ms
                        ? 0U
                        : static_cast<std::uint32_t>(*wait_ms - spent);
      }
      const bool got =
          remaining ? SDL_WaitEventTimeout(
                          &event, static_cast<Sint32>(std::min<std::uint32_t>(
                                      *remaining, 0x7fffffffU)))
                    : SDL_WaitEvent(&event);
      if (!got) {
        if (!remaining) return std::nullopt;
        return with_elapsed({DialogInput::Kind::wait_elapsed, {}, {}});
      }
      if (event.type == SDL_EVENT_QUIT) {
        quit_requested_ = true;
        break;
      }
      if (event.type == SDL_EVENT_WINDOW_EXPOSED) {
        modal_changed();
        continue;
      }
      if (event.type == SDL_EVENT_WINDOW_RESIZED) {
        // Session::resize re-places the open dialogs.
        resize_session(event, session_);
        modal_changed();
        continue;
      }
      if (const auto input = pointer_input(event)) {
        return with_elapsed({DialogInput::Kind::pointer, *input, {}});
      }
      if (event.type == SDL_EVENT_TEXT_INPUT && event.text.text) {
        // WM_CHAR: each typed character in Windows-1252 (others dropped).
        for (const char character :
             simtower::original_utf8_to_cp1252(event.text.text)) {
          DialogInput input{DialogInput::Kind::character, {},
                            DialogKey::other};
          input.character = character;
          pending_.push_back(input);
        }
        if (!pending_.empty()) {
          auto input = pending_.front();
          pending_.pop_front();
          return with_elapsed(input);
        }
        continue;
      }
      if (const auto key = dialog_key_input(event)) {
        return with_elapsed(*key);
      }
      if (wait_ms) {
        return with_elapsed({DialogInput::Kind::wait_elapsed, {}, {}});
      }
    }
    // Quit: the dialog takes its default answer and the main loop ends.
    return std::nullopt;
  }

  void dialog_closed() override {
    pending_.clear();
    if (text_input_ && session_.dialogs().empty()) {
      SDL_StopTextInput(window_.handle());
      text_input_ = false;
    }
  }

 private:
  Window& window_;
  simtower::sdl::Session& session_;
  bool quit_requested_{};
  bool text_input_{};
  // The rest of a multi-character SDL text event.
  std::deque<simtower::sdl::DialogInput> pending_{};
};

// The native Open / Save As sheets (SDL_ShowOpenFileDialog /
// SDL_ShowSaveFileDialog). SDL calls back asynchronously, so the sheet runs
// a nested wait loop that only re-presents the window, like a Win32 modal:
// no idle pass runs until it closes. TEMP: no initial directory (the Win32
// host's [Paths] Save has no SDL profile yet), and Save As uses the save
// sheet, whose own replace prompt can precede the game's.
class SdlFilePicker final : public simtower::sdl::FilePicker {
 public:
  SdlFilePicker(Window& window, simtower::sdl::Session& session)
      : window_(window), session_(session) {}

  [[nodiscard]] bool quit_requested() const { return quit_requested_; }

  std::optional<std::filesystem::path> pick(bool save) override {
    static constexpr SDL_DialogFileFilter kFilters[] = {
        {"SimTower Data (*.TDT)", "TDT;tdt"},
        {"all files (*.*)", "*"},
    };
    State state{};
    if (save) {
      SDL_ShowSaveFileDialog(&SdlFilePicker::done, &state, window_.handle(),
                             kFilters, 2, nullptr);
    } else {
      SDL_ShowOpenFileDialog(&SdlFilePicker::done, &state, window_.handle(),
                             kFilters, 2, nullptr, false);
    }
    while (!state.finished.load()) {
      SDL_Event event{};
      if (!SDL_WaitEventTimeout(&event, 50)) continue;
      if (event.type == SDL_EVENT_QUIT) {
        quit_requested_ = true;
      } else if (event.type == SDL_EVENT_WINDOW_EXPOSED) {
        window_.present(simtower::sdl::compose_window(session_));
      }
    }
    const std::lock_guard lock(state.mutex);
    if (!state.error.empty()) {
      std::fprintf(stderr, "simtower_sdl: file dialog: %s\n",
                   state.error.c_str());
    }
    return state.path;
  }

 private:
  struct State {
    std::mutex mutex;
    std::optional<std::filesystem::path> path;
    std::string error;
    std::atomic<bool> finished{false};
  };

  static void SDLCALL done(void* userdata, const char* const* files, int) {
    auto& state = *static_cast<State*>(userdata);
    {
      const std::lock_guard lock(state.mutex);
      if (!files) {
        state.error = SDL_GetError();
      } else if (files[0]) {
        state.path = std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t*>(files[0])));
      }
    }
    state.finished.store(true);
  }

  Window& window_;
  simtower::sdl::Session& session_;
  bool quit_requested_{};
};

// USER enters (and leaves) keyboard menu mode on a lone Alt: pressed and
// released with no other key, mouse button, wheel or focus change in
// between, so Alt+Tab, Alt+F4, Alt+arrow and Alt+click never do. On macOS
// Option is Alt.
class LoneAlt {
 public:
  // Feeds one event; true when it is the release of a lone Alt.
  bool released(const SDL_Event& event) {
    switch (event.type) {
      case SDL_EVENT_KEY_DOWN:
        if (!is_alt(event.key.key)) {
          alone_ = false;
        } else if (!event.key.repeat) {
          alone_ = true;
        }
        return false;
      case SDL_EVENT_KEY_UP: {
        if (!is_alt(event.key.key)) return false;
        const bool alone = alone_;
        alone_ = false;
        return alone;
      }
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_WHEEL:
      case SDL_EVENT_WINDOW_FOCUS_GAINED:
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        alone_ = false;
        return false;
      default:
        return false;
    }
  }

 private:
  static bool is_alt(SDL_Keycode key) {
    return key == SDLK_LALT || key == SDLK_RALT;
  }

  bool alone_{};
};

// The bar title whose mnemonic is `key` (Alt+letter), or -1.
int menu_title_for_mnemonic(const simtower::OriginalMenuItems& menu,
                            SDL_Keycode key) {
  if (key < SDLK_A || key > SDLK_Z) return -1;
  const char character = static_cast<char>('a' + (key - SDLK_A));
  for (std::size_t index = 0; index < menu.size(); ++index) {
    if (simtower::original_menu_mnemonic(menu[index].text) == character) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

// USER's modal menu loop: from a press on a title (or Alt/F10/Alt+letter)
// until the menu closes, events go to the tracker only and no idle pass
// runs. A keyboard entry selects the title only, or with open_popup also
// opens its drop-down (Alt+letter). The chosen command is dispatched after
// the menu has closed, as the posted WM_COMMAND is. Returns false on quit.
bool run_menu_loop(Window& window, simtower::sdl::Session& session,
                   int bar, bool keyboard, bool open_popup = false) {
  simtower::sdl::MenuTracker tracker(session.menu(), session.layout());
  auto step = tracker.open(bar, keyboard);
  if (keyboard && open_popup) step = tracker.key(simtower::sdl::MenuKey::down);
  LoneAlt lone_alt;
  bool running = true;
  while (true) {
    if (step.redraw || step.close) {
      session.set_menu_view(step.close ? simtower::sdl::MenuView{}
                                       : tracker.view());
      window.present(simtower::sdl::compose_window(session));
    }
    if (step.close) break;
    SDL_Event event{};
    if (!SDL_WaitEvent(&event)) break;
    const bool lone_alt_up = lone_alt.released(event);
    step = {};
    if (event.type == SDL_EVENT_QUIT) {
      running = false;
      step.close = true;
    } else if (event.type == SDL_EVENT_WINDOW_EXPOSED) {
      step.redraw = true;
    } else if (event.type == SDL_EVENT_WINDOW_RESIZED ||
               event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
      // USER ends menu mode when the window loses activation; a resize
      // also closes it here (the layout changes under the tracker).
      if (event.type == SDL_EVENT_WINDOW_RESIZED) {
        resize_session(event, session);
      }
      step.close = true;
    } else if (const auto input = pointer_input(event)) {
      step = tracker.pointer(*input);
    } else if (event.type == SDL_EVENT_KEY_DOWN) {
      using simtower::sdl::MenuKey;
      switch (event.key.key) {
        case SDLK_LEFT: step = tracker.key(MenuKey::left); break;
        case SDLK_RIGHT: step = tracker.key(MenuKey::right); break;
        case SDLK_UP: step = tracker.key(MenuKey::up); break;
        case SDLK_DOWN: step = tracker.key(MenuKey::down); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: step = tracker.key(MenuKey::enter); break;
        case SDLK_ESCAPE: step = tracker.key(MenuKey::escape); break;
        case SDLK_F10: step = tracker.key(MenuKey::toggle); break;
        default:
          if (event.key.key >= SDLK_A && event.key.key <= SDLK_Z) {
            step = tracker.key(MenuKey::character,
                               static_cast<char>('a' + (event.key.key -
                                                        SDLK_A)));
          }
          break;
      }
    } else if (lone_alt_up) {
      step = tracker.key(simtower::sdl::MenuKey::toggle);
    }
  }
  if (running && step.command) {
    (void)session.choose_menu_command(*step.command);
  }
  return running;
}

// Returns false on quit.
bool handle_event(const SDL_Event& event, simtower::sdl::Session& session,
                  Window& window, bool& expose,
                  WindowActivation& activation, LoneAlt& lone_alt) {
  const bool lone_alt_up = lone_alt.released(event);
  // While Elevator Control is open it is the active window and Main (with
  // its menu bar) is disabled. 1258:00ea-0118 (DS:31a0 set): a message goes
  // to TranslateAccelerator(Elevator Control, TOWER_MENU's table) first
  // (1258:00fd), and only when that returns 0 to its dialog manager
  // (IsDialogMessage, 1258:010f). TranslateAccelerator takes only key-downs
  // (and characters), so every key-up reaches the dialog manager. Presses
  // on the menu bar reach nothing, and the SDL-only arrow / wheel scroll
  // stand-ins of Main are off.
  const bool control_open = session.elevator_control() != nullptr;
  if (control_open) {
    if (accelerator_key_down(event) &&
        session.accelerator(simtower::kOriginalVirtualKeyF1)) {
      expose = true;
      return true;
    }
    if (const auto key = dialog_key_input(event)) {
      // Auto-repeat downs are WM_KEYDOWN repeats, as in USER.
      session.elevator_control_input(*key);
      expose = true;
      return true;
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL) return true;
  }
  if (const auto input = pointer_input(event)) {
    if (!control_open &&
        (input->kind == simtower::sdl::InputKind::down ||
         input->kind == simtower::sdl::InputKind::double_click)) {
      const auto bar =
          simtower::sdl::menu_bar_layout(session.menu(), session.layout());
      if (bar.strip.contains(input->x, input->y)) {
        // A press on the bar enters menu mode (on a title) or, between
        // titles, does nothing; it never reaches the panels.
        const int title =
            simtower::sdl::menu_title_at(bar, input->x, input->y);
        expose = true;
        return title < 0 || run_menu_loop(window, session, title, false);
      }
    }
    session.pointer_event(*input);
    expose = true;
    return true;
  }
  switch (event.type) {
    case SDL_EVENT_QUIT:
      return false;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      activation.active = true;
      break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      activation.active = false;
      break;
    case SDL_EVENT_WINDOW_MINIMIZED:
      activation.iconic = true;
      break;
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
      activation.iconic = false;
      break;
    case SDL_EVENT_WINDOW_RESIZED:
      resize_session(event, session);
      expose = true;
      break;
    case SDL_EVENT_WINDOW_EXPOSED:
      expose = true;
      break;
    case SDL_EVENT_KEY_UP:
      // A lone Alt (or F10 below) enters keyboard menu mode on File.
      if (lone_alt_up) {
        expose = true;
        return run_menu_loop(window, session, 0, true);
      }
      break;
    case SDL_EVENT_KEY_DOWN:
      if ((event.key.mod & SDL_KMOD_ALT) != 0 &&
          (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) == 0 &&
          !event.key.repeat) {
        // Alt+mnemonic opens that title's drop-down.
        const int title = menu_title_for_mnemonic(session.menu(),
                                                  event.key.key);
        if (title >= 0) {
          expose = true;
          return run_menu_loop(window, session, title, true, true);
        }
      }
      if (accelerator_key_down(event)) {
        // TranslateAccelerator: TOWER_MENU's one entry, VK_F1 -> 40021.
        (void)session.accelerator(simtower::kOriginalVirtualKeyF1);
        expose = true;
        break;
      }
      if (event.key.key == SDLK_F10 && !event.key.repeat) {
        expose = true;
        return run_menu_loop(window, session, 0, true);
      }
      // SDL-only stand-ins for the Main scrollbars, which this frontend
      // does not draw yet. MAINWNDPROC has no WM_KEYDOWN or WM_MOUSEWHEEL
      // scrolling: only WM_HSCROLL/WM_VSCROLL reach scroll_original
      // (main_window_proc). The bindings are new input, but they send the Win16
      // scrollbar codes (0/1 line up/down, 2/3 page up/down) through the same
      // original_main_scroll_request_position path.
      switch (event.key.key) {
        case SDLK_LEFT: session.scroll_by_code(true, 0U); break;
        case SDLK_RIGHT: session.scroll_by_code(true, 1U); break;
        case SDLK_UP: session.scroll_by_code(false, 0U); break;
        case SDLK_DOWN: session.scroll_by_code(false, 1U); break;
        case SDLK_PAGEUP: session.scroll_by_code(false, 2U); break;
        case SDLK_PAGEDOWN: session.scroll_by_code(false, 3U); break;
        default: break;
      }
      expose = true;
      break;
    case SDL_EVENT_MOUSE_WHEEL: {
      // SDL-only, as above: one line-scroll request per wheel notch.
      const int flip =
          event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
      const int vertical = event.wheel.integer_y * flip;
      const int horizontal = event.wheel.integer_x * flip;
      for (int step = 0; step < (vertical < 0 ? -vertical : vertical); ++step) {
        session.scroll_by_code(false, vertical > 0 ? 0U : 1U);
      }
      for (int step = 0; step < (horizontal < 0 ? -horizontal : horizontal);
           ++step) {
        session.scroll_by_code(true, horizontal > 0 ? 1U : 0U);
      }
      expose = true;
      break;
    }
    default:
      break;
  }
  return true;
}

// Help pages in the user's browser. Not installed for the dummy or
// offscreen video driver (a test run), so no browser opens there.
class SdlHelpViewer final : public simtower::sdl::HelpViewer {
 public:
  bool open_url(const std::string& url) override {
    if (SDL_OpenURL(url.c_str())) return true;
    std::fprintf(stderr, "simtower_sdl: help: %s\n", SDL_GetError());
    return false;
  }
};

[[nodiscard]] bool test_video_driver() {
  const char* driver = SDL_GetCurrentVideoDriver();
  const std::string_view name = driver ? driver : "";
  return name == "dummy" || name == "offscreen";
}

int run_window(const Options& options) {
  Window window(options);
  // Declared after the window (SDL_Quit runs last) and before the session,
  // whose audio runtime stops it on destruction.
  std::optional<simtower::sdl::SdlAudioDevice> audio;
  if (options.sound_enabled()) audio.emplace();
  simtower::sdl::Session session(
      read_file(options.pack),
      [] { return static_cast<std::uint32_t>(SDL_GetTicks()); },
      {.fast_mode_enabled = options.fast_mode,
       .sound_enabled = options.sound_enabled(),
       .log_sounds = options.log_sounds},
      audio ? &*audio : nullptr);
  const auto [width, height] = window.client_size();
  session.resize(width, height);
  WindowModalSource modal(window, session);
  session.set_input_source(&modal);
  SdlFilePicker picker(window, session);
  session.set_file_picker(&picker);
  SdlHelpViewer help_viewer;
  session.set_help_directory(options.help_dir);
  if (!test_video_driver()) session.set_help_viewer(&help_viewer);
  // WinMain's startup: the splash, WAVE/20000, then New / Load (or the
  // --open tower) through the window's dialog runtime and picker.
  if (options.view) session.restore_view(options.view->x, options.view->y);
  const auto present = [&] {
    window.present(simtower::sdl::compose_window(session));
    window.set_cursor(session.cursor_id());
  };
  const bool started = session.run_startup(
      {.present = present,
       .wait =
           [&] {
             // 1128:007f-00a0 is a busy loop; only a quit, a resize and
             // an expose are taken here so the window stays responsive.
             SDL_Event event{};
             while (SDL_PollEvent(&event)) {
               if (event.type == SDL_EVENT_QUIT) return false;
               if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                 resize_session(event, session);
                 present();
               } else if (event.type == SDL_EVENT_WINDOW_EXPOSED) {
                 present();
               }
             }
             SDL_Delay(10);
             return true;
           }},
      options.open);
  if (!started || modal.quit_requested() || picker.quit_requested()) {
    session.set_file_picker(nullptr);
    session.set_input_source(nullptr);
    return 0;
  }
  WindowActivation activation = initial_activation(window.flags());
  LoneAlt lone_alt;
  window.set_title(session.window_title());

  // Replaces the PeekMessage busy loop (WinMain): wait for an event
  // or until the next pass has work, drain the queue, then run one idle pass.
  bool running = true;
  while (running) {
    bool expose = false;
    SDL_Event event{};
    if (SDL_WaitEventTimeout(
            &event, static_cast<Sint32>(session.milliseconds_until_due()))) {
      running = handle_event(event, session, window, expose, activation,
                             lone_alt);
      while (running && !modal.quit_requested() &&
             !picker.quit_requested() && !session.quit_requested() &&
             SDL_PollEvent(&event)) {
        running = handle_event(event, session, window, expose, activation,
                               lone_alt);
      }
    }
    // File > Exit destroys Main (WM_DESTROY posts WM_QUIT).
    if (!running || modal.quit_requested() || picker.quit_requested() ||
        session.quit_requested()) {
      break;
    }
    window.set_title(session.window_title());
    session.set_window_activation(activation.active, activation.iconic);
    const bool presented = session.run_idle_pass();
    if (session.take_window_repaint() || presented || expose) {
      window.present(simtower::sdl::compose_window(session));
    }
    window.set_cursor(session.cursor_id());
  }
  session.set_help_viewer(nullptr);
  session.set_file_picker(nullptr);
  session.set_input_source(nullptr);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = parse_options(argc, argv);
    return options.headless() ? run_headless(options) : run_window(options);
  } catch (const simtower::sdl::ScriptFailure& failure) {
    std::fflush(stdout);
    std::fprintf(stderr, "simtower_sdl: input script: %s\n", failure.what());
    return failure.exit_code();
  } catch (const std::exception& error) {
    std::fprintf(stderr, "simtower_sdl: %s\n", error.what());
    return 1;
  }
}
