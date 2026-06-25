// liney-win - entry point.
//
// Opens a Win32 window and runs a real interactive local shell. Pipeline:
// ConPTY (src/pty) -> terminal core (src/vt) -> cell Grid -> Direct2D/DirectWrite
// renderer (src/render). The terminal core is Ghostty's libghostty-vt, built
// from Ghostty via Zig (see CMakeLists.txt / tools/build.ps1).

#include <windows.h>
#include <objbase.h>  // CoInitializeEx (WIN32_LEAN_AND_MEAN excludes it)

#include "app/Window.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // for WIC image loading
    liney::Window window;
    if (!window.create(hInstance, L"liney-win", 1000, 640)) {
        return 1;
    }
    window.show(nCmdShow);
    return window.runMessageLoop();
}
