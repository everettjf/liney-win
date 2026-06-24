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

void Window::renderFrame() {
    if (sessionActive_) terminal_.snapshotInto(grid_);
    renderer_->render(grid_);
}

} // namespace liney
