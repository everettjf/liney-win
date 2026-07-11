#include "util/InputBox.h"

namespace liney {

namespace {

constexpr int kIdEdit = 100;
constexpr int kIdOk = IDOK;       // 1
constexpr int kIdCancel = IDCANCEL;  // 2

struct State {
    HWND edit = nullptr;
    bool done = false;
    bool accepted = false;
    std::wstring result;
};

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_COMMAND:
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
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0)) {
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

} // namespace liney
