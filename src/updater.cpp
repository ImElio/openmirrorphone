#include "updater.h"

#include "network.h"

#include <windows.h>
#include <winhttp.h>

#include <plist/plist.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#ifndef OMP_VERSION
#define OMP_VERSION "0.0.0"
#endif
#ifndef OMP_GITHUB_REPO
#define OMP_GITHUB_REPO "ImElio/openmirrorphone"
#endif

namespace {

// Where the release list comes from. OPENMIRRORPHONE_UPDATE_FEED overrides it,
// so the updater can be tested against a local server before anything is published.
std::string feed_url() {
    const char *override_url = std::getenv("OPENMIRRORPHONE_UPDATE_FEED");
    if (override_url && *override_url) return override_url;
    return std::string("https://api.github.com/repos/") + OMP_GITHUB_REPO + "/releases?per_page=30";
}

struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET handle) : h(handle) {}
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
    operator HINTERNET() const { return h; }
};

// GET `url`, calling `sink` for each chunk of the body. Follows redirects (GitHub
// download links redirect to its CDN).
bool http_get(const std::string &url, const std::function<bool(const char *, size_t, uint64_t)> &sink,
              std::string *error) {
    std::wstring wurl = to_wide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof parts;
    wchar_t host[256], path[2048];
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    wchar_t extra[2048];
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) {
        *error = "invalid address";
        return false;
    }
    std::wstring object = std::wstring(path, parts.dwUrlPathLength) + std::wstring(extra, parts.dwExtraInfoLength);

    std::wstring agent = L"OpenMirrorPhone/" + to_wide(OMP_VERSION);
    Handle session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        *error = "no network access";
        return false;
    }
    WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000);
    Handle connection(WinHttpConnect(session, std::wstring(host, parts.dwHostNameLength).c_str(), parts.nPort, 0));
    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle request(connection ? WinHttpOpenRequest(connection, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, flags)
                              : nullptr);
    if (!request) {
        *error = "cannot connect";
        return false;
    }
    const wchar_t *headers = L"Accept: application/vnd.github+json\r\n";
    if (!WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        *error = "no answer from the server (offline?)";
        return false;
    }
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        *error = status == 404 ? "no releases published yet" : "server answered " + std::to_string(status);
        return false;
    }
    uint64_t total = 0;
    wchar_t length[32];
    DWORD length_size = sizeof length;
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length, &length_size,
                            WINHTTP_NO_HEADER_INDEX)) {
        total = _wcstoui64(length, nullptr, 10);
    }

    std::vector<char> buf(64 * 1024);
    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(request, buf.data(), static_cast<DWORD>(buf.size()), &got)) {
            *error = "download interrupted";
            return false;
        }
        if (got == 0) return true;
        if (!sink(buf.data(), got, total)) {
            *error = "cancelled";
            return false;
        }
    }
}

const char *dict_string(plist_t dict, const char *key) {
    plist_t node = plist_dict_get_item(dict, key);
    if (!node || plist_get_node_type(node) != PLIST_STRING) return "";
    const char *s = plist_get_string_ptr(node, nullptr);
    return s ? s : "";
}

bool dict_bool(plist_t dict, const char *key) {
    plist_t node = plist_dict_get_item(dict, key);
    uint8_t value = 0;
    if (node && plist_get_node_type(node) == PLIST_BOOLEAN) plist_get_bool_val(node, &value);
    return value != 0;
}

bool ends_with(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Markdown release notes → plain text for a read-only edit box.
std::string plain_notes(const std::string &markdown) {
    std::istringstream in(markdown);
    std::string line, out;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t first = line.find_first_not_of(' ');
        if (first != std::string::npos && line[first] == '#') {
            size_t text = line.find_first_not_of("# ", first);
            line = text == std::string::npos ? "" : line.substr(text);
        } else if (first != std::string::npos && (line.compare(first, 2, "- ") == 0 || line.compare(first, 2, "* ") == 0)) {
            line = std::string(first, ' ') + u8"  \u2022 " + line.substr(first + 2);
        }
        for (size_t p; (p = line.find("**")) != std::string::npos;) line.erase(p, 2);
        for (size_t p; (p = line.find('`')) != std::string::npos;) line.erase(p, 1);
        out += line + "\r\n";
    }
    while (ends_with(out, "\r\n\r\n")) out.resize(out.size() - 2);
    return out;
}

