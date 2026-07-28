#include "app/SettingsDialog.h"

#include <commdlg.h>  // ChooseColorW (accent picker)
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>   // SHBrowseForFolderW (workspace root picker)
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "core/Themes.h"
#include "core/Config.h"

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
constexpr int kIdSplitDir = 112;
constexpr int kIdAutoUpdate = 113;
constexpr int kIdAiProvider = 114;
constexpr int kIdAiModel = 115;
constexpr int kIdAiCwd = 116;
constexpr int kIdLigatures = 117;
constexpr int kIdOpenConfig = 118;
constexpr int kIdResetPage = 119;
constexpr int kIdPageAppearance = 201;
constexpr int kIdPageTerminal = 202;
constexpr int kIdPageWorkspace = 203;
constexpr int kIdPageAi = 204;

struct State {
    HWND shell = nullptr;
    HWND font = nullptr;
    HWND fontSize = nullptr;
    HWND fontLigatures = nullptr;
    HWND theme = nullptr;
    HWND accentSwatch = nullptr;  // static showing the current accent hex
    HWND scrollback = nullptr;
    HWND copyOnSelect = nullptr;
    HWND pasteWarn = nullptr;
    HWND unixTools = nullptr;
    HWND rememberLayout = nullptr;
    HWND splitWorkspaceDir = nullptr;
    HWND autoUpdate = nullptr;
    HWND aiProvider = nullptr;
    HWND aiModel = nullptr;
    HWND aiCwd = nullptr;
    HWND root = nullptr;
    HWND accentHex = nullptr;     // "#RRGGBB" caption next to the swatch
    HWND themePreview = nullptr;
    HWND validation = nullptr;
    HBRUSH accentBrush = nullptr; // fills the swatch via WM_CTLCOLORSTATIC
    HBRUSH themePreviewBrush = nullptr;
    COLORREF themePreviewText = RGB(232, 232, 208);
    HBRUSH backgroundBrush = nullptr;
    HBRUSH fieldBrush = nullptr;
    Color accent{ 120, 200, 160 };
    bool accentChanged = false;   // user used the color picker
    int themeInitialSel = 0;      // selection when the dialog opened
    bool done = false;
    bool accepted = false;
    int buildingPage = -1;
    int activePage = 0;
    std::array<std::vector<HWND>, 4> pageControls;
    std::array<HWND, 4> pageButtons{};
};

void setAccentSwatch(State* st);
void updateThemePreview(State* st);

