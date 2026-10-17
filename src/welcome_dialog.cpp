#include "welcome_dialog.h"

#include "network.h"
#include "resource.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <wincodec.h>

#include <algorithm>

#ifndef OMP_DONATE_URL
#define OMP_DONATE_URL "https://paypal.me/CallMeElio"
#endif

namespace {

struct WelcomeState {
    HFONT bold = nullptr;
    HBITMAP donate = nullptr; // premultiplied BGRA, sized to the button
    int donate_w = 0, donate_h = 0;
};

WelcomeState &state_of(HWND dlg) { return *reinterpret_cast<WelcomeState *>(GetWindowLongPtrW(dlg, DWLP_USER)); }

template <typename T> void release(T *&p) {
    if (p) p->Release();
    p = nullptr;
}

// Loads the PNG embedded as RCDATA `name`, scaled to fit `max_w` x `max_h` with its
// aspect ratio, as a premultiplied 32-bit DIB ready for AlphaBlend.
HBITMAP load_png_resource(const char *name, int max_w, int max_h, int *out_w, int *out_h) {
    HRSRC res = FindResourceA(nullptr, name, MAKEINTRESOURCEA(10) /* RT_RCDATA */);
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
        SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromMemory(data, size)) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w && h) {
        double scale = std::min(static_cast<double>(max_w) / w, static_cast<double>(max_h) / h);
        if (scale > 1.0) scale = 1.0; // never upscale
        UINT sw = static_cast<UINT>(w * scale + 0.5), sh = static_cast<UINT>(h * scale + 0.5);
        if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(frame, sw, sh, WICBitmapInterpolationModeFant)) &&
            SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                            0.0, WICBitmapPaletteTypeCustom))) {
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof bi.bmiHeader;
            bi.bmiHeader.biWidth = static_cast<LONG>(sw);
            bi.bmiHeader.biHeight = -static_cast<LONG>(sh); // top-down
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            void *bits = nullptr;
            bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (bitmap && FAILED(converter->CopyPixels(nullptr, sw * 4, sw * sh * 4, static_cast<BYTE *>(bits)))) {
                DeleteObject(bitmap);
                bitmap = nullptr;
            }
            *out_w = static_cast<int>(sw);
            *out_h = static_cast<int>(sh);
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

void draw_donate_button(const DRAWITEMSTRUCT &di, const WelcomeState &s) {
    HDC dc = di.hDC;
    RECT rc = di.rcItem;
    FillRect(dc, &rc, GetSysColorBrush(COLOR_3DFACE));
    if (s.donate) {
        int x = rc.left + (rc.right - rc.left - s.donate_w) / 2;
        int y = rc.top + (rc.bottom - rc.top - s.donate_h) / 2;
        if (di.itemState & ODS_SELECTED) y += 1; // a small press effect
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, s.donate);
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        AlphaBlend(dc, x, y, s.donate_w, s.donate_h, mem, 0, 0, s.donate_w, s.donate_h, blend);
        SelectObject(mem, old);
        DeleteDC(mem);
    } else {
        DrawTextW(dc, L"Donate with PayPal", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if ((di.itemState & ODS_FOCUS) && !(di.itemState & ODS_NOFOCUSRECT)) {
        RECT focus = rc;
        InflateRect(&focus, -2, -2);
        DrawFocusRect(dc, &focus);
    }
}

INT_PTR CALLBACK welcome_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        WelcomeState &s = state_of(dlg);
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(dlg, WM_GETFONT, 0, 0));
        LOGFONTW lf{};
        GetObjectW(font, sizeof lf, &lf);
        lf.lfWeight = FW_BOLD;
        lf.lfHeight = lf.lfHeight * 13 / 10;
        s.bold = CreateFontIndirectW(&lf);
        SendDlgItemMessageW(dlg, IDC_WELCOME_TITLE, WM_SETFONT, reinterpret_cast<WPARAM>(s.bold), TRUE);

        RECT rc;
        GetClientRect(GetDlgItem(dlg, IDC_DONATE), &rc);
        s.donate = load_png_resource("PAYPAL_PNG", rc.right - 8, rc.bottom - 8, &s.donate_w, &s.donate_h);
        SendMessageW(dlg, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(GetDlgItem(dlg, IDOK)), TRUE);
        return FALSE;
    }
    case WM_DRAWITEM:
        if (wp == IDC_DONATE) {
            draw_donate_button(*reinterpret_cast<DRAWITEMSTRUCT *>(lp), state_of(dlg));
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, TRUE);
            return TRUE;
        }
        break;
    case WM_SETCURSOR:
        if (reinterpret_cast<HWND>(wp) == GetDlgItem(dlg, IDC_DONATE)) {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649) /* IDC_HAND */));
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, TRUE);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_DONATE:
            ShellExecuteW(nullptr, L"open", to_wide(OMP_DONATE_URL).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        case IDOK:
        case IDCANCEL: EndDialog(dlg, 0); return TRUE;
        }
        break;
    case WM_DESTROY: {
        WelcomeState &s = state_of(dlg);
        if (s.bold) DeleteObject(s.bold);
        if (s.donate) DeleteObject(s.donate);
        break;
    }
    }
    return FALSE;
}

} // namespace

void show_welcome_dialog(void *parent_hwnd) {
    WelcomeState state;
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_WELCOME), static_cast<HWND>(parent_hwnd),
                    welcome_proc, reinterpret_cast<LPARAM>(&state));
}
