#pragma once

#include <functional>
#include "keybinds.h"
#include <string>
#include <vector>

class SessionLog;

// What the Settings dialog needs from the rest of the app.
struct SettingsHost {
    std::function<std::string()> current_name;
    // Saves and re-advertises the receiver under a new name; false on failure.
    std::function<bool(const std::string &)> apply_name;
    // True while the running session is being recorded (its file can't be renamed).
    std::function<bool()> recording;
    std::function<bool()> always_on_top;
    std::function<bool(bool)> set_always_on_top;
    std::function<void()> toggle_fullscreen;
    std::function<void()> toggle_recording;
    std::function<void()> open_recordings_folder;
    std::function<std::vector<std::string>()> audio_output_devices;
    std::function<std::string()> audio_output_device;
    std::function<bool(const std::string &)> set_audio_output_device;
    std::function<std::string()> take_screenshot;
    KeybindSet keybinds = default_keybinds();
    std::function<bool(const KeybindSet &)> set_keybinds;
    std::function<void()> exit_app;      // quit after an update installed from Settings
    std::function<void()> theme_changed; // re-theme the app's other windows
    SessionLog *sessions = nullptr;
};

// Modal, native Windows window containing all app settings and actions.
void show_settings_dialog(void *parent_hwnd, SettingsHost &host);

// Opens a folder or file with its default app (Explorer, the video player).
void shell_open(const std::string &utf8_path);
