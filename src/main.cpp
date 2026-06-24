// liney-win - entry point.
//
// Stage-1 scaffold: opens a Win32 window and renders a sample monospace cell
// grid through the Direct2D/DirectWrite renderer (see RENDERING.md). ConPTY
// (src/pty) and libghostty-vt integration are the next milestones.

#include <windows.h>

#include "app/Window.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    liney::Window window;
    if (!window.create(hInstance, L"liney-win", 1000, 640)) {
        return 1;
    }
    window.show(nCmdShow);
    return window.runMessageLoop();
}
