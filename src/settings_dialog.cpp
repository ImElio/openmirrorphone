#include "settings_dialog.h"

#include "icons.h"
#include "network.h"
#include "paypal_banner.h"
#include "resource.h"
#include "sessions.h"
#include "settings.h"
#include "theme.h"
#include "update_dialog.h"
#include "updater.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <thread>
#include <utility>
#include <vector>

#ifndef OMP_VERSION
#define OMP_VERSION "0.0.0"
#endif

namespace {

constexpr UINT WM_REFILL = WM_APP + 1;   // refill the list after an in-place edit ends
constexpr UINT WM_RELEASES = WM_APP + 2; // lParam: ReleasesReply*, from the fetch thread
constexpr int kSidebarDlu = 112;         // width of the sidebar in app.rc, in dialog units

enum Column { kColName, kColDate, kColDevice, kColDuration, kColRecording };

struct SettingsState {
    SettingsHost *host = nullptr;
    HFONT title_font = nullptr;
    HBITMAP donate = nullptr;
    int sidebar_px = 0; // kSidebarDlu converted to pixels for this dialog's font
    int donate_w = 0;
    int donate_h = 0;
    std::vector<std::string> audio_devices;
    KeybindSet keybinds = default_keybinds();
    HWND capture_control = nullptr;
    std::vector<Release> releases; // from GitHub, newest first
    Release offer;                 // newer version found by the check, if any
};

// The release list, fetched off the UI thread for the Updates page.
struct ReleasesReply {
    bool ok = false;
    std::string error;
    std::vector<Release> releases;
};

void fetch_releases_for(HWND dlg) {
    std::thread([dlg] {
        auto *reply = new ReleasesReply();
        reply->ok = fetch_releases(&reply->releases, &reply->error);
        if (!IsWindow(dlg) || !PostMessageW(dlg, WM_RELEASES, 0, reinterpret_cast<LPARAM>(reply))) delete reply;
    }).detach();
}

bool in_sidebar(HWND dlg, HWND control, int sidebar_px) {
    RECT rc;
    GetWindowRect(control, &rc);
    MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT *>(&rc), 2);
    return rc.left < sidebar_px;
}

// Pixel-art icons of the sidebar pages, in page order.
const Icon kPageIcons[] = {Icon::Gear, Icon::Clock, Icon::Globe, Icon::Keyboard, Icon::Info};

SettingsState &state_of(HWND dlg) { return *reinterpret_cast<SettingsState *>(GetWindowLongPtrW(dlg, DWLP_USER)); }
SettingsHost &host_of(HWND dlg) { return *state_of(dlg).host; }

void show_page(HWND dlg, int page) {
    static const int general[] = {IDC_GENERAL_TITLE, IDC_GENERAL_INFO,  IDC_RECEIVER_GROUP, IDC_RECEIVER_LABEL,
                                  IDC_NAME,          IDC_APPLY,         IDC_NAME_NOTE,      IDC_WINDOW_GROUP,
                                  IDC_ALWAYS_ON_TOP, IDC_AUDIO_OUTPUT_LABEL, IDC_AUDIO_OUTPUT, IDC_SCREENSHOT,
                                  IDC_FULLSCREEN,    IDC_RECORDING,      IDC_OPEN_RECORDINGS,
                                  IDC_APPEARANCE_GROUP, IDC_DARK_THEME};
    static const int history[] = {IDC_HISTORY_TITLE, IDC_HISTORY_INFO, IDC_HISTORY_GROUP, IDC_SESSIONS,
                                  IDC_RENAME,        IDC_PLAY,         IDC_DELETE};
    static const int updates[] = {IDC_UPDATES_TITLE,     IDC_UPDATES_INFO,  IDC_UPDATES_GROUP,
                                  IDC_AUTO_UPDATE,       IDC_CHECK_UPDATES_NOW, IDC_UPDATE_STATUS,
                                  IDC_WHATS_NEW_GROUP,   IDC_WHATS_NEW_TEXT};
    static const int about[] = {IDC_ABOUT_TITLE,   IDC_ABOUT_INFO,    IDC_ABOUT_GROUP,
                                IDC_ABOUT_PRODUCT, IDC_ABOUT_VERSION, IDC_ABOUT_DESCRIPTION};
    static const int keybinds[] = {IDC_KEYBINDS_TITLE, IDC_KEYBINDS_INFO, IDC_KEYBINDS_GROUP,
                                   IDC_KEY_FULLSCREEN_LABEL, IDC_KEY_FULLSCREEN, IDC_KEY_EXIT_FULLSCREEN_LABEL,
                                   IDC_KEY_EXIT_FULLSCREEN, IDC_KEY_RECORDING_LABEL, IDC_KEY_RECORDING,
                                   IDC_KEY_SCREENSHOT_LABEL, IDC_KEY_SCREENSHOT,
                                   IDC_KEY_SETTINGS_LABEL, IDC_KEY_SETTINGS, IDC_KEYBINDS_DEFAULTS};
    auto show = [dlg](const int *ids, size_t count, bool visible) {
        for (size_t i = 0; i < count; ++i) ShowWindow(GetDlgItem(dlg, ids[i]), visible ? SW_SHOW : SW_HIDE);
    };
    show(general, sizeof general / sizeof general[0], page == 0);
    show(history, sizeof history / sizeof history[0], page == 1);
    show(updates, sizeof updates / sizeof updates[0], page == 2);
    show(keybinds, sizeof keybinds / sizeof keybinds[0], page == 3);
    show(about, sizeof about / sizeof about[0], page == 4);
}

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

