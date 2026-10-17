#include "audio_player.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr int kSampleRate = 44100;
constexpr int kChannels = 2;
constexpr int kBytesPerSecond = kSampleRate * kChannels * 2;
constexpr uint32_t kMaxQueuedBytes = kBytesPerSecond / 4; // 250 ms

// ALAC "magic cookie" for 44100 Hz / 16 bit / stereo with 352 samples per frame,
// the only ALAC format AirPlay senders use.
const uint8_t kAlacCookie[] = {
    0x00, 0x00, 0x00, 0x24, 'a', 'l', 'a', 'c', 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x60, 0x00, 0x10, 0x28, 0x0a, 0x0e, 0x02, 0x00, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xac, 0x44,
};
// MPEG-4 AudioSpecificConfig for AAC-LC and AAC-ELD, 44100 Hz stereo.
const uint8_t kAacLcConfig[] = {0x12, 0x10};
const uint8_t kAacEldConfig[] = {0xf8, 0xe8, 0x50, 0x00};
} // namespace

bool airplay_audio_codec(unsigned char ct, int *codec_id, const uint8_t **config, size_t *config_size) {
    switch (ct) {
    case 2: *codec_id = AV_CODEC_ID_ALAC; *config = kAlacCookie; *config_size = sizeof kAlacCookie; return true;
    case 4: *codec_id = AV_CODEC_ID_AAC; *config = kAacLcConfig; *config_size = sizeof kAacLcConfig; return true;
    case 8: *codec_id = AV_CODEC_ID_AAC; *config = kAacEldConfig; *config_size = sizeof kAacEldConfig; return true;
    default: return false;
    }
}

AudioPlayer::AudioPlayer() = default;

AudioPlayer::~AudioPlayer() {
    if (device_) SDL_CloseAudioDevice(device_);
    std::lock_guard<std::mutex> lock(mutex_);
    close_decoder_locked();
}

bool AudioPlayer::open_device() {
    SDL_AudioSpec want{}, have{};
    want.freq = kSampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = kChannels;
    want.samples = 512;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!device_) {
        std::fprintf(stderr, "[audio] cannot open output device: %s\n", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void AudioPlayer::close_decoder_locked() {
    avcodec_free_context(&ctx_);
    av_packet_free(&pkt_);
    av_frame_free(&frame_);
    swr_free(&swr_);
    ct_ = 0;
}

bool AudioPlayer::open_decoder_locked(unsigned char ct) {
    close_decoder_locked();
    const uint8_t *config = nullptr;
    size_t config_size = 0;
    int id = 0;
    if (!airplay_audio_codec(ct, &id, &config, &config_size)) {
        std::fprintf(stderr, "[audio] unsupported compression type %d\n", ct);
        return false;
    }
    const AVCodec *codec = avcodec_find_decoder(static_cast<AVCodecID>(id));
    if (!codec) return false;
    ctx_ = avcodec_alloc_context3(codec);
    ctx_->sample_rate = kSampleRate;
    av_channel_layout_default(&ctx_->ch_layout, kChannels);
    ctx_->extradata = static_cast<uint8_t *>(av_mallocz(config_size + AV_INPUT_BUFFER_PADDING_SIZE));
    std::copy(config, config + config_size, ctx_->extradata);
    ctx_->extradata_size = static_cast<int>(config_size);
    if (avcodec_open2(ctx_, codec, nullptr) < 0) {
        avcodec_free_context(&ctx_);
        return false;
    }
    pkt_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    ct_ = ct;
    std::fprintf(stderr, "[audio] decoder ready: %s\n", ct == 2 ? "ALAC" : ct == 4 ? "AAC-LC" : "AAC-ELD");
    return true;
}

void AudioPlayer::play(unsigned char ct, const uint8_t *data, int size) {
    if (!device_ || size <= 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (ct != ct_ && !open_decoder_locked(ct)) return;

    pkt_->data = const_cast<uint8_t *>(data);
    pkt_->size = size;
    if (avcodec_send_packet(ctx_, pkt_) < 0) return;

    while (avcodec_receive_frame(ctx_, frame_) == 0) {
        if (!swr_) {
            AVChannelLayout out_layout;
            av_channel_layout_default(&out_layout, kChannels);
            swr_alloc_set_opts2(&swr_, &out_layout, AV_SAMPLE_FMT_S16, kSampleRate,
                                &frame_->ch_layout, static_cast<AVSampleFormat>(frame_->format),
                                frame_->sample_rate, 0, nullptr);
            if (!swr_ || swr_init(swr_) < 0) {
                swr_free(&swr_);
                return;
            }
        }
        int max_out = swr_get_out_samples(swr_, frame_->nb_samples);
        pcm_.resize(static_cast<size_t>(max_out) * kChannels);
        uint8_t *out[] = {reinterpret_cast<uint8_t *>(pcm_.data())};
        int got = swr_convert(swr_, out, max_out, const_cast<const uint8_t **>(frame_->extended_data),
                              frame_->nb_samples);
        if (got <= 0) continue;

        float gain = gain_;
        size_t n = static_cast<size_t>(got) * kChannels;
        if (gain < 0.999f) {
            for (size_t i = 0; i < n; ++i) pcm_[i] = static_cast<int16_t>(pcm_[i] * gain);
        }
        // Drop what is already queued rather than letting latency build up.
        if (SDL_GetQueuedAudioSize(device_) > kMaxQueuedBytes) SDL_ClearQueuedAudio(device_);
        SDL_QueueAudio(device_, pcm_.data(), static_cast<Uint32>(n * sizeof(int16_t)));
    }
}

void AudioPlayer::flush() {
    if (device_) SDL_ClearQueuedAudio(device_);
    std::lock_guard<std::mutex> lock(mutex_);
    if (ctx_) avcodec_flush_buffers(ctx_);
}

void AudioPlayer::set_volume_db(float db) {
    volume_db_ = db;
    if (db <= -144.0f) {
        gain_ = 0.0f;
    } else {
        db = std::clamp(db, -30.0f, 0.0f);
        gain_ = std::pow(10.0f, db / 20.0f);
    }
}
