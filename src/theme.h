#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Light or dark theme for the app's windows and dialogs ("theme" in settings.ini).
bool dark_theme();
void set_dark_theme(bool dark);

struct ThemeColors {
    COLORREF page;             // dialog background
    COLORREF sidebar;          // Settings sidebar
    COLORREF field;            // edit boxes, lists
    COLORREF text;
    COLORREF secondary_text;
    COLORREF divider;
    COLORREF selection_fill;   // selected page in the Settings sidebar
    COLORREF selection_border;
};
const ThemeColors &theme_colors();

// Brushes for the current theme, owned by the theme module.
HBRUSH theme_page_brush();
HBRUSH theme_sidebar_brush();
HBRUSH theme_field_brush();

// Applies the theme to a window: its title bar and every child control. Call it
// in WM_INITDIALOG, and again on every open window when the theme changes.
void apply_theme(HWND window);

// Handles WM_CTLCOLOR* for a dialog that uses the page colour everywhere.
// Returns true and sets *result when it handled the message.
bool theme_ctlcolor(UINT msg, WPARAM wp, LPARAM lp, INT_PTR *result);