std::wstring app_dir() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path, n);
    return p.substr(0, p.find_last_of(L'\\'));
}

bool run_hidden(const std::wstring &command_line, DWORD *exit_code) {
    std::wstring cmd = command_line;
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 120000);
    GetExitCodeProcess(pi.hProcess, exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// Files under `dir` (with subfolders if `recursive`), as paths relative to it.
void list_files(const std::wstring &dir, const std::wstring &rel, std::vector<std::wstring> *files,
                bool recursive = true) {
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\" + rel + L"*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recursive) list_files(dir, rel + name + L"\\", files);
        } else {
            files->push_back(rel + name);
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

bool exists(const std::wstring &path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

} // namespace

int compare_versions(const std::string &a, const std::string &b) {
    std::istringstream sa(a), sb(b);
    for (int i = 0; i < 4; ++i) {
        long va = 0, vb = 0;
        std::string pa, pb;
        if (std::getline(sa, pa, '.')) va = std::atol(pa.c_str());
        if (std::getline(sb, pb, '.')) vb = std::atol(pb.c_str());
        if (va != vb) return va < vb ? -1 : 1;
    }
    return 0;
}

bool fetch_releases(std::vector<Release> *releases, std::string *error) {
    std::string json;
    bool ok = http_get(
        feed_url(),
        [&](const char *data, size_t size, uint64_t) {
            json.append(data, size);
            return json.size() < 8 * 1024 * 1024;
        },
        error);
    if (!ok) return false;

    plist_t root = nullptr;
    if (plist_from_json(json.data(), static_cast<uint32_t>(json.size()), &root) != PLIST_ERR_SUCCESS || !root ||
        plist_get_node_type(root) != PLIST_ARRAY) {
        if (root) plist_free(root);
        *error = "unexpected answer from GitHub";
        return false;
    }
    releases->clear();
    for (uint32_t i = 0; i < plist_array_get_size(root); ++i) {
        plist_t r = plist_array_get_item(root, i);
        if (plist_get_node_type(r) != PLIST_DICT || dict_bool(r, "draft") || dict_bool(r, "prerelease")) continue;
        Release rel;
        rel.version = dict_string(r, "tag_name");
        if (!rel.version.empty() && (rel.version[0] == 'v' || rel.version[0] == 'V')) rel.version.erase(0, 1);
        if (rel.version.empty()) continue;
        rel.notes = dict_string(r, "body");
        plist_t assets = plist_dict_get_item(r, "assets");
        for (uint32_t j = 0; assets && j < plist_array_get_size(assets); ++j) {
            plist_t asset = plist_array_get_item(assets, j);
            if (ends_with(dict_string(asset, "name"), "-win64.zip")) {
                rel.zip_url = dict_string(asset, "browser_download_url");
                break;
            }
        }
        releases->push_back(rel);
    }
    plist_free(root);
    std::sort(releases->begin(), releases->end(),
              [](const Release &a, const Release &b) { return compare_versions(a.version, b.version) > 0; });
    return true;
}

std::string notes_between(const std::vector<Release> &releases, const std::string &after,
                          const std::string &up_to) {
    std::string text;
    for (const Release &r : releases) {
        if (compare_versions(r.version, up_to) > 0) continue;
        if (!after.empty() && compare_versions(r.version, after) <= 0) continue;
        if (!text.empty()) text += "\r\n";
        text += "Version " + r.version + "\r\n\r\n";
        std::string notes = plain_notes(r.notes);
        text += notes.empty() ? "No details for this version.\r\n" : notes;
    }
    return text;
}

bool download_file(const std::string &url, const std::wstring &path,
                   const std::function<void(uint64_t, uint64_t)> &progress, const std::atomic<bool> &cancel,
                   std::string *error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        *error = "cannot write the download to the temp folder";
        return false;
    }
    uint64_t done = 0;
    bool ok = http_get(
        url,
        [&](const char *data, size_t size, uint64_t total) {
            DWORD written = 0;
            if (cancel || !WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) || written != size)
                return false;
            done += size;
            progress(done, total);
            return true;
        },
        error);
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

