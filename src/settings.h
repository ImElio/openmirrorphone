#pragma once

#include <string>

// Settings in %APPDATA%\OpenMirrorPhone\settings.ini: "key=value" lines, UTF-8.
std::string load_setting(const std::string &key);
void save_setting(const std::string &key, const std::string &value);

// Receiver name chosen by the user ("name" key). Empty if never set.
std::string load_receiver_name();
void save_receiver_name(const std::string &name);

// Trims spaces and cuts the name to what Bonjour accepts: the RAOP service is
// advertised as "<12 hex digits>@<name>" and a DNS-SD label is at most 63 bytes.
std::string clean_receiver_name(const std::string &name);
