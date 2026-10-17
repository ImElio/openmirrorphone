#include "settings.h"

#include "network.h"

#include <fstream>
#include <map>

namespace {
constexpr size_t kMaxNameBytes = 63 - 13; // 63-byte label minus "XXXXXXXXXXXX@"

std::string settings_path() { return app_data_dir() + "\\settings.ini"; }

std::map<std::string, std::string> read_all() {
    std::map<std::string, std::string> values;
    std::ifstream in(settings_path(), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq != std::string::npos) values[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return values;
}
} // namespace

std::string load_setting(const std::string &key) {
    auto values = read_all();
    auto it = values.find(key);
    return it == values.end() ? std::string() : it->second;
}

void save_setting(const std::string &key, const std::string &value) {
    auto values = read_all();
    values[key] = value;
    std::ofstream out(settings_path(), std::ios::binary | std::ios::trunc);
    for (const auto &kv : values) out << kv.first << '=' << kv.second << "\r\n";
}

std::string load_receiver_name() { return clean_receiver_name(load_setting("name")); }

void save_receiver_name(const std::string &name) { save_setting("name", name); }

std::string clean_receiver_name(const std::string &name) {
    size_t begin = name.find_first_not_of(" \t");
    if (begin == std::string::npos) return {};
    size_t end = name.find_last_not_of(" \t");
    std::string s = name.substr(begin, end - begin + 1);
    if (s.size() > kMaxNameBytes) {
        s.resize(kMaxNameBytes);
        // Don't leave half of a UTF-8 character at the end.
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
        if (!s.empty() && (static_cast<unsigned char>(s.back()) & 0x80)) s.pop_back();
    }
    for (char &c : s) {
        if (c == '\r' || c == '\n') c = ' '; // keep settings.ini one entry per line
    }
    return s;
}
