// OpenMirrorPhone: an AirPlay screen-mirroring receiver for Windows.
// Shows an iPhone/iPad/Mac screen in a window on the PC.

#include "audio_player.h"
#include "network.h"
#include "receiver.h"
#include "recorder.h"
#include "resource.h"
#include "sessions.h"
#include "settings.h"
#include "settings_dialog.h"
#include "update_dialog.h"
#include "updater.h"
#include "video_decoder.h"
#include "welcome_dialog.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_ttf.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef OMP_VERSION
#define OMP_VERSION "0.0.0"
#endif

namespace {

Uint32 g_receiver_event = 0;
Uint32 g_update_event = 0;

// Why the release list was fetched, and what came back. Sent from the fetch
// thread to the UI thread as an SDL event (data1).
enum class FetchPurpose { Startup, CheckNow, WhatsNew };
struct FetchResult {
    FetchPurpose purpose;
    bool ok = false;
    std::string error;
    std::vector<Release> releases;
};

void fetch_releases_async(FetchPurpose purpose) {
    std::thread([purpose] {
        auto *result = new FetchResult();
        result->purpose = purpose;
        result->ok = fetch_releases(&result->releases, &result->error);
        SDL_Event ev{};
        ev.type = g_update_event;
        ev.user.data1 = result;
        if (SDL_PushEvent(&ev) <= 0) delete result;
    }).detach();
}

// Release notes handed from the old version to the new one across the restart.
std::string whats_new_path() { return app_data_dir() + "\\whatsnew.txt"; }

std::string read_text_file(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct Ui {
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    HWND hwnd = nullptr;
    HMENU menu = nullptr;
    SDL_Texture *video = nullptr;
    int tex_w = 0, tex_h = 0;
    Uint32 tex_format = 0;
    TTF_Font *name_font = nullptr;  // Segoe UI Light, large
    TTF_Font *body_font = nullptr;  // Segoe UI
    bool showing_video = false;
};

// Waiting screen palette: black, like a TV input waiting for a signal.
const SDL_Color kName{235, 235, 235, 255};
const SDL_Color kStatus{160, 160, 160, 255};
const SDL_Color kHint{105, 105, 105, 255};

// Draws `text` with its top edge at `y`; `align` is -1 left, 0 centre, 1 right of `x`.
int draw_text(Ui &ui, TTF_Font *font, const std::string &text, int x, int y, SDL_Color color, int align = 0) {
    if (!font || text.empty()) return 0;
    SDL_Surface *s = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!s) return 0;
    SDL_Texture *t = SDL_CreateTextureFromSurface(ui.renderer, s);
    int left = align < 0 ? x : align > 0 ? x - s->w : x - s->w / 2;
    SDL_Rect dst{left, y, s->w, s->h};
    SDL_RenderCopy(ui.renderer, t, nullptr, &dst);
    SDL_DestroyTexture(t);
    SDL_FreeSurface(s);
    return dst.w;
}

void draw_idle(Ui &ui, const std::string &name, const std::string &status) {
    int w, h;
    SDL_GetRendererOutputSize(ui.renderer, &w, &h);
    SDL_SetRenderDrawColor(ui.renderer, 0, 0, 0, 255);
    SDL_RenderClear(ui.renderer);

    int y = h / 2 - 90;
    draw_text(ui, ui.name_font, name, w / 2, y, kName);
    draw_text(ui, ui.body_font, status, w / 2, y + 86, kStatus);
    draw_text(ui, ui.body_font, u8"On your iPhone or iPad, open Control Center, tap Screen Mirroring", w / 2, y + 132,
              kHint);
    draw_text(ui, ui.body_font, u8"and choose \u201c" + name + u8"\u201d.", w / 2, y + 158, kHint);
}

bool upload_frame(Ui &ui, const AVFrame *f) {
    Uint32 fmt;
    switch (f->format) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P: fmt = SDL_PIXELFORMAT_IYUV; break;
    case AV_PIX_FMT_NV12: fmt = SDL_PIXELFORMAT_NV12; break;
    default: {
        static bool warned = false;
        if (!warned) std::fprintf(stderr, "[video] unsupported pixel format %d\n", f->format);
        warned = true;
        return false;
    }
    }
    if (!ui.video || ui.tex_w != f->width || ui.tex_h != f->height || ui.tex_format != fmt) {
        if (ui.video) SDL_DestroyTexture(ui.video);
        ui.video = SDL_CreateTexture(ui.renderer, fmt, SDL_TEXTUREACCESS_STREAMING, f->width, f->height);
        ui.tex_w = f->width;
        ui.tex_h = f->height;
        ui.tex_format = fmt;
        std::fprintf(stderr, "[video] %dx%d\n", f->width, f->height);
    }
    if (fmt == SDL_PIXELFORMAT_IYUV) {
        SDL_UpdateYUVTexture(ui.video, nullptr, f->data[0], f->linesize[0], f->data[1], f->linesize[1], f->data[2],
                             f->linesize[2]);
    } else {
        SDL_UpdateNVTexture(ui.video, nullptr, f->data[0], f->linesize[0], f->data[1], f->linesize[1]);
    }
    return true;
}

