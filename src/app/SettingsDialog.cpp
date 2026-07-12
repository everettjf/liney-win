#include "app/SettingsDialog.h"

#include <commdlg.h>  // ChooseColorW (accent picker)
#include <shlobj.h>   // SHBrowseForFolderW (workspace root picker)

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "core/Themes.h"

namespace liney {

namespace {

constexpr int kIdShell = 100;
constexpr int kIdScrollback = 101;
constexpr int kIdCopyOnSelect = 102;
constexpr int kIdPasteWarn = 103;
constexpr int kIdUnixTools = 104;
constexpr int kIdRoot = 105;
constexpr int kIdBrowse = 106;
constexpr int kIdFont = 107;
constexpr int kIdFontSize = 108;
constexpr int kIdTheme = 109;
constexpr int kIdAccent = 110;
constexpr int kIdRemember = 111;

struct State {
    HWND shell = nullptr;
    HWND font = nullptr;
    HWND fontSize = nullptr;
    HWND theme = nullptr;
    HWND accentSwatch = nullptr;  // static showing the current accent hex
    HWND scrollback = nullptr;
    HWND copyOnSelect = nullptr;
    HWND pasteWarn = nullptr;
    HWND unixTools = nullptr;
    HWND rememberLayout = nullptr;
    HWND root = nullptr;
    HWND accentHex = nullptr;     // "#RRGGBB" caption next to the swatch
    HBRUSH accentBrush = nullptr; // fills the swatch via WM_CTLCOLORSTATIC
    Color accent{ 120, 200, 160 };
    bool accentChanged = false;   // user used the color picker
    int themeInitialSel = 0;      // selection when the dialog opened
    bool done = false;
    bool accepted = false;
};

// Enumerate installed fixed-pitch (monospace) font families, deduped + sorted.
std::vector<std::wstring> monospaceFonts() {
    std::vector<std::wstring> out;
    HDC dc = GetDC(nullptr);
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(
        dc, &lf,
        [](const LOGFONTW* f, const TEXTMETRICW*, DWORD, LPARAM p) -> int {
            // FIXED_PITCH is bit 0 of the low nibble; skip vertical (@) faces.
            if ((f->lfPitchAndFamily & 0x03) == FIXED_PITCH &&
                f->lfFaceName[0] != L'@') {
                auto* v = reinterpret_cast<std::vector<std::wstring>*>(p);
                v->push_back(f->lfFaceName);
            }
            return 1;
        },
        reinterpret_cast<LPARAM>(&out), 0);
    ReleaseDC(nullptr, dc);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// Refresh the accent swatch: rebuild its fill brush and repaint, and update
// the hex caption beside it.
void setAccentSwatch(State* st) {
    if (st->accentBrush) DeleteObject(st->accentBrush);
    st->accentBrush = CreateSolidBrush(RGB(st->accent.r, st->accent.g, st->accent.b));
    if (st->accentSwatch) InvalidateRect(st->accentSwatch, nullptr, TRUE);
    if (st->accentHex) {
        wchar_t buf[16];
        swprintf_s(buf, L"#%02X%02X%02X", st->accent.r, st->accent.g, st->accent.b);
        SetWindowTextW(st->accentHex, buf);
    }
}

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
        case kIdAccent: {
            static COLORREF custom[16] = {};
            CHOOSECOLORW cc{};
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = hwnd;
            cc.rgbResult =
                RGB(st->accent.r, st->accent.g, st->accent.b);
            cc.lpCustColors = custom;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                st->accent = { static_cast<uint8_t>(GetRValue(cc.rgbResult)),
                               static_cast<uint8_t>(GetGValue(cc.rgbResult)),
                               static_cast<uint8_t>(GetBValue(cc.rgbResult)) };
                st->accentChanged = true;
                setAccentSwatch(st);
            }
            return 0;
        }
        default:
            break;
        }
        break;
    case WM_CTLCOLORSTATIC:
        // Paint the accent swatch as a solid color chip.
        if (st && reinterpret_cast<HWND>(lParam) == st->accentSwatch &&
            st->accentBrush)
            return reinterpret_cast<LRESULT>(st->accentBrush);
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

    // Per-monitor DPI: lay the dialog out in logical (96-dpi) units and scale.
    const UINT dpi = owner ? GetDpiForWindow(owner) : 96;
    auto S = [dpi](int px) { return MulDiv(px, static_cast<int>(dpi), 96); };

    // Logical layout grid.
    const int W = 500;                 // client width
    const int M = 16;                  // outer margin
    const int labelX = M + 14;         // label column inside a group
    const int ctrlX = M + 104;         // control column
    const int ctrlR = W - M - 14;      // control right edge
    const int ctrlW = ctrlR - ctrlX;
    const int ch = 24;                 // control height

