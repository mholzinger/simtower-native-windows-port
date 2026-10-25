// SDL3 frontend, first milestone: one window showing the main tower view,
// driven by the same portable simulation and renderer as the Win32 host.
// Menus, panels, dialogs, help, fonts and file pickers come later.
//
// Usage:
//   simtower_sdl [--pack FILE] [--open FILE.TDT] [--size WxH] [--slow]
//                [--log-sounds] [--sound | --no-sound]
//                [--audio-capture FILE.wav]
//                [--headless-frame FILE.bmp [--passes N] [--view X,Y]]
//                [--scenario none|busy|full] [--state-digest]
//                [--digest-every K] [--trace-requests]
//                [--event bomb|fire@PASS ...]
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
// Headless mode (no window) is selected by any of --headless-frame,
// --state-digest, --digest-every, --event, --audio-capture, or --scenario
// other than none; --headless-frame is then optional. No other flag selects
// it: without those, --trace-requests, --log-sounds or --sound alone still
// open a window.

#include "sdl_audio.hpp"
#include "sdl_session.hpp"

#include "original_audio_capture.hpp"
#include "original_resources.generated.hpp"
#include "original_simulation.hpp"
#include "original_tdt.hpp"
#include "original_tdt_file.hpp"
#include "original_time.hpp"
#include "original_ui.hpp"
#include "original_wave.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
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
  constexpr simtower::OriginalMainNonclientMetrics metrics{
      .frame_width = 4,
      .frame_height = 4,
      .vertical_scroll_width = 16,
      .horizontal_scroll_height = 16,
      .menu_height = 19,
      .caption_height = 19,
      .border_width = 1,
      .border_height = 1,
  };
  const auto geometry =
      simtower::original_main_window_geometry(1024, 768, metrics);
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

  [[nodiscard]] bool headless() const {
    return headless_frame || state_digest || digest_every > 0 ||
           scenario != "none" || !events.empty() || audio_capture;
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
      " [--trace-requests] [--event bomb|fire@PASS ...]");
}

// Parses "AxB" or "A,B" into two integers.
std::pair<int, int> parse_pair(std::string_view text, char separator) {
  const auto split = text.find(separator);
  if (split == std::string_view::npos) usage_error("bad pair: " +
                                                   std::string(text));
  try {
    return {std::stoi(std::string(text.substr(0, split))),
            std::stoi(std::string(text.substr(split + 1)))};
  } catch (const std::exception&) {
    usage_error("bad pair: " + std::string(text));
  }
}

