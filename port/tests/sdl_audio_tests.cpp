// SdlAudioDevice on SDL's dummy audio driver, with synthetic (non-asset)
// buffers: format checks, U8 and S16 playback until done(), loop passes,
// partial final frames, stop idempotence and destruction while playing.
// The dummy driver is forced with an override hint, so the test never makes
// sound even when run outside CTest. It consumes audio in real time, which
// is what the timing checks rely on.
//
// Usage: simtower_sdl_audio_tests

#include "sdl_audio.hpp"

#include "original_audio_device.hpp"
#include "original_audio_rules.hpp"

#include <SDL3/SDL.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace {

using simtower::OriginalAudioLoopPlan;
using simtower::OriginalPcmFormat;
using simtower::sdl::SdlAudioDevice;

constexpr OriginalPcmFormat kU8Mono{1U, 1U, 11025U, 11025U, 1U, 8U};
constexpr OriginalPcmFormat kS16Mono{1U, 1U, 11025U, 22050U, 2U, 16U};
constexpr OriginalPcmFormat kS16Stereo{1U, 2U, 11025U, 44100U, 4U, 16U};
constexpr OriginalAudioLoopPlan kOnePass{};

// A quiet square wave; content does not matter, only its length.
std::vector<std::byte> u8_buffer(std::size_t bytes) {
  std::vector<std::byte> out(bytes);
  for (std::size_t at = 0; at < bytes; ++at) {
    out[at] = static_cast<std::byte>((at / 32U) % 2U == 0U ? 0x84 : 0x7c);
  }
  return out;
}

std::vector<std::byte> s16_buffer(std::size_t bytes) {
  std::vector<std::byte> out(bytes);
  for (std::size_t at = 0; at < bytes; ++at) {
    out[at] = static_cast<std::byte>(at % 2U == 0U ? 0x00 : 0x01);
  }
  return out;
}

// Milliseconds from now until done(voice), or -1 after `limit_ms`.
std::int64_t wait_done(SdlAudioDevice& device, std::size_t voice,
                       std::uint64_t limit_ms) {
  const std::uint64_t start = SDL_GetTicks();
  while (!device.done(voice)) {
    if (SDL_GetTicks() - start > limit_ms) return -1;
    SDL_Delay(2);
  }
  return static_cast<std::int64_t>(SDL_GetTicks() - start);
}

void check_formats(SdlAudioDevice& device) {
  assert(!device.open(2U, kU8Mono));  // two voices only
  OriginalPcmFormat format = kU8Mono;
  format.format_tag = 2U;  // ADPCM
  assert(!device.open(0U, format));
  format = kU8Mono;
  format.bits_per_sample = 12U;
  assert(!device.open(0U, format));
  format = kU8Mono;
  format.channels = 0U;
  format.block_align = 0U;
  assert(!device.open(0U, format));
  format = kS16Mono;
  format.block_align = 1U;  // not channels * bits / 8
  assert(!device.open(0U, format));
  format = kU8Mono;
  format.samples_per_second = 0U;
  assert(!device.open(0U, format));
  // A failed open leaves the voice closed: submit fails, done() is false.
  const auto samples = u8_buffer(64U);
  assert(!device.submit(0U, samples, kOnePass));
  assert(!device.done(0U));
}

void check_stop(SdlAudioDevice& device) {
  // Stop is idempotent on a closed voice, an opened voice and twice in a
  // row, and harmless out of range.
  device.stop(0U);
  device.stop(5U);
  assert(device.open(0U, kU8Mono));
  assert(!device.done(0U));  // opened, nothing submitted
  device.stop(0U);
  device.stop(0U);
  assert(!device.done(0U));
  // Stopping a playing voice: done() is false afterwards, and a new
  // submit needs a new open.
  const auto samples = u8_buffer(11025U);  // 1 s
  assert(device.open(0U, kU8Mono));
  assert(device.submit(0U, samples, kOnePass));
  assert(!device.done(0U));
  device.stop(0U);
  assert(!device.done(0U));
  device.stop(0U);
  assert(!device.submit(0U, samples, kOnePass));
}

