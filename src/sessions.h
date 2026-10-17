#pragma once

#include <ctime>
#include <string>
#include <vector>

// One mirroring session: from the moment a device starts showing its screen
// until it stops. Shown in Settings, where the user can rename or delete it.
struct Session {
    std::time_t start = 0;
    int seconds = 0;
    std::string title;  // user-editable; starts as the device name
    std::string device; // "iPhone di Elio"
    std::string model;  // "iPhone16,2"
    std::vector<std::string> recordings; // UTF-8 paths of the MP4s made during it
};

// Session history, kept in %APPDATA%\OpenMirrorPhone\sessions.tsv (UTF-8, one
// session per line, tab-separated). Newest first. UI thread only.
class SessionLog {
public:
    SessionLog();

    const std::vector<Session> &sessions() const { return sessions_; }
    bool active() const { return active_; } // sessions()[0] is still running

    void begin(const std::string &device, const std::string &model);
    void end();
    void add_recording(const std::string &path);

    // Renames a session. Its recordings are renamed to "<title>.mp4" too (with
    // " (2)" etc. if taken). Returns false and changes nothing if a file can't be
    // renamed, e.g. because it is still being recorded or open in a player.
    bool rename(size_t index, const std::string &title);
    // Removes a session from the history; with delete_files, also moves its
    // recordings to the Recycle Bin.
    void remove(size_t index, bool delete_files);

private:
    void load();
    void save() const;

    std::vector<Session> sessions_;
    bool active_ = false;
};

// "1:05:09" or "3:07".
std::string format_duration(int seconds);
