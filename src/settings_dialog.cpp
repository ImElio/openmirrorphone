#include "settings_dialog.h"

#include "network.h"
#include "resource.h"
#include "sessions.h"
#include "settings.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <cstdio>

#ifndef OMP_VERSION
#define OMP_VERSION "0.0.0"
#endif

namespace {

constexpr UINT WM_REFILL = WM_APP + 1; // refill the list after an in-place edit ends

enum Column { kColName, kColDate, kColDevice, kColDuration, kColRecording };

SettingsHost &host_of(HWND dlg) { return *reinterpret_cast<SettingsHost *>(GetWindowLongPtrW(dlg, DWLP_USER)); }

int message(HWND owner, const char *text, UINT flags) {
    return MessageBoxW(owner, to_wide(text).c_str(), L"OpenMirrorPhone", flags);
}

std::string format_date(std::time_t t) {
    char buf[32];
    std::tm *tm = std::localtime(&t);
    if (!tm || !std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", tm)) return "";
    return buf;
}

std::string file_name(const std::string &path) {
    size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

void set_cell(HWND list, int row, int col, const std::string &text) {
    std::wstring w = to_wide(text);
    LVITEMW item{};
    item.iSubItem = col;
    item.pszText = const_cast<wchar_t *>(w.c_str());
    SendMessageW(list, LVM_SETITEMTEXTW, row, reinterpret_cast<LPARAM>(&item));
}

int selected(HWND dlg) {
    return static_cast<int>(SendDlgItemMessageW(dlg, IDC_SESSIONS, LVM_GETNEXTITEM, static_cast<WPARAM>(-1),
                                                LVNI_SELECTED));
}

void update_buttons(HWND dlg) {
    SettingsHost &host = host_of(dlg);
    int sel = selected(dlg);
    bool has = sel >= 0;
    bool has_recording = has && !host.sessions->sessions()[sel].recordings.empty();
    EnableWindow(GetDlgItem(dlg, IDC_RENAME), has);
    EnableWindow(GetDlgItem(dlg, IDC_DELETE), has);
    EnableWindow(GetDlgItem(dlg, IDC_PLAY), has_recording);
}

void fill_list(HWND dlg, int select) {
    SettingsHost &host = host_of(dlg);
    HWND list = GetDlgItem(dlg, IDC_SESSIONS);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);
    const auto &sessions = host.sessions->sessions();
    for (size_t i = 0; i < sessions.size(); ++i) {
        const Session &s = sessions[i];
        std::wstring title = to_wide(s.title);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t *>(title.c_str());
        int row = static_cast<int>(SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item)));
        set_cell(list, row, kColDate, format_date(s.start));
        set_cell(list, row, kColDevice, s.model.empty() ? s.device : s.device + " (" + s.model + ")");
        bool running = i == 0 && host.sessions->active();
        set_cell(list, row, kColDuration, running ? "in progress" : format_duration(s.seconds));
        std::string rec = s.recordings.empty()     ? "-"
                          : s.recordings.size() == 1 ? file_name(s.recordings[0])
                                                     : std::to_string(s.recordings.size()) + " files";
        set_cell(list, row, kColRecording, rec);
    }
    if (select >= 0 && select < static_cast<int>(sessions.size())) {
        LVITEMW state{};
        state.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        state.state = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageW(list, LVM_SETITEMSTATE, select, reinterpret_cast<LPARAM>(&state));
        SendMessageW(list, LVM_ENSUREVISIBLE, select, FALSE);
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    update_buttons(dlg);
}

void init_list(HWND dlg) {
    HWND list = GetDlgItem(dlg, IDC_SESSIONS);
    SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    RECT rc;
    GetClientRect(list, &rc);
    int w = rc.right - GetSystemMetrics(SM_CXVSCROLL);
    struct { const wchar_t *title; int percent; } cols[] = {
        {L"Name", 28}, {L"Date", 20}, {L"Device", 24}, {L"Duration", 11}, {L"Recording", 17}};
    for (int i = 0; i < 5; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = const_cast<wchar_t *>(cols[i].title);
        col.cx = w * cols[i].percent / 100;
        col.iSubItem = i;
        SendMessageW(list, LVM_INSERTCOLUMNW, i, reinterpret_cast<LPARAM>(&col));
    }
}

void apply_name(HWND dlg) {
    SettingsHost &host = host_of(dlg);
    wchar_t buf[256];
    GetDlgItemTextW(dlg, IDC_NAME, buf, 256);
    std::string name = clean_receiver_name(to_utf8(buf));
    if (name.empty()) {
        message(dlg, "Please enter a name.", MB_ICONWARNING);
        return;
    }
    if (name == host.current_name()) return;
    if (!host.apply_name(name)) {
        message(dlg, "The receiver could not be restarted. Is the Apple Bonjour service running?", MB_ICONERROR);
    }
    SetDlgItemTextW(dlg, IDC_NAME, to_wide(host.current_name()).c_str());
    fill_list(dlg, selected(dlg)); // a running session ends when the receiver restarts
}

void play(HWND dlg) {
    int sel = selected(dlg);
    if (sel < 0) return;
    for (const std::string &path : host_of(dlg).sessions->sessions()[sel].recordings) {
        if (GetFileAttributesW(to_wide(path).c_str()) != INVALID_FILE_ATTRIBUTES) {
            shell_open(path);
            return;
        }
    }
    message(dlg, "The recording file no longer exists. It may have been moved or deleted.", MB_ICONINFORMATION);
}

void delete_session(HWND dlg) {
    SettingsHost &host = host_of(dlg);
    int sel = selected(dlg);
    if (sel < 0) return;
    if (sel == 0 && host.sessions->active()) {
        message(dlg, "This session is still running. Stop mirroring on the device first.", MB_ICONINFORMATION);
        return;
    }
    const Session &s = host.sessions->sessions()[sel];
    bool delete_files = false;
    if (s.recordings.empty()) {
        if (message(dlg, "Delete this session from the history?", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    } else {
        int answer = message(dlg,
                             "Delete this session from the history?\n\n"
                             "Yes: also move its recording to the Recycle Bin.\n"
                             "No: keep the recording file.",
                             MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL) return;
        delete_files = answer == IDYES;
    }
    host.sessions->remove(static_cast<size_t>(sel), delete_files);
    fill_list(dlg, sel < static_cast<int>(host.sessions->sessions().size()) ? sel : sel - 1);
}

// The user finished typing a new session name in the list.
void end_rename(HWND dlg, int row, const wchar_t *text) {
    SettingsHost &host = host_of(dlg);
    if (!text || row < 0) return; // cancelled
    std::string title = to_utf8(text);
    size_t b = title.find_first_not_of(" \t"), e = title.find_last_not_of(" \t");
    if (b == std::string::npos) return;
    title = title.substr(b, e - b + 1);
    const Session &s = host.sessions->sessions()[row];
    if (title == s.title) return;
    if (row == 0 && host.sessions->active() && host.recording() && !s.recordings.empty()) {
        message(dlg, "This session is being recorded. Stop the recording (R) before renaming it.",
                MB_ICONINFORMATION);
        return;
    }
    if (!host.sessions->rename(static_cast<size_t>(row), title)) {
        message(dlg, "The recording could not be renamed. Close it in any app that has it open and try again.",
                MB_ICONWARNING);
    }
}

INT_PTR CALLBACK settings_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        SettingsHost &host = host_of(dlg);
        SendDlgItemMessageW(dlg, IDC_NAME, EM_LIMITTEXT, 50, 0);
        SetDlgItemTextW(dlg, IDC_NAME, to_wide(host.current_name()).c_str());
        CheckDlgButton(dlg, IDC_AUTO_UPDATE, load_setting("auto_update") != "0" ? BST_CHECKED : BST_UNCHECKED);
        init_list(dlg);
        fill_list(dlg, -1);
        return TRUE;
    }
    case WM_REFILL:
        fill_list(dlg, static_cast<int>(wp));
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK: // Enter
        case IDC_APPLY: apply_name(dlg); return TRUE;
        case IDC_RENAME: {
            int sel = selected(dlg);
            if (sel >= 0) {
                SetFocus(GetDlgItem(dlg, IDC_SESSIONS));
                SendDlgItemMessageW(dlg, IDC_SESSIONS, LVM_EDITLABELW, sel, 0);
            }
            return TRUE;
        }
        case IDC_PLAY: play(dlg); return TRUE;
        case IDC_DELETE: delete_session(dlg); return TRUE;
        case IDC_FOLDER: shell_open(recordings_dir()); return TRUE;
        case IDC_AUTO_UPDATE:
            save_setting("auto_update", IsDlgButtonChecked(dlg, IDC_AUTO_UPDATE) == BST_CHECKED ? "1" : "0");
            return TRUE;
        case IDCANCEL: EndDialog(dlg, 0); return TRUE;
        }
        break;
    case WM_NOTIFY: {
        auto *hdr = reinterpret_cast<NMHDR *>(lp);
        if (hdr->idFrom != IDC_SESSIONS) break;
        switch (hdr->code) {
        case LVN_ITEMCHANGED: update_buttons(dlg); break;
        case NM_DBLCLK: play(dlg); break;
        case LVN_BEGINLABELEDITW: {
            HWND edit = reinterpret_cast<HWND>(SendDlgItemMessageW(dlg, IDC_SESSIONS, LVM_GETEDITCONTROL, 0, 0));
            if (edit) SendMessageW(edit, EM_LIMITTEXT, 80, 0);
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, FALSE); // allow editing
            return TRUE;
        }
        case LVN_ENDLABELEDITW: {
            auto *info = reinterpret_cast<NMLVDISPINFOW *>(lp);
            end_rename(dlg, info->item.iItem, info->item.pszText);
            // Refill once the edit box is gone; the list shows the stored title.
            PostMessageW(dlg, WM_REFILL, static_cast<WPARAM>(info->item.iItem), 0);
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, FALSE);
            return TRUE;
        }
        case LVN_KEYDOWN: {
            WORD key = reinterpret_cast<NMLVKEYDOWN *>(lp)->wVKey;
            if (key == VK_F2) SendMessageW(dlg, WM_COMMAND, IDC_RENAME, 0);
            if (key == VK_DELETE) delete_session(dlg);
            break;
        }
        }
        break;
    }
    }
    return FALSE;
}

} // namespace

void shell_open(const std::string &utf8_path) {
    ShellExecuteW(nullptr, L"open", to_wide(utf8_path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void show_settings_dialog(void *parent_hwnd, SettingsHost &host) {
    static bool common_controls = false;
    if (!common_controls) {
        INITCOMMONCONTROLSEX icc{sizeof icc, ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icc);
        common_controls = true;
    }
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), static_cast<HWND>(parent_hwnd),
                    settings_proc, reinterpret_cast<LPARAM>(&host));
}

void show_about_dialog(void *parent_hwnd) {
    std::wstring text = L"OpenMirrorPhone " + to_wide(OMP_VERSION) +
                        L"\n\n"
                        L"Mirror your iPhone, iPad or Mac screen to this PC.\n"
                        L"Free and open source.";
    MessageBoxW(static_cast<HWND>(parent_hwnd), text.c_str(), L"About OpenMirrorPhone", MB_OK | MB_ICONINFORMATION);
}
