#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct AVAudioFifo;
struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct AVStream;
struct SwrContext;

// Records the mirrored screen to MP4. The H.264 stream is remuxed as it arrives
// (no re-encoding); AirPlay audio (AAC-ELD when mirroring) is re-encoded to
// AAC-LC, which every player handles.
// Both tracks are timed from the sender's NTP timestamps, so they stay in sync.
//
// A file can only start on an IDR frame. The packets since the last IDR are
// kept in memory, so a recording starts at once from that frame instead of
// waiting for the sender's next one.
class Recorder {
public:
    enum class State { Off, WaitingForKeyframe, Recording };

    ~Recorder();

    // UI thread. `path` is UTF-8.
    void start(const std::string &path);
    void stop();
    State state() const { return state_; }
    double seconds() const { return duration_ms_ / 1000.0; } // media written so far

    // AirPlay threads. `ntp_ns` is ntp_time_remote of the decode structs.
    void video(const uint8_t *data, int size, uint64_t ntp_ns, bool h265);
    void audio(unsigned char ct, const uint8_t *data, int size, uint64_t ntp_ns);
    void reset(); // the stream restarted: the cached frames no longer decode

private:
    struct CachedPacket {
        std::vector<uint8_t> data;
        uint64_t ntp_ns;
        bool key;
    };

    bool open_locked();
    void close_locked();
    void write_video_locked(const uint8_t *data, int size, uint64_t ntp_ns, bool key);
    bool open_audio_decoder_locked(unsigned char ct);
    void push_audio_locked(const float *const *planes, int samples, int64_t start);
    void queue_audio_locked(const float *const *planes, int samples);
    void encode_audio_locked(AVFrame *frame);

    mutable std::mutex mutex_;
    std::atomic<State> state_{State::Off};
    std::atomic<int64_t> duration_ms_{0};
    std::string path_;

    // Packets since the last IDR, and the newest SPS + PPS (Annex-B).
    std::vector<CachedPacket> gop_;
    size_t gop_bytes_ = 0;
    bool gop_valid_ = false;
    std::vector<uint8_t> params_;
    bool warned_h265_ = false;

    AVFormatContext *fmt_ = nullptr;
    AVStream *vst_ = nullptr;
    AVStream *ast_ = nullptr;
    uint64_t base_ns_ = 0; // sender time of the file's first frame
    int64_t last_vpts_ = -1;
    bool write_failed_ = false;

    AVCodecContext *dec_ = nullptr;
    unsigned char dec_ct_ = 0;
    SwrContext *swr_ = nullptr;
    AVCodecContext *enc_ = nullptr;
    AVAudioFifo *fifo_ = nullptr;
    AVFrame *in_frame_ = nullptr;
    AVFrame *enc_frame_ = nullptr;
    AVPacket *dec_pkt_ = nullptr;
    AVPacket *pkt_ = nullptr;
    std::vector<float> pcm_[2];
    int64_t next_sample_ = -1; // file position, in samples, of the next sample queued in fifo_
    int64_t enc_pts_ = 0;
};