void showPage(State* st, int page) {
    if (!st || page < 0 || page >= 4) return;
    st->activePage = page;
    for (int p = 0; p < 4; ++p) {
        for (HWND control : st->pageControls[p])
            ShowWindow(control, p == page ? SW_SHOW : SW_HIDE);
        if (st->pageButtons[p])
            SendMessageW(st->pageButtons[p], BM_SETCHECK,
                         p == page ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

void resetActivePage(State* st) {
    if (!st) return;
    if (st->validation) ShowWindow(st->validation, SW_HIDE);
    switch (st->activePage) {
    case 0: {
        SetWindowTextW(st->font, L"Cascadia Mono");
        SetWindowTextW(st->fontSize, L"16");
        SendMessageW(st->theme, CB_SETCURSEL, 0, 0);
        const auto presets = builtinThemePresets();
        if (!presets.empty()) {
            st->accent = presets.front().ui.accent;
            st->accentChanged = true;
            setAccentSwatch(st);
            updateThemePreview(st);
        }
        break;
    }
    case 1:
        SetWindowTextW(st->shell, L"cmd.exe");
        SetWindowTextW(st->scrollback, L"10000");
        SendMessageW(st->fontLigatures, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(st->copyOnSelect, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(st->pasteWarn, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(st->unixTools, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(st->rememberLayout, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(st->splitWorkspaceDir, BM_SETCHECK, BST_UNCHECKED, 0);
#ifndef LINEY_STORE_BUILD
        SendMessageW(st->autoUpdate, BM_SETCHECK, BST_CHECKED, 0);
#endif
        break;
    case 2:
        SetWindowTextW(st->root, L"");
        break;
    case 3:
        SendMessageW(st->aiProvider, CB_SETCURSEL, 0, 0);
        SetWindowTextW(st->aiModel, L"gpt-5.6-luna");
        SendMessageW(st->aiCwd, BM_SETCHECK, BST_UNCHECKED, 0);
        break;
    }
}

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

void updateThemePreview(State* st) {
    if (!st || !st->theme || !st->themePreview) return;
    const int selected =
        static_cast<int>(SendMessageW(st->theme, CB_GETCURSEL, 0, 0));
    const auto presets = builtinThemePresets();
    if (selected < 0 || selected >= static_cast<int>(presets.size())) return;
    const Theme& theme = presets[selected].terminal;
    if (st->themePreviewBrush) DeleteObject(st->themePreviewBrush);
    st->themePreviewBrush = CreateSolidBrush(
        RGB(theme.background.r, theme.background.g, theme.background.b));
    st->themePreviewText =
        RGB(theme.foreground.r, theme.foreground.g, theme.foreground.b);
    const std::wstring text =
        presets[selected].name + L"   PS> git status";
    SetWindowTextW(st->themePreview, text.c_str());
    InvalidateRect(st->themePreview, nullptr, TRUE);
}

std::wstring windowText(HWND h) {
    const int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

bool showValidation(State* st, int page, HWND field,
                    const std::wstring& message) {
    if (!st) return false;
    showPage(st, page);
    if (st->validation) {
        SetWindowTextW(st->validation, message.c_str());
        ShowWindow(st->validation, SW_SHOW);
    }
    SetFocus(field);
    return false;
}

bool parseIntegerField(State* st, HWND field, const wchar_t* label,
                       int minimum, int maximum, int page) {
    const std::wstring value = windowText(field);
    if (value.empty()) {
        std::wstring message = std::wstring(label) + L" is required.";
        return showValidation(st, page, field, message);
    }
    wchar_t* end = nullptr;
    const long number = wcstol(value.c_str(), &end, 10);
    if (!end || *end != L'\0' || number < minimum || number > maximum) {
        std::wstring message = std::wstring(label) + L" must be between " +
            std::to_wstring(minimum) + L" and " + std::to_wstring(maximum) +
            L".";
        showValidation(st, page, field, message);
        SendMessageW(field, EM_SETSEL, 0, -1);
        return false;
    }
    return true;
}

bool validateSettings(HWND owner, State* st) {
    (void)owner;
    if (st->validation) {
        SetWindowTextW(st->validation, L"");
        ShowWindow(st->validation, SW_HIDE);
    }
    if (windowText(st->shell).empty()) {
        return showValidation(st, 1, st->shell, L"Choose a shell command.");
    }
    if (!parseIntegerField(st, st->fontSize, L"Font size", 6, 96, 0) ||
        !parseIntegerField(st, st->scrollback, L"Scrollback", 0, 1000000, 1))
        return false;
    const std::wstring root = windowText(st->root);
    if (!root.empty()) {
        const DWORD attributes = GetFileAttributesW(root.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            showValidation(
                st, 2, st->root,
                L"Workspace root must be an existing folder, or blank.");
            SendMessageW(st->root, EM_SETSEL, 0, -1);
            return false;
        }
    }
    return true;
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
        if (LOWORD(wParam) == kIdTheme &&
            HIWORD(wParam) == CBN_SELCHANGE) {
            updateThemePreview(st);
            return 0;
        }
        switch (LOWORD(wParam)) {
        case kIdPageAppearance: showPage(st, 0); return 0;
        case kIdPageTerminal: showPage(st, 1); return 0;
        case kIdPageWorkspace: showPage(st, 2); return 0;
        case kIdPageAi: showPage(st, 3); return 0;
        case kIdResetPage:
            resetActivePage(st);
            return 0;
        case kIdOpenConfig: {
            const std::wstring path = configDir() + L"\\config.json";
            ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
            return 0;
        }
        case IDOK:
            if (!validateSettings(hwnd, st)) return 0;
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
        if (st && reinterpret_cast<HWND>(lParam) == st->themePreview &&
            st->themePreviewBrush) {
            SetTextColor(reinterpret_cast<HDC>(wParam), st->themePreviewText);
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            return reinterpret_cast<LRESULT>(st->themePreviewBrush);
        }
        if (st && reinterpret_cast<HWND>(lParam) == st->validation) {
            SetTextColor(reinterpret_cast<HDC>(wParam), RGB(255, 145, 145));
            SetBkColor(reinterpret_cast<HDC>(wParam), RGB(31, 33, 40));
            return reinterpret_cast<LRESULT>(st->backgroundBrush);
        }
        if (st) {
            SetTextColor(reinterpret_cast<HDC>(wParam), RGB(225, 228, 235));
            SetBkColor(reinterpret_cast<HDC>(wParam), RGB(31, 33, 40));
            return reinterpret_cast<LRESULT>(st->backgroundBrush);
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        if (st) {
            SetTextColor(reinterpret_cast<HDC>(wParam), RGB(236, 238, 244));
            SetBkColor(reinterpret_cast<HDC>(wParam), RGB(39, 42, 51));
            return reinterpret_cast<LRESULT>(st->fieldBrush);
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
        wc.hbrBackground = CreateSolidBrush(RGB(31, 33, 40));
        wc.lpszClassName = kClass;
        RegisterClassW(&wc);
        registered = true;
    }

    // Per-monitor DPI: lay the dialog out in logical (96-dpi) units and scale.
    const UINT dpi = owner ? GetDpiForWindow(owner) : 96;
    auto S = [dpi](int px) { return MulDiv(px, static_cast<int>(dpi), 96); };

    // Logical layout grid.
    const int W = 520;                 // client width
    const int M = 20;                  // outer margin
    const int labelX = M + 8;          // option column inside a page
    const int ctrlX = M + 112;         // control column
    const int ctrlR = W - M;           // control right edge
    const int ctrlW = ctrlR - ctrlX;
    const int ch = 24;                 // control height

    State st;
    st.accent = v.accent;
    st.backgroundBrush = CreateSolidBrush(RGB(31, 33, 40));
    st.fieldBrush = CreateSolidBrush(RGB(39, 42, 51));

    // Size the window so the *client* area is exactly W × contentH.
    // Four focused pages keep the dialog short enough for 200% DPI laptops;
    // the previous 700-DIP single page exceeded their working area.
    const int contentH = 350;
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
    // Ask Windows 11 for a dark caption and rounded native frame. Older
    // Windows versions safely ignore unsupported attributes.
    BOOL dark = TRUE;
    DwmSetWindowAttribute(dlg, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */,
                          &dark, sizeof(dark));
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(dlg, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &corners, sizeof(corners));

    // Scaled control factory + a couple of shorthands.
    auto mk = [&](DWORD ex, const wchar_t* cls, const wchar_t* txt, DWORD s,
                  int lx, int ly, int lw, int lh, int id) -> HWND {
        HWND control = CreateWindowExW(
            ex, cls, txt, s, S(lx), S(ly), S(lw), S(lh), dlg,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
        if (control && st.buildingPage >= 0 && st.buildingPage < 4)
            st.pageControls[st.buildingPage].push_back(control);
        return control;
    };
    auto group = [&](const wchar_t* title, int gy, int) {
        mk(0, L"STATIC", title, WS_CHILD | WS_VISIBLE, M, gy,
           W - 2 * M, 24, -1);
    };
    auto label = [&](const wchar_t* text, int ly) {
        // Right-aligned in the column between the group's left edge and the
        // control column (ends ~10px before the controls start).
        mk(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT, M, ly + 4,
           ctrlX - M - 12, 18, -1);
    };

    // Page navigation keeps the dialog scannable instead of presenting one
    // long control-panel form. BS_PUSHLIKE radio buttons provide native
    // keyboard and high-contrast behavior while reading as a compact tab row.
    const wchar_t* pageLabels[] = {L"Appearance", L"Terminal", L"Workspace", L"AI"};
    const int pageIds[] = {kIdPageAppearance, kIdPageTerminal,
                           kIdPageWorkspace, kIdPageAi};
    for (int i = 0; i < 4; ++i) {
        st.pageButtons[i] = CreateWindowExW(
            0, L"BUTTON", pageLabels[i],
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON |
                BS_PUSHLIKE,
            S(M + i * 118), S(14), S(112), S(30), dlg,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(pageIds[i])),
            inst, nullptr);
    }

    // ---- Appearance -------------------------------------------------------
    st.buildingPage = 0;
    group(L"Appearance", 58, 0);
    int r = 86;
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
    r += 30;
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
    r += 30;
    // Accent color: a color chip + Choose… + hex caption.
    label(L"Accent", r);
    st.accentSwatch = mk(WS_EX_STATICEDGE, L"STATIC", L"",
                         WS_CHILD | WS_VISIBLE, ctrlX, r, 34, ch, -1);
    mk(0, L"BUTTON", L"Choose…", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
       ctrlX + 44, r, 84, ch, kIdAccent);
    st.accentHex = mk(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, ctrlX + 138,
                      r + 4, 90, 18, -1);
    setAccentSwatch(&st);
    r += 36;
    label(L"Preview", r);
    st.themePreview = mk(
        WS_EX_STATICEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
        ctrlX, r, ctrlW, 34, -1);
    updateThemePreview(&st);

    // ---- Terminal ---------------------------------------------------------
    st.buildingPage = 1;
    group(L"Terminal", 58, 0);
    r = 86;
    label(L"Shell", r);
    st.shell = mk(0, L"COMBOBOX", L"",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN |
                      CBS_AUTOHSCROLL,
                  ctrlX, r, ctrlW, 200, kIdShell);
    for (const std::wstring& s : detectShells())
        SendMessageW(st.shell, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(s.c_str()));
    SetWindowTextW(st.shell, v.shell.c_str());
    r += 30;
    label(L"Scrollback", r);
    st.scrollback = mk(WS_EX_CLIENTEDGE, L"EDIT",
                       std::to_wstring(v.scrollback).c_str(),
                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL |
                           ES_NUMBER,
                       ctrlX, r, 90, ch, kIdScrollback);
    SendMessageW(st.scrollback, EM_LIMITTEXT, 7, 0);
    mk(0, L"STATIC", L"lines of history per pane", WS_CHILD | WS_VISIBLE,
       ctrlX + 100, r + 4, ctrlR - ctrlX - 100, 18, -1);
    r += 30;
    auto checkbox = [&](int id, const wchar_t* text, bool checked, int cyRow) {
        HWND c = mk(0, L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, labelX,
                    cyRow, ctrlR - labelX, 20, id);
        SendMessageW(c, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        return c;
    };
    st.fontLigatures = checkbox(
        kIdLigatures, L"Enable programming ligatures (opt-in shaping)",
        v.fontLigatures, r);
    r += 22;
    st.copyOnSelect = checkbox(kIdCopyOnSelect,
                               L"Copy to clipboard when a selection ends",
                               v.copyOnSelect, r);
    r += 22;
    st.pasteWarn = checkbox(kIdPasteWarn,
                            L"Warn before pasting multiple lines",
                            v.multiLinePasteWarning, r);
    r += 22;
    st.unixTools = checkbox(
        kIdUnixTools, L"Unix tools — add Git's ls / grep / sed … to PATH",
        v.unixTools, r);
    r += 22;
    st.rememberLayout = checkbox(
        kIdRemember, L"Restore tabs & panes on launch", v.rememberLayout, r);
    r += 22;
    st.splitWorkspaceDir = checkbox(
        kIdSplitDir,
        L"New splits open in the workspace / home dir (else inherit the pane's)",
        v.splitUseWorkspaceDir, r);
    r += 22;
#ifndef LINEY_STORE_BUILD
    st.autoUpdate = checkbox(
        kIdAutoUpdate, L"Check for stable updates when Liney starts",
        v.checkForUpdatesOnStartup, r);
#endif

    // ---- Workspace --------------------------------------------------------
    st.buildingPage = 2;
    group(L"Workspace", 58, 0);
    r = 86;
    label(L"Root", r);
    st.root = mk(WS_EX_CLIENTEDGE, L"EDIT", v.workspaceRoot.c_str(),
                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, ctrlX, r,
                 ctrlW - 80, ch, kIdRoot);
    mk(0, L"BUTTON", L"Browse…", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
       ctrlR - 74, r, 74, ch, kIdBrowse);
    r += 30;
    mk(0, L"STATIC", L"Empty = only projects you add explicitly.",
       WS_CHILD | WS_VISIBLE, ctrlX, r, ctrlW, 16, -1);

    // ---- AI ---------------------------------------------------------------
    st.buildingPage = 3;
    group(L"AI - terminal contents are sent only when requested", 58, 0);
    r = 86;
    label(L"Provider", r);
    st.aiProvider = mk(0, L"COMBOBOX", L"",
                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                       ctrlX, r, 150, 120, kIdAiProvider);
    const wchar_t* providerLabels[] = {L"Off", L"OpenAI", L"Codex CLI", L"Custom API"};
    const wchar_t* providerValues[] = {L"off", L"openai", L"codex", L"custom"};
    int providerSelection = 0;
    for (int i = 0; i < 4; ++i) {
        SendMessageW(st.aiProvider, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(providerLabels[i]));
        if (v.aiProvider == providerValues[i]) providerSelection = i;
    }
    SendMessageW(st.aiProvider, CB_SETCURSEL, providerSelection, 0);
    mk(0, L"STATIC", L"API keys come from environment variables",
       WS_CHILD | WS_VISIBLE, ctrlX + 162, r + 4, ctrlW - 162, 18, -1);
    r += 32;
    label(L"Model", r);
    st.aiModel = mk(WS_EX_CLIENTEDGE, L"EDIT", v.aiModel.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                    ctrlX, r, ctrlW, ch, kIdAiModel);
    r += 30;
    st.aiCwd = checkbox(kIdAiCwd,
                        L"Include the current directory in AI requests",
                        v.aiIncludeCwd, r);

    st.buildingPage = -1;
    // ---- OK / Cancel ------------------------------------------------------
    st.validation = mk(0, L"STATIC", L"",
                       WS_CHILD | SS_RIGHT, M, 286,
                       ctrlR - M - 230, 18, -1);
    mk(0, L"BUTTON", L"Open config",
       WS_CHILD | WS_VISIBLE | WS_TABSTOP, M, 310, 92, 28, kIdOpenConfig);
    mk(0, L"BUTTON", L"Reset page",
       WS_CHILD | WS_VISIBLE | WS_TABSTOP, M + 102, 310, 92, 28,
       kIdResetPage);
    mk(0, L"BUTTON", L"OK",
       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, ctrlR - 178, 310,
       84, 28, IDOK);
    mk(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, ctrlR - 84,
       310, 84, 28, IDCANCEL);

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
        SetWindowTheme(child, L"DarkMode_Explorer", nullptr);
        return TRUE;
    }, reinterpret_cast<LPARAM>(uiFont));

    showPage(&st, std::clamp(v.page, 0, 3));
    ShowWindow(dlg, SW_SHOW);
    const HWND firstPageControl[] = {
        st.font, st.shell, st.root, st.aiProvider};
    SetFocus(firstPageControl[st.activePage]);
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
        v.fontLigatures =
            SendMessageW(st.fontLigatures, BM_GETCHECK, 0, 0) == BST_CHECKED;
        v.unixTools =
            SendMessageW(st.unixTools, BM_GETCHECK, 0, 0) == BST_CHECKED;
        v.rememberLayout =
            SendMessageW(st.rememberLayout, BM_GETCHECK, 0, 0) == BST_CHECKED;
        v.splitUseWorkspaceDir =
            SendMessageW(st.splitWorkspaceDir, BM_GETCHECK, 0, 0) == BST_CHECKED;
#ifdef LINEY_STORE_BUILD
        v.checkForUpdatesOnStartup = false;
#else
        v.checkForUpdatesOnStartup =
            SendMessageW(st.autoUpdate, BM_GETCHECK, 0, 0) == BST_CHECKED;
#endif
        const int provider = static_cast<int>(
            SendMessageW(st.aiProvider, CB_GETCURSEL, 0, 0));
        static const wchar_t* providers[] = {L"off", L"openai", L"codex", L"custom"};
        v.aiProvider = providers[provider >= 0 && provider < 4 ? provider : 0];
        v.aiModel = windowText(st.aiModel);
        if (v.aiModel.empty()) v.aiModel = L"gpt-5.6-luna";
        v.aiIncludeCwd =
            SendMessageW(st.aiCwd, BM_GETCHECK, 0, 0) == BST_CHECKED;
        v.workspaceRoot = windowText(st.root);
    }

    v.page = st.activePage;
    if (owner) EnableWindow(owner, TRUE);
    DestroyWindow(dlg);
    if (owner) SetForegroundWindow(owner);
    // Release the GDI objects we created for the dialog.
    if (uiFont &&
        uiFont != reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)))
        DeleteObject(uiFont);
    if (st.accentBrush) DeleteObject(st.accentBrush);
    if (st.themePreviewBrush) DeleteObject(st.themePreviewBrush);
    if (st.backgroundBrush) DeleteObject(st.backgroundBrush);
    if (st.fieldBrush) DeleteObject(st.fieldBrush);
    return st.accepted;
}

} // namespace liney
