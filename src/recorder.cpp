#include "recorder.h"

#include "audio_player.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cstdio>

namespace {
constexpr int kSampleRate = 44100;
constexpr int kChannels = 2;
constexpr size_t kMaxCacheBytes = 32u << 20;
// Audio packets carry exact RTP-derived times, so anything past this is a real
// gap (a lost packet, the sender pausing audio) or an overlap, not jitter.
constexpr int64_t kAudioTolerance = kSampleRate / 100; // 10 ms, about one AAC-ELD packet
const AVRational kNanoseconds{1, 1000000000};

// Calls fn(type, nal, len) for each NAL unit of an Annex-B buffer; `nal`
// starts at its start code (00 00 01 or 00 00 00 01).
template <typename Fn> void for_each_nal(const uint8_t *data, int size, Fn fn) {
    auto find_start = [&](int from) {
        for (int i = from; i + 3 <= size; ++i) {
            if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) return i > from && data[i - 1] == 0 ? i - 1 : i;
        }
        return size;
    };
    int nal = find_start(0);
    while (nal < size) {
        int header = nal + (data[nal + 2] == 1 ? 3 : 4);
        int next = header < size ? find_start(header) : size;
        if (header < size) fn(data[header] & 0x1f, data + nal, next - nal);
        nal = next;
    }
}

// Picture size from the SPS, via FFmpeg's H.264 parser (it needs a slice too).
bool h264_size(const std::vector<uint8_t> &params, const std::vector<uint8_t> &idr, int *w, int *h) {
    std::vector<uint8_t> buf(params);
    buf.insert(buf.end(), idr.begin(), idr.end());
    size_t size = buf.size();
    buf.resize(size + AV_INPUT_BUFFER_PADDING_SIZE, 0);
    AVCodecContext *ctx = avcodec_alloc_context3(nullptr);
    AVCodecParserContext *parser = av_parser_init(AV_CODEC_ID_H264);
    if (!ctx || !parser) {
        avcodec_free_context(&ctx);
        if (parser) av_parser_close(parser);
        return false;
    }
    parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
    uint8_t *out = nullptr;
    int out_size = 0;
    av_parser_parse2(parser, ctx, &out, &out_size, buf.data(), static_cast<int>(size), AV_NOPTS_VALUE, AV_NOPTS_VALUE,
                     0);
    *w = parser->width;
    *h = parser->height;
    av_parser_close(parser);
    avcodec_free_context(&ctx);
    return *w > 0 && *h > 0;
}

void log_error(const char *what, int err) {
    char msg[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, msg, sizeof msg);
    std::fprintf(stderr, "[rec] %s: %s\n", what, msg);
}
} // namespace

Recorder::~Recorder() {
    stop();
    std::lock_guard<std::mutex> lock(mutex_);
    avcodec_free_context(&dec_);
    swr_free(&swr_);
    av_frame_free(&in_frame_);
    av_packet_free(&dec_pkt_);
}

void Recorder::start(const std::string &path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != State::Off) return;
    path_ = path;
    duration_ms_ = 0;
    state_ = State::WaitingForKeyframe;
    if (gop_valid_ && !params_.empty()) {
        if (!open_locked()) state_ = State::Off;
    } else {
        std::fprintf(stderr, "[rec] waiting for a keyframe from the sender\n");
    }
}

void Recorder::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fmt_) {
        close_locked();
    } else if (state_ != State::Off) {
        std::fprintf(stderr, "[rec] stopped before any video arrived, nothing saved\n");
    }
    state_ = State::Off;
}

void Recorder::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    gop_.clear();
    gop_bytes_ = 0;
    gop_valid_ = false;
}

