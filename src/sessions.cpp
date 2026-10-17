#include "sessions.h"

#include "network.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

std::string log_path() { return app_data_dir() + "\\sessions.tsv"; }

// Fields are tab-separated and recordings '|'-separated, so those characters
// (and line breaks) can't appear inside a field.
std::string field(std::string s) {
    for (char &c : s) {
        if (c == '\t' || c == '\n' || c == '\r' || c == '|') c = ' ';
    }
    return s;
}

std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> parts;
    std::string part;
    std::istringstream in(s);
    while (std::getline(in, part, sep)) parts.push_back(part);
    if (!s.empty() && s.back() == sep) parts.emplace_back();
    return parts;
}

// Characters Windows does not allow in file names become spaces.
std::string safe_file_name(const std::string &title) {
    std::string s;
    for (char c : title) {
        unsigned char u = static_cast<unsigned char>(c);
        s += (u < 32 || std::string("<>:\"/\\|?*").find(c) != std::string::npos) ? ' ' : c;
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    return s.empty() ? "Recording" : s;
}

bool file_exists(const std::string &path) {
    return GetFileAttributesW(to_wide(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string dir_of(const std::string &path) {
    size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

} // namespace

std::string format_duration(int seconds) {
    char buf[32];
    if (seconds >= 3600) {
        std::snprintf(buf, sizeof buf, "%d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
    } else {
        std::snprintf(buf, sizeof buf, "%d:%02d", seconds / 60, seconds % 60);
    }
    return buf;
}

SessionLog::SessionLog() { load(); }

void SessionLog::load() {
    std::ifstream in(log_path(), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f = split(line, '\t');
        if (f.size() < 6) continue;
        Session s;
        s.start = static_cast<std::time_t>(std::strtoll(f[0].c_str(), nullptr, 10));
        s.seconds = std::atoi(f[1].c_str());
        s.title = f[2];
        s.device = f[3];
        s.model = f[4];
        for (const std::string &r : split(f[5], '|')) {
            if (!r.empty()) s.recordings.push_back(r);
        }
        sessions_.push_back(s);
    }
}

void SessionLog::save() const {
    std::ofstream out(log_path(), std::ios::binary | std::ios::trunc);
    for (const Session &s : sessions_) {
        out << static_cast<long long>(s.start) << '\t' << s.seconds << '\t' << field(s.title) << '\t'
            << field(s.device) << '\t' << field(s.model) << '\t';
        for (size_t i = 0; i < s.recordings.size(); ++i) out << (i ? "|" : "") << field(s.recordings[i]);
        out << "\r\n";
    }
}

void SessionLog::begin(const std::string &device, const std::string &model) {
    end();
    Session s;
    s.start = std::time(nullptr);
    s.device = device.empty() ? "Unknown device" : device;
    s.model = model;
    s.title = s.device;
    sessions_.insert(sessions_.begin(), s);
    active_ = true;
    save();
}

void SessionLog::end() {
    if (!active_) return;
    Session &s = sessions_[0];
    s.seconds = static_cast<int>(std::time(nullptr) - s.start);
    // A recording stopped before the first keyframe never creates its file.
    s.recordings.erase(std::remove_if(s.recordings.begin(), s.recordings.end(),
                                      [](const std::string &p) { return !file_exists(p); }),
                       s.recordings.end());
    active_ = false;
    save();
}

void SessionLog::add_recording(const std::string &path) {
    if (!active_) return;
    sessions_[0].recordings.push_back(path);
    save();
}

bool SessionLog::rename(size_t index, const std::string &title) {
    if (index >= sessions_.size() || title.empty()) return false;
    Session &s = sessions_[index];

    // Work out every new file name first, so either all files move or none do.
    std::vector<std::string> renamed = s.recordings;
    std::string base = safe_file_name(title);
    int n = 1;
    for (std::string &path : renamed) {
        if (!file_exists(path)) continue; // deleted outside the app: keep the old entry
        std::string target;
        do {
            target = dir_of(path) + "\\" + base + (n > 1 ? " (" + std::to_string(n) + ")" : "") + ".mp4";
            ++n;
        } while (target != path && file_exists(target));
        path = target;
    }
    for (size_t i = 0; i < renamed.size(); ++i) {
        if (renamed[i] == s.recordings[i]) continue;
        if (!MoveFileW(to_wide(s.recordings[i]).c_str(), to_wide(renamed[i]).c_str())) {
            for (size_t j = 0; j < i; ++j) { // undo the ones already moved
                if (renamed[j] != s.recordings[j])
                    MoveFileW(to_wide(renamed[j]).c_str(), to_wide(s.recordings[j]).c_str());
            }
            return false;
        }
    }
    s.recordings = renamed;
    s.title = title;
    save();
    return true;
}

void SessionLog::remove(size_t index, bool delete_files) {
    if (index >= sessions_.size()) return;
    if (delete_files) {
        for (const std::string &path : sessions_[index].recordings) {
            if (!file_exists(path)) continue;
            std::wstring from = to_wide(path);
            from.push_back(L'\0'); // SHFileOperation wants a double-terminated list
            SHFILEOPSTRUCTW op{};
            op.wFunc = FO_DELETE;
            op.pFrom = from.c_str();
            op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
            SHFileOperationW(&op);
        }
    }
    if (index == 0) active_ = false;
    sessions_.erase(sessions_.begin() + static_cast<long>(index));
    save();
}
