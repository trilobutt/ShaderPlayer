#include "VideoEncoder.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace SP {

namespace {

// The audio queue is bounded like the video one, but a dropped audio chunk is worse than a
// dropped frame: the PTS counter walks over what was actually encoded, so a gap does not
// leave a hole, it pulls everything after it earlier and the track drifts out of sync for
// the rest of the file. A full queue therefore waits for the encoder thread rather than
// dropping on the spot, and only gives up after this long. Audio encoding is microseconds
// against a frame's milliseconds, so the wait is there for a stall, not for steady state.
constexpr int  kAudioQueueMax  = ENCODER_QUEUE_SIZE * 8;
constexpr auto kAudioQueueWait = std::chrono::milliseconds(200);

// What one call to the audio encoder takes when the codec does not fix it itself (PCM does
// not; AAC does, at 1024).
constexpr int kDefaultAudioFrameSize = 1024;

constexpr int kAudioChannels = 2;   // see VideoDecoder::SetRecordingTap

}  // namespace

VideoEncoder::VideoEncoder() {
    m_packet = av_packet_alloc();
    if (!m_packet) {
        throw std::runtime_error("Failed to allocate AVPacket");
    }
}

VideoEncoder::~VideoEncoder() {
    // Signal any running encoder thread to stop.
    if (m_recording.load()) {
        m_recording = false;
        m_stopRequested = true;
        m_queueCV.notify_all();
    }
    // Join before freeing m_packet: the encoder thread uses m_packet in
    // EncodeFrame/FlushEncoder, so we must not free it until the thread exits.
    if (m_encoderThread.joinable()) {
        m_encoderThread.join();
    }
    av_packet_free(&m_packet);
    av_packet_free(&m_audioPacket);
}

bool VideoEncoder::StartRecording(const RecordingSettings& settings, int sourceWidth,
                                  int sourceHeight, double sourceFPS, int audioSampleRate) {
    if (m_recording.load()) {
        return false;  // Already recording
    }

    // A previous recording may still be finalising (draining queue, flushing
    // codec, closing file) on the encoder thread. Join before touching any
    // shared state (m_stopRequested, m_frameIndex, FFmpeg resource pointers).
    if (m_encoderThread.joinable()) {
        m_encoderThread.join();
    }

    // Use source dimensions/fps if not specified
    int width = settings.width > 0 ? settings.width : sourceWidth;
    int height = settings.height > 0 ? settings.height : sourceHeight;
    double fps = settings.fps > 0 ? static_cast<double>(settings.fps) : sourceFPS;

    m_audioSrcRate = audioSampleRate;
    if (!InitEncoder(settings, width, height, fps)) {
        return false;
    }

    m_audioPts = 0;
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

    // Signal the encoder thread to drain its queue and exit. The thread itself
    // handles flush, file close, and resource free — so this call returns
    // immediately and does not block the main thread.
    m_recording = false;
    m_stopRequested = true;
    m_queueCV.notify_all();
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

    // By name first, then by ID. RecordingSettings::codec names the encoder the settings
    // were written against, and for ProRes the two are not the same encoder: the default
    // for AV_CODEC_ID_PRORES is `prores`, which has no `profile` option at all, so the
    // panel's Profile combo set an option that silently did not exist and every ProRes
    // recording came out Standard whatever it said.
    const AVCodec* codec = avcodec_find_encoder_by_name(settings.codec.c_str());
    if (!codec) codec = avcodec_find_encoder(codecId);
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
        // 0 proxy, 1 LT, 2 standard, 3 HQ, matching both the combo and prores_ks's own
        // option values. Absent on the fallback `prores` encoder, where it does nothing.
        av_opt_set_int(m_codecCtx->priv_data, "profile", settings.proresProfile, 0);
    } else {
        m_codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
        m_codecCtx->bit_rate = settings.bitrate;
        m_codecCtx->gop_size = static_cast<int>(fps);  // One keyframe per second
        m_codecCtx->max_b_frames = 0;  // No B-frames: each frame is self-contained, sws_scale destination is never cloned
        
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

    // Audio, before the header: a stream added after avformat_write_header is not in the
    // file. AAC beside H.264 in MP4, uncompressed beside ProRes, which is what a ProRes
    // deliverable is for.
    if (m_audioSrcRate > 0) {
        InitAudioStream(codecId == AV_CODEC_ID_PRORES ? AV_CODEC_ID_PCM_S16LE
                                                      : AV_CODEC_ID_AAC,
                        m_audioSrcRate);
    }

    // Open output file
    if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_formatCtx->pb, settings.outputPath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            // The audio stream itself belongs to the format context and goes with it.
            FreeAudioResources();
            m_audioStream = nullptr;
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
        FreeAudioResources();
        m_audioStream = nullptr;
        avcodec_free_context(&m_codecCtx);
        avformat_free_context(m_formatCtx);
        m_formatCtx = nullptr;
        m_codecCtx = nullptr;
        return false;
    }

    // Destination (encoder) frame
    m_frame = av_frame_alloc();
    if (!m_frame) return false;
    m_frame->format = m_codecCtx->pix_fmt;
    m_frame->width  = width;
    m_frame->height = height;
    if (av_frame_get_buffer(m_frame, 0) < 0) return false;

    // Source (RGBA) frame — FFmpeg allocates this with AV_INPUT_BUFFER_PADDING_SIZE
    // extra bytes per plane, guaranteeing swscale's SIMD/chroma read-ahead can't
    // stray into an unmapped page regardless of dimension alignment.
    m_srcFrame = av_frame_alloc();
    if (!m_srcFrame) return false;
    m_srcFrame->format = AV_PIX_FMT_RGBA;
    m_srcFrame->width  = width;
    m_srcFrame->height = height;
    if (av_frame_get_buffer(m_srcFrame, 32) < 0) return false;

    // Create swscale context for RGBA -> YUV conversion
    m_swsCtx = sws_getContext(
        width, height, AV_PIX_FMT_RGBA,
        width, height, m_codecCtx->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    return m_swsCtx != nullptr;
}