constexpr int kKeybindControls[] = {IDC_KEY_FULLSCREEN, IDC_KEY_EXIT_FULLSCREEN, IDC_KEY_RECORDING,
                                    IDC_KEY_SCREENSHOT, IDC_KEY_SETTINGS};

void display_keybinds(HWND dlg, const KeybindSet &bindings) {
    for (size_t i = 0; i < bindings.size(); ++i)
        SetDlgItemTextW(dlg, kKeybindControls[i], to_wide(keybind_label(bindings[i])).c_str());
}

uint8_t pressed_modifiers() {
    uint8_t modifiers = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= KeybindCtrl;
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= KeybindShift;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= KeybindAlt;
    return modifiers;
}

LRESULT CALLBACK keybind_edit_proc(HWND edit, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    HWND dlg = reinterpret_cast<HWND>(ref);
    SettingsState &state = state_of(dlg);
    int id = GetDlgCtrlID(edit);
    size_t action = 0;
    while (action < std::size(kKeybindControls) && kKeybindControls[action] != id) ++action;

    if (msg == WM_GETDLGCODE) return DefSubclassProc(edit, msg, wp, lp) | DLGC_WANTALLKEYS | DLGC_WANTARROWS;
    if (msg == WM_SETFOCUS) {
        state.capture_control = edit;
        SetWindowTextW(edit, L"Press a key combination...");
        return DefSubclassProc(edit, msg, wp, lp);
    }
    if (msg == WM_KILLFOCUS && state.capture_control == edit) {
        state.capture_control = nullptr;
        if (action < state.keybinds.size())
            SetWindowTextW(edit, to_wide(keybind_label(state.keybinds[action])).c_str());
    }
    if (state.capture_control != edit || action >= state.keybinds.size())
        return DefSubclassProc(edit, msg, wp, lp);

    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        unsigned int vk = static_cast<unsigned int>(wp);
        if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_SHIFT || vk == VK_LSHIFT ||
            vk == VK_RSHIFT || vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN)
            return 0;
        Keybind binding = keybind_from_virtual_key(vk, pressed_modifiers());
        if (binding.key == SDL_SCANCODE_UNKNOWN || vk == VK_RETURN || vk == VK_TAB || vk == VK_BACK ||
            ((binding.modifiers & KeybindAlt) && vk == VK_F4) ||
            (binding.modifiers == (KeybindCtrl | KeybindAlt) && vk == VK_DELETE)) {
            MessageBeep(MB_ICONWARNING);
            return 0;
        }
        for (size_t i = 0; i < state.keybinds.size(); ++i) {
            if (i != action && same_keybind(binding, state.keybinds[i])) {
                MessageBeep(MB_ICONWARNING);
                SetWindowTextW(edit, L"Already assigned - press another key");
                return 0;
            }
        }
        KeybindSet updated = state.keybinds;
        updated[action] = binding;
        if (!host_of(dlg).set_keybinds || !host_of(dlg).set_keybinds(updated)) {
            MessageBeep(MB_ICONWARNING);
            SetWindowTextW(edit, L"Could not save - press another key");
            return 0;
        }
        state.keybinds = updated;
        state.capture_control = nullptr;
        display_keybinds(dlg, state.keybinds);
        return 0;
    }
    if (msg == WM_KEYUP || msg == WM_SYSKEYUP || msg == WM_CHAR || msg == WM_SYSCHAR) return 0;
    return DefSubclassProc(edit, msg, wp, lp);
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
        {L"Name", 21}, {L"Date", 23}, {L"Device", 21}, {L"Duration", 14}, {L"Recording", 21}};
    for (int i = 0; i < 5; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = const_cast<wchar_t *>(cols[i].title);
        col.cx = w * cols[i].percent / 100;
        col.iSubItem = i;
        SendMessageW(list, LVM_INSERTCOLUMNW, i, reinterpret_cast<LPARAM>(&col));
    }
}