void draw_video(Ui &ui) {
    int w, h;
    SDL_GetRendererOutputSize(ui.renderer, &w, &h);
    SDL_SetRenderDrawColor(ui.renderer, 0, 0, 0, 255);
    SDL_RenderClear(ui.renderer);
    if (ui.video && ui.tex_w > 0 && ui.tex_h > 0) {
        // Letterbox: fit the phone screen inside the window, keeping its aspect ratio.
        double scale = std::min(static_cast<double>(w) / ui.tex_w, static_cast<double>(h) / ui.tex_h);
        int dw = static_cast<int>(ui.tex_w * scale), dh = static_cast<int>(ui.tex_h * scale);
        SDL_Rect dst{(w - dw) / 2, (h - dh) / 2, dw, dh};
        SDL_RenderCopy(ui.renderer, ui.video, nullptr, &dst);
    }
}

// "REC" badge in the top-left corner: a red dot and the recorded time, or a
// grey dot while the recorder waits for the sender's next keyframe.
void draw_rec(Ui &ui, const Recorder &rec) {
    Recorder::State state = rec.state();
    if (state == Recorder::State::Off) return;
    std::string label = "REC";
    if (state == Recorder::State::Recording) {
        int s = static_cast<int>(rec.seconds());
        char time[16];
        std::snprintf(time, sizeof time, " %02d:%02d", s / 60, s % 60);
        label += time;
    }
    int tw = 0, th = 20;
    if (ui.body_font) TTF_SizeUTF8(ui.body_font, label.c_str(), &tw, &th);
    const int pad = 8, dot = 12, x = 16, y = 16;
    SDL_Rect bg{x, y, pad + dot + pad + tw + pad, th + pad};
    SDL_SetRenderDrawBlendMode(ui.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ui.renderer, 0, 0, 0, 160);
    SDL_RenderFillRect(ui.renderer, &bg);
    if (state == Recorder::State::Recording) SDL_SetRenderDrawColor(ui.renderer, 235, 50, 50, 255);
    else SDL_SetRenderDrawColor(ui.renderer, 150, 150, 160, 255);
    int cx = x + pad + dot / 2, cy = y + bg.h / 2, r = dot / 2;
    for (int dy = -r; dy <= r; ++dy) {
        int dx = static_cast<int>(SDL_sqrt(static_cast<double>(r * r - dy * dy)));
        SDL_RenderDrawLine(ui.renderer, cx - dx, cy + dy, cx + dx, cy + dy);
    }
    draw_text(ui, ui.body_font, label, x + pad + dot + pad, y + pad / 2, SDL_Color{255, 255, 255, 255}, -1);
}

// %USERPROFILE%\Videos\OpenMirrorPhone\<date-time>.mp4
std::string recording_path() {
    std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S", std::localtime(&now));
    return recordings_dir() + "\\" + stamp + ".mp4";
}

