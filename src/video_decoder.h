#pragma once

#include <cstdint>
#include <mutex>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

// Decodes the Annex-B H.264/H.265 stream delivered by the AirPlay core and keeps
// only the newest decoded frame: mirroring wants the lowest latency, not every frame.
class VideoDecoder {
public:
    ~VideoDecoder();

    // Called from the AirPlay mirror thread.
    bool set_codec(bool h265);
    void decode(const uint8_t *data, int size);
    void reset();

    // Called from the UI thread. Moves the newest frame into `out` (an allocated
    // AVFrame) and returns true if there was one the caller has not seen yet.
    bool take_frame(AVFrame *out);

private:
    bool open_locked(bool h265);
    void close_locked();

    std::mutex codec_mutex_;
    AVCodecContext *ctx_ = nullptr;
    AVPacket *pkt_ = nullptr;
    AVFrame *decoded_ = nullptr;
    bool h265_ = false;

    std::mutex frame_mutex_;
    AVFrame *latest_ = nullptr;
    bool fresh_ = false;
};