void VideoEncoder::InitAudioStream(AVCodecID codecId, int sampleRate) {
    const AVCodec* codec = avcodec_find_encoder(codecId);
    if (!codec) return;

    m_audioCodecCtx = avcodec_alloc_context3(codec);
    if (!m_audioCodecCtx) return;

    // The rate the encoder will actually run at. Most take anything, but AAC publishes a
    // list, and picking off it here is what lets the resampler below do the conversion
    // instead of avcodec_open2 refusing the whole stream.
    int rate = sampleRate;
    const int* rates = nullptr;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_RATE, 0,
                                     reinterpret_cast<const void**>(&rates), nullptr) >= 0
        && rates) {
        bool exact = false;
        int best = rates[0];
        for (int i = 0; rates[i]; ++i) {
            if (rates[i] == sampleRate) { exact = true; break; }
            if (std::abs(rates[i] - sampleRate) < std::abs(best - sampleRate)) best = rates[i];
        }
        if (!exact) rate = best;
    }

    enum AVSampleFormat fmt = AV_SAMPLE_FMT_FLTP;
    const enum AVSampleFormat* fmts = nullptr;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                                     reinterpret_cast<const void**>(&fmts), nullptr) >= 0
        && fmts) {
        fmt = fmts[0];
    }

    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    av_channel_layout_copy(&m_audioCodecCtx->ch_layout, &stereo);
    m_audioCodecCtx->sample_fmt  = fmt;
    m_audioCodecCtx->sample_rate = rate;
    m_audioCodecCtx->bit_rate    = 192000;   // ignored by PCM
    m_audioCodecCtx->time_base   = AVRational{1, rate};

    if (m_formatCtx->oformat->flags & AVFMT_GLOBALHEADER)
        m_audioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(m_audioCodecCtx, codec, nullptr) < 0) {
        FreeAudioResources();
        return;
    }

    m_audioFrameSize = m_audioCodecCtx->frame_size > 0 ? m_audioCodecCtx->frame_size
                                                       : kDefaultAudioFrameSize;

    // Packed stereo float in, whatever the codec asked for out.
    if (swr_alloc_set_opts2(&m_audioSwr,
            &m_audioCodecCtx->ch_layout, fmt,                rate,
            &stereo,                     AV_SAMPLE_FMT_FLT,  sampleRate,
            0, nullptr) < 0 || !m_audioSwr || swr_init(m_audioSwr) < 0) {
        FreeAudioResources();
        return;
    }

    m_audioFifo = av_audio_fifo_alloc(fmt, kAudioChannels, m_audioFrameSize * 4);
    m_audioFrame = av_frame_alloc();
    if (!m_audioPacket) m_audioPacket = av_packet_alloc();
    if (!m_audioFifo || !m_audioFrame || !m_audioPacket) {
        FreeAudioResources();
        return;
    }

    m_audioFrame->format      = fmt;
    m_audioFrame->sample_rate = rate;
    m_audioFrame->nb_samples  = m_audioFrameSize;
    av_channel_layout_copy(&m_audioFrame->ch_layout, &m_audioCodecCtx->ch_layout);
    if (av_frame_get_buffer(m_audioFrame, 0) < 0) {
        FreeAudioResources();
        return;
    }

    m_audioStream = avformat_new_stream(m_formatCtx, nullptr);
    if (!m_audioStream) {
        FreeAudioResources();
        return;
    }
    m_audioStream->id = 1;
    avcodec_parameters_from_context(m_audioStream->codecpar, m_audioCodecCtx);
    m_audioStream->time_base = m_audioCodecCtx->time_base;
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