// Full screen hides the menu bar; windowed mode brings it back.
void toggle_fullscreen(Ui &ui) {
    bool fs = SDL_GetWindowFlags(ui.window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (!fs && ui.hwnd) SetMenu(ui.hwnd, nullptr);
    SDL_SetWindowFullscreen(ui.window, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (fs && ui.hwnd) SetMenu(ui.hwnd, ui.menu);
}

TTF_Font *open_system_font(const char *file, int size) {
    std::string path = std::string(SDL_getenv("WINDIR") ? SDL_getenv("WINDIR") : "C:\\Windows") + "\\Fonts\\" + file;
    return TTF_OpenFont(path.c_str(), size);
}

void set_title(Ui &ui, const std::string &name) {
    SDL_SetWindowTitle(ui.window, ("OpenMirrorPhone - " + name).c_str());
}

void print_usage() {
    std::printf("Usage: OpenMirrorPhone [-n NAME] [-fs] [-d]\n"
                "  -n NAME   name shown on the iPhone for this run only (default: the name\n"
                "            set in Settings, or the computer name)\n"
                "  -fs       start in full screen\n"
                "  -d        verbose AirPlay protocol log\n");
}

} // namespace

int main(int argc, char **argv) {
    std::string name = load_receiver_name();
    if (name.empty()) name = clean_receiver_name(computer_name());
    bool start_fullscreen = false;
    bool debug_log = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "-n") && i + 1 < argc) {
            std::string n = clean_receiver_name(argv[++i]);
            if (!n.empty()) name = n;
        } else if (!std::strcmp(argv[i], "--after-update") && i + 1 < argc) {
            // Started by the updater: let the old version quit before advertising.
            wait_for_previous_instance(std::strtoul(argv[++i], nullptr, 10));
        } else if (!std::strcmp(argv[i], "-fs")) {
            start_fullscreen = true;
        } else if (!std::strcmp(argv[i], "-d")) {
            debug_log = true;
        } else {
            print_usage();
            return 1;
        }
    }
    remove_old_update_files();

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    TTF_Init();
    g_receiver_event = SDL_RegisterEvents(1);
    g_update_event = SDL_RegisterEvents(1);

    Ui ui;
    ui.window = SDL_CreateWindow("OpenMirrorPhone", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 640,
                                 SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    ui.renderer = SDL_CreateRenderer(ui.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ui.window || !ui.renderer) {
        std::fprintf(stderr, "Cannot create window: %s\n", SDL_GetError());
        return 1;
    }
    set_title(ui, name);
    ui.name_font = open_system_font("segoeuil.ttf", 54);
    ui.body_font = open_system_font("segoeui.ttf", 18);
    SDL_StopTextInput(); // SDL2 starts with text input on; nothing here needs it

    // Native Windows menu bar; its WM_COMMAND messages arrive as SDL_SYSWMEVENT.
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (SDL_GetWindowWMInfo(ui.window, &wm)) {
        ui.hwnd = wm.info.win.window;
        ui.menu = LoadMenuW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDR_MAIN_MENU));
        SetMenu(ui.hwnd, ui.menu);
        SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
    }
    if (start_fullscreen) toggle_fullscreen(ui);

    VideoDecoder video;
    AudioPlayer audio;
    audio.open_device();
    Recorder recorder;
    SessionLog sessions;

    Receiver receiver(video, audio, recorder, [](Receiver::Event e) {
        SDL_Event ev{};
        ev.type = g_receiver_event;
        ev.user.code = static_cast<int>(e);
        SDL_PushEvent(&ev);
    });

    receiver.set_debug_log(debug_log);
    const std::string kWaiting = u8"Waiting for a device\u2026";
    const std::string kNoBonjour = "Cannot start: the Apple Bonjour service is required";
    std::string status = kWaiting;
    if (!receiver.start(name, find_mac_address(), app_data_dir() + "\\airplay.pem")) status = kNoBonjour;

    bool dirty = true;
    std::string pending_recording; // started before the session it belongs to

    auto end_session = [&] {
        recorder.stop();
        sessions.end();
        pending_recording.clear();
    };
    auto toggle_recording = [&] {
        if (recorder.state() == Recorder::State::Off) {
            std::string path = recording_path();
            recorder.start(path);
            if (sessions.active()) sessions.add_recording(path);
            else pending_recording = path;
        } else {
            recorder.stop();
        }
        dirty = true;
    };
    auto open_settings = [&] {
        SettingsHost host;
        host.current_name = [&] { return name; };
        host.apply_name = [&](const std::string &new_name) {
            end_session();
            ui.showing_video = false;
            name = new_name;
            save_receiver_name(name);
            set_title(ui, name);
            bool ok = receiver.rename(name);
            status = ok ? kWaiting : kNoBonjour;
            return ok;
        };
        host.recording = [&] { return recorder.state() != Recorder::State::Off; };
        host.sessions = &sessions;
        show_settings_dialog(ui.hwnd, host);
        dirty = true;
    };
    bool running = true;

    // --- Updates -------------------------------------------------------------
    // What's New: if this version differs from the one that ran last, show what
    // changed. The updater leaves the notes in whatsnew.txt; after a manual update
    // (new zip unpacked by hand) they are fetched from GitHub instead.
    const bool auto_update = load_setting("auto_update") != "0";
    const std::string last_version = load_setting("last_version");
    std::string whats_new;            // shown once the window is idle
    std::string whats_new_since;      // waiting for the release list to build it
    if (last_version != OMP_VERSION) {
        whats_new = read_text_file(whats_new_path());
        if (!whats_new.empty() || last_version.empty()) save_setting("last_version", OMP_VERSION);
        else whats_new_since = last_version;
    }
    std::remove(whats_new_path().c_str());
    bool show_welcome = load_setting("welcome_shown") != "1"; // first launch ever

    Release offered;     // newer release to offer once the window is idle
    std::string offered_notes;
    if (auto_update || !whats_new_since.empty()) fetch_releases_async(FetchPurpose::Startup);

    auto newest_update = [](const std::vector<Release> &releases) -> const Release * {
        for (const Release &r : releases) { // newest first
            if (compare_versions(r.version, OMP_VERSION) > 0 && !r.zip_url.empty()) return &r;
        }
        return nullptr;
    };
    auto on_releases = [&](const FetchResult &r) {
        if (r.purpose == FetchPurpose::WhatsNew) {
            std::string notes = r.ok ? notes_between(r.releases, "", OMP_VERSION) : "";
            if (notes.empty()) notes = r.ok ? "No release notes have been published yet."
                                            : "Could not load the release notes: " + r.error + ".";
            show_whats_new_dialog(ui.hwnd, std::string("What's new in OpenMirrorPhone ") + OMP_VERSION, notes);
            return;
        }
        if (!r.ok) {
            if (r.purpose == FetchPurpose::CheckNow) {
                std::string text = "Could not check for updates: " + r.error + ".";
                MessageBoxW(ui.hwnd, to_wide(text).c_str(), L"OpenMirrorPhone", MB_ICONWARNING);
            }
            return;
        }
        if (!whats_new_since.empty()) {
            whats_new = notes_between(r.releases, whats_new_since, OMP_VERSION);
            whats_new_since.clear();
            save_setting("last_version", OMP_VERSION);
        }
        const Release *update = newest_update(r.releases);
        bool skipped = update && r.purpose == FetchPurpose::Startup && load_setting("skip_version") == update->version;
        if (update && !skipped && (auto_update || r.purpose == FetchPurpose::CheckNow)) {
            offered = *update;
            offered_notes = notes_between(r.releases, OMP_VERSION, update->version);
        } else if (r.purpose == FetchPurpose::CheckNow) {
            std::string text = std::string("You have the latest version of OpenMirrorPhone (") + OMP_VERSION + ").";
            MessageBoxW(ui.hwnd, to_wide(text).c_str(), L"OpenMirrorPhone", MB_ICONINFORMATION);
        }
    };
    // Dialogs about updates wait until nobody is mirroring.
    auto show_pending_update_dialogs = [&] {
        if (ui.showing_video) return;
        if (show_welcome) {
            show_welcome = false;
            save_setting("welcome_shown", "1");
            show_welcome_dialog(ui.hwnd);
            dirty = true;
        }
        if (!whats_new.empty()) {
            std::string notes = whats_new;
            whats_new.clear();
            show_whats_new_dialog(ui.hwnd, std::string("OpenMirrorPhone has been updated to version ") + OMP_VERSION + ".",
                                  notes);
            dirty = true;
        }
        if (!offered.version.empty()) {
            Release r = offered;
            offered = Release{};
            switch (show_update_dialog(ui.hwnd, r.version, offered_notes, r.zip_url)) {
            case UpdateChoice::Skip: save_setting("skip_version", r.version); break;
            case UpdateChoice::Installed: {
                std::ofstream(whats_new_path(), std::ios::binary) << offered_notes;
                running = false; // the new version is starting and waits for us to exit
                break;
            }
            case UpdateChoice::Later: break;
            }
            dirty = true;
        }
    };

    auto on_menu = [&](int id) {
        switch (id) {
        case IDM_SETTINGS: open_settings(); break;
        case IDM_FULLSCREEN: toggle_fullscreen(ui); break;
        case IDM_RECORD: toggle_recording(); break;
        case IDM_FOLDER: shell_open(recordings_dir()); break;
        case IDM_CHECK_UPDATES: fetch_releases_async(FetchPurpose::CheckNow); break;
        case IDM_WHATS_NEW: fetch_releases_async(FetchPurpose::WhatsNew); break;
        case IDM_DONATE: show_welcome_dialog(ui.hwnd); break;
        case IDM_ABOUT: show_about_dialog(ui.hwnd); break;
        case IDM_EXIT: running = false; break;
        }
    };

    AVFrame *frame = av_frame_alloc();
    int shown_rec_second = -1;
    while (running) {
        SDL_Event e;
        // Wake up often enough to pick up decoded frames promptly (~250 Hz).
        if (SDL_WaitEventTimeout(&e, 4)) {
            do {
                if (e.type == SDL_QUIT) {
                    running = false;
                } else if (e.type == SDL_SYSWMEVENT && e.syswm.msg->msg.win.msg == WM_COMMAND &&
                           HIWORD(e.syswm.msg->msg.win.wParam) == 0) {
                    on_menu(LOWORD(e.syswm.msg->msg.win.wParam));
                } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                    SDL_Keycode k = e.key.keysym.sym;
                    if (k == SDLK_F11) toggle_fullscreen(ui);
                    else if (k == SDLK_r && !(e.key.keysym.mod & (KMOD_CTRL | KMOD_ALT))) toggle_recording();
                    else if (k == SDLK_COMMA && (e.key.keysym.mod & KMOD_CTRL)) open_settings();
                    else if (k == SDLK_ESCAPE && (SDL_GetWindowFlags(ui.window) & SDL_WINDOW_FULLSCREEN_DESKTOP))
                        toggle_fullscreen(ui);
                } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                           e.button.clicks == 2) {
                    toggle_fullscreen(ui);
                } else if (e.type == SDL_WINDOWEVENT) {
                    dirty = true;
                } else if (e.type == g_receiver_event) {
                    switch (static_cast<Receiver::Event>(e.user.code)) {
                    case Receiver::Event::ClientConnected: status = "Device connected"; break;
                    case Receiver::Event::VideoStarted: {
                        std::string device, model;
                        receiver.client(&device, &model);
                        sessions.begin(device, model);
                        if (!pending_recording.empty() && recorder.state() != Recorder::State::Off)
                            sessions.add_recording(pending_recording);
                        pending_recording.clear();
                        ui.showing_video = true;
                        break;
                    }
                    case Receiver::Event::VideoStopped:
                    case Receiver::Event::ClientDisconnected:
                        end_session();
                        ui.showing_video = false;
                        status = kWaiting;
                        break;
                    case Receiver::Event::ConnectionLost:
                        end_session();
                        ui.showing_video = false;
                        status = u8"Connection lost, waiting\u2026";
                        receiver.restart_after_reset();
                        break;
                    }
                    dirty = true;
                } else if (e.type == g_update_event) {
                    auto *result = static_cast<FetchResult *>(e.user.data1);
                    on_releases(*result);
                    delete result;
                    dirty = true;
                }
            } while (SDL_PollEvent(&e));
        }

        if (video.take_frame(frame) && upload_frame(ui, frame)) {
            ui.showing_video = true;
            dirty = true;
        }
        // Keep the REC clock ticking even when the mirrored screen is still.
        int rec_second = recorder.state() == Recorder::State::Recording ? static_cast<int>(recorder.seconds()) : -1;
        if (rec_second != shown_rec_second) {
            shown_rec_second = rec_second;
            dirty = true;
        }
        if (dirty) {
            if (ui.showing_video) draw_video(ui);
            else draw_idle(ui, name, status);
            draw_rec(ui, recorder);
            SDL_RenderPresent(ui.renderer);
            dirty = false;
        }
        if (show_welcome || !whats_new.empty() || !offered.version.empty()) show_pending_update_dialogs();
    }

    end_session();
    receiver.stop();
    av_frame_free(&frame);
    if (ui.video) SDL_DestroyTexture(ui.video);
    for (TTF_Font *f : {ui.name_font, ui.body_font}) {
        if (f) TTF_CloseFont(f);
    }
    TTF_Quit();
    SDL_DestroyRenderer(ui.renderer);
    SDL_DestroyWindow(ui.window);
    if (ui.menu) DestroyMenu(ui.menu);
    SDL_Quit();
    return 0;
}
