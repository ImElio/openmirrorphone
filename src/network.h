#pragma once

#include <string>

// MAC address of the first active, non-loopback adapter, as "AA:BB:CC:DD:EE:FF".
// AirPlay uses it as the receiver's device ID, so it must stay stable across runs.
std::string find_mac_address();

// Windows computer name, used as the default AirPlay receiver name.
std::string computer_name();

// %APPDATA%\OpenMirrorPhone, created if missing.
std::string app_data_dir();


// UTF-8 <-> UTF-16 for the wide Windows APIs (file names may have accents).
std::wstring to_wide(const std::string &utf8);
std::string to_utf8(const std::wstring &wide);

// The user's Videos folder (normally %USERPROFILE%\Videos) + \OpenMirrorPhone,
// created if missing. UTF-8, since it goes to FFmpeg.
std::string recordings_dir();
