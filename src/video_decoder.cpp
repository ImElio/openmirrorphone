#include "video_decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

#include <cstdio>

VideoDecoder::~VideoDecoder() {
    {
        std::lock_guard<std::mutex> lock(codec_mutex_);
        close_locked();
    }
    av_frame_free(&latest_);
}

void VideoDecoder::close_locked() {
    avcodec_free_context(&ctx_);
    av_packet_free(&pkt_);
    av_frame_free(&decoded_);
}

bool VideoDecoder::open_locked(bool h265) {
    close_locked();
    h265_ = h265;
    const AVCodec *codec = avcodec_find_decoder(h265 ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
    if (!codec) return false;
    ctx_ = avcodec_alloc_context3(codec);
    ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    ctx_->thread_count = 0;              // let FFmpeg pick
    ctx_->thread_type = FF_THREAD_SLICE; // frame threading adds a frame of latency per thread
    if (avcodec_open2(ctx_, codec, nullptr) < 0) {
        avcodec_free_context(&ctx_);
        return false;
    }
    pkt_ = av_packet_alloc();
    decoded_ = av_frame_alloc();
    std::fprintf(stderr, "[video] decoder ready: %s\n", h265 ? "H.265" : "H.264");
    return true;
}

bool VideoDecoder::set_codec(bool h265) {
    std::lock_guard<std::mutex> lock(codec_mutex_);
    return open_locked(h265);
}

void VideoDecoder::reset() {
    {
        std::lock_guard<std::mutex> lock(codec_mutex_);
        if (ctx_) avcodec_flush_buffers(ctx_);
    }
    std::lock_guard<std::mutex> lock(frame_mutex_);
    av_frame_free(&latest_);
    fresh_ = false;
}

void VideoDecoder::decode(const uint8_t *data, int size) {
    std::lock_guard<std::mutex> lock(codec_mutex_);
    if (!ctx_ && !open_locked(h265_)) return;
    pkt_->data = const_cast<uint8_t *>(data);
    pkt_->size = size;
    if (avcodec_send_packet(ctx_, pkt_) < 0) return;
    while (avcodec_receive_frame(ctx_, decoded_) == 0) {
        std::lock_guard<std::mutex> flock(frame_mutex_);
        if (!latest_) latest_ = av_frame_alloc();
        av_frame_unref(latest_);
        av_frame_move_ref(latest_, decoded_);
        fresh_ = true;
    }
}

bool VideoDecoder::take_frame(AVFrame *out) {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (!fresh_ || !latest_) return false;
    av_frame_unref(out);
    av_frame_move_ref(out, latest_);
    fresh_ = false;
    return true;
}
