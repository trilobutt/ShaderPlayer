#include "VideoDecoder.h"
#include <stdexcept>
#include <cstring>

namespace SP {

VideoDecoder::VideoDecoder() {
    m_frame      = av_frame_alloc();
    m_hwFrame    = av_frame_alloc();
    m_audioFrame = av_frame_alloc();
    m_packet     = av_packet_alloc();

    if (!m_frame || !m_hwFrame || !m_audioFrame || !m_packet) {
        throw std::runtime_error("Failed to allocate FFmpeg structures");
    }
}

VideoDecoder::~VideoDecoder() {
    Close();
    av_frame_free(&m_frame);
    av_frame_free(&m_hwFrame);
    av_frame_free(&m_audioFrame);
    av_packet_free(&m_packet);
}

bool VideoDecoder::Open(const std::string& filepath) {
    Close();

    // Open input file
    if (avformat_open_input(&m_formatCtx, filepath.c_str(), nullptr, nullptr) < 0) {
        return false;
    }

    // Find stream info
    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
        Close();
        return false;
    }

    // Find video stream
    m_videoStreamIdx = av_find_best_stream(m_formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_videoStreamIdx < 0) {
        Close();
        return false;
    }

    AVStream* videoStream = m_formatCtx->streams[m_videoStreamIdx];
    AVCodecParameters* codecParams = videoStream->codecpar;

    // Find decoder
    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        Close();
        return false;
    }

    // Allocate codec context
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        Close();
        return false;
    }

    // Copy codec parameters
    if (avcodec_parameters_to_context(m_codecCtx, codecParams) < 0) {
        Close();
        return false;
    }

    // Try hardware acceleration for H.264
    // (Skip for now, software decode is more reliable cross-system)
    
    // Open codec
    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        Close();
        return false;
    }

    // Store video properties
    m_width = m_codecCtx->width;
    m_height = m_codecCtx->height;
    m_pixelFormat = m_codecCtx->pix_fmt;
    m_codecName = codec->name;

    // Calculate FPS
    if (videoStream->avg_frame_rate.den != 0) {
        m_fps = av_q2d(videoStream->avg_frame_rate);
    } else if (videoStream->r_frame_rate.den != 0) {
        m_fps = av_q2d(videoStream->r_frame_rate);
    } else {
        m_fps = 25.0;  // Fallback
    }

    // Calculate duration
    if (m_formatCtx->duration != AV_NOPTS_VALUE) {
        m_duration = static_cast<double>(m_formatCtx->duration) / AV_TIME_BASE;
    } else if (videoStream->duration != AV_NOPTS_VALUE) {
        m_duration = static_cast<double>(videoStream->duration) * av_q2d(videoStream->time_base);
    }

    // Estimate frame count
    m_frameCount = static_cast<int64_t>(m_duration * m_fps);

    // Allocate conversion buffer for RGBA output.
    // align=1 gives the exact minimum size with no row padding. sws_scale's SIMD
    // routines (SSE/AVX) can overshoot by up to one full SIMD vector on the last row,
    // corrupting the CRT heap guard. 64 bytes of tail padding absorbs that overshoot.
    int bufferSize = av_image_get_buffer_size(m_outputFormat, m_width, m_height, 1);
    m_conversionBuffer.resize(bufferSize + 64);

    // Open audio stream (non-fatal — many videos have no audio).
    OpenAudioStream();

    return true;
}

void VideoDecoder::Close() {
    CloseAudioStream();
    FlushDecoder();
    
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    
    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwDeviceCtx = nullptr;
    }
    
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }
    
    if (m_formatCtx) {
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
    }

    m_videoStreamIdx  = -1;
    m_audioStreamIdx  = -1;
    m_audioSampleRate = 0;
    m_audioChannels   = 0;
    m_width = 0;
    m_height = 0;
    m_fps = 0.0;
    m_duration = 0.0;
    m_frameCount = 0;
    m_currentTime = 0.0;
    m_pixelFormat = AV_PIX_FMT_NONE;
    m_codecName.clear();
    m_conversionBuffer.clear();
    m_audioPending.clear();
    m_isLiveCapture = false;
}

