#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;

// FFmpeg decoder (an AVCodecID) and its extradata for the AirPlay compression
// type `ct`: 2 = ALAC, 4 = AAC-LC, 8 = AAC-ELD. False if `ct` is not supported.
bool airplay_audio_codec(unsigned char ct, int *codec_id, const uint8_t **config, size_t *config_size);

// Decodes AirPlay audio (ALAC for music, AAC-ELD for screen mirroring) and queues
// it on an SDL audio device. Audio is played as it arrives, with a small cap on
// the queue so latency never grows past what mirroring tolerates.
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    bool open_device(); // UI thread, after SDL_Init(SDL_INIT_AUDIO)
    std::vector<std::string> output_devices() const;
    std::string output_device() const;
    bool set_output_device(const std::string &name);

    // Called from the AirPlay audio thread. `ct` is the AirPlay compression type:
    // 2 = ALAC, 4 = AAC-LC, 8 = AAC-ELD.
    void play(unsigned char ct, const uint8_t *data, int size);
    void flush();

    // AirPlay volume in dB: 0 is full, -30 is quietest, -144 means mute.
    void set_volume_db(float db);
    float volume_db() const { return volume_db_; }

private:
    bool open_decoder_locked(unsigned char ct);
    void close_decoder_locked();

    mutable std::mutex mutex_;
    AVCodecContext *ctx_ = nullptr;
    AVPacket *pkt_ = nullptr;
    AVFrame *frame_ = nullptr;
    SwrContext *swr_ = nullptr;
    unsigned char ct_ = 0;
    std::vector<int16_t> pcm_;

    uint32_t device_ = 0;
    std::string device_name_; // empty means the Windows default output
    std::atomic<float> volume_db_{0.0f};
    std::atomic<float> gain_{1.0f};
};
