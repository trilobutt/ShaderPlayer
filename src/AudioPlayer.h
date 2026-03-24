#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

// ma_device and SwrContext are defined in AudioPlayer.cpp only — forward-declare to
// keep miniaudio.h and FFmpeg headers out of this header and everything that includes it.
struct ma_device;
struct SwrContext;

namespace SP {

// Mono float audio player backed by miniaudio (WASAPI on Windows).
// Submit() feeds decoded samples from the main thread; miniaudio's callback thread
// drains them independently, so main-thread stalls (GPU, ImGui, file I/O) never
// cause audio underruns as long as the ring buffer holds enough cushion.
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    // Initialise WASAPI device and start audio thread. Non-fatal (returns false when
    // no audio output device is present). Safe to call multiple times after Shutdown().
    bool Initialize();
    void Shutdown();

    // Push decoded mono float samples. Always call from the main thread.
    // sampleRate: source sample rate reported by VideoDecoder::GetAudioSampleRate().
    // Resamples to device rate on the fly if needed; drops samples silently when the
    // ring buffer is full (≈10 s at 48 kHz) rather than blocking.
    void Submit(const float* mono, int count, int sampleRate);

    // Discard all buffered samples immediately. Call on seek, pause, stop, or close.
    // The callback outputs silence until new samples arrive.
    void Flush();

    void SetVolume(float vol);  // 0.0–1.0, clamped
    void SetMute(bool mute);

    bool  IsInitialized()       const { return m_initialized; }
    int   GetDeviceSampleRate() const { return m_deviceRate; }

    // Approximate number of samples currently in the ring buffer (thread-safe estimate).
    int GetBufferedSamples() const {
        if (!m_initialized) return 0;
        return static_cast<int>(
            m_wPos.load(std::memory_order_relaxed) -
            m_rPos.load(std::memory_order_relaxed));
    }

private:
    void InitResampler(int sourceRate);
    void PushToRing(const float* data, int count);

    // ── Ring buffer ──────────────────────────────────────────────────────────
    // SPSC lock-free.  Producer = main thread (Submit).  Consumer = audio thread (callback).
    // Stores mono f32 samples at device sample rate.
    static constexpr int kRingCap  = 1 << 19;  // 524 288 samples ≈ 10.9 s at 48 kHz
    static constexpr int kRingMask = kRingCap - 1;
    std::unique_ptr<float[]> m_ring;
    std::atomic<uint64_t>    m_wPos{0};
    std::atomic<uint64_t>    m_rPos{0};
    std::atomic<bool>        m_flush{false};

    // ── Resampler (main thread only) ─────────────────────────────────────────
    SwrContext*        m_swrCtx      = nullptr;
    int                m_srcRate     = 0;   // source rate currently configured in SWR
    int                m_deviceRate  = 0;   // device sample rate (set after Initialize)
    std::vector<float> m_resampleBuf;       // scratch buffer for converted output

    // ── miniaudio device ────────────────────────────────────────────────────
    ma_device* m_device      = nullptr;
    bool       m_initialized = false;

    // ── Volume (atomic — read by callback, written by main thread) ──────────
    std::atomic<float> m_volume{1.0f};
    std::atomic<bool>  m_mute{false};
};

} // namespace SP
