#pragma once

#include "Common.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libswscale/swscale.h>
}

namespace SP {

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    // Non-copyable
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // File operations
    bool Open(const std::string& filepath);
    void Close();
    bool IsOpen() const { return m_formatCtx != nullptr; }

    // Decoding
    bool DecodeNextFrame(VideoFrame& outFrame);
    bool SeekToTime(double seconds);
    bool SeekToFrame(int64_t frameNumber);

    // Video properties
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    double GetFPS() const { return m_fps; }
    double GetDuration() const { return m_duration; }
    int64_t GetFrameCount() const { return m_frameCount; }
    double GetCurrentTime() const { return m_currentTime; }
    AVPixelFormat GetPixelFormat() const { return m_pixelFormat; }
    std::string GetCodecName() const { return m_codecName; }
    
    // Hardware acceleration
    bool IsHardwareAccelerated() const { return m_hwDeviceCtx != nullptr; }
    ID3D11Device* GetD3D11Device() const;

private:
    bool InitHardwareDecoder(ID3D11Device* device);
    bool ConvertFrame(AVFrame* frame, VideoFrame& outFrame);
    void FlushDecoder();

    AVFormatContext* m_formatCtx = nullptr;
    AVCodecContext* m_codecCtx = nullptr;
    AVBufferRef* m_hwDeviceCtx = nullptr;
    SwsContext* m_swsCtx = nullptr;
    AVFrame* m_frame = nullptr;
    AVFrame* m_hwFrame = nullptr;
    AVPacket* m_packet = nullptr;

    int m_videoStreamIdx = -1;
    int m_width = 0;
    int m_height = 0;
    double m_fps = 0.0;
    double m_duration = 0.0;
    int64_t m_frameCount = 0;
    double m_currentTime = 0.0;
    AVPixelFormat m_pixelFormat = AV_PIX_FMT_NONE;
    std::string m_codecName;

    // For YUV to RGB conversion
    AVPixelFormat m_outputFormat = AV_PIX_FMT_RGBA;
    std::vector<uint8_t> m_conversionBuffer;
};

} // namespace SP
