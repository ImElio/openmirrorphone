#include "network.h"

#include <winsock2.h>
#include <iphlpapi.h>
#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

std::string find_mac_address() {
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf(size);
    auto *addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buf.data());
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &size) == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buf.data());
    }
    if (GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &size) == NO_ERROR) {
        for (auto *a = addrs; a; a = a->Next) {
            bool usable = a->OperStatus == IfOperStatusUp && a->PhysicalAddressLength == 6 &&
                          (a->IfType == IF_TYPE_ETHERNET_CSMACD || a->IfType == IF_TYPE_IEEE80211) &&
                          a->FirstGatewayAddress != nullptr;
            if (!usable) continue;
            char mac[18];
            const BYTE *p = a->PhysicalAddress;
            std::snprintf(mac, sizeof mac, "%02X:%02X:%02X:%02X:%02X:%02X", p[0], p[1], p[2], p[3], p[4], p[5]);
            return mac;
        }
    }
    // Locally administered fallback so the receiver still starts without a real adapter.
    return "02:53:43:52:4E:52";
}

std::string computer_name() {
    char name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD len = sizeof name;
    if (GetComputerNameA(name, &len)) return std::string(name, len);
    return "OpenMirrorPhone";
}

std::string app_data_dir() {
    char path[MAX_PATH];
    std::string dir = ".";
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        dir = std::string(path) + "\\OpenMirrorPhone";
        CreateDirectoryA(dir.c_str(), nullptr);
    }
    return dir;
}

std::wstring to_wide(const std::string &utf8) {
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wide(len > 0 ? len - 1 : 0, L'\0');
    if (len > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], len);
    return wide;
}

std::string to_utf8(const std::wstring &wide) {
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(len > 0 ? len - 1 : 0, '\0');
    if (len > 1) WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], len, nullptr, nullptr);
    return utf8;
}

std::string recordings_dir() {
    std::wstring dir;
    PWSTR videos = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &videos))) dir = videos;
    CoTaskMemFree(videos);
    if (dir.empty()) {
        const wchar_t *profile = _wgetenv(L"USERPROFILE");
        dir = std::wstring(profile ? profile : L".") + L"\\Videos";
    }
    dir += L"\\OpenMirrorPhone";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return to_utf8(dir);
}
