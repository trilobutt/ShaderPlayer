#include "VideoEncoder.h"
#include <stdexcept>

namespace SP {

VideoEncoder::VideoEncoder() {
    m_packet = av_packet_alloc();
    if (!m_packet) {
        throw std::runtime_error("Failed to allocate AVPacket");
    }
}

VideoEncoder::~VideoEncoder() {
    StopRecording();
    av_packet_free(&m_packet);
}

bool VideoEncoder::StartRecording(const RecordingSettings& settings, int sourceWidth, int sourceHeight, double sourceFPS) {
    if (m_recording.load()) {
        return false;  // Already recording
    }

    // Use source dimensions/fps if not specified
    int width = settings.width > 0 ? settings.width : sourceWidth;
    int height = settings.height > 0 ? settings.height : sourceHeight;
    double fps = settings.fps > 0 ? static_cast<double>(settings.fps) : sourceFPS;

    if (!InitEncoder(settings, width, height, fps)) {
        return false;
    }

    m_framesEncoded = 0;
    m_framesDropped = 0;
    m_frameIndex = 0;
    m_stopRequested = false;
    m_recording = true;
    m_startTime = std::chrono::steady_clock::now();

    // Start encoder thread
    m_encoderThread = std::thread(&VideoEncoder::EncoderThread, this);

    return true;
}

void VideoEncoder::StopRecording() {
    if (!m_recording.load()) return;

    m_stopRequested = true;
    m_queueCV.notify_all();

    if (m_encoderThread.joinable()) {
        m_encoderThread.join();
    }

    FlushEncoder();

    // Write trailer and close
    if (m_formatCtx) {
        av_write_trailer(m_formatCtx);
        
        if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&m_formatCtx->pb);
        }
        avformat_free_context(m_formatCtx);
        m_formatCtx = nullptr;
    }

    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }

    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }

    if (m_frame) {
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }

    m_videoStream = nullptr;
    m_recording = false;
}

bool VideoEncoder::InitEncoder(const RecordingSettings& settings, int width, int height, double fps) {
    m_width = width;
    m_height = height;
    m_fps = fps;

    // Determine format from extension
    std::string ext;
    size_t dotPos = settings.outputPath.rfind('.');
    if (dotPos != std::string::npos) {
        ext = settings.outputPath.substr(dotPos + 1);
    }

    const AVOutputFormat* outputFormat = av_guess_format(nullptr, settings.outputPath.c_str(), nullptr);
    if (!outputFormat) {
        return false;
    }

    // Allocate format context
    int ret = avformat_alloc_output_context2(&m_formatCtx, outputFormat, nullptr, settings.outputPath.c_str());
    if (ret < 0 || !m_formatCtx) {
        return false;
    }

    // Find encoder
    AVCodecID codecId = AV_CODEC_ID_H264;
    if (settings.codec == "prores_ks" || settings.codec == "prores") {
        codecId = AV_CODEC_ID_PRORES;
    }

    const AVCodec* codec = avcodec_find_encoder(codecId);
    if (!codec) {
        avformat_free_context(m_formatCtx);
        m_formatCtx = nullptr;
        return false;
    }

    // Create video stream
    m_videoStream = avformat_new_stream(m_formatCtx, nullptr);
    if (!m_videoStream) {
        avformat_free_context(m_formatCtx);
        m_formatCtx = nullptr;
        return false;
    }
    m_videoStream->id = 0;

    // Allocate codec context
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        avformat_free_context(m_formatCtx);
        m_formatCtx = nullptr;
        return false;
    }

    // Set codec parameters
    m_codecCtx->width = width;
    m_codecCtx->height = height;
    m_codecCtx->time_base = AVRational{1, static_cast<int>(fps * 1000)};
    m_codecCtx->framerate = AVRational{static_cast<int>(fps * 1000), 1000};
    
    if (codecId == AV_CODEC_ID_PRORES) {
        m_codecCtx->pix_fmt = AV_PIX_FMT_YUV422P10LE;
        av_opt_set_int(m_codecCtx->priv_data, "profile", settings.proresProfile, 0);
    } else {
        m_codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
        m_codecCtx->bit_rate = settings.bitrate;
        m_codecCtx->gop_size = static_cast<int>(fps);  // One keyframe per second
        m_codecCtx->max_b_frames = 2;
        
        if (settings.codec == "libx264") {
            av_opt_set(m_codecCtx->priv_data, "preset", settings.preset.c_str(), 0);
            av_opt_set(m_codecCtx->priv_data, "tune", "film", 0);
        }
    }

    // Global header flag
    if (m_formatCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        m_codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // Open codec
    ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        avcodec_free_context(&m_codecCtx);
        avformat_free_context(m_formatCtx);
        m_formatCtx = nullptr;
        m_codecCtx = nullptr;
        return false;
    }

    // Copy codec params to stream
    avcodec_parameters_from_context(m_videoStream->codecpar, m_codecCtx);
    m_videoStream->time_base = m_codecCtx->time_base;

    // Open output file
    if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_formatCtx->pb, settings.outputPath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            avcodec_free_context(&m_codecCtx);
            avformat_free_context(m_formatCtx);
            m_formatCtx = nullptr;
            m_codecCtx = nullptr;
            return false;
        }
    }

    // Write header
    ret = avformat_write_header(m_formatCtx, nullptr);
    if (ret < 0) {
        if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&m_formatCtx->pb);
        }
        avcodec_free_context(&m_codecCtx);
        avformat_free_context(m_formatCtx);
        m_formatCtx = nullptr;
        m_codecCtx = nullptr;
        return false;
    }

    // Allocate frame
    m_frame = av_frame_alloc();
    m_frame->format = m_codecCtx->pix_fmt;
    m_frame->width = width;
    m_frame->height = height;
    av_frame_get_buffer(m_frame, 0);

    // Create swscale context for RGBA -> YUV conversion
    m_swsCtx = sws_getContext(
        width, height, AV_PIX_FMT_RGBA,
        width, height, m_codecCtx->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    return m_swsCtx != nullptr;
}

