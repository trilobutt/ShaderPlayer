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
#include <libswresample/swresample.h>
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

    // Live capture: dshow webcam (isDshow=true) or any URL (isDshow=false, e.g. rtsp://)
    bool OpenCapture(const std::string& deviceOrUrl, bool isDshow = true);
    bool IsLiveCapture() const { return m_isLiveCapture; }

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

    // Audio stream (optional — not all videos have audio)
    bool HasAudio() const { return m_audioStreamIdx >= 0; }
    int  GetAudioSampleRate() const { return m_audioSampleRate; }
    int  GetAudioChannels()   const { return m_audioChannels; }

    // Drain accumulated decoded audio samples (mono float, interleaved if channels>1
    // but we always emit mono after SWR conversion). Returns number of floats written.
    int DrainAudioSamples(float* buf, int maxFloats);

    // A second conversion of the same packets, kept separate from the analyser's mono
    // mix: packed stereo float at the source rate, which is what the recorder muxes into
    // the output file. Off by default, so a playback session pays nothing for it; the
    // render turns it on for its duration. Enabling it drops whatever it was holding, so
    // the tap always starts at the position the caller enabled it from.
    void SetRecordingTap(bool on);
    bool RecordingTapOn() const { return m_recordTap; }

    // Drain the recording tap. maxFloats counts floats, so a stereo frame is two of them.
    int DrainRecordingSamples(float* buf, int maxFloats);

    // Read ahead for audio: call av_read_frame in a loop, decode audio packets
    // eagerly, queue video packets for later consumption by DecodeNextFrame.
    // Stops when m_audioPending.size() >= targetSamples or EOF.
    void ReadAudioAhead(int targetSamples);

    // True if the last ReadAudioAhead call hit AVERROR_EOF before filling the
    // requested samples. Cleared by FlushDecoder (SeekToTime / Close).
    bool AudioEOFReached() const { return m_audioEOFReached; }


    // Hardware acceleration
    bool IsHardwareAccelerated() const { return m_hwDeviceCtx != nullptr; }
    ID3D11Device* GetD3D11Device() const;

private:
    bool InitHardwareDecoder(ID3D11Device* device);
    bool ConvertFrame(AVFrame* frame, VideoFrame& outFrame);
    void FlushDecoder();
    void OpenAudioStream();   // Called from Open(); non-fatal if no audio stream
    void CloseAudioStream();
    void DecodeAudioPacket(); // Sends m_packet to audio codec, drains frames into m_audioPending
    void FlushVideoQueue();   // Free all queued video packets

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
    bool m_isLiveCapture = false;

    // For YUV to RGB conversion
    AVPixelFormat m_outputFormat = AV_PIX_FMT_RGBA;
    std::vector<uint8_t> m_conversionBuffer;

    // Audio stream (optional)
    int              m_audioStreamIdx  = -1;
    int              m_audioSampleRate = 0;
    int              m_audioChannels   = 0;
    AVCodecContext*  m_audioCtx        = nullptr;
    SwrContext*      m_swrCtx          = nullptr;
    AVFrame*         m_audioFrame      = nullptr;
    std::vector<float> m_audioPending; // accumulated mono-float samples awaiting drain

    // The recording tap (see SetRecordingTap). Its resampler is built on first use and
    // torn down with the stream, so a session that never records never allocates it.
    bool             m_recordTap        = false;
    SwrContext*      m_recordSwr        = nullptr;
    std::vector<float> m_recordPending;  // packed stereo float awaiting drain

    // Video packet queue — populated by ReadAudioAhead(), consumed by DecodeNextFrame().
    // Entries are av_packet_clone()'d; caller must av_packet_free() on pop.
    std::queue<AVPacket*> m_videoPktQueue;

    bool m_audioEOFReached = false;
};

} // namespace SP
