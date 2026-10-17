#include "receiver.h"

#include "audio_player.h"
#include "recorder.h"
#include "video_decoder.h"

extern "C" {
#include "dnssd.h"
#include "logger.h"
#include "raop.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

Receiver &self(void *cls) { return *static_cast<Receiver *>(cls); }

std::vector<char> parse_mac(const std::string &mac) {
    std::vector<char> bytes;
    for (size_t i = 0; i + 1 < mac.size(); i += 3) {
        bytes.push_back(static_cast<char>(std::stoul(mac.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

// --- AirPlay core callbacks -------------------------------------------------

void on_log(void *, int, const char *msg) {
    std::fprintf(stderr, "[airplay] %s\n", msg);
    std::fflush(stderr);
}

void on_conn_init(void *cls) {
    auto &r = self(cls);
    r.connections_++;
    r.sink_(Receiver::Event::ClientConnected);
}

void on_conn_destroy(void *cls) {
    auto &r = self(cls);
    if (--r.connections_ <= 0) {
        r.connections_ = 0;
        r.sink_(Receiver::Event::ClientDisconnected);
    }
}

void on_conn_reset(void *cls, int reason) {
    if (reason == 1) std::fprintf(stderr, "[airplay] lost connection with client\n");
    self(cls).sink_(Receiver::Event::ConnectionLost);
}

void on_conn_feedback(void *) {}

void on_video_process(void *cls, raop_ntp_t *, video_decode_struct *data) {
    auto &r = self(cls);
    if (!r.video_running_.exchange(true)) r.sink_(Receiver::Event::VideoStarted);
    r.recorder_.video(data->data, data->data_len, data->ntp_time_remote, data->is_h265);
    r.video_.decode(data->data, data->data_len);
}

void on_audio_process(void *cls, raop_ntp_t *, audio_decode_struct *data) {
    auto &r = self(cls);
    r.recorder_.audio(data->ct, data->data, data->data_len, data->ntp_time_remote);
    r.audio_.play(data->ct, data->data, data->data_len);
}

int on_video_set_codec(void *cls, video_codec_t codec) {
    return self(cls).video_.set_codec(codec == VIDEO_CODEC_H265) ? 0 : -1;
}

void on_video_reset(void *cls, reset_type_t) {
    auto &r = self(cls);
    r.video_.reset();
    r.recorder_.reset();
    if (r.video_running_.exchange(false)) r.sink_(Receiver::Event::VideoStopped);
}

void on_video_flush(void *cls) { self(cls).video_.reset(); }
void on_audio_flush(void *cls) { self(cls).audio_.flush(); }
void on_video_pause(void *) {}
void on_video_resume(void *) {}

void on_mirror_video_running(void *cls, bool running) {
    auto &r = self(cls);
    if (!running && r.video_running_.exchange(false)) r.sink_(Receiver::Event::VideoStopped);
}

double on_audio_set_client_volume(void *cls) { return self(cls).audio_.volume_db(); }
void on_audio_set_volume(void *cls, float db) { self(cls).audio_.set_volume_db(db); }

void on_audio_get_format(void *, unsigned char *ct, unsigned short *spf, bool *using_screen, bool *is_media,
                         uint64_t *) {
    std::fprintf(stderr, "[airplay] audio stream: ct=%d spf=%d screen=%d media=%d\n", *ct, *spf, *using_screen,
                 *is_media);
}

void on_video_report_size(void *, float *ws, float *hs, float *w, float *h) {
    std::fprintf(stderr, "[airplay] mirror source %gx%g, stream %gx%g\n", *ws, *hs, *w, *h);
}

void on_report_client_request(void *cls, char *deviceid, char *model, char *name, bool *admit) {
    std::fprintf(stderr, "[airplay] connection from \"%s\" (%s, %s)\n", name, model, deviceid);
    self(cls).set_client(name ? name : "", model ? model : "");
    *admit = true;
}

// Metadata, cover art and HLS video ("AirPlay a video URL") are not handled.
void on_audio_set_metadata(void *, const void *, int) {}
void on_audio_set_coverart(void *, const void *, int) {}
void on_audio_stop_coverart(void *) {}
void on_audio_set_progress(void *, uint32_t *, uint32_t *, uint32_t *) {}
void on_audio_remote_control_id(void *, const char *, const char *) {}
void on_export_dacp(void *, const char *, const char *) {}
void on_display_pin(void *, char *pin) { std::fprintf(stderr, "[airplay] PIN: %s\n", pin); }
void on_register_client(void *, const char *, const char *, const char *) {}
bool on_check_register(void *, const char *) { return true; }
void on_video_play(void *, const char *, const float) {}
void on_video_scrub(void *, const float) {}
void on_video_rate(void *, const float) {}
void on_video_stop(void *) {}
void on_video_playback_info(void *, playback_info_t *) {}
float on_video_playlist_remove(void *) { return 0.0f; }
void on_get_custom_profile(void *, const char **profile) { *profile = nullptr; }

} // namespace

Receiver::Receiver(VideoDecoder &video, AudioPlayer &audio, Recorder &recorder, EventSink sink)
    : video_(video), audio_(audio), recorder_(recorder), sink_(std::move(sink)) {}

Receiver::~Receiver() { stop(); }

bool Receiver::start(const std::string &name, const std::string &mac, const std::string &key_file) {
    name_ = name;
    mac_ = mac;
    key_file_ = key_file;
    ntp_global_init(); // caches the QPC frequency used to timestamp NTP packets on Windows
    int err = 0;
    std::vector<char> hw = parse_mac(mac);
    dnssd_ = dnssd_init(name.c_str(), static_cast<int>(name.size()), hw.data(), static_cast<int>(hw.size()), 0, &err);
    if (err || !dnssd_) {
        std::fprintf(stderr, "Bonjour (dnssd.dll) not available, error %d. Is the Bonjour service installed?\n", err);
        return false;
    }
    dnssd_set_airplay_features(dnssd_, 0, 0);  // no HLS video URLs
    dnssd_set_airplay_features(dnssd_, 4, 0);
    dnssd_set_airplay_features(dnssd_, 27, 0); // no legacy pairing: iOS skips /pair-setup, no PIN
    dnssd_set_airplay_features(dnssd_, 42, 0); // H.264 only

    raop_callbacks_t cbs;
    std::memset(&cbs, 0, sizeof cbs);
    cbs.cls = this;
    cbs.audio_process = on_audio_process;
    cbs.video_process = on_video_process;
    cbs.video_pause = on_video_pause;
    cbs.video_resume = on_video_resume;
    cbs.conn_feedback = on_conn_feedback;
    cbs.conn_reset = on_conn_reset;
    cbs.video_reset = on_video_reset;
    cbs.conn_init = on_conn_init;
    cbs.conn_destroy = on_conn_destroy;
    cbs.audio_flush = on_audio_flush;
    cbs.video_flush = on_video_flush;
    cbs.audio_set_client_volume = on_audio_set_client_volume;
    cbs.audio_set_volume = on_audio_set_volume;
    cbs.audio_set_metadata = on_audio_set_metadata;
    cbs.audio_set_coverart = on_audio_set_coverart;
    cbs.audio_stop_coverart_rendering = on_audio_stop_coverart;
    cbs.audio_remote_control_id = on_audio_remote_control_id;
    cbs.audio_set_progress = on_audio_set_progress;
    cbs.audio_get_format = on_audio_get_format;
    cbs.video_report_size = on_video_report_size;
    cbs.mirror_video_running = on_mirror_video_running;
    cbs.report_client_request = on_report_client_request;
    cbs.display_pin = on_display_pin;
    cbs.register_client = on_register_client;
    cbs.check_register = on_check_register;
    cbs.passwd = nullptr; // no access password
    cbs.export_dacp = on_export_dacp;
    cbs.video_set_codec = on_video_set_codec;
    cbs.on_video_play = on_video_play;
    cbs.on_video_scrub = on_video_scrub;
    cbs.on_video_rate = on_video_rate;
    cbs.on_video_stop = on_video_stop;
    cbs.on_video_acquire_playback_info = on_video_playback_info;
    cbs.on_video_playlist_remove = on_video_playlist_remove;
    cbs.get_custom_profile = on_get_custom_profile;

    raop_ = raop_init(&cbs);
    if (!raop_) return false;
    raop_set_log_callback(raop_, on_log, this);
    raop_set_log_level(raop_, debug_log_ ? LOGGER_DEBUG : LOGGER_INFO);
    // nohold = 1: a new device takes over the screen instead of being refused.
    if (raop_init2(raop_, 1, mac.c_str(), key_file.c_str())) {
        std::fprintf(stderr, "Failed to initialise the AirPlay core\n");
        std::free(raop_); // half-initialised: raop_destroy would touch missing members
        raop_ = nullptr;
        return false;
    }
    raop_set_plist(raop_, "width", 1920);
    raop_set_plist(raop_, "height", 1080);
    raop_set_plist(raop_, "refreshRate", 60);
    raop_set_plist(raop_, "maxFPS", 60);

    unsigned short tcp[2] = {0, 0}, udp[3] = {0, 0, 0}; // 0 = pick free ports
    raop_set_tcp_ports(raop_, tcp);
    raop_set_udp_ports(raop_, udp);
    unsigned short port = raop_get_port(raop_);
    if (raop_start_httpd(raop_, &port) < 0) {
        std::fprintf(stderr, "Failed to start the AirPlay server\n");
        return false;
    }
    raop_set_port(raop_, port);
    raop_set_dnssd(raop_, dnssd_);

    if ((err = dnssd_register_raop(dnssd_, port)) || (err = dnssd_register_airplay(dnssd_, port))) {
        std::fprintf(stderr, "Bonjour registration failed, error %d\n", err);
        return false;
    }
    std::fprintf(stderr, "AirPlay receiver \"%s\" listening on port %u (device id %s)\n", name.c_str(), port,
                 mac.c_str());
    return true;
}

void Receiver::restart_after_reset() {
    if (!raop_) return;
    raop_stop_httpd(raop_);
    raop_remove_known_connections(raop_);
    video_.reset();
    audio_.flush();
    unsigned short port = raop_get_port(raop_);
    raop_start_httpd(raop_, &port);
    raop_set_port(raop_, port);
}

void Receiver::stop() {
    if (dnssd_) {
        dnssd_unregister_raop(dnssd_);
        dnssd_unregister_airplay(dnssd_);
    }
    if (raop_) {
        raop_destroy(raop_);
        raop_ = nullptr;
    }
    if (dnssd_) {
        dnssd_destroy(dnssd_);
        dnssd_ = nullptr;
    }
    connections_ = 0;
    video_running_ = false;
}

bool Receiver::rename(const std::string &name) {
    stop();
    video_.reset();
    audio_.flush();
    return start(name, mac_, key_file_);
}
