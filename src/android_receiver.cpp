#include "android_receiver.h"

#include "audio_player.h"
#include "network.h"
#include "recorder.h"
#include "video_decoder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <vector>

namespace {
constexpr uint32_t kMagic = 0x4F4D5048; // OMPH
constexpr uint32_t kVersion1 = 1;
constexpr uint32_t kVersion2 = 2;
constexpr uint32_t kMaxPacketBytes = 12u * 1024u * 1024u;
constexpr uint32_t kMaxAudioPacketBytes = 64u * 1024u;
constexpr uint32_t kVisibilityControl = 0xFFFFFFFFu;
constexpr uint8_t kVideoPacket = 1;
constexpr uint8_t kAudioPacket = 2;
constexpr uint8_t kVisibilityPacket = 3;

bool recv_exact(SOCKET socket, uint8_t *data, size_t size) {
    size_t done = 0;
    while (done < size) {
        int n = recv(socket, reinterpret_cast<char *>(data + done), static_cast<int>(size - done), 0);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

uint32_t read_be32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
} // namespace

AndroidReceiver::AndroidReceiver(VideoDecoder &video, AudioPlayer &audio, Recorder &recorder, CanAccept can_accept,
                                 StreamEvent event, VisibilityEvent visibility_event)
    : video_(video), audio_(audio), recorder_(recorder), can_accept_(std::move(can_accept)),
      event_(std::move(event)), visibility_event_(std::move(visibility_event)) {}

AndroidReceiver::~AndroidReceiver() { stop(); }

bool AndroidReceiver::start() {
    if (running_) return true;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    winsock_started_ = true;

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        WSACleanup();
        winsock_started_ = false;
        return false;
    }
    BOOL reuse = TRUE;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof reuse);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kPort);
    if (bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == SOCKET_ERROR || listen(server, 1) == SOCKET_ERROR) {
        std::fprintf(stderr, "[android] cannot listen on TCP port %u (error %d)\n", kPort, WSAGetLastError());
        closesocket(server);
        WSACleanup();
        winsock_started_ = false;
        return false;
    }

    listener_ = server;
    running_ = true;
    thread_ = std::thread(&AndroidReceiver::accept_loop, this);
    std::fprintf(stderr, "[android] receiver listening at %s:%u\n", address().c_str(), kPort);
    return true;
}

void AndroidReceiver::stop() {
    if (!running_.exchange(false)) return;
    SOCKET client = client_.exchange(INVALID_SOCKET);
    if (client != INVALID_SOCKET) {
        shutdown(client, SD_BOTH);
        closesocket(client);
    }
    SOCKET server = listener_.exchange(INVALID_SOCKET);
    if (server != INVALID_SOCKET) {
        shutdown(server, SD_BOTH);
        closesocket(server);
    }
    if (thread_.joinable()) thread_.join();
    if (winsock_started_) {
        WSACleanup();
        winsock_started_ = false;
    }
}

std::string AndroidReceiver::address() const { return find_ipv4_address(); }

void AndroidReceiver::accept_loop() {
    while (running_) {
        SOCKET server = listener_;
        if (server == INVALID_SOCKET) break;
        SOCKET incoming = accept(server, nullptr, nullptr);
        if (incoming == INVALID_SOCKET) {
            if (running_) std::fprintf(stderr, "[android] accept failed (error %d)\n", WSAGetLastError());
            continue;
        }
        if (!can_accept_ || !can_accept_()) {
            closesocket(incoming);
            continue;
        }
        client_ = incoming;
        handle_client(incoming);
        SOCKET expected = incoming;
        client_.compare_exchange_strong(expected, INVALID_SOCKET);
        closesocket(incoming);
    }
}

void AndroidReceiver::handle_client(SOCKET client) {
    std::array<uint8_t, 8> hello{};
    if (!recv_exact(client, hello.data(), hello.size()) || read_be32(hello.data()) != kMagic) {
        std::fprintf(stderr, "[android] client sent an invalid stream header\n");
        return;
    }
    const uint32_t version = read_be32(hello.data() + 4);
    if (version != kVersion1 && version != kVersion2) {
        std::fprintf(stderr, "[android] unsupported stream protocol version %u\n", version);
        return;
    }
    if (!video_.set_codec(false)) {
        std::fprintf(stderr, "[android] could not initialise H.264 decoder\n");
        return;
    }
    video_.reset();
    audio_.flush();
    recorder_.reset();

    bool announced = false;
    bool audio_announced = false;
    unsigned video_packets = 0;
    std::array<uint8_t, 4> size_buf{};
    while (running_ && client_.load() == client) {
        uint8_t type = kVideoPacket;
        if (version == kVersion2 && !recv_exact(client, &type, 1)) break;
        if (!recv_exact(client, size_buf.data(), size_buf.size())) break;
        uint32_t size = read_be32(size_buf.data());
        if (version == kVersion1 && size == kVisibilityControl) {
            uint8_t visible = 0;
            if (!recv_exact(client, &visible, 1)) break;
            if (visible <= 1) {
                std::fprintf(stderr, "[android] captured app visibility: %s\n", visible ? "visible" : "hidden");
                if (visibility_event_) visibility_event_(visible != 0);
            }
            continue;
        }
        if (version == kVersion2 && type == kVisibilityPacket) {
            uint8_t visible = 0;
            if (size != 1 || !recv_exact(client, &visible, 1)) {
                std::fprintf(stderr, "[android] rejected invalid visibility packet\n");
                break;
            }
            if (visible <= 1 && visibility_event_) visibility_event_(visible != 0);
            continue;
        }
        const uint32_t max_size = version == kVersion2 && type == kAudioPacket ? kMaxAudioPacketBytes
                                                                                : kMaxPacketBytes;
        if (size == 0 || size > max_size ||
            (version == kVersion2 && type != kVideoPacket && type != kAudioPacket)) {
            std::fprintf(stderr, "[android] rejected invalid H.264 packet length %u\n", size);
            break;
        }
        if (version == kVersion2 && type == kAudioPacket && size % 4 != 0) {
            std::fprintf(stderr, "[android] rejected misaligned PCM audio packet length %u\n", size);
            break;
        }
        std::vector<uint8_t> packet(size);
        if (!recv_exact(client, packet.data(), packet.size())) break;

        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const uint64_t timestamp_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
        if (version == kVersion2 && type == kAudioPacket) {
            if (!audio_announced) {
                audio_announced = true;
                std::fprintf(stderr, "[android] app audio packets received (44.1 kHz stereo PCM)\n");
            }
            audio_.play_pcm(packet.data(), static_cast<int>(packet.size()));
            recorder_.audio_pcm(packet.data(), static_cast<int>(packet.size()), timestamp_ns);
            continue;
        }
        if (version == kVersion2 && video_packets++ < 4) {
            std::fprintf(stderr, "[android] video packet %u: %u bytes\n", video_packets, size);
        }
        recorder_.video(packet.data(), static_cast<int>(packet.size()), timestamp_ns, false);
        if (video_.decode(packet.data(), static_cast<int>(packet.size())) && !announced) {
            announced = true;
            active_stream_ = true;
            event_(true);
        }
    }

    video_.reset();
    audio_.flush();
    recorder_.reset();
    if (active_stream_.exchange(false)) event_(false);
    std::fprintf(stderr, "[android] stream disconnected\n");
}
