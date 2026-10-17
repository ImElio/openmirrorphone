#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

struct raop_s;
struct dnssd_s;
class AudioPlayer;
class Recorder;
class VideoDecoder;

// AirPlay receiver: advertises itself over Bonjour as "_airplay._tcp" and
// "_raop._tcp", runs the AirPlay core and feeds the decoded streams to the
// video decoder and audio player.
class Receiver {
public:
    enum class Event { ClientConnected, ClientDisconnected, ConnectionLost, VideoStarted, VideoStopped };
    using EventSink = std::function<void(Event)>; // called from AirPlay threads

    Receiver(VideoDecoder &video, AudioPlayer &audio, Recorder &recorder, EventSink sink);
    ~Receiver();

    bool start(const std::string &name, const std::string &mac, const std::string &key_file);
    void stop();
    // Re-advertise under a new name: restarts Bonjour and the AirPlay server with
    // the same device ID and pairing key, so the device list updates within seconds.
    // Drops a connected device. UI thread only.
    bool rename(const std::string &name);
    // Restart the HTTP/RTSP server after the client dropped. UI thread only.
    void restart_after_reset();

    void set_debug_log(bool on) { debug_log_ = on; }

    // Name and model ("iPhone16,2") of the device that connected last.
    void set_client(const std::string &name, const std::string &model) {
        std::lock_guard<std::mutex> lock(client_mutex_);
        client_name_ = name;
        client_model_ = model;
    }
    void client(std::string *name, std::string *model) const {
        std::lock_guard<std::mutex> lock(client_mutex_);
        *name = client_name_;
        *model = client_model_;
    }
    int connections() const { return connections_; }
    const std::string &name() const { return name_; }

    // Entry points for the C callbacks in receiver.cpp; not part of the public API.
    VideoDecoder &video_;
    AudioPlayer &audio_;
    Recorder &recorder_;
    EventSink sink_;
    std::atomic<int> connections_{0};
    std::atomic<bool> video_running_{false};

private:
    raop_s *raop_ = nullptr;
    dnssd_s *dnssd_ = nullptr;
    std::string name_;
    std::string mac_;
    std::string key_file_;
    bool debug_log_ = false;
    mutable std::mutex client_mutex_;
    std::string client_name_;
    std::string client_model_;
};