bool VideoEncoder::SubmitFrame(const std::vector<uint8_t>& rgbaData, int width, int height) {
    if (!m_recording.load()) return false;

    std::unique_lock<std::mutex> lock(m_queueMutex);

    // Drop frames if queue is full
    if (m_frameQueue.size() >= ENCODER_QUEUE_SIZE) {
        m_framesDropped++;
        return false;
    }

    QueuedFrame qf;
    qf.data = rgbaData;
    qf.width = width;
    qf.height = height;
    m_frameQueue.push(std::move(qf));

    lock.unlock();
    m_queueCV.notify_one();

    return true;
}

void VideoEncoder::EncoderThread() {
    while (true) {
        QueuedFrame qf;

        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCV.wait(lock, [this] {
                return !m_frameQueue.empty() || m_stopRequested.load();
            });

            if (m_stopRequested.load() && m_frameQueue.empty()) {
                break;
            }

            if (!m_frameQueue.empty()) {
                qf = std::move(m_frameQueue.front());
                m_frameQueue.pop();
            } else {
                continue;
            }
        }

        // Convert RGBA to encoder pixel format
        const uint8_t* srcData[4] = { qf.data.data(), nullptr, nullptr, nullptr };
        int srcLinesize[4] = { qf.width * 4, 0, 0, 0 };

        av_frame_make_writable(m_frame);

        sws_scale(
            m_swsCtx,
            srcData, srcLinesize,
            0, qf.height,
            m_frame->data, m_frame->linesize
        );

        m_frame->pts = m_frameIndex++;

        if (EncodeFrame(m_frame)) {
            m_framesEncoded++;
        }
    }
}

bool VideoEncoder::EncodeFrame(AVFrame* frame) {
    int ret = avcodec_send_frame(m_codecCtx, frame);
    if (ret < 0) return false;

    while (ret >= 0) {
        ret = avcodec_receive_packet(m_codecCtx, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) return false;

        // Rescale timestamps
        av_packet_rescale_ts(m_packet, m_codecCtx->time_base, m_videoStream->time_base);
        m_packet->stream_index = m_videoStream->index;

        ret = av_interleaved_write_frame(m_formatCtx, m_packet);
        av_packet_unref(m_packet);
        
        if (ret < 0) return false;
    }

    return true;
}

void VideoEncoder::FlushEncoder() {
    if (!m_codecCtx) return;

    // Send flush signal
    avcodec_send_frame(m_codecCtx, nullptr);

    // Receive remaining packets
    while (true) {
        int ret = avcodec_receive_packet(m_codecCtx, m_packet);
        if (ret == AVERROR_EOF || ret < 0) break;

        av_packet_rescale_ts(m_packet, m_codecCtx->time_base, m_videoStream->time_base);
        m_packet->stream_index = m_videoStream->index;
        av_interleaved_write_frame(m_formatCtx, m_packet);
        av_packet_unref(m_packet);
    }
}

double VideoEncoder::GetEncodingFPS() const {
    if (!m_recording.load()) return 0.0;
    
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - m_startTime).count();
    
    if (elapsed <= 0.0) return 0.0;
    return static_cast<double>(m_framesEncoded.load()) / elapsed;
}

} // namespace SP
