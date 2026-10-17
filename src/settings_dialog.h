#pragma once

#include <functional>
#include <string>

class SessionLog;

// What the Settings dialog needs from the rest of the app.
struct SettingsHost {
    std::function<std::string()> current_name;
    // Saves and re-advertises the receiver under a new name; false on failure.
    std::function<bool(const std::string &)> apply_name;
    // True while the running session is being recorded (its file can't be renamed).
    std::function<bool()> recording;
    SessionLog *sessions = nullptr;
};

// Modal, native Windows dialog: receiver name and session history.
void show_settings_dialog(void *parent_hwnd, SettingsHost &host);

// Help → About.
void show_about_dialog(void *parent_hwnd);

// Opens a folder or file with its default app (Explorer, the video player).
void shell_open(const std::string &utf8_path);
