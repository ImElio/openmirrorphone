#include "welcome_dialog.h"

#include "icons.h"
#include "paypal_banner.h"
#include "resource.h"
#include "theme.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

struct WelcomeState {
    HFONT bold = nullptr;
    HBITMAP donate = nullptr; // premultiplied BGRA, sized to the button
    int donate_w = 0, donate_h = 0;
};

WelcomeState &state_of(HWND dlg) { return *reinterpret_cast<WelcomeState *>(GetWindowLongPtrW(dlg, DWLP_USER)); }

INT_PTR CALLBACK welcome_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    INT_PTR brush = 0;
    if (theme_ctlcolor(msg, wp, lp, &brush)) return brush; // light or dark theme
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
        s.donate = load_paypal_banner(rc.right - 8, rc.bottom - 8, &s.donate_w, &s.donate_h);
        set_button_icon(dlg, IDOK, Icon::Play);
        apply_theme(dlg);
        SendMessageW(dlg, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(GetDlgItem(dlg, IDOK)), TRUE);
        return FALSE;
    }
    case WM_DRAWITEM:
        if (wp == IDC_DONATE) {
            const WelcomeState &s = state_of(dlg);
            draw_paypal_banner(*reinterpret_cast<DRAWITEMSTRUCT *>(lp), s.donate, s.donate_w, s.donate_h);
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
            open_paypal_page();
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