int parse_count(std::string_view text) {
  try {
    std::size_t used = 0U;
    const int count = std::stoi(std::string(text), &used);
    if (used != text.size()) usage_error("bad passes: " + std::string(text));
    return count;
  } catch (const std::exception&) {
    usage_error("bad passes: " + std::string(text));
  }
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
    } else if (argument == "--view") {
      const auto [x, y] = parse_pair(value(index), ',');
      options.view = simtower::OriginalWorldPoint{x, y};
    } else {
      usage_error("unknown option: " + std::string(argument));
    }
  }
  if (options.audio_capture && options.sound == false) {
    usage_error("--audio-capture needs sound; drop --no-sound");
  }
  if (options.width <= 0 || options.height <= 0 || options.passes < 0) {
    usage_error("size and passes must be positive");
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

// New, or Open with the Win32 host's fall-back to New on a load failure
// (load_original_tower_from_path).
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
  start_tower(session, options);
  if (options.scenario != "none") {
    if (options.open) usage_error("--scenario builds on New, not --open");
    const auto summary = session.apply_scenario(options.scenario);
    std::string built;
    for (const auto& [type, count] : summary.built_by_type) {
      built += " " + simtower::sdl::scenario_type_name(type) + "=" +
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
  const auto print_digest = [&session](int pass) {
    const auto digest = session.state_digest();
    std::printf("digest %016llx pass %d tower %016llx process %016llx"
                " host %016llx requests %016llx\n",
                static_cast<unsigned long long>(digest.combined), pass,
                static_cast<unsigned long long>(digest.tower),
                static_cast<unsigned long long>(digest.process),
                static_cast<unsigned long long>(digest.host),
                static_cast<unsigned long long>(digest.requests));
  };
  for (int pass = 0; pass < options.passes; ++pass) {
    for (const auto& [name, at_pass] : options.events) {
      if (at_pass != pass) continue;
      const bool started = session.trigger_event(name);
      std::fprintf(stderr, "simtower_sdl: event %s before pass %d: %s\n",
                   name.c_str(), pass, started ? "started" : "not offered");
    }
    clock_ms += pass_ms;
    (void)session.run_idle_pass();
    if (options.digest_every > 0 && (pass + 1) % options.digest_every == 0) {
      print_digest(pass + 1);
    }
  }
  // Taken before the presentation step below, which may rebuild the backing.
  if (options.state_digest) print_digest(options.passes);
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
               options.passes, static_cast<unsigned>(header.frame_time),
               static_cast<int>(header.current_day),
               static_cast<unsigned>(header.rating),
               static_cast<int>(header.balance),
               static_cast<unsigned>(session.document()->people_count),
               static_cast<int>(
                   session.document()->post_elevator.finance.total_population),
               requests.dialogs, requests.sounds, requests.focus,
               requests.status, tally.c_str());
  if (capture && !write_audio_capture(options, session, *capture)) return 1;
  if (!options.headless_frame) return 0;

  // Presentation only: no scheduler, palette or clock step.
  session.present();
  const auto& raster = session.main_raster();
  if (!write_bmp(raster, *options.headless_frame)) {
    std::fprintf(stderr, "simtower_sdl: could not write %s: %s\n",
                 options.headless_frame->string().c_str(), SDL_GetError());
    return 1;
  }
  const auto& view = session.viewport();
  std::fprintf(stderr, "simtower_sdl: wrote %s (%dx%d) view %d,%d\n",
               options.headless_frame->string().c_str(), raster.width,
               raster.height, view.x, view.y);
  return 0;
}

class Window {
 public:
  explicit Window(const Options& options) {
    if (!SDL_Init(SDL_INIT_VIDEO)) fail("SDL_Init");
    // One window; panels will live inside it. The client size is in window
    // coordinates and the texture is scaled to the drawable on HiDPI.
    if (!SDL_CreateWindowAndRenderer("SimTower", options.width,
                                     options.height, SDL_WINDOW_RESIZABLE,
                                     &window_, &renderer_)) {
      const std::string error = SDL_GetError();
      SDL_Quit();
      throw std::runtime_error("SDL_CreateWindowAndRenderer: " + error);
    }
    // MINMAXINFO tracking limits (1158:0334), as client sizes.
    const auto limits = main_client_limits();
    SDL_SetWindowMinimumSize(window_, limits.minimum_width,
                             limits.minimum_height);
    SDL_SetWindowMaximumSize(window_, limits.maximum_width,
                             limits.maximum_height);
  }
  ~Window() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
  }
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  // Clamped as well, in case a window manager ignores the size limits.
  [[nodiscard]] std::pair<int, int> client_size() const {
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window_, &width, &height);
    return clamp_client(width, height);
  }

  [[nodiscard]] SDL_WindowFlags flags() const {
    return SDL_GetWindowFlags(window_);
  }

  // Replaces draw_original_world_raster's SetDIBitsToDevice.
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

 private:
  [[noreturn]] static void fail(const char* what) {
    throw std::runtime_error(std::string(what) + ": " + SDL_GetError());
  }

  SDL_Window* window_{};
  SDL_Renderer* renderer_{};
  SDL_Texture* texture_{};
  int texture_width_{};
  int texture_height_{};
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

// Returns false on quit.
bool handle_event(const SDL_Event& event, simtower::sdl::Session& session,
                  bool& expose, WindowActivation& activation) {
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
    case SDL_EVENT_WINDOW_RESIZED: {
      const auto [width, height] =
          clamp_client(event.window.data1, event.window.data2);
      session.resize(width, height);
      break;
    }
    case SDL_EVENT_WINDOW_EXPOSED:
      expose = true;
      break;
    case SDL_EVENT_KEY_DOWN:
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
      break;
    }
    default:
      break;
  }
  return true;
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
  // WinMain plays WAVE/20000 right before the new/load startup path.
  session.play_startup_sound();
  start_tower(session, options);
  WindowActivation activation = initial_activation(window.flags());

  // Replaces the PeekMessage busy loop (WinMain): wait for an event
  // or until the next pass has work, drain the queue, then run one idle pass.
  bool running = true;
  while (running) {
    bool expose = false;
    SDL_Event event{};
    if (SDL_WaitEventTimeout(
            &event, static_cast<Sint32>(session.milliseconds_until_due()))) {
      running = handle_event(event, session, expose, activation);
      while (running && SDL_PollEvent(&event)) {
        running = handle_event(event, session, expose, activation);
      }
    }
    if (!running) break;
    session.set_window_activation(activation.active, activation.iconic);
    if (session.run_idle_pass() || expose) window.present(session.main_raster());
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = parse_options(argc, argv);
    return options.headless() ? run_headless(options) : run_window(options);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "simtower_sdl: %s\n", error.what());
    return 1;
  }
}
