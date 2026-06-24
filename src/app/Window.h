#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "pty/ConPty.h"
#include "render/Cell.h"
#include "render/IRenderer.h"
#include "vt/Terminal.h"

namespace liney {

// Top-level Win32 window. When libghostty-vt is available it runs a real local
// shell: ConPTY output -> Terminal (libghostty-vt) -> Grid -> renderer. Without
// it, the window falls back to rendering a static demo grid.
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
};

} // namespace liney
