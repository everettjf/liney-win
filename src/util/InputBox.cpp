#include "util/InputBox.h"

#include <algorithm>

namespace liney {

namespace {

constexpr int kIdEdit = 100;
constexpr int kIdOk = IDOK;       // 1
constexpr int kIdCancel = IDCANCEL;  // 2

struct State {
    HWND edit = nullptr;
    HWND preview = nullptr;
    std::wstring previewPrefix;
    bool done = false;
    bool accepted = false;
    std::wstring result;
};

void updatePreview(State* st) {
    if (!st || !st->preview || !st->edit) return;
    const int n = GetWindowTextLengthW(st->edit);
    std::wstring value(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(st->edit, value.data(), n + 1);
    value.resize(static_cast<size_t>(n));
    std::replace(value.begin(), value.end(), L'/', L'-');
    SetWindowTextW(st->preview,
                   (L"Directory: " + st->previewPrefix + value).c_str());
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_COMMAND:
        if (st && LOWORD(wParam) == kIdEdit &&
            (HIWORD(wParam) == EN_CHANGE ||
             HIWORD(wParam) == CBN_EDITCHANGE ||
             HIWORD(wParam) == CBN_SELCHANGE)) {
            updatePreview(st);
            return 0;
        }
        if (st && (LOWORD(wParam) == kIdOk || LOWORD(wParam) == kIdCancel)) {
            if (LOWORD(wParam) == kIdOk) {
                int n = GetWindowTextLengthW(st->edit);
                std::wstring buf(static_cast<size_t>(n) + 1, L'\0');
                GetWindowTextW(st->edit, buf.data(), n + 1);
                buf.resize(static_cast<size_t>(n));
                st->result = buf;
                st->accepted = true;
            }
            st->done = true;
            return 0;
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

} // namespace

std::wstring inputBox(HWND owner, const std::wstring& title,
                      const std::wstring& label, const std::wstring& initial) {
    static const wchar_t* kClass = L"LineyInputBox";
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

    const int w = 360, h = 150;
    RECT orc{};
    if (owner) GetWindowRect(owner, &orc);
    else { orc.left = 200; orc.top = 200; orc.right = 800; orc.bottom = 600; }
    const int x = orc.left + ((orc.right - orc.left) - w) / 2;
    const int y = orc.top + ((orc.bottom - orc.top) - h) / 2;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kClass, title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, owner, nullptr, inst,
        nullptr);
    if (!dlg) return L"";

    State st;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));

    CreateWindowExW(0, L"STATIC", label.c_str(), WS_CHILD | WS_VISIBLE, 12, 10,
                    w - 30, 18, dlg, nullptr, inst, nullptr);
    st.edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", initial.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 12, 32, w - 36, 24,
        dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEdit)), inst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"OK",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    w - 180, 70, 75, 26, dlg,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOk)), inst,
                    nullptr);
    CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    w - 95, 70, 75, 26, dlg,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCancel)),
                    inst, nullptr);

    // Use a readable UI font on the controls.
    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    EnumChildWindows(dlg, [](HWND child, LPARAM f) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(f), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));

    ShowWindow(dlg, SW_SHOW);
    SetFocus(st.edit);
    SendMessageW(st.edit, EM_SETSEL, 0, -1);
    if (owner) EnableWindow(owner, FALSE);

    // Modal message loop. IsDialogMessage gives us Tab/Enter/Esc handling.
    MSG msg{};
    BOOL gm;
    while (!st.done && (gm = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (gm == -1) break;  // GetMessage error: bail rather than dispatch junk
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    // An app-quit that arrived while this modal loop ran must not be
    // swallowed — re-post it so the outer message loop still terminates.
    if (!st.done && msg.message == WM_QUIT)
        PostQuitMessage(static_cast<int>(msg.wParam));

    if (owner) EnableWindow(owner, TRUE);
    DestroyWindow(dlg);
    if (owner) SetForegroundWindow(owner);
    return st.accepted ? st.result : L"";
}

std::wstring inputBoxWithSuggestions(
    HWND owner, const std::wstring& title, const std::wstring& label,
    const std::wstring& initial, const std::vector<std::wstring>& suggestions,
    const std::wstring& previewPrefix) {
    static const wchar_t* kClass = L"LineySuggestionInput";
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
    const int w = 460, h = 190;
    RECT ownerRect{200, 200, 900, 700};
    if (owner) GetWindowRect(owner, &ownerRect);
    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kClass, title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        ownerRect.left + ((ownerRect.right - ownerRect.left) - w) / 2,
        ownerRect.top + ((ownerRect.bottom - ownerRect.top) - h) / 2,
        w, h, owner, nullptr, inst, nullptr);
    if (!dlg) return L"";
    State st;
    st.previewPrefix = previewPrefix;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));
    CreateWindowExW(0, L"STATIC", label.c_str(), WS_CHILD | WS_VISIBLE,
                    14, 12, w - 36, 18, dlg, nullptr, inst, nullptr);
    st.edit = CreateWindowExW(
        0, L"COMBOBOX", initial.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN |
            CBS_AUTOHSCROLL | WS_VSCROLL,
        14, 36, w - 42, 220, dlg,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEdit)), inst, nullptr);
    for (const std::wstring& item : suggestions)
        SendMessageW(st.edit, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(item.c_str()));
    SetWindowTextW(st.edit, initial.c_str());
    st.preview = CreateWindowExW(
        0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        14, 72, w - 42, 20, dlg, nullptr, inst, nullptr);
    updatePreview(&st);
    CreateWindowExW(0, L"BUTTON", L"Create worktree",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    w - 230, 108, 125, 28, dlg,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOk)),
                    inst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Cancel",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    w - 95, 108, 75, 28, dlg,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCancel)),
                    inst, nullptr);
    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    EnumChildWindows(dlg, [](HWND child, LPARAM f) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(f), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));
    ShowWindow(dlg, SW_SHOW);
    SetFocus(st.edit);
    if (owner) EnableWindow(owner, FALSE);
    MSG msg{};
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) EnableWindow(owner, TRUE);
    DestroyWindow(dlg);
    if (owner) SetForegroundWindow(owner);
    return st.accepted ? st.result : L"";
}

} // namespace liney
