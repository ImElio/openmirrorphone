#include "update_dialog.h"

#include "network.h"
#include "resource.h"
#include "icons.h"
#include "theme.h"
#include "updater.h"

#include <windows.h>
#include <commctrl.h>

#include <atomic>
#include <thread>

#ifndef OMP_VERSION
#define OMP_VERSION "0.0.0"
#endif

namespace {

constexpr UINT WM_DL_PROGRESS = WM_APP + 10; // wParam: percent, or -1 if size unknown
constexpr UINT WM_DL_DONE = WM_APP + 11;     // wParam: 1 ok, 0 failed

struct DialogState {
    bool whats_new = false;
    std::string heading, version, notes, zip_url;
    UpdateChoice choice = UpdateChoice::Later;

    std::thread worker;
    std::atomic<bool> cancel{false};
    std::wstring zip_path;
    std::string error;
    bool downloading = false;
    HFONT bold = nullptr;
};

DialogState &state_of(HWND dlg) { return *reinterpret_cast<DialogState *>(GetWindowLongPtrW(dlg, DWLP_USER)); }

void set_text(HWND dlg, int id, const std::string &text) { SetDlgItemTextW(dlg, id, to_wide(text).c_str()); }

void start_download(HWND dlg) {
    DialogState &s = state_of(dlg);
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    s.zip_path = std::wstring(temp) + L"OpenMirrorPhone-" + to_wide(s.version) + L"-update.zip";
    s.downloading = true;
    EnableWindow(GetDlgItem(dlg, IDOK), FALSE);
    EnableWindow(GetDlgItem(dlg, IDC_UPD_SKIP), FALSE);
    set_text(dlg, IDCANCEL, "Cancel");
    set_button_icon(dlg, IDCANCEL, Icon::Cross);
    set_text(dlg, IDC_UPD_TITLE, u8"Downloading OpenMirrorPhone " + s.version + u8"…");
    ShowWindow(GetDlgItem(dlg, IDC_UPD_PROGRESS), SW_SHOW);

    s.worker = std::thread([dlg, &s] {
        bool ok = download_file(
            s.zip_url, s.zip_path,
            [dlg](uint64_t done, uint64_t total) {
                PostMessageW(dlg, WM_DL_PROGRESS, total ? static_cast<WPARAM>(done * 100 / total) : WPARAM(-1), 0);
            },
            s.cancel, &s.error);
        PostMessageW(dlg, WM_DL_DONE, ok ? 1 : 0, 0);
    });
}

void finish_download(HWND dlg, bool ok) {
    DialogState &s = state_of(dlg);
    if (s.worker.joinable()) s.worker.join();
    s.downloading = false;
    if (s.cancel) {
        EndDialog(dlg, 0);
        return;
    }
    if (ok) {
        set_text(dlg, IDC_UPD_TITLE, u8"Installing…");
        UpdateWindow(dlg);
        if (install_update(s.zip_path, s.version, &s.error)) {
            if (!s.error.empty()) MessageBoxW(dlg, to_wide(s.error).c_str(), L"OpenMirrorPhone", MB_ICONWARNING);
            s.choice = UpdateChoice::Installed;
            EndDialog(dlg, 0);
            return;
        }
    }
    MessageBoxW(dlg, to_wide("The update could not be installed: " + s.error + ".").c_str(), L"OpenMirrorPhone",
                MB_ICONERROR);
    EndDialog(dlg, 0);
}

INT_PTR CALLBACK update_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    INT_PTR brush = 0;
    if (theme_ctlcolor(msg, wp, lp, &brush)) return brush; // light or dark theme
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        DialogState &s = state_of(dlg);
        // A bold heading, like classic installers.
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(dlg, WM_GETFONT, 0, 0));
        LOGFONTW lf{};
        GetObjectW(font, sizeof lf, &lf);
        lf.lfWeight = FW_BOLD;
        s.bold = CreateFontIndirectW(&lf);
        SendDlgItemMessageW(dlg, IDC_UPD_TITLE, WM_SETFONT, reinterpret_cast<WPARAM>(s.bold), TRUE);

        set_text(dlg, IDC_UPD_NOTES, s.notes);
        ShowWindow(GetDlgItem(dlg, IDC_UPD_PROGRESS), SW_HIDE);
        if (s.whats_new) {
            SetWindowTextW(dlg, L"What's New");
            set_text(dlg, IDC_UPD_TITLE, s.heading);
            set_text(dlg, IDOK, "OK");
            ShowWindow(GetDlgItem(dlg, IDCANCEL), SW_HIDE);
            ShowWindow(GetDlgItem(dlg, IDC_UPD_SKIP), SW_HIDE);
        } else {
            SetWindowTextW(dlg, L"Update available");
            set_button_icon(dlg, IDOK, Icon::Download);
            set_button_icon(dlg, IDCANCEL, Icon::Clock);
            set_button_icon(dlg, IDC_UPD_SKIP, Icon::Skip);
            set_text(dlg, IDC_UPD_TITLE, "OpenMirrorPhone " + s.version + " is available. You have version " +
                                             OMP_VERSION + ".");
        }
        // Start on the main button, so Enter installs (or closes What's New) and the
        // notes are not all selected.
        apply_theme(dlg);
        SendMessageW(dlg, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(GetDlgItem(dlg, IDOK)), TRUE);
        return FALSE;
    }
    case WM_COMMAND: {
        DialogState &s = state_of(dlg);
        switch (LOWORD(wp)) {
        case IDOK:
            if (s.whats_new) EndDialog(dlg, 0);
            else if (!s.downloading) start_download(dlg);
            return TRUE;
        case IDC_UPD_SKIP:
            s.choice = UpdateChoice::Skip;
            EndDialog(dlg, 0);
            return TRUE;
        case IDCANCEL:
            if (s.downloading) s.cancel = true; // WM_DL_DONE closes the dialog
            else EndDialog(dlg, 0);
            return TRUE;
        }
        break;
    }
    case WM_DL_PROGRESS: {
        HWND bar = GetDlgItem(dlg, IDC_UPD_PROGRESS);
        if (static_cast<int>(wp) < 0) {
            SetWindowLongPtrW(bar, GWL_STYLE, GetWindowLongPtrW(bar, GWL_STYLE) | PBS_MARQUEE);
            SendMessageW(bar, PBM_SETMARQUEE, TRUE, 30);
        } else {
            SendMessageW(bar, PBM_SETPOS, wp, 0);
        }
        return TRUE;
    }
    case WM_DL_DONE:
        finish_download(dlg, wp == 1);
        return TRUE;
    case WM_DESTROY: {
        DialogState &s = state_of(dlg);
        s.cancel = true;
        if (s.worker.joinable()) s.worker.join();
        if (s.bold) DeleteObject(s.bold);
        break;
    }
    }
    return FALSE;
}

void run(void *parent, DialogState &state) {
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_UPDATE), static_cast<HWND>(parent), update_proc,
                    reinterpret_cast<LPARAM>(&state));
}

} // namespace

UpdateChoice show_update_dialog(void *parent_hwnd, const std::string &new_version, const std::string &notes,
                                const std::string &zip_url) {
    DialogState state;
    state.version = new_version;
    state.notes = notes;
    state.zip_url = zip_url;
    run(parent_hwnd, state);
    return state.choice;
}

void show_whats_new_dialog(void *parent_hwnd, const std::string &heading, const std::string &notes) {
    DialogState state;
    state.whats_new = true;
    state.heading = heading;
    state.notes = notes;
    run(parent_hwnd, state);
}
