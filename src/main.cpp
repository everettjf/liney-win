// Liney - entry point.
//
// Normally opens a Win32 window and runs a real interactive local shell:
// ConPTY (src/pty) -> terminal core (src/vt) -> cell Grid -> Direct2D/DirectWrite
// renderer (src/render). The terminal core is Ghostty's libghostty-vt.
//
// The same executable also acts as the companion CLI when invoked with a
// `notify` / `title` argument (e.g. `liney notify "done"` from inside a pane):
// it writes an OSC sequence to its inherited stdout, which the hosting Liney
// pane turns into a tray notification / title change. Keeping one `Liney.exe`
// avoids a separate `liney.exe`, which would collide with the GUI's name on
// Windows' case-insensitive filesystem.

#include <windows.h>
#include <objbase.h>  // CoInitializeEx (WIN32_LEAN_AND_MEAN excludes it)

#include <cstdlib>  // __argc, __wargv
#include <string>

#include "app/Window.h"

namespace {

// ---- companion CLI (notify / title) --------------------------------------

std::string cliWideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        s.data(), n, nullptr, nullptr);
    return s;
}

// Strip bytes that would terminate/corrupt an OSC payload (C0 controls; and
// the field separator when the field is one of several `;`-separated parts).
std::string sanitizeOsc(const std::wstring& in, bool stripSemicolon) {
    std::string s = cliWideToUtf8(in);
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (static_cast<unsigned char>(c) < 0x20) continue;
        if (stripSemicolon && c == ';') { out.push_back(','); continue; }
        out.push_back(c);
    }
    return out;
}

// Write raw bytes to the inherited stdout. A GUI-subsystem process launched
// inside a Liney pane inherits the pseudoconsole as its stdout, so the OSC
// reaches the terminal's output stream and gets parsed there.
void emitRaw(const std::string& seq) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, seq.data(), static_cast<DWORD>(seq.size()), &written,
                  nullptr);
    }
}

// If the command line is a CLI command, handle it and return true (the caller
// should exit without opening a window). Otherwise return false.
bool runCliIfRequested(int& exitCode) {
    if (__argc < 2 || !__wargv) return false;
    const std::wstring cmd = __wargv[1];
    if (cmd == L"notify" && __argc >= 3) {
        const std::wstring title = __argc >= 4 ? __wargv[2] : L"Liney";
        const std::wstring body = __argc >= 4 ? __wargv[3] : __wargv[2];
        emitRaw("\x1b]777;notify;" + sanitizeOsc(title, true) + ";" +
                sanitizeOsc(body, false) + "\x07");
        exitCode = 0;
        return true;
    }
    if (cmd == L"title" && __argc >= 3) {
        emitRaw("\x1b]2;" + sanitizeOsc(__wargv[2], false) + "\x07");
        exitCode = 0;
        return true;
    }
    return false;
}

// Opt into Per-Monitor-V2 DPI awareness so the OS doesn't bitmap-stretch the
// window (fuzzy text on the >100% scaling most laptops use). Loaded dynamically
// so the binary still launches on pre-1703 Windows (system-DPI fallback).
void enablePerMonitorDpi() {
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

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    int cliExit = 0;
    if (runCliIfRequested(cliExit)) return cliExit;  // `Liney notify …` — no window

    enablePerMonitorDpi();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // for WIC image loading
    liney::Window window;
    if (!window.create(hInstance, L"Liney", 1000, 640)) {
        return 1;
    }
    window.show(nCmdShow);
    return window.runMessageLoop();
}