// The release list arrived: show this version's notes and whether a newer one exists.
void on_releases(HWND dlg, ReleasesReply *reply) {
    SettingsState &state = state_of(dlg);
    EnableWindow(GetDlgItem(dlg, IDC_CHECK_UPDATES_NOW), TRUE);
    if (!reply->ok) {
        SetDlgItemTextW(dlg, IDC_UPDATE_STATUS, to_wide("Could not check for updates: " + reply->error + ".").c_str());
        if (state.releases.empty()) {
            SetDlgItemTextW(dlg, IDC_WHATS_NEW_TEXT,
                            to_wide("Could not load the release notes: " + reply->error + ".").c_str());
        }
        delete reply;
        return;
    }
    state.releases = std::move(reply->releases);
    delete reply;

    // Notes of the running version: everything after the previous release, up to this one.
    std::string notes;
    for (size_t i = 0; i < state.releases.size(); ++i) {
        if (compare_versions(state.releases[i].version, OMP_VERSION) == 0) {
            std::string previous = i + 1 < state.releases.size() ? state.releases[i + 1].version : "";
            notes = notes_between(state.releases, previous, OMP_VERSION);
            break;
        }
    }
    if (notes.empty()) notes = std::string("No release notes have been published for version ") + OMP_VERSION + " yet.";
    SetDlgItemTextW(dlg, IDC_WHATS_NEW_TEXT, to_wide(notes).c_str());

    state.offer = Release{};
    for (const Release &r : state.releases) { // newest first
        if (compare_versions(r.version, OMP_VERSION) > 0 && !r.zip_url.empty()) {
            state.offer = r;
            break;
        }
    }
    if (state.offer.version.empty()) {
        SetDlgItemTextW(dlg, IDC_UPDATE_STATUS, L"You have the latest version.");
        SetDlgItemTextW(dlg, IDC_CHECK_UPDATES_NOW, L"&Check for updates");
        set_button_icon(dlg, IDC_CHECK_UPDATES_NOW, Icon::Globe);
    } else {
        SetDlgItemTextW(dlg, IDC_UPDATE_STATUS, to_wide("Version " + state.offer.version + " is available.").c_str());
        SetDlgItemTextW(dlg, IDC_CHECK_UPDATES_NOW, L"&Install update");
        set_button_icon(dlg, IDC_CHECK_UPDATES_NOW, Icon::Download);
    }
}

