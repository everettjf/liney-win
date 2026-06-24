#pragma once

#include <windows.h>

#include <memory>

#include "render/Cell.h"
#include "render/IRenderer.h"

namespace liney {

// Top-level Win32 window. Owns the renderer and (for now) a demo grid; later
// milestones replace the demo grid with a libghostty-vt-driven screen fed by
// ConPTY output.
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
    void rebuildDemoGrid();
    void renderFrame();

    HWND hwnd_ = nullptr;
    std::unique_ptr<IRenderer> renderer_;
    Grid grid_;
};

} // namespace liney