    State st;
    st.accent = v.accent;

    // Size the window so the *client* area is exactly W × contentH.
    const int contentH = 470;
    RECT wr{ 0, 0, S(W), S(contentH) };
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectExForDpi(&wr, style, FALSE, WS_EX_DLGMODALFRAME, dpi);
    const int winW = wr.right - wr.left, winH = wr.bottom - wr.top;

    RECT orc{};
    if (owner) GetWindowRect(owner, &orc);
    else { orc.left = 200; orc.top = 200; orc.right = 900; orc.bottom = 700; }
    const int x = orc.left + ((orc.right - orc.left) - winW) / 2;
    const int y = orc.top + ((orc.bottom - orc.top) - winH) / 2;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"Settings", style,
                               x, y, winW, winH, owner, nullptr, inst, nullptr);
    if (!dlg) return false;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));

    // Scaled control factory + a couple of shorthands.
    auto mk = [&](DWORD ex, const wchar_t* cls, const wchar_t* txt, DWORD s,
                  int lx, int ly, int lw, int lh, int id) -> HWND {
        return CreateWindowExW(ex, cls, txt, s, S(lx), S(ly), S(lw), S(lh), dlg,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               inst, nullptr);
    };
    auto group = [&](const wchar_t* title, int gy, int gh) {
        mk(0, L"BUTTON", title, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, M, gy,
           W - 2 * M, gh, -1);
    };
    auto label = [&](const wchar_t* text, int ly) {
        // Right-aligned in the column between the group's left edge and the
        // control column (ends ~10px before the controls start).
        mk(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT, M, ly + 4,
           ctrlX - M - 12, 18, -1);
    };

    // ---- Appearance -------------------------------------------------------
    group(L"Appearance", 10, 132);
    int r = 32;
    // Font family (editable monospace dropdown) + size.
    label(L"Font", r);
    st.font = mk(0, L"COMBOBOX", L"",
                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN |
                     CBS_AUTOHSCROLL | WS_VSCROLL,
                 ctrlX, r, ctrlW - 66, 260, kIdFont);
    for (const std::wstring& f : monospaceFonts())
        SendMessageW(st.font, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(f.c_str()));
    SetWindowTextW(st.font, v.fontFamily.c_str());
    st.fontSize = mk(WS_EX_CLIENTEDGE, L"EDIT",
                     std::to_wstring(static_cast<int>(v.fontSize)).c_str(),
                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_CENTER,
                     ctrlR - 46, r, 30, ch, kIdFontSize);
    SendMessageW(st.fontSize, EM_LIMITTEXT, 3, 0);
    mk(0, L"STATIC", L"pt", WS_CHILD | WS_VISIBLE, ctrlR - 12, r + 4, 16, 18, -1);
    r += 32;
    // Theme preset.
    label(L"Theme", r);
    st.theme = mk(0, L"COMBOBOX", L"",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                      WS_VSCROLL,
                  ctrlX, r, ctrlW, 260, kIdTheme);
    {
        int sel = 0, idx = 0;
        for (const ThemePreset& p : builtinThemePresets()) {
            SendMessageW(st.theme, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(p.name.c_str()));
            if (p.name == v.themeName) sel = idx;
            ++idx;
        }
        SendMessageW(st.theme, CB_SETCURSEL, sel, 0);
        st.themeInitialSel = sel;
    }
    r += 32;
    // Accent color: a color chip + Choose… + hex caption.
    label(L"Accent", r);
    st.accentSwatch = mk(WS_EX_STATICEDGE, L"STATIC", L"",
                         WS_CHILD | WS_VISIBLE, ctrlX, r, 34, ch, -1);
    mk(0, L"BUTTON", L"Choose…", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
       ctrlX + 44, r, 84, ch, kIdAccent);
    st.accentHex = mk(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, ctrlX + 138,
                      r + 4, 90, 18, -1);
    setAccentSwatch(&st);

    // ---- Terminal ---------------------------------------------------------
    group(L"Terminal", 152, 172);
    r = 174;
    label(L"Shell", r);
    st.shell = mk(0, L"COMBOBOX", L"",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN |
                      CBS_AUTOHSCROLL,
                  ctrlX, r, ctrlW, 200, kIdShell);
    for (const std::wstring& s : detectShells())
        SendMessageW(st.shell, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(s.c_str()));
    SetWindowTextW(st.shell, v.shell.c_str());
    r += 32;
    label(L"Scrollback", r);
    st.scrollback = mk(WS_EX_CLIENTEDGE, L"EDIT",
                       std::to_wstring(v.scrollback).c_str(),
                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL |
                           ES_NUMBER,
                       ctrlX, r, 90, ch, kIdScrollback);
    SendMessageW(st.scrollback, EM_LIMITTEXT, 7, 0);
    mk(0, L"STATIC", L"lines of history per pane", WS_CHILD | WS_VISIBLE,
       ctrlX + 100, r + 4, ctrlR - ctrlX - 100, 18, -1);
    r += 32;
    auto checkbox = [&](int id, const wchar_t* text, bool checked, int cyRow) {
        HWND c = mk(0, L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, labelX,
                    cyRow, ctrlR - labelX, 20, id);
        SendMessageW(c, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        return c;
    };
    st.copyOnSelect = checkbox(kIdCopyOnSelect,
                               L"Copy to clipboard when a selection ends",
                               v.copyOnSelect, r);
    r += 26;
    st.pasteWarn = checkbox(kIdPasteWarn,
                            L"Warn before pasting multiple lines",
                            v.multiLinePasteWarning, r);
    r += 26;
    st.unixTools = checkbox(
        kIdUnixTools, L"Unix tools — add Git's ls / grep / sed … to PATH",
        v.unixTools, r);
    r += 26;
    st.rememberLayout = checkbox(
        kIdRemember, L"Restore tabs & panes on launch", v.rememberLayout, r);

    // ---- Workspace --------------------------------------------------------
    group(L"Workspace", 334, 84);
    r = 356;
    label(L"Root", r);
    st.root = mk(WS_EX_CLIENTEDGE, L"EDIT", v.workspaceRoot.c_str(),
                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, ctrlX, r,
                 ctrlW - 80, ch, kIdRoot);
    mk(0, L"BUTTON", L"Browse…", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
       ctrlR - 74, r, 74, ch, kIdBrowse);
    r += 30;
    mk(0, L"STATIC", L"Empty = the parent of the launch folder.",
       WS_CHILD | WS_VISIBLE, ctrlX, r, ctrlW, 16, -1);

    // ---- OK / Cancel ------------------------------------------------------
    mk(0, L"BUTTON", L"OK",
       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, ctrlR - 178, 430,
       84, 28, IDOK);
    mk(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, ctrlR - 84,
       430, 84, 28, IDCANCEL);

    // A real Segoe UI font at the monitor's DPI — the biggest single upgrade
    // over the legacy bitmap DEFAULT_GUI_FONT.
    HFONT uiFont = CreateFontW(-MulDiv(9, static_cast<int>(dpi), 72), 0, 0, 0,
                               FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                               L"Segoe UI");
    if (!uiFont) uiFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    EnumChildWindows(dlg, [](HWND child, LPARAM f) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(f), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(uiFont));

    ShowWindow(dlg, SW_SHOW);
    SetFocus(st.font);
    if (owner) EnableWindow(owner, FALSE);

    MSG msg{};
    BOOL gm;
    while (!st.done && (gm = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (gm == -1) break;  // GetMessage error: bail rather than dispatch junk
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
        v.fontFamily = windowText(st.font);
        if (v.fontFamily.empty()) v.fontFamily = L"Cascadia Mono";
        const std::wstring fs = windowText(st.fontSize);
        if (!fs.empty()) {
            int pt = 0;
            for (wchar_t c : fs)
                if (c >= L'0' && c <= L'9') pt = pt * 10 + (c - L'0');
            if (pt < 6) pt = 6;
            if (pt > 96) pt = 96;  // same range as loadConfig / zoom
            v.fontSize = static_cast<float>(pt);
        }
        const int ti = static_cast<int>(SendMessageW(st.theme, CB_GETCURSEL, 0, 0));
        v.themePicked = (ti != st.themeInitialSel);  // did the user switch?
        if (ti >= 0) {
            const auto presets = builtinThemePresets();
            if (ti < static_cast<int>(presets.size()))
                v.themeName = presets[ti].name;
        }
        v.accent = st.accent;
        v.accentExplicit = st.accentChanged;
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
        v.rememberLayout =
            SendMessageW(st.rememberLayout, BM_GETCHECK, 0, 0) == BST_CHECKED;
        v.workspaceRoot = windowText(st.root);
    }

    if (owner) EnableWindow(owner, TRUE);
    DestroyWindow(dlg);
    if (owner) SetForegroundWindow(owner);
    // Release the GDI objects we created for the dialog.
    if (uiFont &&
        uiFont != reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)))
        DeleteObject(uiFont);
    if (st.accentBrush) DeleteObject(st.accentBrush);
    return st.accepted;
}

} // namespace liney
