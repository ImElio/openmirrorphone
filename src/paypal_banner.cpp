#include "paypal_banner.h"

#include "network.h"

#include <shellapi.h>
#include <wincodec.h>

#include <algorithm>

#ifndef OMP_DONATE_URL
#define OMP_DONATE_URL "https://paypal.me/CallMeElio"
#endif

namespace {

template <typename T> void release(T *&p) {
    if (p) p->Release();
    p = nullptr;
}

} // namespace

HBITMAP load_paypal_banner(int max_width, int max_height, int *out_width, int *out_height) {
    HRSRC res = FindResourceA(nullptr, "PAYPAL_PNG", MAKEINTRESOURCEA(10) /* RT_RCDATA */);
    HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
    if (!mem) return nullptr;
    auto *data = static_cast<BYTE *>(LockResource(mem));
    DWORD size = SizeofResource(nullptr, res);

    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory *factory = nullptr;
    IWICStream *stream = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICBitmapScaler *scaler = nullptr;
    IWICFormatConverter *converter = nullptr;
    HBITMAP bitmap = nullptr;

    UINT w = 0, h = 0;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateStream(&stream)) && stream &&
        SUCCEEDED(stream->InitializeFromMemory(data, size)) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w && h) {
        double scale = std::min(static_cast<double>(max_width) / w, static_cast<double>(max_height) / h);
        if (scale > 1.0) scale = 1.0;
        UINT sw = static_cast<UINT>(w * scale + 0.5), sh = static_cast<UINT>(h * scale + 0.5);
        if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(frame, sw, sh, WICBitmapInterpolationModeFant)) &&
            SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                            0.0, WICBitmapPaletteTypeCustom))) {
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof bi.bmiHeader;
            bi.bmiHeader.biWidth = static_cast<LONG>(sw);
            bi.bmiHeader.biHeight = -static_cast<LONG>(sh);
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            void *bits = nullptr;
            bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (bitmap && FAILED(converter->CopyPixels(nullptr, sw * 4, sw * sh * 4, static_cast<BYTE *>(bits)))) {
                DeleteObject(bitmap);
                bitmap = nullptr;
            }
            *out_width = static_cast<int>(sw);
            *out_height = static_cast<int>(sh);
        }
    }
    release(converter);
    release(scaler);
    release(frame);
    release(decoder);
    release(stream);
    release(factory);
    if (SUCCEEDED(com)) CoUninitialize();
    return bitmap;
}

void draw_paypal_banner(const DRAWITEMSTRUCT &item, HBITMAP bitmap, int width, int height) {
    RECT rc = item.rcItem;
    HWND parent = GetParent(item.hwndItem);
    RECT screen{};
    GetWindowRect(item.hwndItem, &screen);
    POINT sample{screen.left - 1, screen.top - 1};
    if (parent) ScreenToClient(parent, &sample);
    HDC parent_dc = parent ? GetDC(parent) : nullptr;
    COLORREF background = parent_dc ? GetPixel(parent_dc, sample.x, sample.y) : GetSysColor(COLOR_3DFACE);
    if (parent_dc) ReleaseDC(parent, parent_dc);
    HBRUSH background_brush = CreateSolidBrush(background);
    FillRect(item.hDC, &rc, background_brush);
    DeleteObject(background_brush);
    if (bitmap) {
        int x = rc.left + (rc.right - rc.left - width) / 2;
        int y = rc.top + (rc.bottom - rc.top - height) / 2;
        if (item.itemState & ODS_SELECTED) ++y;
        HDC mem = CreateCompatibleDC(item.hDC);
        HGDIOBJ old = SelectObject(mem, bitmap);
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        AlphaBlend(item.hDC, x, y, width, height, mem, 0, 0, width, height, blend);
        SelectObject(mem, old);
        DeleteDC(mem);
    } else {
        DrawTextW(item.hDC, L"Donate with PayPal", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        InflateRect(&rc, -2, -2);
        DrawFocusRect(item.hDC, &rc);
    }
}

void open_paypal_page() {
    ShellExecuteW(nullptr, L"open", to_wide(OMP_DONATE_URL).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
