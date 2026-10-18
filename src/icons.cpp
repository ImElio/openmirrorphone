#include "icons.h"

#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Colour {
    char ch;
    uint8_t r, g, b;
};

const Colour kPalette[] = {
#define PALETTE(ch, r, g, b) {ch, r, g, b},
#define ICON(name, rows)
#include "icons_data.inc"
#undef ICON
#undef PALETTE
};

const char *const kRows[] = {
#define PALETTE(ch, r, g, b)
#define ICON(name, rows) rows,
#include "icons_data.inc"
#undef ICON
#undef PALETTE
};

// Decoded once: kIconSize * kIconSize RGBA pixels per icon.
const std::vector<uint8_t> &decoded(Icon icon) {
    static std::vector<std::vector<uint8_t>> cache;
    if (cache.empty()) {
        for (const char *rows : kRows) {
            std::vector<uint8_t> px(kIconSize * kIconSize * 4, 0);
            for (int i = 0; i < kIconSize * kIconSize; ++i) {
                for (const Colour &c : kPalette) {
                    if (c.ch == rows[i]) {
                        px[i * 4 + 0] = c.r;
                        px[i * 4 + 1] = c.g;
                        px[i * 4 + 2] = c.b;
                        px[i * 4 + 3] = 255;
                        break;
                    }
                }
            }
            cache.push_back(std::move(px));
        }
    }
    return cache[static_cast<size_t>(icon)];
}

HICON create_icon(Icon icon, int size) {
    const std::vector<uint8_t> &px = decoded(icon);
    int scale = size / kIconSize > 0 ? size / kIconSize : 1;
    size = kIconSize * scale;

    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof bi;
    bi.bV5Width = size;
    bi.bV5Height = -size; // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void *bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO *>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color) return nullptr;
    auto *out = static_cast<uint32_t *>(bits);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) { // nearest neighbour keeps the pixels sharp
            const uint8_t *p = &px[((y / scale) * kIconSize + x / scale) * 4];
            out[y * size + x] = (uint32_t(p[3]) << 24) | (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
        }
    }
    std::vector<uint8_t> mask_bits(static_cast<size_t>(((size + 15) / 16) * 2 * size), 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, mask_bits.data());
    ICONINFO info{TRUE, 0, 0, mask, color};
    HICON handle = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return handle;
}

} // namespace

const uint8_t *icon_rgba(Icon icon) { return decoded(icon).data(); }

HICON icon_handle(Icon icon, int size) {
    static std::map<std::pair<int, int>, HICON> cache;
    auto key = std::make_pair(static_cast<int>(icon), size);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    HICON handle = create_icon(icon, size);
    cache[key] = handle;
    return handle;
}

void set_button_icon(HWND dialog, int button_id, Icon icon) {
    HWND button = GetDlgItem(dialog, button_id);
    // A leading space keeps the text from touching the icon.
    wchar_t text[128];
    int len = GetWindowTextW(button, text, 127);
    if (len > 0 && text[0] != L' ') {
        std::wstring spaced = L" " + std::wstring(text, len);
        SetWindowTextW(button, spaced.c_str());
    }
    SendMessageW(button, BM_SETIMAGE, IMAGE_ICON, reinterpret_cast<LPARAM>(icon_handle(icon)));
}
