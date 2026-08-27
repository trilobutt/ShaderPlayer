#pragma once

#include "Common.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/audio_fifo.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace SP {

class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();

    // Non-copyable
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    // Recording control. audioSampleRate > 0 adds an audio stream fed by SubmitAudio;
    // 0 writes a video-only file, which is what a live capture or a generative shader
    // gets, neither having any audio to mux.
    bool StartRecording(const RecordingSettings& settings, int sourceWidth, int sourceHeight,
                        double sourceFPS, int audioSampleRate = 0);
    void StopRecording();
    bool IsRecording() const { return m_recording.load(); }
    bool HasAudioStream() const { return m_audioStream != nullptr; }

    // Frame submission (thread-safe)
    bool SubmitFrame(const std::vector<uint8_t>& rgbaData, int width, int height);

    // Packed stereo float at the rate passed to StartRecording, as VideoDecoder's
    // recording tap emits it. floatCount counts floats, so a frame is two of them.
    // Silently does nothing when the file has no audio stream.
    bool SubmitAudio(const float* interleavedStereo, int floatCount);
    
    // Statistics
    int64_t GetFramesEncoded() const { return m_framesEncoded.load(); }
    int64_t GetFramesDropped() const { return m_framesDropped.load(); }
    double GetEncodingFPS() const;

private:
    void EncoderThread();
    bool InitEncoder(const RecordingSettings& settings, int width, int height, double fps);
    // Non-fatal: a failure here leaves m_audioStream null and the recording goes ahead
    // silent, which beats refusing to record at all.
    void InitAudioStream(AVCodecID codecId, int sampleRate);
    // Codec context, resampler, FIFO and frame. Not the stream, which the format context
    // owns, and not m_audioPacket, which outlives a recording.
    void FreeAudioResources();
    bool EncodeFrame(AVFrame* frame);
    void FlushEncoder();
    // Resample, buffer, and emit whole codec frames. WriteAudio takes what SubmitAudio
    // queued; DrainAudioFifo cuts it into frame_size pieces (and, with flush set, takes
    // the short tail too).
    void WriteAudio(const float* interleavedStereo, size_t floatCount);
    void DrainAudioFifo(bool flush);
    bool EncodeAudioFrame(AVFrame* frame);
    void FlushAudioEncoder();

    // FFmpeg encoding context
    AVFormatContext* m_formatCtx = nullptr;
    AVCodecContext* m_codecCtx = nullptr;
    AVStream* m_videoStream = nullptr;

    // Audio, all null when the file is video-only. m_audioPts counts samples on the audio
    // codec's own time_base ({1, sample_rate}), so it is the position in the file rather
    // than anything the video clock decides.
    AVStream* m_audioStream = nullptr;
    AVCodecContext* m_audioCodecCtx = nullptr;
    SwrContext* m_audioSwr = nullptr;
    AVAudioFifo* m_audioFifo = nullptr;
    AVFrame* m_audioFrame = nullptr;
    AVPacket* m_audioPacket = nullptr;
    int m_audioSrcRate = 0;
    int m_audioFrameSize = 0;
    int64_t m_audioPts = 0;

    SwsContext* m_swsCtx = nullptr;
    AVFrame* m_frame = nullptr;
    AVFrame* m_srcFrame = nullptr;  // Source RGBA frame; FFmpeg-allocated so sws_scale has safe padding
    AVPacket* m_packet = nullptr;

    // Frame queue
    struct QueuedFrame {
        std::vector<uint8_t> data;
        int width;
        int height;
    };
    std::queue<QueuedFrame> m_frameQueue;
    // Shares m_queueMutex and m_queueCV with the video queue: the encoder thread has to
    // wake for either, and one predicate over both is what keeps it from sleeping through
    // a submission to the other.
    std::queue<std::vector<float>> m_audioQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCV;

    // Encoder thread
    std::thread m_encoderThread;
    std::atomic<bool> m_recording{false};
    std::atomic<bool> m_stopRequested{false};

    // Statistics
    std::atomic<int64_t> m_framesEncoded{0};
    std::atomic<int64_t> m_framesDropped{0};
    std::chrono::steady_clock::time_point m_startTime;

    // Settings
    int m_width = 0;
    int m_height = 0;
    double m_fps = 0.0;
    int64_t m_frameIndex = 0;
};

} // namespace SP
