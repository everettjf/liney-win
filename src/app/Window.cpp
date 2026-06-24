#include "app/Window.h"

#include <iterator>
#include <string>

#include "render/D2DRenderer.h"

namespace liney {

static const wchar_t* kClassName = L"LineyWinMainWindow";

Window::Window() : renderer_(std::make_unique<D2DRenderer>()) {}
Window::~Window() = default;

bool Window::create(HINSTANCE hInstance, const wchar_t* title, int width,
                    int height) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Window::wndProcThunk;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_IBEAM);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0, kClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        width, height, nullptr, nullptr, hInstance, this);
    if (!hwnd_) return false;

    if (!renderer_->initialize(hwnd_)) return false;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    renderer_->resize(rc.right - rc.left, rc.bottom - rc.top);

    int cols = 0, rows = 0;
    clientCells(cols, rows);
    startSession(cols, rows);
    if (!sessionActive_) rebuildDemoGrid();
    return true;
}

void Window::show(int nCmdShow) {
    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
}

int Window::runMessageLoop() {
    MSG msg{};
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return static_cast<int>(msg.wParam);
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        // Present is vsync-throttled (Present(1, 0)), so this does not spin.
        renderFrame();
    }
}

LRESULT CALLBACK Window::wndProcThunk(HWND hwnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam) {
    Window* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->wndProc(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Window::wndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        onResize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_CHAR:
        onChar(static_cast<wchar_t>(wParam));
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (onKeyDown(wParam)) return 0;
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

void Window::onResize(unsigned widthPx, unsigned heightPx) {
    if (widthPx == 0 || heightPx == 0) return;
    renderer_->resize(widthPx, heightPx);

    int cols = 0, rows = 0;
    clientCells(cols, rows);
    if (sessionActive_) {
        unsigned cw = 0, ch = 0;
        renderer_->cellSize(cw, ch);
        terminal_.resize(cols, rows, cw, ch);
        pty_.resize(static_cast<short>(cols), static_cast<short>(rows));
    } else {
        rebuildDemoGrid();
    }
}

void Window::clientCells(int& cols, int& rows) const {
    unsigned cw = 0, ch = 0;
    renderer_->cellSize(cw, ch);
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    cols = cw ? static_cast<int>((rc.right - rc.left) / cw) : 80;
    rows = ch ? static_cast<int>((rc.bottom - rc.top) / ch) : 24;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
}

void Window::startSession(int cols, int rows) {
    // create() returns false when libghostty-vt is not compiled in.
    if (!terminal_.create(cols, rows)) return;
    const bool ok = pty_.start(
        shell_, static_cast<short>(cols), static_cast<short>(rows),
        [this](const char* data, size_t len) { terminal_.write(data, len); });
    sessionActive_ = ok;
}

void Window::rebuildDemoGrid() {
    unsigned cw = 0, ch = 0;
    renderer_->cellSize(cw, ch);
    if (cw == 0 || ch == 0) return;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const unsigned wPx = rc.right - rc.left;
    const unsigned hPx = rc.bottom - rc.top;

    int cols = static_cast<int>(wPx / cw);
    int rows = static_cast<int>(hPx / ch);
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    grid_.resize(cols, rows);

    // Placeholder content until libghostty-vt drives the grid.
    const std::wstring lines[] = {
        L"liney-win  -  Direct2D/DirectWrite scaffold",
        L"",
        L"Stage 1: per-cell direct draw (this view).",
        L"Stage 2: glyph atlas + Direct3D 11.",
        L"",
        L"Build with -DLINEY_WITH_LIBGHOSTTY=ON for a live shell.",
    };
    const Color accent{ 120, 220, 160 };
    for (int row = 0; row < rows && row < static_cast<int>(std::size(lines)); ++row) {
        const std::wstring& text = lines[row];
        for (int col = 0; col < cols && col < static_cast<int>(text.size()); ++col) {
            Cell& cell = grid_.at(col, row);
            cell.ch = std::wstring(1, text[col]);
            cell.fg = (row == 0) ? accent : Color{ 210, 210, 210 };
        }
    }
}

void Window::sendUtf16(const wchar_t* s, size_t len) {
    if (!sessionActive_ || len == 0) return;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(len),
                                    nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return;
    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(len), utf8.data(), bytes,
                        nullptr, nullptr);
    pty_.write(utf8.data(), utf8.size());
}

void Window::onChar(wchar_t unit) {
    // Reassemble surrogate pairs that arrive as two WM_CHAR messages.
    if (unit >= 0xD800 && unit <= 0xDBFF) {
        pendingHighSurrogate_ = unit;
        return;
    }
    if (unit >= 0xDC00 && unit <= 0xDFFF) {
        if (pendingHighSurrogate_) {
            wchar_t pair[2] = { pendingHighSurrogate_, unit };
            sendUtf16(pair, 2);
            pendingHighSurrogate_ = 0;
        }
        return;
    }
    pendingHighSurrogate_ = 0;
    sendUtf16(&unit, 1);
}

bool Window::onKeyDown(WPARAM vk) {
    // Keys that do not produce a WM_CHAR: translate to xterm escape sequences.
    const char* seq = nullptr;
    switch (vk) {
    case VK_UP:     seq = "\x1b[A"; break;
    case VK_DOWN:   seq = "\x1b[B"; break;
    case VK_RIGHT:  seq = "\x1b[C"; break;
    case VK_LEFT:   seq = "\x1b[D"; break;
    case VK_HOME:   seq = "\x1b[H"; break;
    case VK_END:    seq = "\x1b[F"; break;
    case VK_PRIOR:  seq = "\x1b[5~"; break;  // Page Up
    case VK_NEXT:   seq = "\x1b[6~"; break;  // Page Down
    case VK_INSERT: seq = "\x1b[2~"; break;
    case VK_DELETE: seq = "\x1b[3~"; break;
    case VK_F1:  seq = "\x1bOP"; break;
    case VK_F2:  seq = "\x1bOQ"; break;
    case VK_F3:  seq = "\x1bOR"; break;
    case VK_F4:  seq = "\x1bOS"; break;
    case VK_F5:  seq = "\x1b[15~"; break;
    case VK_F6:  seq = "\x1b[17~"; break;
    case VK_F7:  seq = "\x1b[18~"; break;
    case VK_F8:  seq = "\x1b[19~"; break;
    case VK_F9:  seq = "\x1b[20~"; break;
    case VK_F10: seq = "\x1b[21~"; break;
    case VK_F11: seq = "\x1b[23~"; break;
    case VK_F12: seq = "\x1b[24~"; break;
    default: return false;  // let WM_CHAR handle character keys
    }
    if (sessionActive_) pty_.write(seq, std::char_traits<char>::length(seq));
    return true;
}

void Window::renderFrame() {
    if (sessionActive_) terminal_.snapshotInto(grid_);
    renderer_->render(grid_);
}

} // namespace liney
