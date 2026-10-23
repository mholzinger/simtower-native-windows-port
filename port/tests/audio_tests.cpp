// Portable audio coverage: the pure 11c8 rules, then OriginalAudioRuntime
// driving a recording fake device (arbitration, the priority-5 reservation,
// repeats, the stop gates, reaping, saturation and failure cleanup), then the
// capture device's timing and rendering. No audio device is opened.
//
// Every expectation about sample bytes is taken from the pack at run time
// (parse_original_wave); no asset bytes or hashes are stored here. Without
// the pack the rule checks still run and the test exits 77 (CTest skip).
//
// Usage: simtower_audio_tests <original_resources.pack>

#include "original_audio.hpp"
#include "original_audio_capture.hpp"
#include "original_audio_device.hpp"
#include "original_audio_rules.hpp"
#include "original_resources.hpp"
#include "original_resources.generated.hpp"
#include "original_time.hpp"
#include "original_wave.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace simtower;

constexpr int kSkip = 77;

std::vector<std::byte> read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return {};
  const std::vector<char> chars((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(chars.size());
  for (std::size_t index = 0; index < chars.size(); ++index) {
    bytes[index] =
        static_cast<std::byte>(static_cast<unsigned char>(chars[index]));
  }
  return bytes;
}

bool pack_matches_descriptors(std::span<const std::byte> pack) {
  std::size_t required = 0U;
  for (const auto& descriptor : generated::kResources) {
    required = std::max<std::size_t>(
        required, static_cast<std::size_t>(descriptor.offset) +
                      static_cast<std::size_t>(descriptor.size));
  }
  return !pack.empty() && pack.size() == required;
}

// Pure rule checks, formerly in the Win32-only resource_tests.cpp.
void check_rules() {
  // Headless branch coverage for 11c8:0167/0597 arbitration and category
  // gates.
  OriginalAudioArbiter audio;
  assert(audio.select_channel(1) == 0);
  audio.start(0, 1577, 1);
  assert(audio.select_channel(1) == 1);
  audio.start(1, 1384, 1);
  assert(audio.select_channel(1) == -1);
  assert(audio.select_channel(2) == 0);
  assert(audio.select_channel(5) == 1);
  // Direct 11c8:09d2 completion-state tail: after the backend frees the
  // completed wave, both active/priority words for the channel are cleared.
  audio.stop(0);
  assert(audio.select_channel(0) == 0);
  // Direct 11c8:02c0 gate coverage and 11c8:0135 ordering: the latter's fixed
  // loop invokes the gated stop for channel zero and then channel one, never
  // a dynamic count.
  static_assert(!original_audio_stop_channel_allowed(false, true, true, 0U));
  static_assert(!original_audio_stop_channel_allowed(true, false, true, 0U));
  static_assert(!original_audio_stop_channel_allowed(true, true, false, 0U));
  static_assert(original_audio_stop_channel_allowed(true, true, true, 0U));
  static_assert(original_audio_stop_channel_allowed(true, true, true, 1U));
  static_assert(!original_audio_stop_channel_allowed(true, true, true, 2U));
  // Direct 11c8:0100: the reserved priority-five request is submitted
  // exactly when channel one is idle.
  static_assert(original_reserved_audio_should_submit(false));
  static_assert(!original_reserved_audio_should_submit(true));
  // Direct 11c8:0978: zero leaves an ordinary one-shot buffer, while nonzero
  // counts enable a loop for that many total passes.
  constexpr auto kLoop = kOriginalWaveBeginLoop | kOriginalWaveEndLoop;
  static_assert(original_audio_loop_plan(0U) == OriginalAudioLoopPlan{});
  static_assert(original_audio_loop_plan(1U) ==
                (OriginalAudioLoopPlan{kLoop, 1U}));
  static_assert(original_audio_loop_plan(0xffffU) ==
                (OriginalAudioLoopPlan{kLoop, 0xffffU}));
  audio.stop_all();
  assert(!audio.channels()[0].active && !audio.channels()[1].active);
  assert(audio.channels()[0].resource_id == -1);
  assert(original_audio_priority_enabled(0, {true, false, false}));
  assert(!original_audio_priority_enabled(1, {true, false, false}));
  assert(original_audio_priority_enabled(5, {false, false, true}));
  static_assert(kOriginalAudioSaturationTicks == 600U);
  static_assert(kOriginalAudioSaturationNominalMs == 9600U);
  static_assert(!original_audio_saturation_elapsed(599U, 0U));
  static_assert(original_audio_saturation_elapsed(600U, 0U));
  static_assert(original_audio_saturation_elapsed(599U, 0xffffffffU));
  static_assert(!original_audio_saturation_elapsed(0U, 1U));

  // Direct 11e0:0e84: the unsigned deadline comparison is strict, one due
  // call advances by 0x30 rather than snapping to now, callback 03BD is
  // drained even with all categories disabled, and wraparound is literal.
  using PumpPlan = OriginalWaveMixPumpPlan;
  static_assert(original_wavemix_pump_plan(100U, 147U, true, false, false) ==
                PumpPlan{false, false, false, 100U});
  static_assert(original_wavemix_pump_plan(100U, 148U, false, false, false) ==
                PumpPlan{true, true, false, 148U});
  static_assert(original_wavemix_pump_plan(100U, 1'000U, false, true, false) ==
                PumpPlan{true, true, true, 148U});
  static_assert(original_wavemix_pump_plan(0xfffffff0U, 0x10U, true, true,
                                           true) ==
                PumpPlan{false, false, false, 0xfffffff0U});
  static_assert(original_wavemix_pump_plan(0xfffffff0U, 0x20U, false, false,
                                           true) ==
                PumpPlan{true, true, true, 0x20U});

  // Capture timing is pure arithmetic on the format.
  const OriginalPcmFormat format{1U, 1U, 11025U, 11025U, 1U, 8U};
  assert(OriginalAudioCaptureDevice::duration_ms(format, 11025U, 1U) == 1000U);
  assert(OriginalAudioCaptureDevice::duration_ms(format, 11026U, 1U) == 1001U);
  assert(OriginalAudioCaptureDevice::duration_ms(format, 11026U, 3U) == 3003U);
}

// Records every device call; done() and failures are scripted.
class FakeDevice final : public OriginalAudioDevice {
 public:
  struct Call {
    std::string operation;
    std::size_t voice{};
    OriginalPcmFormat format{};
    std::vector<std::byte> samples{};
    OriginalAudioLoopPlan plan{};
  };

  bool available() noexcept override {
    calls.push_back({"available"});
    return has_endpoint;
  }
  bool open(std::size_t voice, const OriginalPcmFormat& format) noexcept override {
    calls.push_back({"open", voice, format});
    if (fail_open) return false;
    opened[voice] = true;
    return true;
  }
  bool submit(std::size_t voice, std::span<const std::byte> samples,
              OriginalAudioLoopPlan plan) noexcept override {
    assert(opened[voice]);
    calls.push_back({"submit", voice, {},
                     std::vector<std::byte>(samples.begin(), samples.end()),
                     plan});
    if (fail_submit) {
      opened[voice] = false;  // the device closes the voice itself
      return false;
    }
    playing[voice] = true;
    return true;
  }
  void stop(std::size_t voice) noexcept override {
    // Only effective stops are recorded, as waveOut makes calls only for an
    // open slot. A repeated stop must be harmless.
    if (opened[voice]) calls.push_back({"stop", voice});
    opened[voice] = false;
    playing[voice] = false;
    finished[voice] = false;
  }
  bool done(std::size_t voice) noexcept override {
    return playing[voice] && finished[voice];
  }

  // "open 0", "submit 0", "stop 1", ... (available() omitted).
  [[nodiscard]] std::vector<std::string> trace() const {
    std::vector<std::string> out;
    for (const auto& call : calls) {
      if (call.operation == "available") continue;
      out.push_back(call.operation + " " + std::to_string(call.voice));
    }
    return out;
  }
  void clear() { calls.clear(); }

  bool has_endpoint{true};
  bool fail_open{};
  bool fail_submit{};
  bool opened[2]{};
  bool playing[2]{};
  bool finished[2]{};
  std::vector<Call> calls{};
};

using Trace = std::vector<std::string>;

void check_runtime(const OriginalResources& resources) {
  std::uint32_t clock = 50'000U;
  FakeDevice device;
  OriginalAudioRuntime audio(resources, device, [&clock] { return clock; });

  const auto wave_7000 = parse_original_wave(resources.find("WAVE", 7000));
  const auto wave_7002 = parse_original_wave(resources.find("WAVE", 7002));
  const auto wave_6002 = parse_original_wave(resources.find("WAVE", 6002));
  assert(wave_7000.logical_size != 0U && wave_7002.logical_size != 0U &&
         wave_6002.logical_size != 0U);
  constexpr auto kLoop = kOriginalWaveBeginLoop | kOriginalWaveEndLoop;

  // 11c8:006b: no endpoint fails initialization; a disabled master switch
  // succeeds without asking the device.
  device.has_endpoint = false;
  assert(!audio.initialize());
  assert(!audio.active());
  assert(!audio.play_resource(7000, 0U, 4U, clock));
  assert(device.trace().empty());
  audio.set_sound_enabled(false);
  device.clear();
  assert(audio.initialize());
  assert(device.calls.empty());
  assert(!audio.active() && !audio.sound_enabled());
  assert(!audio.channel_active(0U) && !audio.channel_active(2U));
  audio.set_sound_enabled(true);
  device.has_endpoint = true;
  assert(audio.initialize());
  assert(audio.active() && audio.sound_enabled());
  device.clear();

  // 11c8:0167 first request: channel 0, the WAVE fmt chunk as the format,
  // the resource's PCM bytes, and a plain one-shot buffer for repeat 0.
  assert(audio.play_resource(7000, 0U, 4U, clock));
  assert(device.trace() == (Trace{"open 0", "submit 0"}));
  assert(device.calls[0].format ==
         (OriginalPcmFormat{wave_7000.format_tag, wave_7000.channels,
                            wave_7000.samples_per_second,
                            wave_7000.average_bytes_per_second,
                            wave_7000.block_align, wave_7000.bits_per_sample}));
  assert(std::ranges::equal(device.calls[1].samples, wave_7000.samples));
  assert(device.calls[1].plan == OriginalAudioLoopPlan{});
  assert(audio.arbiter().channels()[0] ==
         (OriginalAudioChannelState{true, 4U, 7000}));

  // Second request takes the free channel 1; repeat 2 loops two passes.
  device.clear();
  assert(audio.play_resource(6002, 2U, 1U, clock));
  assert(device.trace() == (Trace{"open 1", "submit 1"}));
  assert(std::ranges::equal(device.calls[1].samples, wave_6002.samples));
  assert(device.calls[1].plan == (OriginalAudioLoopPlan{kLoop, 2U}));
  assert(audio.channel_active(1U));

  // Both busy: an equal priority is refused inside the 600-tick window
  // without touching the device.
  device.clear();
  assert(!audio.play_resource(7002, 0U, 1U, clock));
  assert(device.calls.empty());
  // A higher priority replaces the lowest-indexed lower-priority channel
  // (channel 1, priority 1): stop it, then open and submit it again.
  assert(audio.play_resource(7002, 10U, 4U, clock));
  assert(device.trace() == (Trace{"stop 1", "open 1", "submit 1"}));
  assert(std::ranges::equal(device.calls[2].samples, wave_7002.samples));
  assert(device.calls[2].plan == (OriginalAudioLoopPlan{kLoop, 10U}));
  assert(audio.arbiter().channels()[1] ==
         (OriginalAudioChannelState{true, 4U, 7002}));

  // 11c8:0100: the priority-5 path is skipped while channel 1 is active,
  // and the skip makes no device call.
  device.clear();
  assert(!audio.play_reserved_if_idle(7002, 0U, clock));
  assert(device.calls.empty());

  // 11c8:02c0/0135: force zero is always a no-op; channel 2 is out of range.
  audio.stop_all(false);
  audio.stop_channel(0U, false);
  audio.stop_channel(2U, true);
  assert(device.calls.empty());
  assert(audio.channel_active(0U) && audio.channel_active(1U));
  audio.stop_channel(1U, true);
  assert(device.trace() == (Trace{"stop 1"}));
  assert(!audio.channel_active(1U));

  // With channel 1 idle, the reserved request plays on channel 1 at
  // priority 5 even though it is the only call.
  device.clear();
  assert(audio.play_reserved_if_idle(7002, 0U, clock));
  assert(device.trace() == (Trace{"open 1", "submit 1"}));
  assert(audio.arbiter().channels()[1] ==
         (OriginalAudioChannelState{true, 5U, 7002}));
  // 11c8:0597 reserves channel 1 for priority 5 without looking at its
  // state: a direct priority-5 request replaces the reserved sound.
  device.clear();
  assert(audio.play_resource(7000, 0U, 5U, clock));
  assert(device.trace() == (Trace{"stop 1", "open 1", "submit 1"}));

  // Reaping: a finished buffer is retired only at a reap point. A due pump
  // with every category off does not reap (11e0:0e84 skips WaveMixPump).
  device.clear();
  device.finished[0] = true;
  audio.set_category_enabled(0U, false);
  audio.set_category_enabled(1U, false);
  audio.set_category_enabled(2U, false);
  clock += 1'000U;
  audio.pump(clock);
  assert(device.calls.empty());
  assert(audio.arbiter().channels()[0].active);
  // channel_active (11c8:0390) is a reap point regardless of categories.
  assert(!audio.channel_active(0U));
  assert(device.trace() == (Trace{"stop 0"}));
  // Category gates refuse before channel selection, without device calls.
  device.clear();
  assert(!audio.play_resource(7000, 0U, 0U, clock));
  assert(!audio.play_resource(7000, 0U, 1U, clock));
  assert(!audio.play_resource(7000, 0U, 3U, clock));
  assert(device.calls.empty());
  audio.set_category_enabled(0U, true);
  audio.set_category_enabled(1U, true);
  audio.set_category_enabled(2U, true);
  // A due pump with a category on reaps.
  device.finished[1] = true;
  clock += 1'000U;
  audio.pump(clock);
  assert(device.trace() == (Trace{"stop 1"}));
  assert(!audio.arbiter().channels()[1].active);

  // Saturation: two priority-4 sounds, then another priority 4 is refused
  // until 600 coarse ticks after the last accepted play; then 11c8:0167
  // stops both channels and takes channel 0.
  device.clear();
  assert(audio.play_resource(7000, 0U, 4U, clock));
  assert(audio.play_resource(7000, 0U, 4U, clock));
  const std::uint32_t last_play = clock;
  device.clear();
  clock = last_play + kOriginalAudioSaturationNominalMs - 16U;
  assert(!audio.play_resource(7002, 0U, 4U, clock));
  assert(device.calls.empty());
  clock = last_play + kOriginalAudioSaturationNominalMs;
  assert(audio.play_resource(7002, 0U, 4U, clock));
  assert(device.trace() ==
         (Trace{"stop 0", "stop 1", "open 0", "submit 0"}));
  assert(!audio.arbiter().channels()[1].active);

  // A resource that does not parse (WAVE/8000) still frees the selected
  // channel first, then fails without opening the device.
  device.clear();
  assert(!audio.play_resource(8000, 0U, 5U, clock));
  assert(device.trace().empty());
  assert(audio.play_resource(7000, 0U, 5U, clock));
  device.clear();
  assert(!audio.play_resource(8000, 0U, 5U, clock));
  assert(device.trace() == (Trace{"stop 1"}));
  assert(!audio.arbiter().channels()[1].active);

  // Device failures leave the channel inactive; the follow-up stop is
  // harmless (stop is idempotent).
  device.clear();
  audio.stop_all(true);
  assert(device.trace() == (Trace{"stop 0"}));
  device.clear();
  device.fail_open = true;
  assert(!audio.play_resource(7000, 0U, 4U, clock));
  assert(device.trace() == (Trace{"open 0"}));
  assert(!audio.channel_active(0U));
  device.fail_open = false;
  device.fail_submit = true;
  device.clear();
  assert(!audio.play_resource(7000, 0U, 4U, clock));
  assert(device.trace() == (Trace{"open 0", "submit 0"}));
  assert(!audio.channel_active(0U));
  device.fail_submit = false;

  // Muted validation: same length and format, digital silence (PCM/8 rests
  // at 0x80).
  audio.set_host_output_muted(true);
  device.clear();
  assert(audio.play_resource(7000, 0U, 4U, clock));
  assert(device.calls[1].samples.size() == wave_7000.samples.size());
  assert(std::ranges::all_of(device.calls[1].samples, [](std::byte value) {
    return value == std::byte{0x80};
  }));
  audio.set_host_output_muted(false);

  // 11c8:0add deactivate stops both channels; while inactive nothing plays
  // and forced stops are gated. 11c8:0aab reactivates.
  device.clear();
  audio.deactivate();
  assert(device.trace() == (Trace{"stop 0"}));
  assert(!audio.active());
  device.clear();
  assert(!audio.play_resource(7000, 0U, 4U, clock));
  audio.stop_all(true);
  assert(device.calls.empty());
  audio.activate();
  assert(audio.active());
  // About's direct WAVEMIXACTIVATE calls keep the latch.
  audio.deactivate_mixer_backend();
  assert(audio.active());
  audio.activate_mixer_backend();
  assert(audio.play_resource(7000, 0U, 4U, clock));

  // Turning the master switch off runs 11c8:0a31 shutdown; turning it on
  // again does not re-initialize.
  device.clear();
  audio.set_sound_enabled(false);
  assert(device.trace() == (Trace{"stop 0"}));
  audio.set_sound_enabled(true);
  assert(!audio.active());
  assert(!audio.play_resource(7000, 0U, 4U, clock));
}

void check_capture(const OriginalResources& resources) {
  std::uint32_t clock = 0xffffff00U;  // crosses the 32-bit wrap
  OriginalAudioCaptureDevice device([&clock] { return clock; });
  OriginalAudioRuntime audio(resources, device, [&clock] { return clock; });
  assert(audio.initialize());

  const auto wave = parse_original_wave(resources.find("WAVE", 7002));
  const OriginalPcmFormat format{wave.format_tag, wave.channels,
                                 wave.samples_per_second,
                                 wave.average_bytes_per_second,
                                 wave.block_align, wave.bits_per_sample};
  const auto one_pass = OriginalAudioCaptureDevice::duration_ms(
      format, wave.samples.size(), 1U);
  assert(one_pass > 0U);

  // A voice is done exactly when its nominal duration has elapsed.
  const std::uint32_t start = clock;
  assert(audio.play_resource(7002, 2U, 4U, clock));
  clock = start + static_cast<std::uint32_t>(2U * one_pass) - 1U;
  assert(audio.channel_active(0U));
  clock = start + static_cast<std::uint32_t>(2U * one_pass);
  assert(!audio.channel_active(0U));  // reaped: stop recorded at natural end

  // A second voice stopped early is truncated at its stop time.
  const std::uint32_t second_start = clock + 100U;
  clock = second_start;
  assert(audio.play_resource(7002, 0U, 4U, clock));
  clock = second_start + 50U;
  audio.stop_all(true);

  assert(device.submissions().size() == 2U);
  const auto& first = device.submissions()[0];
  assert(first.voice == 0U && first.passes == 2U && first.start_ms == start);
  assert(std::ranges::equal(first.samples, wave.samples));

  const auto render = device.render(44100U);
  assert(render.regions.size() == 2U);
  const std::uint64_t rate = wave.samples_per_second;
  const std::uint64_t frames = wave.samples.size();
  const auto natural = static_cast<std::size_t>(
      (2U * frames * 44100U + rate - 1U) / rate);
  assert(render.regions[0] == (std::pair<std::size_t, std::size_t>{0U, natural}));
  // Every output sample of the first voice is its source frame as signed
  // 16-bit (zero-order hold), across both passes.
  for (std::size_t at = 0; at < natural; ++at) {
    const std::size_t source =
        static_cast<std::size_t>(at * rate / 44100U) % frames;
    const auto expected = static_cast<std::int16_t>(
        (std::to_integer<int>(wave.samples[source]) - 128) * 256);
    assert(render.samples[at] == expected);
  }
  // The second voice starts at its clock offset and ends at the stop.
  const auto offset_ms = static_cast<std::uint64_t>(second_start - start);
  assert(render.regions[1].first == offset_ms * 44100U / 1000U);
  assert(render.regions[1].second ==
         (offset_ms + 50U) * 44100U / 1000U);
  assert(render.samples.size() == render.regions[1].second);
  // Between the voices the timeline is silent.
  for (std::size_t at = natural; at < render.regions[1].first; ++at) {
    assert(render.samples[at] == 0);
  }

  // The WAV file wraps exactly those samples.
  const auto file = OriginalAudioCaptureDevice::wav_file(render);
  assert(file.size() == 44U + render.samples.size() * 2U);
  const auto riff = parse_original_wave(file);
  assert(riff.format_tag == 1U && riff.channels == 1U &&
         riff.bits_per_sample == 16U && riff.samples_per_second == 44100U);
  assert(riff.samples.size() == render.samples.size() * 2U);

  // Mixing two overlapping voices sums them.
  std::uint32_t mix_clock = 1'000U;
  OriginalAudioCaptureDevice mixer([&mix_clock] { return mix_clock; });
  OriginalAudioRuntime mixed(resources, mixer, [&mix_clock] { return mix_clock; });
  assert(mixed.initialize());
  assert(mixed.play_resource(7002, 0U, 4U, mix_clock));
  assert(mixed.play_resource(7002, 0U, 4U, mix_clock));
  const auto both = mixer.render(44100U);
  for (std::size_t at = 0; at < both.regions[0].second; ++at) {
    const std::size_t source = static_cast<std::size_t>(at * rate / 44100U);
    const int single = (std::to_integer<int>(wave.samples[source]) - 128) * 256;
    assert(both.samples[at] ==
           static_cast<std::int16_t>(std::clamp(2 * single, -32768, 32767)));
  }

  // Unsupported formats are refused at open.
  OriginalAudioCaptureDevice strict([&clock] { return clock; });
  assert(!strict.open(0U, OriginalPcmFormat{2U, 1U, 11025U, 11025U, 1U, 8U}));
  assert(!strict.open(0U, OriginalPcmFormat{1U, 1U, 11025U, 11025U, 1U, 4U}));
  strict.stop(0U);
  assert(strict.events().size() == 2U);
}

// Every parsable WAVE resource reaches the device byte for byte.
void check_every_wave(const OriginalResources& resources) {
  std::uint32_t clock = 0U;
  FakeDevice device;
  OriginalAudioRuntime audio(resources, device, [&clock] { return clock; });
  assert(audio.initialize());
  std::size_t submitted = 0U;
  for (const auto& descriptor : generated::kResources) {
    if (descriptor.type != "WAVE") continue;
    const auto wave =
        parse_original_wave(resources.find("WAVE", descriptor.numeric_id));
    device.clear();
    const bool played =
        audio.play_resource(descriptor.numeric_id, 0U, 5U, clock);
    assert(played == (wave.logical_size != 0U));
    if (!played) continue;
    const auto& submit = device.calls.back();
    assert(submit.operation == "submit" && submit.voice == 1U);
    assert(std::ranges::equal(submit.samples, wave.samples));
    ++submitted;
  }
  assert(submitted == 55U);
}

}  // namespace

int main(int argc, char** argv) {
  check_rules();
  const auto pack = argc > 1 ? read_file(argv[1]) : std::vector<std::byte>{};
  if (!pack_matches_descriptors(pack)) {
    std::printf("simtower_audio_tests: rules passed; resource pack missing,"
                " runtime checks skipped\n");
    return kSkip;
  }
  const OriginalResources resources{std::span<const std::byte>(pack)};
  check_runtime(resources);
  check_capture(resources);
  check_every_wave(resources);
  std::printf("simtower_audio_tests: passed\n");
  return 0;
}