bool VideoEncoder::SubmitAudio(const float* interleavedStereo, int floatCount) {
    if (!m_recording.load() || !m_audioStream || floatCount <= 0) return false;

    std::unique_lock<std::mutex> lock(m_queueMutex);

    // See kAudioQueueWait: wait for room rather than dropping, because a dropped chunk
    // desyncs the rest of the track instead of leaving a gap.
    if (m_audioQueue.size() >= kAudioQueueMax) {
        m_queueCV.wait_for(lock, kAudioQueueWait,
                           [this] { return m_audioQueue.size() < kAudioQueueMax; });
        if (m_audioQueue.size() >= kAudioQueueMax) {
            m_framesDropped++;
            return false;
        }
    }

    m_audioQueue.emplace(interleavedStereo, interleavedStereo + floatCount);

    lock.unlock();
    m_queueCV.notify_one();

    return true;
}

void VideoEncoder::EncoderThread() {
    while (true) {
        QueuedFrame qf;
        std::vector<float> audio;
        bool haveVideo = false;

        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCV.wait(lock, [this] {
                return !m_frameQueue.empty() || !m_audioQueue.empty() || m_stopRequested.load();
            });

            if (m_stopRequested.load() && m_frameQueue.empty() && m_audioQueue.empty()) {
                break;
            }

            // One of each per pass. Taking audio first keeps its packets ahead of the
            // video frame they belong with, which is the order the muxer wants to see.
            if (!m_audioQueue.empty()) {
                audio = std::move(m_audioQueue.front());
                m_audioQueue.pop();
            }
            if (!m_frameQueue.empty()) {
                qf = std::move(m_frameQueue.front());
                m_frameQueue.pop();
                haveVideo = true;
            }
            if (!haveVideo && audio.empty()) {
                continue;
            }
        }

        // A producer may be waiting on room in the audio queue.
        m_queueCV.notify_all();

        if (!audio.empty()) {
            WriteAudio(audio.data(), audio.size());
        }
        if (!haveVideo) {
            continue;
        }

        // Copy pixel data into the FFmpeg-managed source frame row by row.
        // m_srcFrame was allocated with av_frame_get_buffer which adds
        // AV_INPUT_BUFFER_PADDING_SIZE (64 bytes) beyond the last row, so
        // swscale's chroma read-ahead and SIMD overshoot can't reach an
        // unmapped page — regardless of width/height alignment.
        const int srcRowBytes = qf.width * 4;
        for (int y = 0; y < qf.height; ++y) {
            memcpy(m_srcFrame->data[0] + y * m_srcFrame->linesize[0],
                   qf.data.data() + y * srcRowBytes,
                   srcRowBytes);
        }

        sws_scale(
            m_swsCtx,
            m_srcFrame->data, m_srcFrame->linesize,
            0, qf.height,
            m_frame->data, m_frame->linesize
        );

        // time_base = {1, fps*1000}, so one frame = 1000 time_base units
        m_frame->pts = static_cast<int64_t>(m_frameIndex++) * 1000LL;

        if (EncodeFrame(m_frame)) {
            m_framesEncoded++;
        }
    }

    // Queue drained. Flush buffered frames out of the codec, write the file
    // trailer, and free all FFmpeg resources. This runs on the encoder thread
    // so the main thread is never blocked by these operations.
    FlushEncoder();
    FlushAudioEncoder();

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
    if (m_srcFrame) {
        av_frame_free(&m_srcFrame);
        m_srcFrame = nullptr;
    }
    FreeAudioResources();
    m_audioStream = nullptr;
    m_audioSrcRate = 0;
    m_videoStream = nullptr;
    // Reset stop flag last, after all work is done. StartRecording() joins this
    // thread before touching any shared state, so the reset is safe.
    m_stopRequested = false;
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

void VideoEncoder::FreeAudioResources() {
    if (m_audioCodecCtx) avcodec_free_context(&m_audioCodecCtx);
    if (m_audioSwr)      swr_free(&m_audioSwr);
    if (m_audioFifo) {
        av_audio_fifo_free(m_audioFifo);
        m_audioFifo = nullptr;
    }
    if (m_audioFrame) av_frame_free(&m_audioFrame);
}