bool install_update(const std::wstring &zip_path, const std::string &version, std::string *error) {
    std::wstring dir = app_dir();
    std::wstring staging = zip_path + L".files";
    DWORD ignored = 0;
    run_hidden(L"cmd.exe /c rmdir /s /q \"" + staging + L"\"", &ignored); // leftovers of an earlier try
    CreateDirectoryW(staging.c_str(), nullptr);

    // tar.exe ships with Windows 10 1803+ and unpacks zip files.
    wchar_t system_dir[MAX_PATH];
    GetSystemDirectoryW(system_dir, MAX_PATH);
    DWORD code = 1;
    if (!run_hidden(L"\"" + std::wstring(system_dir) + L"\\tar.exe\" -xf \"" + zip_path + L"\" -C \"" + staging + L"\"",
                    &code) ||
        code != 0) {
        *error = "the downloaded file could not be unpacked";
        return false;
    }
    // Accept a zip with the files at its root or inside one folder.
    std::wstring root = staging;
    if (!exists(root + L"\\OpenMirrorPhone.exe")) {
        WIN32_FIND_DATAW fd;
        HANDLE find = FindFirstFileW((staging + L"\\*").c_str(), &fd);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                std::wstring sub = staging + L"\\" + fd.cFileName;
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.' &&
                    exists(sub + L"\\OpenMirrorPhone.exe"))
                    root = sub;
            } while (FindNextFileW(find, &fd));
            FindClose(find);
        }
    }
    if (!exists(root + L"\\OpenMirrorPhone.exe")) {
        *error = "the download does not contain OpenMirrorPhone.exe";
        return false;
    }

    std::vector<std::wstring> files;
    list_files(root, L"", &files);

    // Running files can't be overwritten but can be renamed: move each current file
    // to *.old, then the new one into place. Undo everything if a step fails.
    std::vector<std::wstring> replaced, added;
    auto rollback = [&] {
        for (const std::wstring &f : added) DeleteFileW((dir + L"\\" + f).c_str());
        for (const std::wstring &f : replaced)
            MoveFileExW((dir + L"\\" + f + L".old").c_str(), (dir + L"\\" + f).c_str(), MOVEFILE_REPLACE_EXISTING);
    };
    for (const std::wstring &f : files) {
        std::wstring target = dir + L"\\" + f;
        size_t slash = target.find_last_of(L'\\');
        CreateDirectoryW(target.substr(0, slash).c_str(), nullptr);
        if (exists(target)) {
            DeleteFileW((target + L".old").c_str());
            if (!MoveFileExW(target.c_str(), (target + L".old").c_str(), MOVEFILE_REPLACE_EXISTING)) {
                rollback();
                *error = "the app folder is not writable (move OpenMirrorPhone to a folder you own, "
                         "or download the update from GitHub by hand)";
                return false;
            }
            replaced.push_back(f);
        } else {
            added.push_back(f);
        }
        if (!MoveFileExW((root + L"\\" + f).c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            rollback();
            *error = "a file could not be replaced";
            return false;
        }
    }

    // Installed with setup.exe: keep "Installed apps" showing the right version.
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenMirrorPhone", 0,
                      KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        std::wstring v = to_wide(version);
        RegSetValueExW(key, L"DisplayVersion", 0, REG_SZ, reinterpret_cast<const BYTE *>(v.c_str()),
                       static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }

    // Start the new version; it waits for this process to exit.
    std::wstring cmd = L"\"" + dir + L"\\OpenMirrorPhone.exe\" --after-update " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        *error = "the new version is installed but could not be started; please start it again";
        return true; // files are in place; quitting is still right
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DeleteFileW(zip_path.c_str());
    return true;
}

void remove_old_update_files() {
    std::wstring dir = app_dir();
    std::vector<std::wstring> files;
    list_files(dir, L"", &files, false); // the package is flat; never walk e.g. all of Downloads
    for (const std::wstring &f : files) {
        if (f.size() > 4 && f.compare(f.size() - 4, 4, L".old") == 0) DeleteFileW((dir + L"\\" + f).c_str());
    }
}

void wait_for_previous_instance(unsigned long pid) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return;
    WaitForSingleObject(process, 15000);
    CloseHandle(process);
}