bool VideoDecoder::OpenCapture(const std::string& deviceOrUrl, bool isDshow) {
    Close();

    const AVInputFormat* fmt = nullptr;
    std::string url = deviceOrUrl;

    if (isDshow) {
        fmt = av_find_input_format("dshow");
        if (!fmt) return false;
        url = "video=" + deviceOrUrl;
    }

    // Request a common default; fall back to whatever the device offers.
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "video_size", "1280x720", 0);
    av_dict_set(&opts, "framerate", "30", 0);

    if (avformat_open_input(&m_formatCtx, url.c_str(), fmt, &opts) < 0) {
        av_dict_free(&opts);
        if (avformat_open_input(&m_formatCtx, url.c_str(), fmt, nullptr) < 0)
            return false;
    } else {
        av_dict_free(&opts);
    }

    // Non-blocking: av_read_frame returns AVERROR(EAGAIN) instead of blocking when
    // the device has no new frame yet. Keeps the render loop responsive.
    m_formatCtx->flags |= AVFMT_FLAG_NONBLOCK;

    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) { Close(); return false; }

    m_videoStreamIdx = av_find_best_stream(m_formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_videoStreamIdx < 0) { Close(); return false; }

    AVStream* videoStream = m_formatCtx->streams[m_videoStreamIdx];
    AVCodecParameters* codecParams = videoStream->codecpar;

    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) { Close(); return false; }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) { Close(); return false; }

    if (avcodec_parameters_to_context(m_codecCtx, codecParams) < 0) { Close(); return false; }
    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) { Close(); return false; }

    m_width       = m_codecCtx->width;
    m_height      = m_codecCtx->height;
    m_pixelFormat = m_codecCtx->pix_fmt;
    m_codecName   = codec->name;
    m_fps         = (videoStream->avg_frame_rate.den != 0) ? av_q2d(videoStream->avg_frame_rate) : 30.0;
    m_duration    = 0.0;
    m_frameCount  = 0;

    int bufferSize = av_image_get_buffer_size(m_outputFormat, m_width, m_height, 1);
    m_conversionBuffer.resize(bufferSize + 64);

    m_isLiveCapture = true;
    return true;
}

void VideoDecoder::FlushVideoQueue() {
    while (!m_videoPktQueue.empty()) {
        av_packet_free(&m_videoPktQueue.front());
        m_videoPktQueue.pop();
    }
}

void VideoDecoder::FlushDecoder() {
    FlushVideoQueue();
    if (m_codecCtx)
        avcodec_flush_buffers(m_codecCtx);
    if (m_audioCtx)
        avcodec_flush_buffers(m_audioCtx);
    av_frame_unref(m_frame);
    av_frame_unref(m_hwFrame);
    av_frame_unref(m_audioFrame);
    av_packet_unref(m_packet);
    m_audioPending.clear();
    m_audioEOFReached = false;
}

bool VideoDecoder::DecodeNextFrame(VideoFrame& outFrame) {
    if (!IsOpen()) return false;

    while (true) {
        // Try to receive a decoded frame
        int ret = avcodec_receive_frame(m_codecCtx, m_frame);
        
        if (ret == 0) {
            // Got a frame, convert and return
            if (ConvertFrame(m_frame, outFrame)) {
                // Update current time
                AVStream* stream = m_formatCtx->streams[m_videoStreamIdx];
                if (m_frame->pts != AV_NOPTS_VALUE) {
                    m_currentTime = static_cast<double>(m_frame->pts) * av_q2d(stream->time_base);
                }
                av_frame_unref(m_frame);
                return true;
            }
            av_frame_unref(m_frame);
            continue;
        }
        
        if (ret == AVERROR(EAGAIN)) {
            // Need more input
        } else if (ret == AVERROR_EOF) {
            return false;  // End of stream
        } else {
            return false;  // Error
        }

        // Prefer queued video packets (populated by ReadAudioAhead) to avoid
        // re-reading from the container. Falls through to av_read_frame if the
        // queue is empty, which also decodes any interleaved audio packets.
        while (true) {
            if (!m_videoPktQueue.empty()) {
                AVPacket* queued = m_videoPktQueue.front();
                m_videoPktQueue.pop();
                ret = avcodec_send_packet(m_codecCtx, queued);
                av_packet_free(&queued);
                if (ret < 0 && ret != AVERROR(EAGAIN)) return false;
                break;
            }

            ret = av_read_frame(m_formatCtx, m_packet);
            if (ret < 0) {
                if (ret == AVERROR_EOF) {
                    avcodec_send_packet(m_codecCtx, nullptr);
                    break;
                }
                return false;
            }

            if (m_packet->stream_index == m_videoStreamIdx) {
                ret = avcodec_send_packet(m_codecCtx, m_packet);
                av_packet_unref(m_packet);
                if (ret < 0 && ret != AVERROR(EAGAIN)) return false;
                break;
            } else if (m_packet->stream_index == m_audioStreamIdx && m_audioCtx) {
                DecodeAudioPacket();
                continue;
            }
            av_packet_unref(m_packet);
        }
    }
}

