#pragma once

#include <winsock2.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

class Recorder;
class AudioPlayer;
class VideoDecoder;

// Receives H.264 access units and captured app audio from the OpenMirrorPhone
// Android companion over the local network.
class AndroidReceiver {
public:
    using CanAccept = std::function<bool()>;
    using StreamEvent = std::function<void(bool)>;
    using VisibilityEvent = std::function<void(bool)>;

    AndroidReceiver(VideoDecoder &video, AudioPlayer &audio, Recorder &recorder, CanAccept can_accept, StreamEvent event,
                    VisibilityEvent visibility_event);
    ~AndroidReceiver();

    bool start();
    void stop();
    bool running() const { return running_.load(); }
    std::string address() const;
    static constexpr unsigned short kPort = 24862;

private:
    void accept_loop();
    void handle_client(SOCKET client);

    VideoDecoder &video_;
    AudioPlayer &audio_;
    Recorder &recorder_;
    CanAccept can_accept_;
    StreamEvent event_;
    VisibilityEvent visibility_event_;
    std::atomic<bool> running_{false};
    std::atomic<bool> active_stream_{false};
    std::atomic<SOCKET> listener_{INVALID_SOCKET};
    std::atomic<SOCKET> client_{INVALID_SOCKET};
    std::thread thread_;
    bool winsock_started_ = false;
};
