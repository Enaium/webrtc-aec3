#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>
#include <memory>
#include <iostream>
#include <iomanip>
#include <string>

#include "api/echo_canceller3_config.h"
#include "api/echo_canceller3_factory.h"
#include "api/echo_control.h"
#include "api/environment.h"
#include "audio_processing/audio_buffer.h"
#include "audio_processing/logging/wav_file.h"
// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
constexpr int kSampleRateHz = 16000;       // WAV file and AEC3 sample rate
constexpr int kChannels = 1;
constexpr int kFrameSamples = 160;         // 10ms @ 16kHz

// ---------------------------------------------------------------------------
// Compute RMS of a signal (dB)
// ---------------------------------------------------------------------------
double ComputeRmsDb(const int16_t* data, size_t n) {
  double sum = 0.0;
  for (size_t i = 0; i < n; ++i) {
    double s = static_cast<double>(data[i]);
    sum += s * s;
  }
  double rms = std::sqrt(sum / n);
  if (rms < 1e-10) rms = 1e-10;
  return 20.0 * std::log10(rms / 32768.0);
}

// ---------------------------------------------------------------------------
// Compute ERLE (Echo Return Loss Enhancement) in dB
// ---------------------------------------------------------------------------
double ComputeErle(const int16_t* mix, const int16_t* output, size_t n) {
  double mix_power = 0.0, out_power = 0.0;
  for (size_t i = 0; i < n; ++i) {
    double m = static_cast<double>(mix[i]);
    double o = static_cast<double>(output[i]);
    mix_power += m * m;
    out_power += o * o;
  }
  if (out_power < 1e-10) out_power = 1e-10;
  if (mix_power < 1e-10) mix_power = 1e-10;
  return 10.0 * std::log10(mix_power / out_power);
}

// ---------------------------------------------------------------------------
// Compute SNR improvement between output and near-end reference
// ---------------------------------------------------------------------------
double ComputeSnrImprovement(const int16_t* output, const int16_t* near_ref,
                             const int16_t* mix, size_t n) {
  double out_error = 0.0, mix_error = 0.0;
  for (size_t i = 0; i < n; ++i) {
    double oe = static_cast<double>(output[i]) - static_cast<double>(near_ref[i]);
    double me = static_cast<double>(mix[i]) - static_cast<double>(near_ref[i]);
    out_error += oe * oe;
    mix_error += me * me;
  }
  if (out_error < 1e-10) out_error = 1e-10;
  if (mix_error < 1e-10) mix_error = 1e-10;
  return 10.0 * std::log10(mix_error / out_error);
}

// ---------------------------------------------------------------------------
// Fill AudioBuffer from int16 data (S16-encoded float, no scaling)
// ---------------------------------------------------------------------------
void FillAudioBufferFromInt16(webrtc::AudioBuffer* audio_buffer,
                              const int16_t* data,
                              size_t num_samples) {
  float* const* channels = audio_buffer->channels();
  for (size_t i = 0; i < num_samples; ++i) {
    channels[0][i] = static_cast<float>(data[i]);
  }
}

// ---------------------------------------------------------------------------
// Extract int16 data from AudioBuffer (S16-encoded float, no scaling)
// ---------------------------------------------------------------------------
void ExtractInt16FromAudioBuffer(webrtc::AudioBuffer* audio_buffer,
                                 int16_t* out,
                                 size_t num_samples) {
  const float* const* channels = audio_buffer->channels_const();
  for (size_t i = 0; i < num_samples; ++i) {
    float s = channels[0][i];
    if (s > 32767.0f) s = 32767.0f;
    if (s < -32768.0f) s = -32768.0f;
    out[i] = static_cast<int16_t>(s);
  }
}

