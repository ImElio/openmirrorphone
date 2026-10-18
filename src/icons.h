#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>

// The app's 16x16 pixel-art icons, drawn by tools/make_icons.py (icons_data.inc).
enum class Icon {
#define PALETTE(ch, r, g, b)
#define ICON(name, rows) name,
#include "icons_data.inc"
#undef ICON
#undef PALETTE
    Count
};

constexpr int kIconSize = 16;

// 16x16 pixels, RGBA with straight alpha, rows top to bottom.
const uint8_t *icon_rgba(Icon icon);

// A Windows icon scaled up by whole pixels (size 16, 32, ...). Cached; never destroy it.
HICON icon_handle(Icon icon, int size = kIconSize);

// Shows the icon left of the text on a push button of a dialog.
void set_button_icon(HWND dialog, int button_id, Icon icon);
