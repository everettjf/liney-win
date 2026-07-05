// liney-win - entry point.
//
// Opens a Win32 window and runs a real interactive local shell. Pipeline:
// ConPTY (src/pty) -> terminal core (src/vt) -> cell Grid -> Direct2D/DirectWrite
// renderer (src/render). The terminal core is Ghostty's libghostty-vt, built
// from Ghostty via Zig (see CMakeLists.txt / tools/build.ps1).

#include <windows.h>
#include <objbase.h>  // CoInitializeEx (WIN32_LEAN_AND_MEAN excludes it)

#include "app/Window.h"

// Opt into Per-Monitor-V2 DPI awareness so the OS doesn't bitmap-stretch the
// window (which makes text fuzzy on the >100% scaling most laptops use). Loaded
// dynamically so the binary still launches on pre-1703 Windows, where we fall
// back to system-DPI awareness.
static void enablePerMonitorDpi() {
    using SetCtxFn = BOOL(WINAPI*)(void*);  // SetProcessDpiAwarenessContext
    if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
        if (auto set = reinterpret_cast<SetCtxFn>(
                GetProcAddress(u, "SetProcessDpiAwarenessContext"))) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4.
            if (set(reinterpret_cast<void*>(static_cast<LONG_PTR>(-4)))) return;
        }
    }
    SetProcessDPIAware();  // fallback (Vista+): system-DPI aware
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    enablePerMonitorDpi();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // for WIC image loading
    liney::Window window;
    if (!window.create(hInstance, L"liney-win", 1000, 640)) {
        return 1;
    }
    window.show(nCmdShow);
    return window.runMessageLoop();
}