// "Check for updates", or "Install update" once a newer version was found.
void check_or_install(HWND dlg) {
    SettingsState &state = state_of(dlg);
    if (state.offer.version.empty()) {
        EnableWindow(GetDlgItem(dlg, IDC_CHECK_UPDATES_NOW), FALSE);
        SetDlgItemTextW(dlg, IDC_UPDATE_STATUS, L"Checking…");
        fetch_releases_for(dlg);
        return;
    }
    Release offer = state.offer;
    std::string notes = notes_between(state.releases, OMP_VERSION, offer.version);
    switch (show_update_dialog(dlg, offer.version, notes, offer.zip_url)) {
    case UpdateChoice::Installed:
        // The new version shows these notes as What's New when it starts.
        std::ofstream(app_data_dir() + "\\whatsnew.txt", std::ios::binary) << notes;
        state.host->exit_app();
        EndDialog(dlg, 0);
        break;
    case UpdateChoice::Skip:
        save_setting("skip_version", offer.version);
        SetDlgItemTextW(dlg, IDC_UPDATE_STATUS, to_wide("Version " + offer.version + " skipped.").c_str());
        break;
    case UpdateChoice::Later: break;
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
        SettingsState &state = state_of(dlg);
        SettingsHost &host = host_of(dlg);
        HFONT base_font = reinterpret_cast<HFONT>(SendMessageW(dlg, WM_GETFONT, 0, 0));
        LOGFONTW title_lf{};
        if (base_font && GetObjectW(base_font, sizeof title_lf, &title_lf)) {
            title_lf.lfWeight = FW_SEMIBOLD;
            title_lf.lfHeight = title_lf.lfHeight * 14 / 10;
            state.title_font = CreateFontIndirectW(&title_lf);
            for (int id : {IDC_GENERAL_TITLE, IDC_HISTORY_TITLE, IDC_UPDATES_TITLE, IDC_KEYBINDS_TITLE,
                           IDC_ABOUT_TITLE, IDC_ABOUT_PRODUCT})
                SendDlgItemMessageW(dlg, id, WM_SETFONT, reinterpret_cast<WPARAM>(state.title_font), TRUE);
        }
        RECT sidebar_rc{0, 0, kSidebarDlu, 0};
        MapDialogRect(dlg, &sidebar_rc);
        state.sidebar_px = sidebar_rc.right;
        SendDlgItemMessageW(dlg, IDC_NAME, EM_LIMITTEXT, 50, 0);
        SetDlgItemTextW(dlg, IDC_NAME, to_wide(host.current_name()).c_str());
        state.keybinds = host.keybinds;
        display_keybinds(dlg, state.keybinds);
        for (int id : kKeybindControls)
            SetWindowSubclass(GetDlgItem(dlg, id), keybind_edit_proc, 1, reinterpret_cast<DWORD_PTR>(dlg));
        CheckDlgButton(dlg, IDC_AUTO_UPDATE, load_setting("auto_update") != "0" ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_ALWAYS_ON_TOP, host.always_on_top() ? BST_CHECKED : BST_UNCHECKED);
        HWND audio_combo = GetDlgItem(dlg, IDC_AUDIO_OUTPUT);
        SendMessageW(audio_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"System default"));
        state.audio_devices = host.audio_output_devices ? host.audio_output_devices() : std::vector<std::string>{};
        int audio_selection = 0;
        const std::string selected_audio = host.audio_output_device ? host.audio_output_device() : std::string();
        for (size_t i = 0; i < state.audio_devices.size(); ++i) {
            std::wstring device = to_wide(state.audio_devices[i]);
            SendMessageW(audio_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(device.c_str()));
            if (state.audio_devices[i] == selected_audio) audio_selection = static_cast<int>(i + 1);
        }
        SendMessageW(audio_combo, CB_SETCURSEL, audio_selection, 0);
        SetDlgItemTextW(dlg, IDC_RECORDING, host.recording() ? L"Stop recording" : L"Start recording");
        SetDlgItemTextW(dlg, IDC_ABOUT_VERSION, (L"Version " + to_wide(OMP_VERSION)).c_str());
        const wchar_t *pages[] = {L"General", L"Session history", L"Updates", L"Keybinds", L"About"};
        for (const wchar_t *page : pages) SendDlgItemMessageW(dlg, IDC_SETTINGS_NAV, LB_ADDSTRING, 0,
                                                               reinterpret_cast<LPARAM>(page));
        HWND navigation = GetDlgItem(dlg, IDC_SETTINGS_NAV);
        LONG_PTR nav_style = GetWindowLongPtrW(navigation, GWL_STYLE);
        SetWindowLongPtrW(navigation, GWL_STYLE, nav_style & ~WS_BORDER);
        LONG_PTR nav_ex_style = GetWindowLongPtrW(navigation, GWL_EXSTYLE);
        SetWindowLongPtrW(navigation, GWL_EXSTYLE, nav_ex_style & ~WS_EX_CLIENTEDGE);
        SetWindowPos(navigation, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        SendDlgItemMessageW(dlg, IDC_SETTINGS_NAV, LB_SETCURSEL, 0, 0);
        RECT banner_rc{};
        GetClientRect(GetDlgItem(dlg, IDC_DONATE), &banner_rc);
        state.donate = load_paypal_banner(banner_rc.right - 8, banner_rc.bottom - 8,
                                          &state.donate_w, &state.donate_h);
        CheckDlgButton(dlg, IDC_DARK_THEME, dark_theme() ? BST_CHECKED : BST_UNCHECKED);
        for (auto [id, icon] : {std::pair{IDC_APPLY, Icon::Check}, {IDC_FULLSCREEN, Icon::Monitor},
                                {IDC_OPEN_RECORDINGS, Icon::Folder}, {IDC_RENAME, Icon::Pencil},
                                {IDC_PLAY, Icon::Play}, {IDC_DELETE, Icon::Cross},
                                {IDC_CHECK_UPDATES_NOW, Icon::Globe}})
            set_button_icon(dlg, id, icon);
        set_button_icon(dlg, IDC_RECORDING, host.recording() ? Icon::Stop : Icon::Record);
        set_button_icon(dlg, IDC_SCREENSHOT, Icon::Camera);
        SetDlgItemTextW(dlg, IDC_WHATS_NEW_GROUP, (L"What's new in version " + to_wide(OMP_VERSION)).c_str());
        SetDlgItemTextW(dlg, IDC_WHATS_NEW_TEXT, L"Loading the release notes…");
        init_list(dlg);
        fill_list(dlg, -1);
        show_page(dlg, 0);
        apply_theme(dlg);
        fetch_releases_for(dlg); // release notes and update status for the Updates page
        return TRUE;
    }
    case WM_REFILL:
        fill_list(dlg, static_cast<int>(wp));
        return TRUE;
    case WM_RELEASES:
        on_releases(dlg, reinterpret_cast<ReleasesReply *>(lp));
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
        case IDC_SETTINGS_NAV:
            if (HIWORD(wp) == LBN_SELCHANGE)
                show_page(dlg, static_cast<int>(SendDlgItemMessageW(dlg, IDC_SETTINGS_NAV, LB_GETCURSEL, 0, 0)));
            return TRUE;
        case IDC_DONATE: open_paypal_page(); return TRUE;
        case IDC_AUDIO_OUTPUT:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int index = static_cast<int>(SendDlgItemMessageW(dlg, IDC_AUDIO_OUTPUT, CB_GETCURSEL, 0, 0));
                std::string name = index <= 0 || static_cast<size_t>(index - 1) >= state_of(dlg).audio_devices.size()
                                       ? std::string()
                                       : state_of(dlg).audio_devices[index - 1];
                if (!host_of(dlg).set_audio_output_device || !host_of(dlg).set_audio_output_device(name)) {
                    message(dlg, "Could not open that audio output device. The previous output is still active.",
                            MB_ICONWARNING);
                    std::string current = host_of(dlg).audio_output_device ? host_of(dlg).audio_output_device() : "";
                    int restore = 0;
                    for (size_t i = 0; i < state_of(dlg).audio_devices.size(); ++i)
                        if (state_of(dlg).audio_devices[i] == current) restore = static_cast<int>(i + 1);
                    SendDlgItemMessageW(dlg, IDC_AUDIO_OUTPUT, CB_SETCURSEL, restore, 0);
                }
            }
            return TRUE;
        case IDC_SCREENSHOT: {
            std::string result = host_of(dlg).take_screenshot ? host_of(dlg).take_screenshot() : "Screenshot unavailable.";
            message(dlg, result.c_str(), result.find(".png") != std::string::npos ? MB_ICONINFORMATION : MB_ICONWARNING);
            return TRUE;
        }
        case IDC_ALWAYS_ON_TOP:
            if (!host_of(dlg).set_always_on_top(IsDlgButtonChecked(dlg, IDC_ALWAYS_ON_TOP) == BST_CHECKED)) {
                CheckDlgButton(dlg, IDC_ALWAYS_ON_TOP,
                               host_of(dlg).always_on_top() ? BST_CHECKED : BST_UNCHECKED);
                message(dlg, "Could not change the window setting.", MB_ICONWARNING);
            }
            return TRUE;
        case IDC_FULLSCREEN: host_of(dlg).toggle_fullscreen(); return TRUE;
        case IDC_RECORDING: {
            SettingsHost &host = host_of(dlg);
            host.toggle_recording();
            SetDlgItemTextW(dlg, IDC_RECORDING, host.recording() ? L"Stop recording" : L"Start recording");
            set_button_icon(dlg, IDC_RECORDING, host.recording() ? Icon::Stop : Icon::Record);
            return TRUE;
        }
        case IDC_OPEN_RECORDINGS: host_of(dlg).open_recordings_folder(); return TRUE;
        case IDC_CHECK_UPDATES_NOW: check_or_install(dlg); return TRUE;
        case IDC_DARK_THEME:
            set_dark_theme(IsDlgButtonChecked(dlg, IDC_DARK_THEME) == BST_CHECKED);
            apply_theme(dlg);
            host_of(dlg).theme_changed();
            return TRUE;
        case IDC_AUTO_UPDATE:
            save_setting("auto_update", IsDlgButtonChecked(dlg, IDC_AUTO_UPDATE) == BST_CHECKED ? "1" : "0");
            return TRUE;
        case IDC_KEYBINDS_DEFAULTS: {
            KeybindSet defaults = default_keybinds();
            if (!host_of(dlg).set_keybinds || !host_of(dlg).set_keybinds(defaults)) {
                message(dlg, "Could not restore the default shortcuts.", MB_ICONWARNING);
                return TRUE;
            }
            state_of(dlg).keybinds = defaults;
            state_of(dlg).capture_control = nullptr;
            display_keybinds(dlg, defaults);
            return TRUE;
        }
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
    case WM_MEASUREITEM: {
        auto *measure = reinterpret_cast<MEASUREITEMSTRUCT *>(lp);
        if (measure->CtlID == IDC_SETTINGS_NAV) {
            measure->itemHeight = 30;
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM:
        if (wp == IDC_SETTINGS_NAV) {
            auto *item = reinterpret_cast<DRAWITEMSTRUCT *>(lp);
            if (item->itemID == static_cast<UINT>(-1)) return TRUE;
            const bool selected = (item->itemState & ODS_SELECTED) != 0;
            const ThemeColors &colors = theme_colors();
            FillRect(item->hDC, &item->rcItem, theme_sidebar_brush());
            if (selected) {
                RECT pill = item->rcItem;
                InflateRect(&pill, 0, -1);
                HBRUSH fill = CreateSolidBrush(colors.selection_fill);
                HPEN outline = CreatePen(PS_SOLID, 1, colors.selection_border);
                HGDIOBJ old_brush = SelectObject(item->hDC, fill);
                HGDIOBJ old_pen = SelectObject(item->hDC, outline);
                RoundRect(item->hDC, pill.left, pill.top, pill.right, pill.bottom, 6, 6);
                SelectObject(item->hDC, old_brush);
                SelectObject(item->hDC, old_pen);
                DeleteObject(fill);
                DeleteObject(outline);
            }
            wchar_t label[64]{};
            SendMessageW(item->hwndItem, LB_GETTEXT, item->itemID, reinterpret_cast<LPARAM>(label));
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, colors.text);
            RECT text = item->rcItem;
            if (item->itemID < sizeof kPageIcons / sizeof kPageIcons[0]) {
                DrawIconEx(item->hDC, text.left + 10, (text.top + text.bottom - kIconSize) / 2,
                           icon_handle(kPageIcons[item->itemID]), kIconSize, kIconSize, 0, nullptr, DI_NORMAL);
            }
            text.left += 36;
            DrawTextW(item->hDC, label, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            return TRUE;
        }
        if (wp == IDC_DONATE) {
            const SettingsState &state = state_of(dlg);
            draw_paypal_banner(*reinterpret_cast<DRAWITEMSTRUCT *>(lp), state.donate, state.donate_w,
                               state.donate_h);
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, TRUE);
            return TRUE;
        }
        break;
    case WM_ERASEBKGND: {
        // Page colour on the right, sidebar colour on the left, a thin divider between.
        const SettingsState &state = state_of(dlg);
        HDC dc = reinterpret_cast<HDC>(wp);
        RECT rc{};
        GetClientRect(dlg, &rc);
        FillRect(dc, &rc, theme_page_brush());
        RECT side = rc;
        side.right = state.sidebar_px;
        FillRect(dc, &side, theme_sidebar_brush());
        HPEN divider = CreatePen(PS_SOLID, 1, theme_colors().divider);
        HGDIOBJ old = SelectObject(dc, divider);
        MoveToEx(dc, state.sidebar_px, 0, nullptr);
        LineTo(dc, state.sidebar_px, rc.bottom);
        SelectObject(dc, old);
        DeleteObject(divider);
        SetWindowLongPtrW(dlg, DWLP_MSGRESULT, TRUE);
        return TRUE;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        // Edit boxes get the field colour; labels, check boxes and group boxes take
        // the colour of what is behind them (sidebar or page).
        const SettingsState &state = state_of(dlg);
        const ThemeColors &colors = theme_colors();
        HDC dc = reinterpret_cast<HDC>(wp);
        HWND control = reinterpret_cast<HWND>(lp);
        wchar_t cls[16];
        GetClassNameW(control, cls, 16);
        if (msg == WM_CTLCOLOREDIT || !lstrcmpiW(cls, L"Edit")) {
            SetTextColor(dc, colors.text);
            SetBkColor(dc, colors.field);
            return reinterpret_cast<INT_PTR>(theme_field_brush());
        }
        int id = GetDlgCtrlID(control);
        bool secondary = id == IDC_GENERAL_INFO || id == IDC_HISTORY_INFO || id == IDC_UPDATES_INFO ||
                         id == IDC_ABOUT_INFO || id == IDC_NAME_NOTE || id == IDC_ABOUT_VERSION ||
                         id == IDC_UPDATE_STATUS;
        SetTextColor(dc, secondary ? colors.secondary_text : colors.text);
        SetBkMode(dc, TRANSPARENT);
        bool side = in_sidebar(dlg, control, state.sidebar_px);
        SetBkColor(dc, side ? colors.sidebar : colors.page);
        return reinterpret_cast<INT_PTR>(side ? theme_sidebar_brush() : theme_page_brush());
    }
    case WM_CTLCOLORLISTBOX: {
        HWND control = reinterpret_cast<HWND>(lp);
        if (GetDlgCtrlID(control) == IDC_SETTINGS_NAV) return reinterpret_cast<INT_PTR>(theme_sidebar_brush());
        const ThemeColors &colors = theme_colors();
        SetTextColor(reinterpret_cast<HDC>(wp), colors.text);
        SetBkColor(reinterpret_cast<HDC>(wp), colors.field);
        return reinterpret_cast<INT_PTR>(theme_field_brush());
    }
    case WM_SETCURSOR:
        if (reinterpret_cast<HWND>(wp) == GetDlgItem(dlg, IDC_DONATE)) {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649) /* IDC_HAND */));
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, TRUE);
            return TRUE;
        }
        break;
    case WM_DESTROY: {
        SettingsState &state = state_of(dlg);
        for (int id : kKeybindControls) RemoveWindowSubclass(GetDlgItem(dlg, id), keybind_edit_proc, 1);
        if (state.title_font) DeleteObject(state.title_font);
        if (state.donate) DeleteObject(state.donate);
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
    SettingsState state;
    state.host = &host;
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), static_cast<HWND>(parent_hwnd),
                        settings_proc, reinterpret_cast<LPARAM>(&state)) == -1) {
        std::fprintf(stderr, "[settings] cannot open the Settings window (error %lu)\n", GetLastError());
    }
}
