#pragma once

#include "Common.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace SP {

class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();

    // Non-copyable
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    // Recording control
    bool StartRecording(const RecordingSettings& settings, int sourceWidth, int sourceHeight, double sourceFPS);
    void StopRecording();
    bool IsRecording() const { return m_recording.load(); }

    // Frame submission (thread-safe)
    bool SubmitFrame(const std::vector<uint8_t>& rgbaData, int width, int height);
    
    // Statistics
    int64_t GetFramesEncoded() const { return m_framesEncoded.load(); }
    int64_t GetFramesDropped() const { return m_framesDropped.load(); }
    double GetEncodingFPS() const;

private:
    void EncoderThread();
    bool InitEncoder(const RecordingSettings& settings, int width, int height, double fps);
    bool EncodeFrame(AVFrame* frame);
    void FlushEncoder();

    // FFmpeg encoding context
    AVFormatContext* m_formatCtx = nullptr;
    AVCodecContext* m_codecCtx = nullptr;
    AVStream* m_videoStream = nullptr;
    SwsContext* m_swsCtx = nullptr;
    AVFrame* m_frame = nullptr;
    AVPacket* m_packet = nullptr;

    // Frame queue
    struct QueuedFrame {
        std::vector<uint8_t> data;
        int width;
        int height;
    };
    std::queue<QueuedFrame> m_frameQueue;
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