bool VideoDecoder::ConvertFrame(AVFrame* frame, VideoFrame& outFrame) {
    AVFrame* srcFrame = frame;
    
    // Handle hardware frames
    if (frame->format == AV_PIX_FMT_D3D11) {
        if (av_hwframe_transfer_data(m_hwFrame, frame, 0) < 0) {
            return false;
        }
        srcFrame = m_hwFrame;
    }

    // Initialize or reinitialize swscale context
    AVPixelFormat srcFormat = static_cast<AVPixelFormat>(srcFrame->format);
    
    m_swsCtx = sws_getCachedContext(
        m_swsCtx,
        srcFrame->width, srcFrame->height, srcFormat,
        m_width, m_height, m_outputFormat,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!m_swsCtx) {
        return false;
    }

    // Set up destination pointers
    uint8_t* dstData[4] = { m_conversionBuffer.data(), nullptr, nullptr, nullptr };
    int dstLinesize[4] = { m_width * 4, 0, 0, 0 };  // RGBA = 4 bytes per pixel

    // Convert
    int result = sws_scale(
        m_swsCtx,
        srcFrame->data, srcFrame->linesize,
        0, srcFrame->height,
        dstData, dstLinesize
    );

    if (result <= 0) {
        return false;
    }

    // Copy to output frame
    outFrame.width = m_width;
    outFrame.height = m_height;
    outFrame.format = m_outputFormat;
    outFrame.pts = srcFrame->pts;
    
    AVStream* stream = m_formatCtx->streams[m_videoStreamIdx];
    if (srcFrame->pts != AV_NOPTS_VALUE) {
        outFrame.timestamp = static_cast<double>(srcFrame->pts) * av_q2d(stream->time_base);
    }

    outFrame.data[0].assign(m_conversionBuffer.begin(), m_conversionBuffer.end());
    outFrame.linesize[0] = dstLinesize[0];
    outFrame.linesize[1] = 0;
    outFrame.linesize[2] = 0;
    outFrame.linesize[3] = 0;

    if (srcFrame == m_hwFrame) {
        av_frame_unref(m_hwFrame);
    }

    return true;
}

bool VideoDecoder::SeekToTime(double seconds) {
    if (!IsOpen()) return false;

    AVStream* stream = m_formatCtx->streams[m_videoStreamIdx];
    int64_t timestamp = static_cast<int64_t>(seconds / av_q2d(stream->time_base));
    
    int ret = av_seek_frame(m_formatCtx, m_videoStreamIdx, timestamp, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        return false;
    }

    FlushDecoder();
    m_currentTime = seconds;
    return true;
}

bool VideoDecoder::SeekToFrame(int64_t frameNumber) {
    if (m_fps <= 0) return false;
    double seconds = static_cast<double>(frameNumber) / m_fps;
    return SeekToTime(seconds);
}

int VideoDecoder::DrainAudioSamples(float* buf, int maxFloats) {
    int available = static_cast<int>(m_audioPending.size());
    int toCopy = std::min(available, maxFloats);
    if (toCopy > 0) {
        std::copy(m_audioPending.begin(), m_audioPending.begin() + toCopy, buf);
        m_audioPending.erase(m_audioPending.begin(), m_audioPending.begin() + toCopy);
    }
    return toCopy;
}

