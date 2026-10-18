#pragma once

#include <string>

struct AVFrame;

// Save one decoded video frame as a standalone PNG. `path` receives the file
// path on success or a short error message on failure.
bool save_screenshot_png(const AVFrame *source, std::string *path);
