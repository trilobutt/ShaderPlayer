// AudioPlayer.cpp — miniaudio implementation.
//
// MINIAUDIO_IMPLEMENTATION must be defined in exactly one translation unit.
// This file owns it. miniaudio.h must be included before Common.h to ensure
// that its internal #define INITGUID + WASAPI COM includes happen before
// any transitive inclusion of windows.h through Common.h.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "AudioPlayer.h"

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <cstring>

namespace SP {

AudioPlayer::AudioPlayer() = default;

AudioPlayer::~AudioPlayer() {
    Shutdown();
}

bool AudioPlayer::Initialize() {
    if (m_initialized) return true;

    m_ring = std::make_unique<float[]>(kRingCap);
    std::memset(m_ring.get(), 0, kRingCap * sizeof(float));
    m_wPos.store(0);
    m_rPos.store(0);
    m_flush.store(false);

    m_device = new ma_device();

    ma_device_config config     = ma_device_config_init(ma_device_type_playback);
    config.playback.format      = ma_format_f32;
    config.playback.channels    = 1;      // mono; miniaudio upmixes to device channels
    config.sampleRate           = 0;      // 0 → MA_DEFAULT_SAMPLE_RATE (48000)
    config.pUserData            = this;

    // Use a capturing lambda to adapt unsigned int to ma_uint32 without exposing
    // miniaudio types through the header.
    config.dataCallback = [](ma_device* dev, void* out, const void* in, ma_uint32 fc) {
        (void)in;
        auto* self = static_cast<AudioPlayer*>(dev->pUserData);

        // Flush request: consumer resets rPos to wPos (SPSC — consumer owns rPos).
        if (self->m_flush.load(std::memory_order_acquire)) {
            self->m_rPos.store(self->m_wPos.load(std::memory_order_relaxed),
                               std::memory_order_release);
            self->m_flush.store(false, std::memory_order_release);
            std::memset(out, 0, fc * sizeof(float));
            return;
        }

        float*   pOut      = static_cast<float*>(out);
        uint64_t rPos      = self->m_rPos.load(std::memory_order_relaxed);
        uint64_t wPos      = self->m_wPos.load(std::memory_order_acquire);
        uint64_t available = wPos - rPos;
        float    vol       = self->m_mute.load(std::memory_order_relaxed) ? 0.0f
                           : self->m_volume.load(std::memory_order_relaxed);

        uint32_t toRead = static_cast<uint32_t>(
            std::min(static_cast<uint64_t>(fc), available));

        for (uint32_t i = 0; i < toRead; ++i) {
            pOut[i] = self->m_ring[(rPos + i) & kRingMask] * vol;
        }
        if (toRead < fc) {
            std::memset(pOut + toRead, 0, (fc - toRead) * sizeof(float));
        }
        self->m_rPos.store(rPos + toRead, std::memory_order_release);
    };

    if (ma_device_init(nullptr, &config, m_device) != MA_SUCCESS) {
        delete m_device;
        m_device = nullptr;
        m_ring.reset();
        return false;
    }

    m_deviceRate = static_cast<int>(m_device->sampleRate);

    if (ma_device_start(m_device) != MA_SUCCESS) {
        ma_device_uninit(m_device);
        delete m_device;
        m_device = nullptr;
        m_ring.reset();
        return false;
    }

    m_initialized = true;
    return true;
}

void AudioPlayer::Shutdown() {
    if (m_device) {
        ma_device_uninit(m_device);  // stops the callback thread before we free resources
        delete m_device;
        m_device = nullptr;
    }
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }
    m_ring.reset();
    m_srcRate    = 0;
    m_deviceRate = 0;
    m_initialized = false;
}

void AudioPlayer::InitResampler(int sourceRate) {
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }
    m_srcRate = 0;

    AVChannelLayout monoLayout = AV_CHANNEL_LAYOUT_MONO;
    if (swr_alloc_set_opts2(&m_swrCtx,
            &monoLayout, AV_SAMPLE_FMT_FLTP, m_deviceRate,   // output: device rate, mono float
            &monoLayout, AV_SAMPLE_FMT_FLTP, sourceRate,     // input:  source rate, mono float
            0, nullptr) < 0 || !m_swrCtx || swr_init(m_swrCtx) < 0) {
        if (m_swrCtx) { swr_free(&m_swrCtx); m_swrCtx = nullptr; }
        return;
    }
    m_srcRate = sourceRate;
}

void AudioPlayer::PushToRing(const float* data, int count) {
    uint64_t wPos      = m_wPos.load(std::memory_order_relaxed);
    uint64_t rPos      = m_rPos.load(std::memory_order_acquire);
    uint64_t space     = static_cast<uint64_t>(kRingCap) - (wPos - rPos);
    int      toCopy    = static_cast<int>(std::min(static_cast<uint64_t>(count), space));

    for (int i = 0; i < toCopy; ++i) {
        m_ring[(wPos + i) & kRingMask] = data[i];
    }
    m_wPos.store(wPos + toCopy, std::memory_order_release);
}

void AudioPlayer::Submit(const float* mono, int count, int sampleRate) {
    if (!m_initialized || count <= 0 || !mono) return;

    if (sampleRate == m_deviceRate) {
        // No resampling required — common case (48 kHz source on 48 kHz device).
        PushToRing(mono, count);
        return;
    }

    // Lazy-init or reconfigure the resampler when the source rate changes.
    if (sampleRate != m_srcRate || !m_swrCtx) {
        InitResampler(sampleRate);
        if (!m_swrCtx) {
            // Resampler init failed — push raw and accept the pitch distortion
            // rather than silencing audio entirely.
            PushToRing(mono, count);
            return;
        }
    }

    int outFrames = swr_get_out_samples(m_swrCtx, count);
    if (outFrames <= 0) return;
    m_resampleBuf.resize(outFrames);

    uint8_t*       outBuf = reinterpret_cast<uint8_t*>(m_resampleBuf.data());
    const uint8_t* inBuf  = reinterpret_cast<const uint8_t*>(mono);
    int converted = swr_convert(m_swrCtx, &outBuf, outFrames, &inBuf, count);

    if (converted > 0) {
        PushToRing(m_resampleBuf.data(), converted);
    }
}

void AudioPlayer::Flush() {
    // Signal the callback thread to discard the ring buffer contents.
    // The callback resets rPos to wPos on the next invocation, then outputs silence
    // until Submit() provides new samples from the post-seek decode position.
    m_flush.store(true, std::memory_order_release);
}

void AudioPlayer::SetVolume(float vol) {
    m_volume.store(std::max(0.0f, std::min(1.0f, vol)), std::memory_order_relaxed);
}

void AudioPlayer::SetMute(bool mute) {
    m_mute.store(mute, std::memory_order_relaxed);
}

} // namespace SP
