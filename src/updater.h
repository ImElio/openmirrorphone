#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Self-update from GitHub Releases. A release is a tag "vX.Y.Z" with the zip made
// by package.sh attached as "OpenMirrorPhone-X.Y.Z-win64.zip"; its description is
// the release notes shown in "Update available" and "What's New".

struct Release {
    std::string version; // "0.2.0", without the "v"
    std::string notes;   // Markdown, as written on GitHub
    std::string zip_url; // empty if the release has no Windows zip
};

// Downloads the list of published releases (newest first), skipping drafts and
// pre-releases. Blocking: call it off the UI thread. On failure returns false and
// a short reason in *error.
bool fetch_releases(std::vector<Release> *releases, std::string *error);

// -1, 0 or 1, comparing dotted versions numerically ("0.10.0" > "0.9.1").
int compare_versions(const std::string &a, const std::string &b);

// Release notes for every version in (after, up_to], newest first, as plain text
// for a dialog. `after` may be empty for "everything up to".
std::string notes_between(const std::vector<Release> &releases, const std::string &after,
                          const std::string &up_to);

// Downloads `url` to `path`, reporting progress (total is 0 if unknown). Blocking.
bool download_file(const std::string &url, const std::wstring &path,
                   const std::function<void(uint64_t done, uint64_t total)> &progress,
                   const std::atomic<bool> &cancel, std::string *error);

// Unpacks the downloaded zip over the app's folder and starts the new version,
// which waits for this process to exit. The caller must quit right after a true
// return. Files in use are renamed to *.old and cleaned up on the next start.
// `version` is recorded in Windows' list of installed apps when the installer
// registered OpenMirrorPhone there.
bool install_update(const std::wstring &zip_path, const std::string &version, std::string *error);

// Removes the *.old files left by the previous update. Call at startup.
void remove_old_update_files();

// Handles "--after-update <pid>": waits for the previous instance to exit so
// the two never advertise the same receiver at once.
void wait_for_previous_instance(unsigned long pid);
