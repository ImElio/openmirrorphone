#include "theme.h"

#include "settings.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

namespace {

const ThemeColors kLight{
    GetSysColor(COLOR_3DFACE), RGB(255, 255, 255), RGB(255, 255, 255), RGB(0, 0, 0),
    RGB(96, 96, 96),           RGB(223, 223, 223), RGB(204, 232, 255), RGB(153, 209, 255),
};
const ThemeColors kDark{
    RGB(32, 32, 32),  RGB(43, 43, 43),  RGB(45, 45, 45), RGB(240, 240, 240),
    RGB(170, 170, 170), RGB(64, 64, 64), RGB(61, 61, 61), RGB(98, 98, 98),
};

int g_dark = -1; // -1: not read from settings yet
HBRUSH g_page = nullptr, g_sidebar = nullptr, g_field = nullptr;

void make_brushes() {
    for (HBRUSH *b : {&g_page, &g_sidebar, &g_field}) {
        if (*b) DeleteObject(*b);
    }
    const ThemeColors &c = theme_colors();
    g_page = CreateSolidBrush(c.page);
    g_sidebar = CreateSolidBrush(c.sidebar);
    g_field = CreateSolidBrush(c.field);
}

// Dark group box: a thin grey frame and a light caption. The classic one Windows
// draws when themes are off has bright white edges.
LRESULT CALLBACK group_box_proc(HWND box, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg != WM_PAINT || !dark_theme()) return DefSubclassProc(box, msg, wp, lp);
    const ThemeColors &c = theme_colors();
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(box, &ps);
    RECT rc;
    GetClientRect(box, &rc);
    HGDIOBJ old_font = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(box, WM_GETFONT, 0, 0)));
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    wchar_t text[128];
    int len = GetWindowTextW(box, text, 128);

    HPEN pen = CreatePen(PS_SOLID, 1, c.divider);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc, rc.left, rc.top + tm.tmHeight / 2, rc.right, rc.bottom, 6, 6);
    if (len > 0) {
        SIZE size;
        GetTextExtentPoint32W(dc, text, len, &size);
        RECT caption{rc.left + 6, rc.top, rc.left + 6 + size.cx + 6, rc.top + tm.tmHeight};
        FillRect(dc, &caption, theme_page_brush());
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, c.text);
        TextOutW(dc, caption.left + 3, caption.top, text, len);
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_font);
    DeleteObject(pen);
    EndPaint(box, &ps);
    return 0;
}

// List view: the header's item text colour follows the theme (its notifications
// go to the list view, not to the dialog).
LRESULT CALLBACK list_view_proc(HWND list, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_NOTIFY && dark_theme()) {
        auto *nm = reinterpret_cast<NMHDR *>(lp);
        if (nm->hwndFrom == ListView_GetHeader(list) && nm->code == NM_CUSTOMDRAW) {
            auto *cd = reinterpret_cast<NMCUSTOMDRAW *>(lp);
            if (cd->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                SetTextColor(cd->hdc, theme_colors().text);
                return CDRF_DODEFAULT;
            }
        }
    }
    return DefSubclassProc(list, msg, wp, lp);
}

constexpr UINT_PTR kSubclassId = 0x0A11;

void theme_control(HWND child, bool dark) {
    wchar_t cls[32];
    GetClassNameW(child, cls, 32);
    if (!lstrcmpiW(cls, L"Button")) {
        LONG type = GetWindowLongW(child, GWL_STYLE) & BS_TYPEMASK;
        if (type == BS_OWNERDRAW) return;
        if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON) {
            SetWindowTheme(child, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
        } else if (type == BS_GROUPBOX) {
            SetWindowSubclass(child, group_box_proc, kSubclassId, 0); // only paints itself in dark mode
        } else {
            // Themed check boxes and group boxes always draw black text; the
            // classic look honours WM_CTLCOLORSTATIC, so use it in dark mode.
            SetWindowTheme(child, dark ? L"" : nullptr, dark ? L"" : nullptr);
        }
    } else if (!lstrcmpiW(cls, WC_LISTVIEWW)) {
        const ThemeColors &c = theme_colors();
        SetWindowTheme(child, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        ListView_SetBkColor(child, c.field);
        ListView_SetTextBkColor(child, c.field);
        ListView_SetTextColor(child, c.text);
        HWND header = ListView_GetHeader(child);
        if (header) SetWindowTheme(header, dark ? L"DarkMode_ItemsView" : nullptr, nullptr);
        SetWindowSubclass(child, list_view_proc, kSubclassId, 0);
    } else if (!lstrcmpiW(cls, L"ComboBox")) {
        SetWindowTheme(child, dark ? L"DarkMode_CFD" : nullptr, nullptr);
    } else if (!lstrcmpiW(cls, L"Edit")) {
        bool multiline = GetWindowLongW(child, GWL_STYLE) & ES_MULTILINE;
        SetWindowTheme(child, dark ? (multiline ? L"DarkMode_Explorer" : L"DarkMode_CFD") : nullptr, nullptr);
    }
}

BOOL CALLBACK theme_child(HWND child, LPARAM dark) {
    theme_control(child, dark != 0);
    return TRUE;
}

} // namespace

bool dark_theme() {
    if (g_dark < 0) g_dark = load_setting("theme") == "dark" ? 1 : 0;
    return g_dark == 1;
}

void set_dark_theme(bool dark) {
    g_dark = dark ? 1 : 0;
    save_setting("theme", dark ? "dark" : "light");
    make_brushes();
}

const ThemeColors &theme_colors() { return dark_theme() ? kDark : kLight; }

HBRUSH theme_page_brush() {
    if (!g_page) make_brushes();
    return g_page;
}
HBRUSH theme_sidebar_brush() {
    if (!g_sidebar) make_brushes();
    return g_sidebar;
}
HBRUSH theme_field_brush() {
    if (!g_field) make_brushes();
    return g_field;
}

void apply_theme(HWND window) {
    bool dark = dark_theme();
    // Dark title bar: attribute 20 on Windows 10 20H1 and later, 19 before.
    BOOL value = dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(window, 20, &value, sizeof value)))
        DwmSetWindowAttribute(window, 19, &value, sizeof value);
    EnumChildWindows(window, theme_child, dark ? 1 : 0);
    SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

bool theme_ctlcolor(UINT msg, WPARAM wp, LPARAM lp, INT_PTR *result) {
    const ThemeColors &c = theme_colors();
    HDC dc = reinterpret_cast<HDC>(wp);
    switch (msg) {
    case WM_CTLCOLORDLG:
        *result = reinterpret_cast<INT_PTR>(theme_page_brush());
        return true;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(dc, c.text);
        SetBkColor(dc, c.field);
        *result = reinterpret_cast<INT_PTR>(theme_field_brush());
        return true;
    case WM_CTLCOLORSTATIC: {
        // Read-only edit boxes also send WM_CTLCOLORSTATIC: keep them field-coloured.
        wchar_t cls[16];
        GetClassNameW(reinterpret_cast<HWND>(lp), cls, 16);
        bool field = !lstrcmpiW(cls, L"Edit");
        SetTextColor(dc, c.text);
        SetBkColor(dc, field ? c.field : c.page);
        *result = reinterpret_cast<INT_PTR>(field ? theme_field_brush() : theme_page_brush());
        return true;
    }
    case WM_CTLCOLORBTN:
        SetBkColor(dc, c.page);
        *result = reinterpret_cast<INT_PTR>(theme_page_brush());
        return true;
    }
    return false;
}