int main(int argc, char* argv[]) {
  // -----------------------------------------------------------------------
  // Parse arguments
  // -----------------------------------------------------------------------
  std::string far_path = "tests/far.wav";
  std::string near_path = "tests/near.wav";
  std::string mix_path = "tests/mix.wav";
  std::string output_path = "tests/cancelled.wav";

  if (argc >= 4) {
    far_path = argv[1];
    near_path = argv[2];
    mix_path = argv[3];
  }
  if (argc >= 5) {
    output_path = argv[4];
  }

  std::cout << "=== AEC3 Echo Cancellation Test ===" << std::endl;
  std::cout << "  Far  : " << far_path << std::endl;
  std::cout << "  Near : " << near_path << std::endl;
  std::cout << "  Mix  : " << mix_path << std::endl;
  std::cout << "  Out  : " << output_path << std::endl;
  std::cout << "  Sample rate : " << kSampleRateHz << " Hz" << std::endl;
  std::cout << "  Channels    : " << kChannels << std::endl;
  std::cout << std::endl;

  // -----------------------------------------------------------------------
  // Read WAV files (16 kHz)
  // -----------------------------------------------------------------------
  webrtc::WavReader far_reader(far_path);
  webrtc::WavReader near_reader(near_path);
  webrtc::WavReader mix_reader(mix_path);

  if (far_reader.sample_rate() != kSampleRateHz ||
      near_reader.sample_rate() != kSampleRateHz ||
      mix_reader.sample_rate() != kSampleRateHz) {
    std::cerr << "ERROR: All WAV files must be " << kSampleRateHz
              << " Hz sample rate." << std::endl;
    return 1;
  }
  if (far_reader.num_channels() != kChannels ||
      near_reader.num_channels() != kChannels ||
      mix_reader.num_channels() != kChannels) {
    std::cerr << "ERROR: All WAV files must be mono." << std::endl;
    return 1;
  }

  size_t total_samples = std::min({far_reader.num_samples(),
                                   near_reader.num_samples(),
                                   mix_reader.num_samples()});
  total_samples = (total_samples / kFrameSamples) * kFrameSamples;
  std::cout << "  Total samples : " << total_samples << " ("
            << (total_samples * 1000 / kSampleRateHz) << " ms)" << std::endl;

  std::vector<int16_t> far_input(total_samples, 0);
  std::vector<int16_t> near_input(total_samples, 0);
  std::vector<int16_t> mix_input(total_samples, 0);

  far_reader.ReadSamples(total_samples, far_input.data());
  near_reader.ReadSamples(total_samples, near_input.data());
  mix_reader.ReadSamples(total_samples, mix_input.data());

  // -----------------------------------------------------------------------
  // Print signal levels before processing
  // -----------------------------------------------------------------------
  double far_rms = ComputeRmsDb(far_input.data(), total_samples);
  double near_rms = ComputeRmsDb(near_input.data(), total_samples);
  double mix_rms = ComputeRmsDb(mix_input.data(), total_samples);
  std::cout << std::endl;
  std::cout << "  Signal levels (RMS dB):" << std::endl;
  std::cout << "    Far  : " << std::fixed << std::setprecision(1) << far_rms
            << " dB" << std::endl;
  std::cout << "    Near : " << near_rms << " dB" << std::endl;
  std::cout << "    Mix  : " << mix_rms << " dB" << std::endl;
  std::cout << std::endl;

  // -----------------------------------------------------------------------
  // Create AEC3 instance
  // -----------------------------------------------------------------------
  webrtc::Environment env;
  webrtc::EchoCanceller3Config config;

  // At 16 kHz, kBlockSize = 64 samples = 4ms per block
  // 100ms delay = 100 / 4 = 25 blocks
  config.delay.default_delay = 25;
  config.filter.initial_state_seconds = 0.5f;
  config.filter.conservative_initial_phase = false;

  webrtc::EchoCanceller3Factory factory(config);
  auto echo_control = factory.Create(env, kSampleRateHz, kChannels, kChannels);

  // -----------------------------------------------------------------------
  // Process audio in 10 ms frames
  // -----------------------------------------------------------------------
  std::vector<int16_t> output(total_samples, 0);
  size_t num_frames = total_samples / kFrameSamples;

  std::cout << "  Processing " << num_frames << " frames (10 ms each)..."
            << std::endl;

  webrtc::AudioBuffer render_buffer(kSampleRateHz, kChannels, kSampleRateHz,
                                    kChannels, kSampleRateHz, kChannels);
  webrtc::AudioBuffer capture_buffer(kSampleRateHz, kChannels, kSampleRateHz,
                                     kChannels, kSampleRateHz, kChannels);

  for (size_t f = 0; f < num_frames; ++f) {
    size_t offset = f * kFrameSamples;

    // --- Render (far-end) ---
    FillAudioBufferFromInt16(&render_buffer, far_input.data() + offset,
                             kFrameSamples);
    echo_control->AnalyzeRender(&render_buffer);

    // --- Capture (mix) ---
    FillAudioBufferFromInt16(&capture_buffer, mix_input.data() + offset,
                             kFrameSamples);
    echo_control->AnalyzeCapture(&capture_buffer);
    echo_control->ProcessCapture(&capture_buffer, /*level_change=*/false);

    // Save output
    ExtractInt16FromAudioBuffer(&capture_buffer, output.data() + offset,
                                kFrameSamples);
  }

  // -----------------------------------------------------------------------
  // Write output WAV
  // -----------------------------------------------------------------------
  webrtc::WavWriter writer(output_path, kSampleRateHz, kChannels);
  writer.WriteSamples(output.data(), output.size());
  std::cout << "  Output written to: " << output_path << std::endl;
  std::cout << std::endl;

  // -----------------------------------------------------------------------
  // Compute metrics
  // -----------------------------------------------------------------------
  double output_rms = ComputeRmsDb(output.data(), total_samples);
  double erle = ComputeErle(mix_input.data(), output.data(), total_samples);
  double snr_imp = ComputeSnrImprovement(output.data(), near_input.data(),
                                         mix_input.data(), total_samples);

  std::cout << "  === Results ===" << std::endl;
  std::cout << "  Output RMS (dB)    : " << std::fixed << std::setprecision(1)
            << output_rms << " dB" << std::endl;
  std::cout << "  ERLE (dB)          : " << std::fixed << std::setprecision(1)
            << erle << " dB" << std::endl;
  std::cout << "  SNR Improvement    : " << std::fixed << std::setprecision(1)
            << snr_imp << " dB" << std::endl;

  size_t half = total_samples / 2;
  double erle_first =
      ComputeErle(mix_input.data(), output.data(), half);
  double erle_second = ComputeErle(mix_input.data() + half,
                                   output.data() + half,
                                   total_samples - half);
  std::cout << "  ERLE (first half)  : " << std::fixed << std::setprecision(1)
            << erle_first << " dB" << std::endl;
  std::cout << "  ERLE (second half) : " << std::fixed << std::setprecision(1)
            << erle_second << " dB" << std::endl;

  std::cout << std::endl;
  std::cout << "  Echo cancellation test completed." << std::endl;

  return 0;
}