void VideoDecoder::OpenAudioStream() {
    if (!m_formatCtx) return;

    int idx = av_find_best_stream(m_formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (idx < 0) return;  // No audio stream — silent video.

    const AVCodec* codec = avcodec_find_decoder(m_formatCtx->streams[idx]->codecpar->codec_id);
    if (!codec) return;

    m_audioCtx = avcodec_alloc_context3(codec);
    if (!m_audioCtx) return;

    if (avcodec_parameters_to_context(m_audioCtx, m_formatCtx->streams[idx]->codecpar) < 0) {
        avcodec_free_context(&m_audioCtx);
        return;
    }

    if (avcodec_open2(m_audioCtx, codec, nullptr) < 0) {
        avcodec_free_context(&m_audioCtx);
        return;
    }

    m_audioStreamIdx  = idx;
    m_audioSampleRate = m_audioCtx->sample_rate;
    m_audioChannels   = m_audioCtx->ch_layout.nb_channels;

    // Set up swresample: decode native format/layout → mono planar float.
    // This handles all source channel counts (stereo, 5.1, etc.) via downmix.
    AVChannelLayout monoLayout = AV_CHANNEL_LAYOUT_MONO;
    if (swr_alloc_set_opts2(&m_swrCtx,
            &monoLayout,              AV_SAMPLE_FMT_FLTP, m_audioSampleRate,
            &m_audioCtx->ch_layout,   m_audioCtx->sample_fmt, m_audioSampleRate,
            0, nullptr) < 0 || !m_swrCtx || swr_init(m_swrCtx) < 0) {
        swr_free(&m_swrCtx);
        avcodec_free_context(&m_audioCtx);
        m_audioStreamIdx = -1;
    }
}

void VideoDecoder::CloseAudioStream() {
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
    }
    if (m_audioCtx) {
        avcodec_free_context(&m_audioCtx);
    }
    m_audioStreamIdx  = -1;
    m_audioSampleRate = 0;
    m_audioChannels   = 0;
    m_audioPending.clear();
}

void VideoDecoder::DecodeAudioPacket() {
    // m_packet is already filled with an audio packet.
    if (avcodec_send_packet(m_audioCtx, m_packet) < 0) {
        av_packet_unref(m_packet);
        return;
    }
    av_packet_unref(m_packet);

    // Drain all decoded frames.
    while (avcodec_receive_frame(m_audioCtx, m_audioFrame) == 0) {
        // Convert to mono float via SWR.
        // Output is planar float (AV_SAMPLE_FMT_FLTP), channel 0 = mono mix.
        int outSamples = swr_get_out_samples(m_swrCtx, m_audioFrame->nb_samples);
        if (outSamples <= 0) {
            av_frame_unref(m_audioFrame);
            continue;
        }
        // Allocate temporary output buffer.
        std::vector<float> tmp(outSamples);
        uint8_t* outBuf = reinterpret_cast<uint8_t*>(tmp.data());
        int converted = swr_convert(m_swrCtx, &outBuf, outSamples,
                                    const_cast<const uint8_t**>(m_audioFrame->data),
                                    m_audioFrame->nb_samples);
        // SWR outputs mono (AV_CHANNEL_LAYOUT_MONO), so tmp holds the mono plane.
        if (converted > 0)
            m_audioPending.insert(m_audioPending.end(), tmp.begin(), tmp.begin() + converted);
        av_frame_unref(m_audioFrame);
    }
}

void VideoDecoder::ReadAudioAhead(int targetSamples) {
    if (!IsOpen() || m_audioStreamIdx < 0 || !m_audioCtx) return;

    m_audioEOFReached = false;
    while (static_cast<int>(m_audioPending.size()) < targetSamples) {
        int ret = av_read_frame(m_formatCtx, m_packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF)
                m_audioEOFReached = true;
            break;
        }

        if (m_packet->stream_index == m_audioStreamIdx) {
            DecodeAudioPacket();  // decodes + unrefs m_packet
        } else if (m_packet->stream_index == m_videoStreamIdx) {
            // Queue the video packet for DecodeNextFrame to consume later.
            m_videoPktQueue.push(av_packet_clone(m_packet));
            av_packet_unref(m_packet);
        } else {
            av_packet_unref(m_packet);
        }
    }
}

bool VideoDecoder::InitHardwareDecoder(ID3D11Device* device) {
    // Create hardware device context for D3D11VA
    AVBufferRef* hwDeviceCtx = nullptr;
    
    if (av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0) < 0) {
        return false;
    }

    // If we have an existing D3D11 device, we could set it here
    // For now, let FFmpeg create its own device

    m_hwDeviceCtx = hwDeviceCtx;
    m_codecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
    
    return true;
}

ID3D11Device* VideoDecoder::GetD3D11Device() const {
    if (!m_hwDeviceCtx) return nullptr;
    
    AVHWDeviceContext* deviceCtx = reinterpret_cast<AVHWDeviceContext*>(m_hwDeviceCtx->data);
    if (deviceCtx->type != AV_HWDEVICE_TYPE_D3D11VA) return nullptr;
    
    AVD3D11VADeviceContext* d3d11Ctx = static_cast<AVD3D11VADeviceContext*>(deviceCtx->hwctx);
    return d3d11Ctx->device;
}

} // namespace SP
