#include "app/SettingsDialog.h"

#include <shlobj.h>  // SHBrowseForFolderW (workspace root picker)

#include <string>
#include <vector>

namespace liney {

namespace {

constexpr int kIdShell = 100;
constexpr int kIdScrollback = 101;
constexpr int kIdCopyOnSelect = 102;
constexpr int kIdPasteWarn = 103;
constexpr int kIdUnixTools = 104;
constexpr int kIdRoot = 105;
constexpr int kIdBrowse = 106;

struct State {
    HWND shell = nullptr;
    HWND scrollback = nullptr;
    HWND copyOnSelect = nullptr;
    HWND pasteWarn = nullptr;
    HWND unixTools = nullptr;
    HWND root = nullptr;
    bool done = false;
    bool accepted = false;
};

std::wstring windowText(HWND h) {
    const int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

// Shells worth offering in the dropdown: present-on-PATH ones only.
std::vector<std::wstring> detectShells() {
    std::vector<std::wstring> out;
    const wchar_t* candidates[] = { L"cmd.exe", L"powershell.exe", L"pwsh.exe",
                                    L"wsl.exe" };
    wchar_t buf[MAX_PATH]{};
    for (const wchar_t* c : candidates)
        if (SearchPathW(nullptr, c, nullptr, MAX_PATH, buf, nullptr) != 0)
            out.push_back(c);
    return out;
}

std::wstring browseFolder(HWND owner) {
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Choose the workspace root (its git repos fill the sidebar)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return L"";
    wchar_t path[MAX_PATH]{};
    const bool ok = SHGetPathFromIDListW(pidl, path) != FALSE;
    CoTaskMemFree(pidl);
    return ok ? path : L"";
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_COMMAND:
        if (!st) break;
        switch (LOWORD(wParam)) {
        case IDOK:
            st->accepted = true;
            st->done = true;
            return 0;
        case IDCANCEL:
            st->done = true;
            return 0;
        case kIdBrowse: {
            const std::wstring dir = browseFolder(hwnd);
            if (!dir.empty()) SetWindowTextW(st->root, dir.c_str());
            return 0;
        }
        default:
            break;
        }
        break;
    case WM_CLOSE:
        if (st) st->done = true;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

bool showSettingsDialog(HWND owner, SettingsValues& v) {
    static const wchar_t* kClass = L"LineySettingsDialog";
    HINSTANCE inst = GetModuleHandleW(nullptr);

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClass;
        RegisterClassW(&wc);
        registered = true;
    }

    const int w = 440, h = 330;
    RECT orc{};
    if (owner) GetWindowRect(owner, &orc);
    else { orc.left = 200; orc.top = 200; orc.right = 900; orc.bottom = 700; }
    const int x = orc.left + ((orc.right - orc.left) - w) / 2;
    const int y = orc.top + ((orc.bottom - orc.top) - h) / 2;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kClass, L"liney-win Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, owner, nullptr, inst,
        nullptr);
    if (!dlg) return false;

    State st;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));

    const int lx = 14;         // label column
    const int cx = 150;        // control column
    const int cw = w - cx - 30;  // control width
    int cy = 14;
    auto label = [&](const wchar_t* text) {
        CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, lx, cy + 4,
                        cx - lx - 6, 18, dlg, nullptr, inst, nullptr);
    };

    // Shell: editable dropdown seeded with the shells found on PATH.
    label(L"Shell for new tabs");
    st.shell = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL,
        cx, cy, cw, 200, dlg,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdShell)), inst, nullptr);
    for (const std::wstring& s : detectShells())
        SendMessageW(st.shell, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(s.c_str()));
    SetWindowTextW(st.shell, v.shell.c_str());
    cy += 34;

    label(L"Scrollback lines");
    st.scrollback = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(v.scrollback).c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, cx, cy,
        100, 24, dlg,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdScrollback)), inst,
        nullptr);
    cy += 34;

    label(L"Workspace root");
    st.root = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", v.workspaceRoot.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, cx, cy, cw - 80,
        24, dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRoot)), inst,
        nullptr);
    CreateWindowExW(0, L"BUTTON", L"Browse…",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP, cx + cw - 74, cy, 74,
                    24, dlg,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdBrowse)),
                    inst, nullptr);
    cy += 30;
    CreateWindowExW(0, L"STATIC", L"(empty = the parent of the launch folder)",
                    WS_CHILD | WS_VISIBLE, cx, cy, cw, 16, dlg, nullptr, inst,
                    nullptr);
    cy += 28;

    auto checkbox = [&](int id, const wchar_t* text, bool checked) {
        HWND c = CreateWindowExW(
            0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, lx, cy,
            w - lx * 2, 22, dlg,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
        SendMessageW(c, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        cy += 26;
        return c;
    };
    st.copyOnSelect = checkbox(kIdCopyOnSelect,
                               L"Copy to clipboard when a selection ends",
                               v.copyOnSelect);
    st.pasteWarn = checkbox(kIdPasteWarn,
                            L"Warn before pasting multiple lines",
                            v.multiLinePasteWarning);
    st.unixTools = checkbox(
        kIdUnixTools,
        L"Unix tools: add Git's ls / grep / sed … to shells' PATH",
        v.unixTools);
    cy += 8;

    CreateWindowExW(0, L"BUTTON", L"OK",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    w - 190, cy, 80, 27, dlg,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), inst,
                    nullptr);
    CreateWindowExW(0, L"BUTTON", L"Cancel",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP, w - 102, cy, 80, 27,
                    dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                    inst, nullptr);

    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    EnumChildWindows(dlg, [](HWND child, LPARAM f) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(f), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));

    ShowWindow(dlg, SW_SHOW);
    SetFocus(st.shell);
    if (owner) EnableWindow(owner, FALSE);

    MSG msg{};
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    // Don't swallow an app-quit that arrived during the modal loop.
    if (!st.done && msg.message == WM_QUIT)
        PostQuitMessage(static_cast<int>(msg.wParam));

    if (st.accepted) {
        v.shell = windowText(st.shell);
        if (v.shell.empty()) v.shell = L"cmd.exe";
        const std::wstring sb = windowText(st.scrollback);
        int lines = v.scrollback;
        if (!sb.empty()) {
            lines = 0;
            for (wchar_t c : sb)
                if (c >= L'0' && c <= L'9') lines = lines * 10 + (c - L'0');
        }
        if (lines < 0) lines = 0;
        if (lines > 1000000) lines = 1000000;  // same cap as loadConfig
        v.scrollback = lines;
        v.copyOnSelect =
            SendMessageW(st.copyOnSelect, BM_GETCHECK, 0, 0) == BST_CHECKED;
        v.multiLinePasteWarning =
            SendMessageW(st.pasteWarn, BM_GETCHECK, 0, 0) == BST_CHECKED;
        v.unixTools =
            SendMessageW(st.unixTools, BM_GETCHECK, 0, 0) == BST_CHECKED;
        v.workspaceRoot = windowText(st.root);
    }

    if (owner) EnableWindow(owner, TRUE);
    DestroyWindow(dlg);
    if (owner) SetForegroundWindow(owner);
    return st.accepted;
}

} // namespace liney
