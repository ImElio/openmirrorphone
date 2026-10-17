#pragma once

#include <string>

enum class UpdateChoice { Later, Skip, Installed };

// "Update available": the notes of every version newer than this one, and
// Install and restart / Later / Skip this version. Install downloads `zip_url`
// with a progress bar and calls install_update(); on Installed the caller must quit.
UpdateChoice show_update_dialog(void *parent_hwnd, const std::string &new_version, const std::string &notes,
                                const std::string &zip_url);

// "What's New": release notes under a one-line heading (after an update, what
// changed since the version used before; from the Help menu, the full history).
void show_whats_new_dialog(void *parent_hwnd, const std::string &heading, const std::string &notes);