// One pass of 200 ms, then the same buffer with three loop passes, on each
// voice in turn.
void check_u8_loop(SdlAudioDevice& device, std::size_t voice) {
  const auto samples = u8_buffer(2205U);  // 200 ms at 11025 Hz
  assert(device.open(voice, kU8Mono));
  assert(device.submit(voice, samples, kOnePass));
  assert(!device.done(voice));
  const auto one = wait_done(device, voice, 3000U);
  assert(one >= 0);
  assert(device.done(voice));  // stays done until stopped
  device.stop(voice);

  const auto plan = simtower::original_audio_loop_plan(3U);
  assert(plan.total_passes == 3U);
  assert(device.open(voice, kU8Mono));
  assert(device.submit(voice, samples, plan));
  const auto three = wait_done(device, voice, 5000U);
  assert(three >= 0);
  device.stop(voice);
  std::printf("simtower_sdl_audio_tests: voice %zu U8 200 ms: one pass done"
              " after %lld ms, three passes after %lld ms\n",
              voice, static_cast<long long>(one),
              static_cast<long long>(three));
  // Real-time consumption: each pass takes at least most of its nominal
  // time, and three passes take clearly longer than one.
  assert(one >= 150);
  assert(three >= 3 * 150);
  assert(three >= one + 300);
  assert(three < 3 * 200 + 2000);
}

void check_s16(SdlAudioDevice& device) {
  // Whole frames.
  auto samples = s16_buffer(4410U);  // 2205 frames, 200 ms mono
  assert(device.open(0U, kS16Mono));
  assert(device.submit(0U, samples, kOnePass));
  assert(wait_done(device, 0U, 3000U) >= 150);
  device.stop(0U);

  // Odd byte count: the partial final frame is dropped, the rest plays.
  samples = s16_buffer(2205U);  // 1102 frames + 1 byte, 100 ms
  assert(device.open(0U, kS16Mono));
  assert(device.submit(0U, samples, kOnePass));
  assert(wait_done(device, 0U, 3000U) >= 0);
  device.stop(0U);

  // Stereo, length not a multiple of block_align (4410 % 4 == 2).
  samples = s16_buffer(4410U);
  assert(device.open(1U, kS16Stereo));
  assert(device.submit(1U, samples, kOnePass));
  assert(wait_done(device, 1U, 3000U) >= 0);
  device.stop(1U);

  // Less than one frame: the submit fails and closes the voice.
  samples = s16_buffer(3U);
  assert(device.open(1U, kS16Stereo));
  assert(!device.submit(1U, samples, kOnePass));
  assert(!device.done(1U));
  assert(!device.submit(1U, s16_buffer(8U), kOnePass));
}

void check_two_voices(SdlAudioDevice& device) {
  const auto low = u8_buffer(1103U);   // 100 ms
  const auto high = u8_buffer(3308U);  // 300 ms
  assert(device.open(0U, kU8Mono));
  assert(device.open(1U, kU8Mono));
  assert(device.submit(0U, low, kOnePass));
  assert(device.submit(1U, high, kOnePass));
  assert(wait_done(device, 0U, 3000U) >= 0);
  assert(!device.done(1U));  // the longer voice is still playing
  assert(wait_done(device, 1U, 3000U) >= 0);
  device.stop(0U);
  device.stop(1U);
}

void check_destroy_while_playing() {
  const auto samples = u8_buffer(11025U);  // 1 s, looped 10 times
  SdlAudioDevice device;
  assert(device.available());
  assert(device.open(0U, kU8Mono));
  assert(device.open(1U, kS16Mono));
  assert(device.submit(0U, samples, simtower::original_audio_loop_plan(10U)));
  assert(device.submit(1U, s16_buffer(22050U), kOnePass));
  SDL_Delay(50);
  assert(!device.done(0U));
  // The destructor stops both voices and closes the device.
}

}  // namespace

int main() {
  SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
  {
    SdlAudioDevice device;
    if (!device.available()) {
      std::printf("simtower_sdl_audio_tests: dummy audio device unavailable:"
                  " %s\n", SDL_GetError());
      return 1;
    }
    check_formats(device);
    check_stop(device);
    check_u8_loop(device, 0U);
    check_u8_loop(device, 1U);
    check_s16(device);
    check_two_voices(device);
  }
  check_destroy_while_playing();
  // A second device after the first was destroyed still works.
  {
    SdlAudioDevice device;
    assert(device.available());
    assert(device.open(0U, kU8Mono));
    assert(device.submit(0U, u8_buffer(552U), kOnePass));
    assert(wait_done(device, 0U, 3000U) >= 0);
  }
  std::printf("simtower_sdl_audio_tests: passed\n");
  return 0;
}