void VideoEncoder::WriteAudio(const float* interleavedStereo, size_t floatCount) {
    if (!m_audioSwr || !m_audioFifo) return;

    const int inSamples = static_cast<int>(floatCount / kAudioChannels);
    if (inSamples <= 0) return;

    // swr_get_out_samples accounts for both the rate ratio and whatever the resampler is
    // still holding, so this is the whole of the conversion's output and not an estimate.
    const int outSamples = swr_get_out_samples(m_audioSwr, inSamples);
    if (outSamples <= 0) return;

    uint8_t** converted = nullptr;
    if (av_samples_alloc_array_and_samples(&converted, nullptr, kAudioChannels, outSamples,
                                           m_audioCodecCtx->sample_fmt, 0) < 0) {
        return;
    }

    const uint8_t* in = reinterpret_cast<const uint8_t*>(interleavedStereo);
    const int got = swr_convert(m_audioSwr, converted, outSamples, &in, inSamples);
    if (got > 0) {
        av_audio_fifo_write(m_audioFifo, reinterpret_cast<void**>(converted), got);
        DrainAudioFifo(false);
    }

    if (converted) av_freep(&converted[0]);
    av_freep(&converted);
}

void VideoEncoder::DrainAudioFifo(bool flush) {
    if (!m_audioFifo || !m_audioFrame) return;

    // Whole frames while the codec wants them; the short tail only at the end, where the
    // encoder pads it itself.
    while (av_audio_fifo_size(m_audioFifo) >= (flush ? 1 : m_audioFrameSize)) {
        const int take = std::min(av_audio_fifo_size(m_audioFifo), m_audioFrameSize);

        if (av_frame_make_writable(m_audioFrame) < 0) return;
        if (av_audio_fifo_read(m_audioFifo, reinterpret_cast<void**>(m_audioFrame->data),
                               take) < take) {
            return;
        }

        m_audioFrame->nb_samples = take;
        m_audioFrame->pts = m_audioPts;
        m_audioPts += take;

        EncodeAudioFrame(m_audioFrame);

        // av_frame_make_writable sizes its buffers from nb_samples, so put the full frame
        // size back before the next pass asks for a writable copy of a short one.
        m_audioFrame->nb_samples = m_audioFrameSize;
    }
}

bool VideoEncoder::EncodeAudioFrame(AVFrame* frame) {
    if (!m_audioCodecCtx || !m_audioStream) return false;

    int ret = avcodec_send_frame(m_audioCodecCtx, frame);
    if (ret < 0) return false;

    while (ret >= 0) {
        ret = avcodec_receive_packet(m_audioCodecCtx, m_audioPacket);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) return false;

        av_packet_rescale_ts(m_audioPacket, m_audioCodecCtx->time_base,
                             m_audioStream->time_base);
        m_audioPacket->stream_index = m_audioStream->index;

        ret = av_interleaved_write_frame(m_formatCtx, m_audioPacket);
        av_packet_unref(m_audioPacket);

        if (ret < 0) return false;
    }

    return true;
}

void VideoEncoder::FlushAudioEncoder() {
    if (!m_audioCodecCtx || !m_audioStream) return;

    // The resampler first: at a converted rate it holds a tail of samples that belong in
    // the file, and they are only released by a null input.
    if (m_audioSwr && m_audioFifo) {
        const int pending = swr_get_out_samples(m_audioSwr, 0);
        if (pending > 0) {
            uint8_t** converted = nullptr;
            if (av_samples_alloc_array_and_samples(&converted, nullptr, kAudioChannels,
                                                   pending, m_audioCodecCtx->sample_fmt,
                                                   0) >= 0) {
                const int got = swr_convert(m_audioSwr, converted, pending, nullptr, 0);
                if (got > 0)
                    av_audio_fifo_write(m_audioFifo, reinterpret_cast<void**>(converted), got);
                if (converted) av_freep(&converted[0]);
                av_freep(&converted);
            }
        }
    }

    DrainAudioFifo(true);

    avcodec_send_frame(m_audioCodecCtx, nullptr);
    while (true) {
        int ret = avcodec_receive_packet(m_audioCodecCtx, m_audioPacket);
        if (ret < 0) break;

        av_packet_rescale_ts(m_audioPacket, m_audioCodecCtx->time_base,
                             m_audioStream->time_base);
        m_audioPacket->stream_index = m_audioStream->index;
        av_interleaved_write_frame(m_formatCtx, m_audioPacket);
        av_packet_unref(m_audioPacket);
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
