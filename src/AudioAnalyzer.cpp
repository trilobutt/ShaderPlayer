#include "AudioAnalyzer.h"

// KissFFT — real-input transform (header is in the kissfft root, found via include path)
#include <kiss_fftr.h>

#include <cmath>
#include <algorithm>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace SP {

AudioAnalyzer::AudioAnalyzer() = default;

AudioAnalyzer::~AudioAnalyzer() {
    if (m_fftCfg) {
        kiss_fftr_free(m_fftCfg);
        m_fftCfg = nullptr;
    }
}

void AudioAnalyzer::Reset() {
    std::fill(m_ring, m_ring + kFFTSize, 0.0f);
    m_ringWrite = 0;
    m_ringFill  = 0;
    std::fill(m_bassHistory, m_bassHistory + kBeatHistory, 0.0f);
    m_bassHistIdx  = 0;
    m_beatDecaying = 0.0f;
    m_data = AudioData{};
}

void AudioAnalyzer::FeedSamples(const float* data, int count, int channels, int sampleRate) {
    // Rebuild the Hann window if the sample rate changed.
    if (sampleRate != m_sampleRate) {
        m_sampleRate = sampleRate;
        for (int i = 0; i < kFFTSize; ++i)
            m_hannWindow[i] = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i / (kFFTSize - 1)));
    }

    // Mix to mono and write into the ring buffer.
    int frames = count / channels;
    for (int f = 0; f < frames; ++f) {
        float mono = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            mono += data[f * channels + ch];
        mono /= channels;

        m_ring[m_ringWrite] = mono;
        m_ringWrite = (m_ringWrite + 1) % kFFTSize;
        if (m_ringFill < kFFTSize) ++m_ringFill;
    }

    // Run FFT whenever we have a full window of samples.
    if (m_ringFill >= kFFTSize)
        RunFFT();
}

void AudioAnalyzer::RunFFT() {
    // Lazy-init the KissFFT plan.
    if (!m_fftCfg)
        m_fftCfg = kiss_fftr_alloc(kFFTSize, 0, nullptr, nullptr);

    // Copy ring buffer into a contiguous windowed array (oldest → newest).
    float windowed[kFFTSize];
    const int readStart = (m_ringWrite - kFFTSize + kFFTSize) % kFFTSize;
    for (int i = 0; i < kFFTSize; ++i) {
        int idx = (readStart + i) % kFFTSize;
        windowed[i] = m_ring[idx] * m_hannWindow[i];
    }

    // Forward FFT — output is kFFTSize/2+1 complex bins.
    kiss_fft_cpx out[kFFTSize / 2 + 1];
    kiss_fftr(m_fftCfg, windowed, out);

    // Magnitude spectrum (kFFTSize/2+1 bins).
    const int halfN = kFFTSize / 2 + 1;
    const float normFactor = 2.0f / kFFTSize;
    float mag[kFFTSize / 2 + 1];
    for (int i = 0; i < halfN; ++i) {
        float re = out[i].r, im = out[i].i;
        mag[i] = std::sqrt(re * re + im * im) * normFactor;
    }

    // Frequency per bin: binHz = sampleRate / kFFTSize
    const float binHz = static_cast<float>(m_sampleRate) / kFFTSize;

    // Band bin limits (skip DC bin 0).
    auto freqToBin = [&](float hz) {
        return std::max(1, std::min(halfN - 1, static_cast<int>(std::round(hz / binHz))));
    };
    const int bassLo = freqToBin(20.0f),   bassHi = freqToBin(250.0f);
    const int midLo  = freqToBin(250.0f),  midHi  = freqToBin(4000.0f);
    const int highLo = freqToBin(4000.0f), highHi = freqToBin(20000.0f);

    auto bandRMS = [&](int lo, int hi) {
        float sum = 0.0f;
        for (int i = lo; i <= hi; ++i) sum += mag[i] * mag[i];
        return std::sqrt(sum / std::max(1, hi - lo + 1));
    };

    // Raw band energies.
    const float rawBass = std::min(1.0f, bandRMS(bassLo, bassHi));
    const float rawMid  = std::min(1.0f, bandRMS(midLo,  midHi));
    const float rawHigh = std::min(1.0f, bandRMS(highLo, highHi));

    // Overall RMS.
    float sumSq = 0.0f;
    for (int i = 0; i < kFFTSize; ++i) sumSq += windowed[i] * windowed[i];
    const float rawRms = std::min(1.0f, std::sqrt(sumSq / kFFTSize));

    // EMA smoothing (s=0 means no smoothing, s=1 means frozen).
    const float s = std::clamp(m_settings.smoothing, 0.0f, 0.999f);
    m_data.bass = rawBass + s * (m_data.bass - rawBass);
    m_data.mid  = rawMid  + s * (m_data.mid  - rawMid);
    m_data.high = rawHigh + s * (m_data.high - rawHigh);
    m_data.rms  = rawRms  + s * (m_data.rms  - rawRms);

    // Beat detection: compare current bass energy against rolling average.
    float bassEnergy = rawBass * rawBass;
    float avgEnergy = std::accumulate(m_bassHistory, m_bassHistory + kBeatHistory, 0.0f)
                      / kBeatHistory;
    m_bassHistory[m_bassHistIdx] = bassEnergy;
    m_bassHistIdx = (m_bassHistIdx + 1) % kBeatHistory;

    if (bassEnergy > avgEnergy * m_settings.beatSensitivity && avgEnergy > 1e-6f)
        m_beatDecaying = 1.0f;
    else
        m_beatDecaying *= m_settings.beatDecay;
    m_data.beat = m_beatDecaying;

    // Spectral centroid (normalised).
    float weightedSum = 0.0f, totalMag = 0.0f;
    for (int i = 1; i < halfN; ++i) {
        weightedSum += i * mag[i];
        totalMag    += mag[i];
    }
    m_data.spectralCentroid = (totalMag > 1e-9f)
        ? std::min(1.0f, (weightedSum / totalMag) / (halfN - 1))
        : 0.0f;

    // Downsample kFFTSize/2 bins → kOutputBins via max-pooling within each group.
    const float groupSize = static_cast<float>(halfN - 1) / kOutputBins;
    for (int k = 0; k < kOutputBins; ++k) {
        int lo = 1 + static_cast<int>(k * groupSize);
        int hi = 1 + static_cast<int>((k + 1) * groupSize);
        hi = std::min(hi, halfN - 1);
        float peak = 0.0f;
        for (int i = lo; i <= hi; ++i) peak = std::max(peak, mag[i]);
        // EMA on spectrum bins too, same smoothing coefficient.
        float raw = std::min(1.0f, peak);
        m_data.spectrum[k] = raw + s * (m_data.spectrum[k] - raw);
    }

    // Consume the window; allow overlap (advance by half FFT size so bass transients
    // don't skip an analysis frame).
    m_ringFill -= kFFTSize / 2;
    if (m_ringFill < 0) m_ringFill = 0;
}

} // namespace SP
