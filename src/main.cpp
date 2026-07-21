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
#include <dwrite.h>

#include <cstdlib>  // __argc, __wargv
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

#include "app/Window.h"
#include "core/TerminalSession.h"
#include "core/Config.h"
#include "core/ShellProfiles.h"
#include "core/CommandHistory.h"
#include "vt/Terminal.h"
#include "util/Diagnostics.h"
#include "util/Process.h"
#include "app/WindowInternal.h"

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
    if (cmd == L"agent-status" && __argc >= 3) {
        const std::wstring value = __wargv[2];
        if (value != L"running" && value != L"waiting" &&
            value != L"needs-input" && value != L"done" &&
            value != L"failed") {
            exitCode = 2;
            return true;
        }
        emitRaw("\x1b]777;agent-status;" + sanitizeOsc(value, true) + "\x07");
        exitCode = 0;
        return true;
    }
    if (cmd == L"self-test") {
        liney::TerminalSession session;
        if (!session.start(L"cmd.exe /d /s /c \"echo liney-self-test\"",
                           L"", 80, 24, 100)) {
            exitCode = 10;
            return true;
        }
        const ULONGLONG deadline = GetTickCount64() + 5000;
        while (!session.exited() && GetTickCount64() < deadline) Sleep(10);
        if (!session.exited()) {
            exitCode = 11;
            return true;
        }
        session.snapshot();
        std::string output;
        const bool rendered = session.dumpBufferUtf8(output);
        exitCode = rendered && output.find("liney-self-test") != std::string::npos
                       ? 0 : 12;
        return true;
    }
    if (cmd == L"config-self-test") {
        wchar_t temp[MAX_PATH]{}, unique[MAX_PATH]{};
        if (!GetTempPathW(MAX_PATH, temp) ||
            !GetTempFileNameW(temp, L"lny", 0, unique)) {
            exitCode = 20;
            return true;
        }
        DeleteFileW(unique);
        if (!CreateDirectoryW(unique, nullptr)) {
            exitCode = 21;
            return true;
        }
        SetEnvironmentVariableW(L"LINEY_CONFIG_DIR", unique);
        SetEnvironmentVariableW(L"LINEY_HEADLESS", L"1");
        const std::wstring path = std::wstring(unique) + L"\\config.json";
        const std::string initial =
            R"({"schemaVersion":1,"shell":"cmd.exe","fontSize":18})";
        bool passed = liney::writeFileAtomic(path, initial);
        if (passed) {
            const liney::Config first = liney::loadConfig();
            passed = first.fontSize == 18.0f;
        }
        if (passed) liney::saveFontSize(20.0f); // creates .bak with fontSize 18
        if (passed) passed = liney::writeFileAtomic(path, "{broken");
        if (passed) {
            const liney::Config recovered = liney::loadConfig();
            passed = recovered.fontSize == 18.0f &&
                     GetFileAttributesW((path + L".invalid").c_str()) !=
                         INVALID_FILE_ATTRIBUTES;
        }
        DeleteFileW((path + L".invalid").c_str());
        DeleteFileW((path + L".bak").c_str());
        DeleteFileW(path.c_str());
        RemoveDirectoryW(unique);
        exitCode = passed ? 0 : 22;
        return true;
    }
    if (cmd == L"process-self-test") {
        bool ok = false;
        const std::wstring output = liney::runCapture(
            L"cmd.exe /d /s /c \"echo capture-ok\"", L"", &ok, 5000);
        if (!ok || output.find(L"capture-ok") == std::wstring::npos) {
            exitCode = 23;
            return true;
        }
        const ULONGLONG started = GetTickCount64();
        liney::runCapture(
            L"cmd.exe /d /s /c \"ping 127.0.0.1 -n 6 >nul\"", L"", &ok, 100);
        exitCode = !ok && GetTickCount64() - started < 3000 ? 0 : 24;
        return true;
    }
    if (cmd == L"stability-self-test") {
        const ULONGLONG started = GetTickCount64();
        liney::TerminalSession large;
        if (!large.start(
                L"powershell.exe -NoLogo -NoProfile -Command \"[Console]::Out.Write(('x' * 2097152) + 'liney-load-final')\"",
                L"", 100, 30, 2000)) {
            exitCode = 60; return true;
        }
        const ULONGLONG deadline = GetTickCount64() + 30000;
        while (!large.exited() && GetTickCount64() < deadline) Sleep(5);
        if (!large.exited()) { exitCode = 61; return true; }
        // The shell process can exit just before ConPTY finishes flushing its
        // buffered output. Keep the read side alive briefly so teardown does
        // not intentionally break a still-draining high-volume pipe.
        Sleep(500);
        large.snapshot();
        std::string output;
        if (!large.dumpBufferUtf8(output) ||
            output.find("liney-load-final") == std::string::npos) {
            exitCode = 62; return true;
        }
        {
            liney::TerminalSession longRunning;
            if (!longRunning.start(L"cmd.exe /d /s /c \"ping 127.0.0.1 -n 30 >nul\"",
                                   L"", 80, 24, 100)) {
                exitCode = 63; return true;
            }
            Sleep(50); // destructor must cancel and join its reader promptly
        }
        const ULONGLONG networkStarted = GetTickCount64();
        liney::TerminalSession unavailableNetwork;
        const bool unexpectedlyStarted = unavailableNetwork.start(
            L"cmd.exe", L"\\\\liney-invalid-host\\missing-share", 80, 24, 100);
        if (unexpectedlyStarted || GetTickCount64() - networkStarted > 5000) {
            exitCode = 64; return true;
        }
        exitCode = GetTickCount64() - started < 45000 ? 0 : 65;
        return true;
    }
    if (cmd == L"recovery-self-test") {
        wchar_t temp[MAX_PATH]{}, unique[MAX_PATH]{};
        if (!GetTempPathW(MAX_PATH, temp) ||
            !GetTempFileNameW(temp, L"lnr", 0, unique)) {
            exitCode = 66; return true;
        }
        DeleteFileW(unique);
        if (!CreateDirectoryW(unique, nullptr)) { exitCode = 67; return true; }
        SetEnvironmentVariableW(L"LINEY_CONFIG_DIR", unique);
        const std::wstring diag = std::wstring(unique) + L"\\diagnostics";
        CreateDirectoryW(diag.c_str(), nullptr);
        const std::wstring stale = diag + L"\\run-4294967294.active";
        const std::wstring recovery = diag + L"\\recovery-4294967294.json";
        { std::ofstream f(stale.c_str()); f << "active\n"; }
        { std::ofstream f(recovery.c_str()); f << "{\"tabs\":[]}"; }
        liney::initializeDiagnostics(L"self-test");
        liney::diagnosticLog("self-test log");
        liney::appendCommandHistory({L"echo history-ok sk-super-secret", L"C:\\test", 0, 1});
        const auto history = liney::searchCommandHistory(L"history-ok", 5);
        const std::wstring zip = std::wstring(unique) + L"\\diagnostics.zip";
        bool passed = liney::previousRunCrashed() &&
            liney::previousRecoveryLayoutPath() == recovery &&
            history.size() == 1 &&
            history[0].command.find(L"super-secret") == std::wstring::npos &&
            liney::exportDiagnosticBundle(zip, L"self-test") &&
            GetFileAttributesW(zip.c_str()) != INVALID_FILE_ATTRIBUTES;
        if (passed) {
            std::ifstream archive(zip.c_str(), std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(archive)), {});
            passed = bytes.size() >= 22 && bytes.compare(0, 4, "PK\x03\x04", 4) == 0 &&
                     bytes.compare(bytes.size() - 22, 4, "PK\x05\x06", 4) == 0 &&
                     bytes.find("history-ok") == std::string::npos &&
                     bytes.find("super-secret") == std::string::npos;
        }
        liney::markCleanShutdown();
        exitCode = passed ? 0 : 68;
        return true;
    }
    if (cmd == L"semantic-self-test") {
        SetEnvironmentVariableW(
            L"PROMPT",
            L"$e]133;D;$e\\$e]7;file://localhost/$p$e\\"
            L"$e]133;A$e\\$p$g$e]133;B$e\\");
        liney::TerminalSession session;
        if (!session.start(L"cmd.exe /d /q", L"", 80, 24, 100)) {
            exitCode = 30;
            return true;
        }
        std::vector<liney::Notification> notes;
        const ULONGLONG readyDeadline = GetTickCount64() + 500;
        while (GetTickCount64() < readyDeadline) {
            session.poll(notes);
            Sleep(10);
        }
        const char command[] = "echo semantic-ok\r";
        session.sendBytes(command, sizeof(command) - 1);
        const ULONGLONG doneDeadline = GetTickCount64() + 5000;
        int semanticResult = 31;
        while (GetTickCount64() < doneDeadline) {
            session.poll(notes);
            const auto& blocks = session.commandBlocks();
            if (!blocks.empty() &&
                blocks.back().state != liney::CommandState::Running) {
                semanticResult = blocks.back().command.find(L"echo semantic-ok") ==
                                     std::wstring::npos
                                     ? 33
                                     : (blocks.back().exitCode == 0 ? 0 : 34);
                if (semanticResult == 0) {
                    const std::string commandOutput =
                        session.commandOutputUtf8(blocks.size() - 1);
                    if (commandOutput.find("semantic-ok") == std::string::npos)
                        semanticResult = 35;
                }
                break;
            }
            Sleep(10);
        }
        const char close[] = "exit\r";
        session.sendBytes(close, sizeof(close) - 1);
        exitCode = semanticResult;
        return true;
    }
    if (cmd == L"shell-integration-self-test") {
        std::wstring powershell;
        for (const auto& profile : liney::discoverShellProfiles()) {
            if (profile.id == L"pwsh" || profile.id == L"windows-powershell") {
                powershell = liney::prepareShellCommand(profile.command);
                break;
            }
        }
        if (powershell.empty()) { exitCode = 0; return true; }
        liney::TerminalSession session;
        if (!session.start(powershell, L"", 80, 24, 100)) {
            exitCode = 36; return true;
        }
        std::vector<liney::Notification> notes;
        const ULONGLONG ready = GetTickCount64() + 2500;
        while (GetTickCount64() < ready) { session.poll(notes); Sleep(10); }
        const char command[] = "Write-Output 'liney-powershell-ok'\r";
        session.sendBytes(command, sizeof(command) - 1);
        const ULONGLONG deadline = GetTickCount64() + 8000;
        int result = 37;
        while (GetTickCount64() < deadline) {
            session.poll(notes);
            const auto& blocks = session.commandBlocks();
            if (!blocks.empty() &&
                blocks.back().state != liney::CommandState::Running) {
                result = blocks.back().exitCode == 0 ? 0 : 38;
                break;
            }
            Sleep(10);
        }
        const char close[] = "exit\r";
        session.sendBytes(close, sizeof(close) - 1);
        exitCode = result;
        return true;
    }
    if (cmd == L"vt-regression-self-test") {
        liney::Terminal terminal;
        liney::Grid grid;
        bool passed = terminal.create(12, 4, 100);
        bool altPassed = false, reflowPassed = false;
        if (passed) {
            liney::Theme theme;
            terminal.setTheme(theme);
            const std::string content =
                "A\x1b[31mR\x1b[0m \xE4\xB8\xAD "
                "\x1b]8;;https://example.com\x07link\x1b]8;;\x07";
            terminal.write(content.data(), content.size());
            passed = terminal.snapshotInto(grid);
        }
        bool sawA = false, sawRed = false, sawWide = false;
        if (passed) {
            for (const liney::Cell& cell : grid.cells) {
                if (cell.ch == L"A") sawA = true;
                if (cell.ch == L"R" && cell.fg.r > 150 && cell.fg.g < 100)
                    sawRed = true;
                if (cell.ch == L"中" && (cell.flags & liney::kFlagWide))
                    sawWide = true;
            }
            passed = sawA && sawRed && sawWide;
        }
        if (passed) {
            static const char enterAlt[] = "\x1b[?1049hALT";
            terminal.write(enterAlt, sizeof(enterAlt) - 1);
            passed = terminal.altScreenActive() && terminal.snapshotInto(grid);
            terminal.write("\x1b[?1049l", 8);
            passed = passed && !terminal.altScreenActive();
            altPassed = passed;
        }
        if (passed) {
            terminal.resize(5, 4, 8, 16);
            terminal.write("abcdefghij", 10);
            std::string buffer;
            passed = terminal.dumpBufferUtf8(buffer);
            buffer.erase(std::remove(buffer.begin(), buffer.end(), '\r'), buffer.end());
            buffer.erase(std::remove(buffer.begin(), buffer.end(), '\n'), buffer.end());
            passed = passed && buffer.find("abcdefghij") != std::string::npos;
            reflowPassed = passed;
        }
        exitCode = passed ? 0 : (!sawA ? 41 : !sawRed ? 42 : !sawWide ? 43 :
                                 !altPassed ? 44 : !reflowPassed ? 45 : 46);
        return true;
    }
    if (cmd == L"font-self-test") {
        IDWriteFactory* factory = nullptr;
        IDWriteTextFormat* format = nullptr;
        IDWriteTextLayout* layout = nullptr;
        HRESULT hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&factory));
        if (SUCCEEDED(hr))
            hr = factory->CreateTextFormat(
                L"Cascadia Mono", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f,
                L"en-us", &format);
        // Includes a programming ligature candidate, combining emoji sequence,
        // and CJK. DirectWrite must shape the whole run without dropping it.
        const wchar_t sample[] = L"=> e\u0301 \U0001F469\u200D\U0001F4BB \u4E2D\u6587";
        if (SUCCEEDED(hr))
            hr = factory->CreateTextLayout(sample, _countof(sample) - 1,
                                            format, 800.0f, 100.0f, &layout);
        DWRITE_TEXT_METRICS metrics{};
        if (SUCCEEDED(hr)) hr = layout->GetMetrics(&metrics);
        const bool passed = SUCCEEDED(hr) && metrics.width > 0.0f &&
                            metrics.height > 0.0f;
        if (layout) layout->Release();
        if (format) format->Release();
        if (factory) factory->Release();
        exitCode = passed ? 0 : 50;
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

    liney::initializeDiagnostics(liney::kAppVersion);
    enablePerMonitorDpi();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // for WIC image loading
    int result = 1;
    {
        liney::Window window;
        if (!window.create(hInstance, L"Liney", 1000, 640)) {
            liney::diagnosticLog("window creation failed");
        } else {
            wchar_t headless[8]{};
            const bool isHeadless = GetEnvironmentVariableW(
                L"LINEY_HEADLESS", headless,
                static_cast<DWORD>(_countof(headless))) > 0;
            window.show(isHeadless ? SW_HIDE : nCmdShow);
            result = window.runMessageLoop();
        }
    }
    liney::diagnosticLog("application exiting");
    liney::markCleanShutdown();
    return result;
}