void Recorder::video(const uint8_t *data, int size, uint64_t ntp_ns, bool h265) {
    if (h265) {
        if (!warned_h265_) std::fprintf(stderr, "[rec] H.265 streams are not recorded\n");
        warned_h265_ = true;
        return;
    }
    // The core marks packets it could not decrypt by overwriting the start code.
    if (size < 5 || data[0] != 0 || data[1] != 0 || data[2] != 0 || data[3] != 1) return;

    std::lock_guard<std::mutex> lock(mutex_);
    bool idr = false;
    std::vector<uint8_t> params;
    for_each_nal(data, size, [&](int type, const uint8_t *nal, int len) {
        if (type == 5) idr = true;
        if (type == 7 || type == 8) params.insert(params.end(), nal, nal + len);
    });
    if (!params.empty()) params_ = std::move(params);

    if (idr) {
        gop_.clear();
        gop_bytes_ = 0;
        gop_valid_ = true;
    }
    if (gop_valid_) {
        gop_.push_back({std::vector<uint8_t>(data, data + size), ntp_ns, idr});
        gop_bytes_ += static_cast<size_t>(size);
        if (gop_bytes_ > kMaxCacheBytes) { // keyframes are too far apart to keep a whole GOP
            gop_.clear();
            gop_bytes_ = 0;
            gop_valid_ = false;
        }
    }

    if (fmt_) {
        write_video_locked(data, size, ntp_ns, idr);
    } else if (state_ == State::WaitingForKeyframe && idr && !params_.empty()) {
        if (!open_locked()) state_ = State::Off; // writes the cache, which is just this packet
    }
}

