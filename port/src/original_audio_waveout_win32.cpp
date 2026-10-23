#include "original_audio_waveout_win32.hpp"

#include <limits>

namespace simtower {

OriginalWaveOutAudioDevice::~OriginalWaveOutAudioDevice() {
  for (std::size_t voice = 0; voice < native_channels_.size(); ++voice) {
    stop(voice);
  }
}

bool OriginalWaveOutAudioDevice::available() noexcept {
  // 11c8:08eb's WAVMIXOPENCHANNEL wrapper is represented by native channel
  // creation on demand; initialization verifies that the PCM backend exists.
  return waveOutGetNumDevs() != 0U;
}

bool OriginalWaveOutAudioDevice::open(std::size_t voice,
                                      const OriginalPcmFormat& pcm) noexcept {
  if (voice >= native_channels_.size()) {
    return false;
  }
  WAVEFORMATEX format{};
  format.wFormatTag = pcm.format_tag;
  format.nChannels = pcm.channels;
  format.nSamplesPerSec = pcm.samples_per_second;
  format.nAvgBytesPerSec = pcm.average_bytes_per_second;
  format.nBlockAlign = pcm.block_align;
  format.wBitsPerSample = pcm.bits_per_sample;
  format.cbSize = 0;

  auto& native = native_channels_[voice];
  if (waveOutOpen(&native.output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
    native = {};
    return false;
  }
  return true;
}

bool OriginalWaveOutAudioDevice::submit(std::size_t voice,
                                        std::span<const std::byte> samples,
                                        OriginalAudioLoopPlan loop) noexcept {
  if (voice >= native_channels_.size() ||
      samples.size() > std::numeric_limits<DWORD>::max()) {
    // The runtime checks the same bounds before open(); closing here keeps
    // the "failed submit closes the voice" contract by construction.
    stop(voice);
    return false;
  }
  auto& native = native_channels_[voice];
  native.header.lpData = reinterpret_cast<LPSTR>(
      const_cast<std::byte*>(samples.data()));
  native.header.dwBufferLength = static_cast<DWORD>(samples.size());
  native.header.dwFlags = loop.flags;
  native.header.dwLoops = loop.total_passes;
  if (waveOutPrepareHeader(native.output, &native.header, sizeof(native.header)) !=
      MMSYSERR_NOERROR) {
    waveOutClose(native.output);
    native = {};
    return false;
  }
  native.prepared = true;
  if (waveOutWrite(native.output, &native.header, sizeof(native.header)) != MMSYSERR_NOERROR) {
    waveOutUnprepareHeader(native.output, &native.header, sizeof(native.header));
    waveOutClose(native.output);
    native = {};
    return false;
  }
  return true;
}

void OriginalWaveOutAudioDevice::stop(std::size_t voice) noexcept {
  if (voice >= native_channels_.size()) {
    return;
  }
  auto& native = native_channels_[voice];
  if (native.output) {
    waveOutReset(native.output);
    if (native.prepared) {
      waveOutUnprepareHeader(native.output, &native.header, sizeof(native.header));
    }
    waveOutClose(native.output);
  }
  native = {};
}

bool OriginalWaveOutAudioDevice::done(std::size_t voice) noexcept {
  if (voice >= native_channels_.size()) {
    return false;
  }
  const auto& native = native_channels_[voice];
  return native.prepared && (native.header.dwFlags & WHDR_DONE) != 0U;
}

}  // namespace simtower
