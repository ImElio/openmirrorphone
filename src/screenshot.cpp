#include "screenshot.h"

#include "network.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}
#include <windows.h>
#include <shlobj.h>

#include <ctime>
#include <cstdio>
#include <string>

namespace {
std::string screenshot_directory() {
    PWSTR pictures = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_CREATE, nullptr, &pictures))) return {};
    std::wstring folder(pictures);
    CoTaskMemFree(pictures);
    folder += L"\\OpenMirrorPhone";
    if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
    return to_utf8(folder);
}

std::string unique_path(const std::string &directory) {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char stamp[32];
    if (!std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S", &local)) return {};
    std::string base = directory + "\\" + stamp;
    std::string path = base + ".png";
    for (int suffix = 2; GetFileAttributesW(to_wide(path).c_str()) != INVALID_FILE_ATTRIBUTES; ++suffix) {
        path = base + " (" + std::to_string(suffix) + ").png";
    }
    return path;
}
} // namespace

bool save_screenshot_png(const AVFrame *source, std::string *path) {
    if (!source || source->width <= 0 || source->height <= 0 || source->format < 0 || !source->data[0]) {
        if (path) *path = "No mirrored screen is available yet.";
        return false;
    }
    std::string directory = screenshot_directory();
    if (directory.empty()) {
        if (path) *path = "Could not open the Pictures folder.";
        return false;
    }
    std::string output = unique_path(directory);
    if (output.empty()) {
        if (path) *path = "Could not create a screenshot filename.";
        return false;
    }

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
    AVCodecContext *encoder = codec ? avcodec_alloc_context3(codec) : nullptr;
    AVFrame *rgb = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    SwsContext *scale = nullptr;
    bool ok = false;
    if (!encoder || !rgb || !packet) goto done;

    encoder->width = source->width;
    encoder->height = source->height;
    encoder->pix_fmt = AV_PIX_FMT_RGB24;
    encoder->time_base = AVRational{1, 1};
    if (avcodec_open2(encoder, codec, nullptr) < 0) goto done;

    rgb->format = encoder->pix_fmt;
    rgb->width = encoder->width;
    rgb->height = encoder->height;
    if (av_frame_get_buffer(rgb, 0) < 0) goto done;
    scale = sws_getContext(source->width, source->height, static_cast<AVPixelFormat>(source->format),
                           rgb->width, rgb->height, encoder->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!scale || sws_scale(scale, source->data, source->linesize, 0, source->height, rgb->data, rgb->linesize) <= 0)
        goto done;
    if (avcodec_send_frame(encoder, rgb) < 0 || avcodec_receive_packet(encoder, packet) < 0) goto done;
    {
        HANDLE file = CreateFileW(to_wide(output).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) goto done;
        DWORD written = 0;
        ok = packet->size > 0 && WriteFile(file, packet->data, static_cast<DWORD>(packet->size), &written, nullptr) &&
             written == static_cast<DWORD>(packet->size);
        CloseHandle(file);
        if (!ok) DeleteFileW(to_wide(output).c_str());
    }
    if (ok && path) *path = output;

done:
    if (!ok && path) *path = "Could not encode or save the PNG screenshot.";
    sws_freeContext(scale);
    av_packet_free(&packet);
    av_frame_free(&rgb);
    avcodec_free_context(&encoder);
    return ok;
}