bool Recorder::open_locked() {
    int w = 0, h = 0;
    if (!h264_size(params_, gop_.front().data, &w, &h)) {
        std::fprintf(stderr, "[rec] cannot read the picture size from the SPS\n");
        return false;
    }

    int err = avformat_alloc_output_context2(&fmt_, nullptr, "mp4", path_.c_str());
    if (err < 0) {
        log_error("cannot create the MP4 muxer", err);
        return false;
    }

    vst_ = avformat_new_stream(fmt_, nullptr);
    vst_->time_base = {1, 90000};
    AVCodecParameters *vp = vst_->codecpar;
    vp->codec_type = AVMEDIA_TYPE_VIDEO;
    vp->codec_id = AV_CODEC_ID_H264;
    vp->width = w;
    vp->height = h;
    // Annex-B SPS + PPS: the MP4 muxer turns them into the avcC box, and the
    // Annex-B packets into length-prefixed samples.
    vp->extradata = static_cast<uint8_t *>(av_mallocz(params_.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    std::copy(params_.begin(), params_.end(), vp->extradata);
    vp->extradata_size = static_cast<int>(params_.size());

    const AVCodec *aac = avcodec_find_encoder(AV_CODEC_ID_AAC);
    enc_ = aac ? avcodec_alloc_context3(aac) : nullptr;
    if (!enc_) {
        std::fprintf(stderr, "[rec] no AAC encoder\n");
        close_locked();
        return false;
    }
    enc_->sample_fmt = AV_SAMPLE_FMT_FLTP; // the native encoder's only format
    enc_->sample_rate = kSampleRate;
    av_channel_layout_default(&enc_->ch_layout, kChannels);
    enc_->bit_rate = 160000;
    enc_->time_base = {1, kSampleRate};
    if (fmt_->oformat->flags & AVFMT_GLOBALHEADER) enc_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if ((err = avcodec_open2(enc_, aac, nullptr)) < 0) {
        log_error("cannot open the AAC encoder", err);
        close_locked();
        return false;
    }
    ast_ = avformat_new_stream(fmt_, nullptr);
    ast_->time_base = enc_->time_base;
    avcodec_parameters_from_context(ast_->codecpar, enc_);

    if ((err = avio_open(&fmt_->pb, path_.c_str(), AVIO_FLAG_WRITE)) < 0) {
        log_error(("cannot create " + path_).c_str(), err);
        close_locked();
        return false;
    }
    if ((err = avformat_write_header(fmt_, nullptr)) < 0) {
        log_error("cannot write the MP4 header", err);
        close_locked();
        return false;
    }

    fifo_ = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, kChannels, enc_->frame_size * 4);
    enc_frame_ = av_frame_alloc();
    enc_frame_->format = AV_SAMPLE_FMT_FLTP;
    enc_frame_->sample_rate = kSampleRate;
    enc_frame_->nb_samples = enc_->frame_size;
    av_channel_layout_default(&enc_frame_->ch_layout, kChannels);
    av_frame_get_buffer(enc_frame_, 0);
    pkt_ = av_packet_alloc();
    next_sample_ = -1;
    enc_pts_ = 0;
    last_vpts_ = -1;
    write_failed_ = false;

    base_ns_ = gop_.front().ntp_ns;
    for (const CachedPacket &p : gop_) {
        write_video_locked(p.data.data(), static_cast<int>(p.data.size()), p.ntp_ns, p.key);
    }
    state_ = State::Recording;
    std::fprintf(stderr, "[rec] recording %dx%d to %s\n", w, h, path_.c_str());
    return true;
}

void Recorder::close_locked() {
    if (fifo_) { // the header was written
        // Pad the last partial frame with silence, then drain the encoder.
        int left = av_audio_fifo_size(fifo_);
        if (left > 0) {
            std::vector<float> silence(static_cast<size_t>(enc_->frame_size - left), 0.0f);
            const float *planes[kChannels] = {silence.data(), silence.data()};
            queue_audio_locked(planes, static_cast<int>(silence.size()));
        }
        encode_audio_locked(nullptr);
        int err = av_write_trailer(fmt_);
        if (err < 0) log_error("cannot finish the MP4 file", err);
        std::fprintf(stderr, "[rec] saved %s (%.1f s)\n", path_.c_str(), seconds());
    }
    if (fmt_ && fmt_->pb) avio_closep(&fmt_->pb);
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    vst_ = ast_ = nullptr;
    avcodec_free_context(&enc_);
    av_audio_fifo_free(fifo_);
    fifo_ = nullptr;
    av_frame_free(&enc_frame_);
    av_packet_free(&pkt_);
}

void Recorder::write_video_locked(const uint8_t *data, int size, uint64_t ntp_ns, bool key) {
    int64_t pts = av_rescale_q(static_cast<int64_t>(ntp_ns - base_ns_), kNanoseconds, vst_->time_base);
    if (pts <= last_vpts_) pts = last_vpts_ + 1; // the muxer needs strictly increasing times
    last_vpts_ = pts;
    duration_ms_ = av_rescale_q(pts, vst_->time_base, {1, 1000});

    av_packet_unref(pkt_);
    pkt_->data = const_cast<uint8_t *>(data); // not ref-counted: the muxer copies it
    pkt_->size = size;
    pkt_->pts = pkt_->dts = pts; // mirroring streams have no B-frames
    pkt_->flags = key ? AV_PKT_FLAG_KEY : 0;
    pkt_->stream_index = vst_->index;
    int err = av_interleaved_write_frame(fmt_, pkt_);
    if (err < 0 && !write_failed_) {
        log_error("cannot write video", err);
        write_failed_ = true;
    }
}

bool Recorder::open_audio_decoder_locked(unsigned char ct) {
    avcodec_free_context(&dec_);
    swr_free(&swr_);
    dec_ct_ = 0;
    int id = 0;
    const uint8_t *config = nullptr;
    size_t config_size = 0;
    if (!airplay_audio_codec(ct, &id, &config, &config_size)) return false;
    const AVCodec *codec = avcodec_find_decoder(static_cast<AVCodecID>(id));
    if (!codec) return false;
    dec_ = avcodec_alloc_context3(codec);
    dec_->sample_rate = kSampleRate;
    av_channel_layout_default(&dec_->ch_layout, kChannels);
    dec_->extradata = static_cast<uint8_t *>(av_mallocz(config_size + AV_INPUT_BUFFER_PADDING_SIZE));
    std::copy(config, config + config_size, dec_->extradata);
    dec_->extradata_size = static_cast<int>(config_size);
    if (avcodec_open2(dec_, codec, nullptr) < 0) {
        avcodec_free_context(&dec_);
        return false;
    }
    if (!in_frame_) in_frame_ = av_frame_alloc();
    if (!dec_pkt_) dec_pkt_ = av_packet_alloc();
    dec_ct_ = ct;
    return true;
}

void Recorder::audio(unsigned char ct, const uint8_t *data, int size, uint64_t ntp_ns) {
    if (size <= 0 || ntp_ns == 0) return; // 0: the core has no clock sync yet
    std::lock_guard<std::mutex> lock(mutex_);
    if (!fmt_) return;
    if (ct != dec_ct_ && !open_audio_decoder_locked(ct)) return;

    dec_pkt_->data = const_cast<uint8_t *>(data);
    dec_pkt_->size = size;
    if (avcodec_send_packet(dec_, dec_pkt_) < 0) return;
    int64_t start = av_rescale_q(static_cast<int64_t>(ntp_ns - base_ns_), kNanoseconds, {1, kSampleRate});
    while (avcodec_receive_frame(dec_, in_frame_) == 0) {
        if (!swr_) {
            AVChannelLayout out_layout;
            av_channel_layout_default(&out_layout, kChannels);
            swr_alloc_set_opts2(&swr_, &out_layout, AV_SAMPLE_FMT_FLTP, kSampleRate, &in_frame_->ch_layout,
                                static_cast<AVSampleFormat>(in_frame_->format), in_frame_->sample_rate, 0, nullptr);
            if (!swr_ || swr_init(swr_) < 0) {
                swr_free(&swr_);
                return;
            }
        }
        int max_out = swr_get_out_samples(swr_, in_frame_->nb_samples);
        for (auto &p : pcm_) p.resize(static_cast<size_t>(max_out));
        uint8_t *out[kChannels] = {reinterpret_cast<uint8_t *>(pcm_[0].data()),
                                   reinterpret_cast<uint8_t *>(pcm_[1].data())};
        int got = swr_convert(swr_, out, max_out, const_cast<const uint8_t **>(in_frame_->extended_data),
                              in_frame_->nb_samples);
        if (got <= 0) continue;
        const float *planes[kChannels] = {pcm_[0].data(), pcm_[1].data()};
        push_audio_locked(planes, got, start);
        start += got;
    }
}

// Places decoded samples at file position `start`, filling gaps with silence
// so the audio track stays continuous and lined up with the video.
void Recorder::push_audio_locked(const float *const *planes, int samples, int64_t start) {
    int64_t tolerance = kAudioTolerance;
    if (next_sample_ < 0) { // first audio of the file: line it up exactly
        if (start + samples <= 0) return; // from before the first video frame
        next_sample_ = 0;
        tolerance = 0;
    }
    int64_t gap = start - next_sample_;
    if (gap > tolerance) {
        std::vector<float> silence(static_cast<size_t>(std::min<int64_t>(gap, kSampleRate)), 0.0f);
        const float *zero[kChannels] = {silence.data(), silence.data()};
        while (gap > 0) {
            int n = static_cast<int>(std::min<int64_t>(gap, static_cast<int64_t>(silence.size())));
            queue_audio_locked(zero, n);
            gap -= n;
        }
    } else if (gap < -tolerance) { // overlaps what is already queued: drop the overlap
        int64_t skip = -gap;
        if (skip >= samples) return;
        const float *rest[kChannels] = {planes[0] + skip, planes[1] + skip};
        queue_audio_locked(rest, samples - static_cast<int>(skip));
        return;
    }
    queue_audio_locked(planes, samples);
}

void Recorder::queue_audio_locked(const float *const *planes, int samples) {
    av_audio_fifo_write(fifo_, reinterpret_cast<void *const *>(const_cast<float *const *>(planes)), samples);
    next_sample_ += samples;
    while (av_audio_fifo_size(fifo_) >= enc_->frame_size) {
        if (av_frame_make_writable(enc_frame_) < 0) return;
        av_audio_fifo_read(fifo_, reinterpret_cast<void **>(enc_frame_->data), enc_->frame_size);
        enc_frame_->pts = enc_pts_;
        enc_pts_ += enc_->frame_size;
        encode_audio_locked(enc_frame_);
    }
}

void Recorder::encode_audio_locked(AVFrame *frame) {
    if (avcodec_send_frame(enc_, frame) < 0) return;
    while (avcodec_receive_packet(enc_, pkt_) == 0) {
        av_packet_rescale_ts(pkt_, enc_->time_base, ast_->time_base);
        pkt_->stream_index = ast_->index;
        int err = av_interleaved_write_frame(fmt_, pkt_);
        if (err < 0 && !write_failed_) {
            log_error("cannot write audio", err);
            write_failed_ = true;
        }
    }
}
