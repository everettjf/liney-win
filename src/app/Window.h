#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "pty/ConPty.h"
#include "render/Cell.h"
#include "render/IRenderer.h"
#include "vt/Terminal.h"

namespace liney {

// Top-level Win32 window. Runs a real local shell: keyboard -> ConPTY ->
// Terminal (built-in VTEmulator, or libghostty-vt when compiled in) -> Grid ->
// renderer. If the shell fails to start it falls back to a static demo grid.
class Window {
public:
    Window();
    ~Window();

    bool create(HINSTANCE hInstance, const wchar_t* title, int width, int height);
    void show(int nCmdShow);
    int runMessageLoop();

private:
    static LRESULT CALLBACK wndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndProc(UINT msg, WPARAM wParam, LPARAM lParam);

    void onResize(unsigned widthPx, unsigned heightPx);
    void clientCells(int& cols, int& rows) const;
    void startSession(int cols, int rows);
    void rebuildDemoGrid();
    void renderFrame();

    // Input: translate keystrokes to PTY bytes.
    void onChar(wchar_t unit);            // WM_CHAR (printable + control chars)
    bool onKeyDown(WPARAM vk);            // special keys; returns true if handled
    void sendUtf16(const wchar_t* s, size_t len);  // UTF-16 -> UTF-8 -> PTY

    HWND hwnd_ = nullptr;
    std::unique_ptr<IRenderer> renderer_;
    Grid grid_;

    // Session: terminal_ must outlive pty_ so the reader thread (which calls
    // terminal_.write) is joined before terminal_ is destroyed. Members destruct
    // in reverse declaration order, so declare terminal_ first.
    Terminal terminal_;
    ConPty pty_;
    bool sessionActive_ = false;
    std::wstring shell_ = L"cmd.exe";
    wchar_t pendingHighSurrogate_ = 0;  // for split UTF-16 WM_CHAR pairs
};

} // namespace liney
