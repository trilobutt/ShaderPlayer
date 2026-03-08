#pragma once

#include "Common.h"

// Forward-declare KissFFT real-FFT state type (avoids including kiss_fftr.h everywhere).
struct kiss_fftr_state;

namespace SP {

// Pure DSP class: accumulates mono float PCM samples decoded from the video
// stream by VideoDecoder, runs a real-input FFT when enough samples are
// available, and computes band energies / beat detection.
//
// Not thread-safe — all methods are called from the main thread.
class AudioAnalyzer {
public:
    AudioAnalyzer();
    ~AudioAnalyzer();

    AudioAnalyzer(const AudioAnalyzer&) = delete;
    AudioAnalyzer& operator=(const AudioAnalyzer&) = delete;

    // Feed interleaved PCM samples (already converted to float by VideoDecoder).
    // channels: number of interleaved channels in data (1=mono, 2=stereo).
    // Samples are mixed to mono internally.
    void FeedSamples(const float* data, int count, int channels, int sampleRate);

    // Copy latest analysis result.
    void GetData(AudioData& out) const { out = m_data; }

    // Clear the ring buffer and zero the output (call on seek or close).
    void Reset();

    void UpdateSettings(const AudioSettings& s) { m_settings = s; }

private:
    void RunFFT();

    static constexpr int kFFTSize     = 2048;  // Must be power of 2
    static constexpr int kOutputBins  = AudioData::kSpectrumBins;

    // KissFFT plan — allocated once in RunFFT on first call or after Reset.
    kiss_fftr_state* m_fftCfg = nullptr;

    // Ring buffer for mono input samples.
    float m_ring[kFFTSize] = {};
    int   m_ringWrite = 0;
    int   m_ringFill  = 0;   // samples available (capped at kFFTSize)

    // Last sample rate seen — triggers Hann window recompute if it changes.
    int   m_sampleRate = 0;
    float m_hannWindow[kFFTSize] = {};

    // Beat detection rolling history (~1 second at typical decode rates).
    static constexpr int kBeatHistory = 43;
    float m_bassHistory[kBeatHistory] = {};
    int   m_bassHistIdx  = 0;
    float m_beatDecaying = 0.0f;

    AudioData     m_data;
    AudioSettings m_settings;
};

} // namespace SP
