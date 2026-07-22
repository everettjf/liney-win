#include "app/Window.h"

#include <windowsx.h>  // GET_X_LPARAM / GET_WHEEL_DELTA_WPARAM
#include <commdlg.h>   // ChooseFontW (the Font… picker)
#include <tlhelp32.h>  // process snapshot for the running-command check

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "app/SettingsDialog.h"
#include "app/WindowInternal.h"
#include "util/AccessibilityProvider.h"
#include "util/Diagnostics.h"
#include "util/InputBox.h"
#include "core/Config.h"
#include "core/CommandHistory.h"
#include "core/RenderSignal.h"
#include "render/D2DRenderer.h"
#include "util/Process.h"  // runDetached (lifecycle hooks)

namespace liney {

static const wchar_t* kClassName = L"LineyWinMainWindow";

// Idle repaint cadence. With nothing happening the loop sleeps in
// MsgWaitForMultipleObjectsEx and only repaints this often (so async pollers —
// notifications / title / update / pane reaping — still run), instead of the old
// unconditional 60fps. Real input / PTY output repaints immediately.
static constexpr UINT kIdleRenderMs = 100;

// One-shot timer id for keep-awake auto-expiry (see setKeepAwake).
static constexpr UINT_PTR kKeepAwakeTimerId = 0x4B41;  // 'KA'
static constexpr UINT_PTR kHeadlessCloseTimerId = 0x4843; // 'HC'
static constexpr UINT_PTR kRecoveryTimerId = 0x5243; // 'RC'

// Monitor DPI as a 96-relative scale. GetDpiForWindow is Win10 1607+, so load it
// dynamically and fall back to the device caps.
static float queryDpiScale(HWND hwnd) {
    wchar_t simulated[16]{};
    wchar_t headless[8]{};
    if (GetEnvironmentVariableW(L"LINEY_HEADLESS", headless,
                                static_cast<DWORD>(_countof(headless))) > 0) {
        const DWORD count = GetEnvironmentVariableW(
            L"LINEY_TEST_DPI", simulated,
            static_cast<DWORD>(_countof(simulated)));
        if (count > 0 && count < _countof(simulated)) {
            const unsigned long dpi = wcstoul(simulated, nullptr, 10);
            if (dpi >= 96 && dpi <= 480) return static_cast<float>(dpi) / 96.0f;
        }
    }
    using GetDpiFn = UINT(WINAPI*)(HWND);
    static GetDpiFn fn = []() -> GetDpiFn {
        if (HMODULE u = GetModuleHandleW(L"user32.dll"))
            return reinterpret_cast<GetDpiFn>(GetProcAddress(u, "GetDpiForWindow"));
        return nullptr;
    }();
    UINT dpi = 0;
    if (fn) dpi = fn(hwnd);
    if (dpi == 0) {
        if (HDC dc = GetDC(hwnd)) { dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(hwnd, dc); }
    }
    return (dpi ? static_cast<float>(dpi) : 96.0f) / 96.0f;
}

namespace {
// Append Git for Windows' Unix tools (usr\bin, mingw64\bin) to this process's
// PATH so shells spawned afterward see ls / cat / grep / rm / sed / awk / …
// Appended (not prepended) so it doesn't shadow Windows' own tools.
void addGitUnixToolsToPath() {
    wchar_t gitExe[MAX_PATH]{};
    if (SearchPathW(nullptr, L"git.exe", nullptr, MAX_PATH, gitExe, nullptr) == 0)
        return;
    std::wstring p = gitExe;  // ...\Git\cmd\git.exe  (or ...\Git\bin\git.exe)
    size_t s = p.find_last_of(L"\\/");
    if (s == std::wstring::npos) return;
    p = p.substr(0, s);  // ...\Git\cmd
    s = p.find_last_of(L"\\/");
    if (s == std::wstring::npos) return;
    const std::wstring root = p.substr(0, s);  // ...\Git
    const std::wstring usrBin = root + L"\\usr\\bin";
    if (GetFileAttributesW(usrBin.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    const std::wstring mingw = root + L"\\mingw64\\bin";

    DWORD n = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    std::wstring path(n ? n - 1 : 0, L'\0');
    if (n) GetEnvironmentVariableW(L"PATH", path.data(), n);
    if (path.find(usrBin) != std::wstring::npos) return;  // already added
    std::wstring next = path;
    if (!next.empty() && next.back() != L';') next += L';';
    next += usrBin + L";" + mingw;
    SetEnvironmentVariableW(L"PATH", next.c_str());
}

std::wstring findExecutable(const wchar_t* name,
                            std::initializer_list<std::wstring> fallbacks = {}) {
    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, name, nullptr, MAX_PATH, found, nullptr) > 0)
        return found;
    for (const std::wstring& path : fallbacks)
        if (!path.empty() &&
            GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            return path;
    return {};
}

std::wstring envPath(const wchar_t* variable, const wchar_t* suffix) {
    wchar_t value[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(variable, value, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::wstring(value) + suffix;
}

std::wstring quoteArg(const std::wstring& value) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') {
            ++slashes;
        } else {
            if (c == L'\"') out.append(slashes + 1, L'\\');
            else out.append(slashes, L'\\');
            slashes = 0;
            out.push_back(c);
        }
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}
}  // namespace

Window::Window() : renderer_(std::make_unique<D2DRenderer>()) {}
Window::~Window() {
    // Wait for update-check/download workers: they capture `this` and would
    // otherwise write into a destroyed Window (bounded by the HTTP timeouts).
    for (std::thread& t : updateThreads_)
        if (t.joinable()) t.join();
    if (accessibilityProvider_) accessibilityProvider_->Release();
}

bool Window::create(HINSTANCE hInstance, const wchar_t* title, int width,
                    int height) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;  // CS_DBLCLKS: word/line select
    wc.lpfnWndProc = &Window::wndProcThunk;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));    // app icon (resource)
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    // Default size: ~70% of the primary work area, centered. A fixed pixel
    // size is postage-stamp small on high-DPI monitors (we're per-monitor DPI
    // aware, so pixels are physical). The caller's width/height act as a
    // floor; a saved layout (restoreLayout) overrides this right afterwards.
    RECT wa{ 0, 0, 1280, 800 };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int waW = wa.right - wa.left, waH = wa.bottom - wa.top;
    int w = waW * 7 / 10, h = waH * 7 / 10;
    if (w < width) w = width;
    if (h < height) h = height;
    // Never exceed the work area (a tiny screen would otherwise push the
    // centered origin negative and open the title bar off the top/left edge).
    if (w > waW) w = waW;
    if (h > waH) h = waH;
    int x = wa.left + (waW - w) / 2;
    int y = wa.top + (waH - h) / 2;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;

    hwnd_ = CreateWindowExW(0, kClassName, title, WS_OVERLAPPEDWINDOW,
                            x, y, w, h, nullptr, nullptr, hInstance, this);
    if (!hwnd_) return false;
    accessibilityProvider_ = createAccessibilityProvider(hwnd_);

    g_wakeHwnd.store(hwnd_, std::memory_order_relaxed);  // PTY thread wakes us here
    dpiScale_ = queryDpiScale(hwnd_);                    // scale the font to the monitor

    if (!renderer_->initialize(hwnd_)) return false;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    renderer_->resize(rc.right - rc.left, rc.bottom - rc.top);

    // User config (shell, font, workspace root); seeds %USERPROFILE%\.liney.
    const Config cfg = loadConfig();
    shell_ = cfg.shell;
    shellProfiles_ = discoverShellProfiles();
    keybindings_ = cfg.keybindings;
    fontFamily_ = cfg.fontFamily;
    fontSize_ = cfg.fontSize;
    defaultFontSize_ = cfg.fontSize;
    sessionStartHook_ = cfg.sessionStartHook;
    sessionExitHook_ = cfg.sessionExitHook;
    appExitHook_ = cfg.appExitHook;
    copyOnSelect_ = cfg.copyOnSelect;
    multiLinePasteWarning_ = cfg.multiLinePasteWarning;
    rememberLayout_ = cfg.rememberLayout;
    splitUseWorkspaceDir_ = cfg.splitUseWorkspaceDir;
    checkForUpdatesOnStartup_ = cfg.checkForUpdatesOnStartup;
    aiProvider_ = cfg.aiProvider;
    aiModel_ = cfg.aiModel;
    aiEndpoint_ = cfg.aiEndpoint;
    aiIncludeCwd_ = cfg.aiIncludeCwd;
    osc52Clipboard_ = cfg.osc52Clipboard;
    scrollback_ = cfg.scrollback;
    unixToolsEnabled_ = cfg.unixTools;
    if (cfg.unixTools) addGitUnixToolsToPath();  // ls/cat/grep/… in spawned shells
    // cmd shell integration: prepend an OSC 7 cwd report to PROMPT so the files
    // panel can follow `cd` (cmd.exe doesn't emit OSC 7 on its own). $e=ESC,
    // $p=current path, then the normal prompt. PowerShell ignores PROMPT.
    {
        wchar_t cur[1024]{};
        DWORD n = GetEnvironmentVariableW(L"PROMPT", cur, 1024);
        std::wstring base = (n > 0 && n < 1024) ? std::wstring(cur) : L"$p$g";
        if (base.find(L"]133;") == std::wstring::npos)  // don't double-add
            SetEnvironmentVariableW(
                L"PROMPT",
                (L"$e]133;D;$e\\$e]7;file://localhost/$p$e\\"
                 L"$e]133;A$e\\" + base + L"$e]133;B$e\\").c_str());
    }
    sshHosts_ = cfg.sshHosts;
    agents_ = cfg.agents;
    projectIcons_ = cfg.projectIcons;
    projects_ = cfg.projects;
    recentProjects_ = cfg.recentProjects;
    workspaceRoot_ = cfg.workspaceRoot;
    theme_ = cfg.theme;
    uiTheme_ = cfg.uiTheme;
    themeName_ = cfg.themeName;
    applyHighContrastIfEnabled();
    // Gutters/margins behind panes use the chrome's workspace color; panes
    // fill with the terminal background themselves.
    renderer_->setColors(uiTheme_.workspaceBg, theme_.background);
    applyFont();

    // Populate the explicitly configured workspace root/projects. An empty
    // root means no implicit discovery.
    rescanWorkspace();

    wchar_t autoClose[16]{};
    const DWORD autoCloseLength = GetEnvironmentVariableW(
        L"LINEY_AUTOCLOSE_MS", autoClose,
        static_cast<DWORD>(_countof(autoClose)));
    if (autoCloseLength > 0 && autoCloseLength < _countof(autoClose)) {
        const unsigned long delay = wcstoul(autoClose, nullptr, 10);
        if (delay >= 10 && delay <= 60000)
            SetTimer(hwnd_, kHeadlessCloseTimerId, static_cast<UINT>(delay), nullptr);
    }

    // Restore the saved tab/pane layout if any; otherwise open one tab.
    // Only restore the saved tab/pane layout when the user opted in (off by
    // default — a fresh window each launch is less surprising). A fresh window
    // opens its first terminal in the user's home directory, not wherever the
    // app happened to be launched from.
    bool restored = false;
    const std::wstring crashedLayout = previousRecoveryLayoutPath();
    wchar_t recoveryTest[8]{};
    const bool suppressRecoveryPrompt = GetEnvironmentVariableW(
        L"LINEY_HEADLESS", recoveryTest, static_cast<DWORD>(_countof(recoveryTest))) > 0;
    if (!suppressRecoveryPrompt && previousRunCrashed() && !crashedLayout.empty()) {
        const int choice = MessageBoxW(hwnd_,
            L"Liney did not shut down cleanly last time. Restore its window, tabs, panes and working directories?",
            L"Liney recovery", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON1);
        if (choice == IDYES) {
            restored = restoreLayoutFrom(crashedLayout);
            if (restored) {
                DeleteFileW(crashedLayout.c_str());
            } else {
                MessageBoxW(hwnd_,
                    L"The recovery snapshot could not be restored. It has been kept in the diagnostics folder so you can retry or export diagnostics.",
                    L"Liney recovery", MB_OK | MB_ICONWARNING);
            }
        } else {
            // Choosing No explicitly dismisses this recovery attempt.
            DeleteFileW(crashedLayout.c_str());
        }
    }
    if (!restored && !(rememberLayout_ && restoreLayout())) newTab(homeDir());
    if (tabs_.empty()) {
        // No session could start — almost always the terminal core DLL is
        // missing/incompatible, or the configured shell doesn't exist. Tell the
        // user instead of vanishing silently.
        MessageBoxW(
            hwnd_,
            L"liney-win couldn't start a terminal session.\n\n"
            L"Likely causes:\n"
            L"  • ghostty-vt.dll is missing or incompatible (must sit next to the exe)\n"
            L"  • the configured \"shell\" in config.json doesn't exist\n\n"
            L"See %USERPROFILE%\\.liney\\config.json.",
            L"Liney", MB_OK | MB_ICONERROR);
        return false;
    }

    initTray();  // for OSC 9/777 balloon notifications
    SetTimer(hwnd_, kRecoveryTimerId, 2000, nullptr);
    wchar_t headlessMode[8]{};
    const bool headless = GetEnvironmentVariableW(
        L"LINEY_HEADLESS", headlessMode,
        static_cast<DWORD>(_countof(headlessMode))) > 0;
    if (cfg.firstRun && !headless) {
        const int configure = MessageBoxW(
            hwnd_,
            L"Welcome to Liney.\n\n"
            L"Use Ctrl+Shift+P to search every command, workspace, shell, SSH "
            L"host and agent. Use the + menu to start PowerShell, CMD or WSL.\n\n"
            L"Open Settings now to choose your default shell, theme and workspace?",
            L"Welcome to Liney", MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON1);
        if (configure == IDYES) openSettingsDialog();
    }
    if (checkForUpdatesOnStartup_ && !headless) checkForUpdates(true);
    return true;
}

void Window::show(int nCmdShow) {
    // Honor a restored maximized state from the saved layout.
    ShowWindow(hwnd_, pendingMaximize_ ? SW_SHOWMAXIMIZED : nCmdShow);
    UpdateWindow(hwnd_);
}

int Window::runMessageLoop() {
    MSG msg{};
    ULONGLONG lastRender = GetTickCount64();
    for (;;) {
        bool didMsg = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                if (rememberLayout_) saveLayout();  // persist only if opted in
                runDetached(appExitHook_, L"");  // hooks.appExit
                return static_cast<int>(msg.wParam);
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            didMsg = true;
        }
        // Repaint on input, on terminal output (g_renderDirty, set by the PTY
        // reader thread), or on the slow fallback tick. Otherwise we skip the
        // frame entirely so an idle terminal stops burning a core at 60fps.
        const bool ptyDirty = g_renderDirty.exchange(false, std::memory_order_relaxed);
        const ULONGLONG now = GetTickCount64();
        if (didMsg || ptyDirty || now - lastRender >= kIdleRenderMs) {
            renderFrame();
            lastRender = now;
        }
        // Sleep until a message arrives (real input or our wake post) or the
        // fallback interval elapses. MWMO_INPUTAVAILABLE avoids a lost-wakeup race
        // for input that landed just after the PeekMessage drain above.
        MsgWaitForMultipleObjectsEx(0, nullptr, kIdleRenderMs, QS_ALLINPUT,
                                    MWMO_INPUTAVAILABLE);
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
    case WM_GETOBJECT:
        if (static_cast<LONG>(lParam) == UiaRootObjectId && accessibilityProvider_)
            return UiaReturnRawElementProvider(hwnd_, wParam, lParam,
                                               accessibilityProvider_);
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_CLOSE: {
        // Quitting the app: one consolidated warning that lists every tab still
        // running a command, instead of a dialog per tab.
        std::vector<size_t> all(tabs_.size());
        for (size_t i = 0; i < tabs_.size(); ++i) all[i] = i;
        if (!confirmCloseRunning(runningTabTitles(all), L"Quit liney?"))
            return 0;  // user cancelled
        DestroyWindow(hwnd_);
        return 0;
    }
    case WM_SIZE:
        if (LOWORD(lParam) && HIWORD(lParam)) {
            renderer_->resize(LOWORD(lParam), HIWORD(lParam));
            // Interactive resize runs a modal loop that starves our render
            // loop; paint here so the window tracks the drag instead of
            // showing undefined flip-model buffer contents.
            renderFrame();
        }
        return 0;
    case WM_PAINT: {
        // Modal loops (menus, dialogs, move/size) bypass runMessageLoop —
        // honor paint requests so the window isn't frozen/black meanwhile.
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        EndPaint(hwnd_, &ps);
        renderFrame();
        return 0;
    }
    case WM_DPICHANGED: {
        // Moved to a monitor with different scaling: rebuild the font + cell
        // metrics at the new DPI and take the OS-suggested window rectangle.
        dpiScale_ = static_cast<float>(HIWORD(wParam)) / 96.0f;
        applyFont();
        if (const RECT* r = reinterpret_cast<const RECT*>(lParam))
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left,
                         r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        markRenderDirty();
        return 0;
    }
    case WM_SETTINGCHANGE:
        applyHighContrastIfEnabled();
        if (renderer_) renderer_->setColors(uiTheme_.workspaceBg, theme_.background);
        markRenderDirty();
        return 0;
    case WM_CHAR:
        onChar(static_cast<wchar_t>(wParam));
        return 0;
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
        // Place the IME composition/candidate window at the cursor, then let the
        // default handler run (committed text arrives via WM_CHAR).
        positionIme();
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (onKeyDown(wParam)) return 0;
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_SYSCHAR:
        // Swallow Alt+<key> chars (e.g. Alt+D) so they don't ring the system
        // bell — there's no menu bar to drive. Alt+F4 etc. stay on WM_SYSKEYDOWN.
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd_);
        onMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONDBLCLK:
        SetFocus(hwnd_);
        onMouseDoubleClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        onMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        onMouseUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_RBUTTONDOWN:
        onMouseDownRight(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_SETCURSOR:
        // Show an I-beam over terminal text and resize arrows over split
        // dividers; fall back to the default arrow over chrome.
        if (LOWORD(lParam) == HTCLIENT && updateCursor()) return TRUE;
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_COPY:
        copySelection();
        return 0;
    case WM_PASTE:
        paste();
        return 0;
    case WM_MOUSEWHEEL:
        // Ctrl+Wheel zooms the font; a plain wheel scrolls (or reports to the
        // app — see onWheel). Wheel coordinates arrive in screen space.
        if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) {
            zoomFont(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? +1 : -1);
        } else {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd_, &pt);
            onWheel(GET_WHEEL_DELTA_WPARAM(wParam), pt.x, pt.y);
        }
        return 0;
    case WM_LINEY_WAKE:
        // Posted by off-thread producers to pop the message wait; the repaint
        // decision happens back in runMessageLoop. Nothing to do here.
        return 0;
    case WM_TIMER:
        if (wParam == kHeadlessCloseTimerId) {
            KillTimer(hwnd_, kHeadlessCloseTimerId);
            DestroyWindow(hwnd_);
            return 0;
        }
        if (wParam == kKeepAwakeTimerId) {
            // setKeepAwake(0) already shows a "Keep awake: off" balloon; don't
            // add a second one for the same event.
            setKeepAwake(0);
            return 0;
        }
        if (wParam == kRecoveryTimerId) {
            const std::wstring path = recoveryLayoutPath();
            if (!path.empty() && !tabs_.empty()) {
                writeLayoutTo(path);
            }
            return 0;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_DESTROY:
        removeTray();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

// ---------------------------------------------------------------------------
// Layout / rendering
// ---------------------------------------------------------------------------

void Window::regions(Rect& leftBar, Rect& rightPanel, Rect& tabBar,
                     Rect& panes) const {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const float W = static_cast<float>(rc.right - rc.left);
    const float H = static_cast<float>(rc.bottom - rc.top);
    const float sw = sidebarVisible_ ? metrics_.sidebarW() : 0.0f;
    const float fw = filesPanelVisible_ ? metrics_.filesPanelW() : 0.0f;
    const float tb = metrics_.tabBarH();
    const float midW = W - sw - fw;
    leftBar = { 0, 0, sw, H };
    rightPanel = { W - fw, 0, fw, H };
    tabBar = { sw, 0, midW, tb };
    panes = { sw, tb, midW, H - tb };
}

void Window::renderFrame() {
    reapExitedPanes();
    if (tabs_.empty()) return;

    pollNotifications();  // OSC 9/777 across all sessions -> balloons
    pollClipboardRequests(); // OSC 52 is policy-gated; never silently trusted
    pollUpdateResult();   // show the update-check result when it arrives
    pollAiResult();       // display an explicitly requested AI explanation
    updateTitle();        // reflect OSC 0/2 title changes live

    Tab* t = activeTab();
    if (t) for (Pane* leaf : t->leaves())
        if (leaf->session) leaf->session->snapshot();

    Rect leftBar, rightPanel, tabBar, panes;
    regions(leftBar, rightPanel, tabBar, panes);

    renderer_->beginFrame();
    sidebarRows_.clear();
    // Each region is clipped to its own bounds so nothing (long names, a wide
    // grid, the tab strip) can bleed across panel boundaries.
    if (sidebarVisible_) {
        renderer_->pushClip(leftBar.x, leftBar.y, leftBar.w, leftBar.h);
        drawLeftSidebar(leftBar);
        renderer_->popClip();
    }
    if (filesPanelVisible_) {
        renderer_->pushClip(rightPanel.x, rightPanel.y, rightPanel.w, rightPanel.h);
        drawFilesPanel(rightPanel);
        renderer_->popClip();
    }
    renderer_->pushClip(tabBar.x, tabBar.y, tabBar.w, tabBar.h);
    drawTabBar(tabBar);
    renderer_->popClip();
    drawPanes(panes);
    drawCommandPalette();
    renderer_->endFrame();
}


void Window::reapExitedPanes() {
    // Preserve exited shells as review/recovery artifacts. The pane context
    // menu offers an explicit restart in the same shell and directory.
}

// ---------------------------------------------------------------------------
// Workspace / tabs
// ---------------------------------------------------------------------------

Tab* Window::activeTab() const {
    if (activeTab_ >= tabs_.size()) return nullptr;
    return tabs_[activeTab_].get();
}

TerminalSession* Window::activeSession() const {
    Tab* t = activeTab();
    if (!t || !t->active()) return nullptr;
    return t->active()->session.get();
}

void Window::cellsForRect(const Rect& r, int& cols, int& rows) const {
    const float pad2 = metrics_.panePad() * 2.0f;
    cols = static_cast<int>((r.w - pad2) / metrics_.cellW);
    rows = static_cast<int>((r.h - pad2) / metrics_.cellH);
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
}

void Window::newTab(const std::wstring& cwd) { newTabShell(shell_, cwd); }

TerminalSession* Window::newTabShell(const std::wstring& shellCmd,
                                     const std::wstring& cwd) {
    clearSelection();
    Rect leftBar, rightPanel, tabBar, panes;
    regions(leftBar, rightPanel, tabBar, panes);
    int cols = 80, rows = 24;
    cellsForRect(panes, cols, rows);

    auto session = std::make_unique<TerminalSession>();
    const std::wstring preparedShell = prepareShellCommand(shellCmd);
    if (!session->start(preparedShell, cwd, cols, rows, scrollback_)) return nullptr;
    session->setTheme(theme_);
    runStartHook(session.get());
    TerminalSession* created = session.get();
    tabs_.push_back(std::make_unique<Tab>(std::move(session)));
    activeTab_ = tabs_.size() - 1;
    updateTitle();
    return created;
}

void Window::runStartHook(TerminalSession* s) {
    if (!s || sessionStartHook_.empty()) return;
    const std::wstring line = sessionStartHook_ + L"\r";
    const std::string utf8 = wideToUtf8(line);
    s->sendBytes(utf8.data(), utf8.size());
}

void Window::splitActive(SplitDir dir) {
    clearSelection();
    Tab* t = activeTab();
    if (!t || !t->active() || !t->active()->session) return;

    const Pane* a = t->active();
    int cols = 80, rows = 24;
    cellsForRect(a->rect, cols, rows);

    // Refuse a split that would make either half unusably small — the "panes
    // get too tiny to read" problem. The user can equalize / zoom / close
    // instead. Minimums are generous enough to keep a prompt legible.
    constexpr int kMinCols = 24, kMinRows = 6;
    const bool tooSmall = (dir == SplitDir::Cols) ? (cols / 2 < kMinCols)
                                                  : (rows / 2 < kMinRows);
    if (tooSmall) {
        MessageBeep(MB_ICONWARNING);
        showBalloon(L"liney",
                    L"Pane too small to split — try Zoom (Ctrl+Shift+Z) or "
                    L"Equalize instead.");
        return;
    }

    // Where the new split pane starts: inherit the current pane's directory
    // (default), or the workspace/home directory when the user opted for that.
    const std::wstring cwd =
        splitUseWorkspaceDir_ ? defaultStartDir() : a->session->cwd();
    auto session = std::make_unique<TerminalSession>();
    if (!session->start(shell_, cwd, cols, rows, scrollback_)) return;
    session->setTheme(theme_);
    runStartHook(session.get());
    t->splitActive(dir, std::move(session));
}

std::wstring Window::defaultStartDir() {
    auto& repos = workspace_.repos();
    if (!repos.empty()) return repos.front().path;  // first workspace project
    return homeDir();
}

void Window::toggleZoom() {
    if (Tab* t = activeTab()) {
        t->setZoom(t->zoom() ? nullptr : t->active());
        markRenderDirty();
    }
}

void Window::equalizePanes() {
    if (Tab* t = activeTab()) {
        t->setZoom(nullptr);  // equalizing implies showing all panes
        t->equalize();
        markRenderDirty();
    }
}

void Window::closeOtherPanes() {
    Tab* t = activeTab();
    if (!t || !t->isSplit()) return;
    // Warn once if any of the panes we're about to close is running a command.
    Pane* keep = t->active();
    int running = 0;
    for (Pane* leaf : t->leaves())
        if (leaf != keep && leaf->session && leaf->session->hasRunningChild())
            ++running;
    if (running > 0) {
        const std::wstring msg =
            std::to_wstring(running) +
            L" of the other panes are running a command.\n\nClose them anyway?";
        if (MessageBoxW(hwnd_, msg.c_str(), L"liney — close panes",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return;
    }
    clearSelection();
    t->closeOthers();
    markRenderDirty();
}

void Window::closeActivePaneConfirming() {
    // User-initiated (Ctrl+Shift+W): warn if the pane is running a command.
    // The automatic reaper calls closeActivePane() directly (no prompt).
    if (TerminalSession* s = activeSession(); s && s->hasRunningChild()) {
        const std::wstring msg =
            L"This pane is still running a command.\n\nClose it anyway?";
        if (MessageBoxW(hwnd_, msg.c_str(), L"Liney — close pane",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return;
    }
    closeActivePane();
}

void Window::closeActivePane() {
    clearSelection();
    runDetached(sessionExitHook_, L"");  // hooks.sessionExit
    Tab* t = activeTab();
    if (!t) return;
    if (!t->closeActive()) {
        tabs_.erase(tabs_.begin() + activeTab_);
        if (tabs_.empty()) { PostQuitMessage(0); return; }
        if (activeTab_ >= tabs_.size()) activeTab_ = tabs_.size() - 1;
    }
    updateTitle();
}

void Window::switchTab(int delta) {
    clearSelection();
    if (tabs_.empty()) return;
    const int n = static_cast<int>(tabs_.size());
    activeTab_ = static_cast<size_t>(((static_cast<int>(activeTab_) + delta) % n + n) % n);
    updateTitle();
}

void Window::updateTitle() {
    Tab* t = activeTab();
    std::wstring title = L"Liney";
    if (t) title += L" — " + t->title();
    if (title == lastTitle_) return;
    lastTitle_ = title;
    SetWindowTextW(hwnd_, title.c_str());
}


void Window::applyFont() {
    // fontSize_ is logical; render at device pixels so glyphs use the monitor's
    // full resolution (sharp on HiDPI) instead of being bitmap-stretched.
    renderer_->setFont(fontFamily_, fontSize_ * dpiScale_);
    unsigned cw = 0, ch = 0;
    renderer_->cellSize(cw, ch);
    metrics_.cellW = cw ? static_cast<float>(cw) : 8.0f;
    metrics_.cellH = ch ? static_cast<float>(ch) : 16.0f;
    // Panes are re-laid out (and sessions resized) on the next frame.
}

void Window::zoomFont(int step) {
    fontSize_ = (step == 0) ? defaultFontSize_ : fontSize_ + step;
    // Same range loadConfig accepts, so a configured size never snaps smaller
    // on the first zoom.
    if (fontSize_ < 6.0f) fontSize_ = 6.0f;
    if (fontSize_ > 96.0f) fontSize_ = 96.0f;
    applyFont();
    saveFontSize(fontSize_);  // remember the zoom level across launches
}

void Window::chooseFontDialog() {
    LOGFONTW lf{};
    wcsncpy_s(lf.lfFaceName, fontFamily_.c_str(), _TRUNCATE);
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    // fontSize_ is a 96-DPI-relative pixel size; ChooseFont previews via
    // lfHeight in device pixels and reports the pick in tenths of a point.
    lf.lfHeight = -static_cast<LONG>(fontSize_ * dpiScale_ + 0.5f);

    CHOOSEFONTW cf{};
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = hwnd_;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_FIXEDPITCHONLY |
               CF_NOSCRIPTSEL | CF_FORCEFONTEXIST;
    if (!ChooseFontW(&cf) || lf.lfFaceName[0] == L'\0') return;

    fontFamily_ = lf.lfFaceName;
    fontSize_ = cf.iPointSize / 10.0f * (96.0f / 72.0f);  // pt → px @96dpi
    if (fontSize_ < 6.0f) fontSize_ = 6.0f;
    if (fontSize_ > 72.0f) fontSize_ = 72.0f;
    defaultFontSize_ = fontSize_;  // Ctrl+0 now resets to the chosen size
    applyFont();
    saveFontFamily(fontFamily_);
    saveFontSize(fontSize_);
}

void Window::setKeepAwake(int hours) {
    KillTimer(hwnd_, kKeepAwakeTimerId);
    keepAwakeHours_ = hours;
    keepAwake_ = hours != 0;
    keepAwakeUntil_ = 0;
    // ES_CONTINUOUS persists the request; SYSTEM/DISPLAY keep both awake.
    SetThreadExecutionState(keepAwake_
        ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)
        : ES_CONTINUOUS);
    if (hours > 0) {
        const ULONGLONG ms = static_cast<ULONGLONG>(hours) * 3600ull * 1000ull;
        keepAwakeUntil_ = GetTickCount64() + ms;
        // USER_TIMER_MAXIMUM is ~24.8 days, so a single shot covers 24h.
        SetTimer(hwnd_, kKeepAwakeTimerId, static_cast<UINT>(ms), nullptr);
        showBalloon(L"Liney",
                    L"Keep awake: on for " + std::to_wstring(hours) +
                        (hours == 1 ? L" hour" : L" hours"));
    } else if (hours < 0) {
        showBalloon(L"Liney", L"Keep awake: on until turned off");
    } else {
        showBalloon(L"Liney", L"Keep awake: off");
    }
}

void Window::toggleKeepAwake() { setKeepAwake(keepAwake_ ? 0 : -1); }

std::wstring Window::keepAwakeStatus() const {
    if (!keepAwake_) return L"";
    if (keepAwakeUntil_ == 0) return L"until turned off";
    const ULONGLONG now = GetTickCount64();
    if (now >= keepAwakeUntil_) return L"ending…";
    const ULONGLONG mins = (keepAwakeUntil_ - now + 59999) / 60000;
    if (mins >= 60)
        return std::to_wstring(mins / 60) + L"h " + std::to_wstring(mins % 60) +
               L"m left";
    return std::to_wstring(mins) + L"m left";
}

// "#RRGGBB" for a Color, for config persistence.
static std::string colorToHex(const Color& c) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

void Window::applyHighContrastIfEnabled() {
    HIGHCONTRASTW highContrast{};
    highContrast.cbSize = sizeof(highContrast);
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast),
                               &highContrast, 0) ||
        !(highContrast.dwFlags & HCF_HIGHCONTRASTON)) return;
    auto systemColor = [](int index) {
        const COLORREF value = GetSysColor(index);
        return Color{GetRValue(value), GetGValue(value), GetBValue(value)};
    };
    theme_.background = systemColor(COLOR_WINDOW);
    theme_.foreground = systemColor(COLOR_WINDOWTEXT);
    uiTheme_.workspaceBg = theme_.background;
    uiTheme_.sidebarBg = systemColor(COLOR_BTNFACE);
    uiTheme_.tabBg = uiTheme_.sidebarBg;
    uiTheme_.tabActiveBg = systemColor(COLOR_HIGHLIGHT);
    uiTheme_.text = systemColor(COLOR_BTNTEXT);
    uiTheme_.dim = systemColor(COLOR_GRAYTEXT);
    uiTheme_.accent = systemColor(COLOR_HIGHLIGHTTEXT);
    uiTheme_.border = systemColor(COLOR_WINDOWFRAME);
    uiTheme_.sidebarHdr = uiTheme_.text;
}

void Window::applyTheme(const std::wstring& presetName, const Color& accent) {
    const auto presets = builtinThemePresets();
    if (const ThemePreset* p = findThemePreset(presets, presetName)) {
        themeName_ = p->name;
        theme_ = p->terminal;
        uiTheme_ = p->ui;
    }
    uiTheme_.accent = accent;  // override on top of the preset
    applyHighContrastIfEnabled();
    renderer_->setColors(uiTheme_.workspaceBg, theme_.background);
    // Push the terminal palette to every live session so the change is
    // instant, not only for tabs opened afterward.
    for (auto& tab : tabs_)
        for (Pane* leaf : tab->leaves())
            if (leaf->session) leaf->session->setTheme(theme_);
    markRenderDirty();
}

void Window::openSettingsDialog() {
    SettingsValues v;
    v.shell = shell_;
    v.fontFamily = fontFamily_;
    v.fontSize = fontSize_;
    v.themeName = themeName_;
    v.accent = uiTheme_.accent;
    v.scrollback = scrollback_;
    v.copyOnSelect = copyOnSelect_;
    v.multiLinePasteWarning = multiLinePasteWarning_;
    v.unixTools = unixToolsEnabled_;
    v.rememberLayout = rememberLayout_;
    v.splitUseWorkspaceDir = splitUseWorkspaceDir_;
    v.checkForUpdatesOnStartup = checkForUpdatesOnStartup_;
    v.aiProvider = aiProvider_;
    v.aiModel = aiModel_;
    v.aiIncludeCwd = aiIncludeCwd_;
    v.workspaceRoot = workspaceRoot_;
    if (!showSettingsDialog(hwnd_, v)) return;

    // Apply live. Shell/scrollback affect sessions started from now on.
    shell_ = v.shell;
    scrollback_ = v.scrollback;
    copyOnSelect_ = v.copyOnSelect;
    multiLinePasteWarning_ = v.multiLinePasteWarning;
    rememberLayout_ = v.rememberLayout;
    splitUseWorkspaceDir_ = v.splitUseWorkspaceDir;
    checkForUpdatesOnStartup_ = v.checkForUpdatesOnStartup;
    aiProvider_ = v.aiProvider;
    aiModel_ = v.aiModel;
    aiIncludeCwd_ = v.aiIncludeCwd;
    if (v.unixTools && !unixToolsEnabled_) addGitUnixToolsToPath();
    unixToolsEnabled_ = v.unixTools;
    if (v.workspaceRoot != workspaceRoot_) {
        workspaceRoot_ = v.workspaceRoot;
        rescanWorkspace();
    }

    const bool fontChanged =
        v.fontFamily != fontFamily_ || v.fontSize != fontSize_;
    if (fontChanged) {
        fontFamily_ = v.fontFamily;
        fontSize_ = v.fontSize;
        defaultFontSize_ = v.fontSize;
        applyFont();
    }

    // Theme: only switch preset when the user actively picked one (so opening
    // Settings on a legacy overrides-object theme and clicking OK doesn't
    // force a preset). Accent: an explicit color pick wins; otherwise a newly
    // picked preset brings its own designed accent instead of the old one.
    const std::wstring targetTheme = v.themePicked ? v.themeName : themeName_;
    Color accent = uiTheme_.accent;
    if (v.accentExplicit) {
        accent = v.accent;
    } else if (v.themePicked) {
        const auto presets = builtinThemePresets();
        if (const ThemePreset* p = findThemePreset(presets, targetTheme))
            accent = p->ui.accent;
    }
    const bool themeChanged = v.themePicked || v.accentExplicit;
    if (themeChanged) applyTheme(targetTheme, accent);

    // Persist, preserving keys the dialog doesn't edit (hooks, sshHosts, …).
    updateConfigJson([&](Json& j) {
        j.set("shell", Json::str(wideToUtf8(shell_)));
        j.set("fontFamily", Json::str(wideToUtf8(fontFamily_)));
        j.set("fontSize", Json::number(fontSize_));
        // Only rewrite `theme` when a preset was chosen — leave a hand-authored
        // overrides object untouched otherwise.
        if (v.themePicked) j.set("theme", Json::str(wideToUtf8(themeName_)));
        j.set("accentColor", Json::str(colorToHex(uiTheme_.accent)));
        j.set("scrollback", Json::number(scrollback_));
        j.set("copyOnSelect", Json::boolean(copyOnSelect_));
        j.set("multiLinePasteWarning", Json::boolean(multiLinePasteWarning_));
        j.set("unixTools", Json::boolean(unixToolsEnabled_));
        j.set("rememberLayout", Json::boolean(rememberLayout_));
        j.set("splitUseWorkspaceDir", Json::boolean(splitUseWorkspaceDir_));
        j.set("checkForUpdatesOnStartup", Json::boolean(checkForUpdatesOnStartup_));
        Json ai = j["ai"].isObject() ? j["ai"] : Json::object();
        ai.set("provider", Json::str(wideToUtf8(aiProvider_)));
        ai.set("model", Json::str(wideToUtf8(aiModel_)));
        ai.set("includeCwd", Json::boolean(aiIncludeCwd_));
        j.set("ai", std::move(ai));
        j.set("workspaceRoot", Json::str(wideToUtf8(workspaceRoot_)));
    });
}

void Window::openConfigFile() {
    const std::wstring dir = configDir();
    if (dir.empty()) return;
    ShellExecuteW(hwnd_, L"open", (dir + L"\\config.json").c_str(), nullptr,
                  nullptr, SW_SHOWNORMAL);
}

void Window::openCurrentDirectory(UINT choice) {
    const std::wstring cwd = activeSession() && !activeSession()->cwd().empty()
                                 ? activeSession()->cwd()
                                 : homeDir();
    const std::wstring code = findExecutable(
        L"code.exe", { envPath(L"LOCALAPPDATA", L"\\Programs\\Microsoft VS Code\\Code.exe") });
    const std::wstring subl = findExecutable(
        L"subl.exe", { L"C:\\Program Files\\Sublime Text\\subl.exe",
                        L"C:\\Program Files (x86)\\Sublime Text\\subl.exe" });
    const std::wstring warp = findExecutable(
        L"warp.exe", { envPath(L"LOCALAPPDATA", L"\\Programs\\Warp\\warp.exe") });
    const std::wstring powershell = findExecutable(L"pwsh.exe").empty()
                                        ? findExecutable(L"powershell.exe")
                                        : findExecutable(L"pwsh.exe");
    switch (choice) {
    case 30:
        ShellExecuteW(hwnd_, L"open", cwd.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case 31: {
        if (powershell.empty()) return;
        std::wstring escaped = cwd;
        size_t pos = 0;
        while ((pos = escaped.find(L'\'', pos)) != std::wstring::npos) {
            escaped.insert(pos, 1, L'\'');
            pos += 2;
        }
        const std::wstring args =
            L"-NoExit -Command \"Set-Location -LiteralPath '" + escaped + L"'\"";
        ShellExecuteW(hwnd_, L"open", powershell.c_str(), args.c_str(), cwd.c_str(),
                      SW_SHOWNORMAL);
        break;
    }
    case 32: {
        if (code.empty()) return;
        const std::wstring args = L"--new-window " + quoteArg(cwd);
        ShellExecuteW(hwnd_, L"open", code.c_str(), args.c_str(), cwd.c_str(),
                      SW_SHOWNORMAL);
        break;
    }
    case 33: {
        if (subl.empty()) return;
        const std::wstring args = L"--new-window " + quoteArg(cwd);
        ShellExecuteW(hwnd_, L"open", subl.c_str(), args.c_str(), cwd.c_str(),
                      SW_SHOWNORMAL);
        break;
    }
    case 34: {
        if (warp.empty()) return;
        const std::wstring args = L"--cwd " + quoteArg(cwd);
        ShellExecuteW(hwnd_, L"open", warp.c_str(), args.c_str(), cwd.c_str(),
                      SW_SHOWNORMAL);
        break;
    }
    default: break;
    }
}

void Window::openDirectoryMenu() {
    POINT pt{ static_cast<int>(openButtonRect_.right()),
              static_cast<int>(openButtonRect_.bottom()) };
    ClientToScreen(hwnd_, &pt);
    const std::wstring code = findExecutable(
        L"code.exe", { envPath(L"LOCALAPPDATA", L"\\Programs\\Microsoft VS Code\\Code.exe") });
    const std::wstring subl = findExecutable(
        L"subl.exe", { L"C:\\Program Files\\Sublime Text\\subl.exe",
                        L"C:\\Program Files (x86)\\Sublime Text\\subl.exe" });
    const std::wstring warp = findExecutable(
        L"warp.exe", { envPath(L"LOCALAPPDATA", L"\\Programs\\Warp\\warp.exe") });
    const bool hasPowerShell = !findExecutable(L"pwsh.exe").empty() ||
                               !findExecutable(L"powershell.exe").empty();
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 30, L"File Explorer");
    AppendMenuW(m, MF_STRING | (hasPowerShell ? 0 : MF_GRAYED), 31, L"PowerShell");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (code.empty() ? MF_GRAYED : 0), 32,
                L"Visual Studio Code");
    AppendMenuW(m, MF_STRING | (subl.empty() ? MF_GRAYED : 0), 33, L"Sublime Text");
    AppendMenuW(m, MF_STRING | (warp.empty() ? MF_GRAYED : 0), 34, L"Warp");
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN, pt.x, pt.y,
                                   0, hwnd_, nullptr);
    DestroyMenu(m);
    if (cmd) openCurrentDirectory(static_cast<UINT>(cmd));
}

void Window::openNewWindow(bool elevated) {
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(
        nullptr, executable, static_cast<DWORD>(_countof(executable)));
    if (length == 0 || length >= _countof(executable)) return;
    const std::wstring cwd = activeSession() ? activeSession()->cwd() : L"";
    ShellExecuteW(hwnd_, elevated ? L"runas" : L"open", executable, nullptr,
                  cwd.empty() ? nullptr : cwd.c_str(), SW_SHOWNORMAL);
}

void Window::openDiagnosticsFolder() {
    const std::wstring dir = diagnosticsDir();
    if (!dir.empty())
        ShellExecuteW(hwnd_, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void Window::copyDiagnosticSummary() {
    const std::wstring summary = diagnosticSummary(kAppVersion);
    if (!OpenClipboard(hwnd_)) return;
    EmptyClipboard();
    const SIZE_T bytes = (summary.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        if (void* target = GlobalLock(memory)) {
            memcpy(target, summary.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
    showBalloon(L"Liney", L"Diagnostic summary copied");
}

void Window::exportDiagnostics() {
    wchar_t path[MAX_PATH] = L"Liney-diagnostics.zip";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"ZIP archive (*.zip)\0*.zip\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = static_cast<DWORD>(_countof(path));
    ofn.lpstrDefExt = L"zip";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    if (exportDiagnosticBundle(path, kAppVersion)) {
        showBalloon(L"Liney", L"Diagnostic bundle exported");
        ShellExecuteW(hwnd_, L"open", L"explorer.exe",
                      (L"/select,\"" + std::wstring(path) + L"\"").c_str(),
                      nullptr, SW_SHOWNORMAL);
    } else {
        MessageBoxW(hwnd_, L"The diagnostic bundle could not be created.",
                    L"Liney", MB_OK | MB_ICONERROR);
    }
}

void Window::searchHistory() {
    const std::wstring query = inputBox(hwnd_, L"Command history",
                                        L"Search local command history:", L"");
    if (query.empty()) return;
    const std::vector<HistoryEntry> results = searchCommandHistory(query, 20);
    if (results.empty()) {
        showBalloon(L"Liney", L"No matching commands");
        return;
    }
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < results.size(); ++i) {
        std::wstring label = results[i].command;
        std::replace(label.begin(), label.end(), L'\t', L' ');
        if (label.size() > 120) label = label.substr(0, 117) + L"...";
        AppendMenuW(menu, MF_STRING, static_cast<UINT>(500 + i), label.c_str());
    }
    POINT pt{}; GetCursorPos(&pt);
    const int selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    const size_t index = selected >= 500 ? static_cast<size_t>(selected - 500)
                                         : results.size();
    if (index < results.size()) {
        // Insert for review; history search never executes a command directly.
        sendUtf16(results[index].command.c_str(), results[index].command.size());
    }
}

void Window::restartSession(TerminalSession* session) {
    if (!session || !session->exited()) return;
    Tab* tab = activeTab();
    if (!tab) return;
    Pane* target = nullptr;
    for (Pane* leaf : tab->leaves())
        if (leaf->session.get() == session) { target = leaf; break; }
    if (!target) return;
    const std::wstring shell = session->shellCommand();
    const std::wstring cwd = session->cwd();
    const SessionContext context = session->context();
    int cols = 80, rows = 24;
    cellsForRect(target->rect, cols, rows);
    auto replacement = std::make_unique<TerminalSession>();
    if (!replacement->start(shell, cwd, cols, rows, scrollback_)) {
        MessageBoxW(hwnd_, L"The shell could not be restarted.", L"Liney",
                    MB_OK | MB_ICONERROR);
        return;
    }
    replacement->setContext(context);
    replacement->setTheme(theme_);
    target->session = std::move(replacement);
    runStartHook(target->session.get());
    updateTitle();
}

void Window::openKeepAwakeMenu() {
    POINT pt{ static_cast<int>(awakeButtonRect_.right()),
              static_cast<int>(awakeButtonRect_.bottom()) };
    ClientToScreen(hwnd_, &pt);
    HMENU awake = CreatePopupMenu();
    struct AwakeOpt { UINT id; int hours; const wchar_t* label; };
    static const AwakeOpt kAwake[] = {
        { 20, 0, L"Off" },        { 21, 1, L"For 1 hour" },
        { 22, 2, L"For 2 hours" },{ 23, 3, L"For 3 hours" },
        { 24, 6, L"For 6 hours" },{ 25, 24, L"For 24 hours" },
        { 26, -1, L"Until turned off" },
    };
    for (const AwakeOpt& o : kAwake) {
        AppendMenuW(awake, MF_STRING, o.id, o.label);
        if (o.hours == keepAwakeHours_)
            CheckMenuRadioItem(awake, 20, 26, o.id, MF_BYCOMMAND);
    }
    const int cmd = TrackPopupMenu(awake, TPM_RETURNCMD | TPM_RIGHTALIGN,
                                   pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(awake);
    for (const AwakeOpt& o : kAwake)
        if (o.id == static_cast<UINT>(cmd)) { setKeepAwake(o.hours); break; }
}

void Window::openMainMenu() {
    POINT pt{ static_cast<int>(menuButtonRect_.right()),
              static_cast<int>(menuButtonRect_.bottom()) };
    ClientToScreen(hwnd_, &pt);
    HMENU m = CreatePopupMenu();
    auto item = [&](UINT id, const wchar_t* text, bool checked = false) {
        AppendMenuW(m, MF_STRING | (checked ? MF_CHECKED : 0), id, text);
    };

    item(4, L"New tab\tCtrl+Shift+T");
    HMENU profiles = CreatePopupMenu();
    for (size_t i = 0; i < shellProfiles_.size() && i < 50; ++i)
        AppendMenuW(profiles, MF_STRING, static_cast<UINT>(200 + i),
                    shellProfiles_[i].name.c_str());
    AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(profiles),
                L"New tab with profile");
    item(5, L"Split side by side\tAlt+D");
    item(6, L"Split stacked\tShift+Alt+D");
    item(16, L"New window");
    item(17, L"New administrator window…");
    item(15, L"Workspace snapshots…");

    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING | (sidebarVisible_ ? MF_CHECKED : 0), 2,
                L"Sidebar\tCtrl+Shift+B");
    AppendMenuW(view, MF_STRING | (filesPanelVisible_ ? MF_CHECKED : 0), 3,
                L"Files panel\tCtrl+Shift+F");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    Tab* at = activeTab();
    const bool zoomed = at && at->zoom();
    AppendMenuW(view, MF_STRING | (zoomed ? MF_CHECKED : 0), 13,
                zoomed ? L"Restore pane\tCtrl+Shift+Z"
                       : L"Zoom pane\tCtrl+Shift+Z");
    AppendMenuW(view, MF_STRING, 14, L"Equalize panes\tCtrl+Shift+E");
    AppendMenuW(view, MF_STRING, 9, L"Find on screen…\tCtrl+F");
    AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"View");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    item(11, L"Settings…\tCtrl+,");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    item(12, L"Report an issue…");
    item(18, L"Open diagnostics folder");
    item(19, L"Copy diagnostic summary");
    item(20, L"Export diagnostic bundle...");
    item(21, L"Search command history...");
    item(8, L"Check for updates\tCtrl+Shift+U");

    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN, pt.x, pt.y,
                                   0, hwnd_, nullptr);
    DestroyMenu(m);  // also frees the submenu
    if (cmd >= 200 && cmd < 200 + static_cast<int>(shellProfiles_.size())) {
        newTabShell(shellProfiles_[static_cast<size_t>(cmd - 200)].command,
                    activeSession() ? activeSession()->cwd() : homeDir());
        return;
    }
    switch (cmd) {
    case 2: sidebarVisible_ = !sidebarVisible_; break;
    case 3: filesPanelVisible_ = !filesPanelVisible_; break;
    case 4: newTab(activeSession() ? activeSession()->cwd() : homeDir()); break;
    case 5: splitActive(SplitDir::Cols); break;
    case 6: splitActive(SplitDir::Rows); break;
    case 7: openConfigFile(); break;
    case 8: checkForUpdates(); break;
    case 9: openFind(); break;
    case 10: chooseFontDialog(); break;
    case 11: openSettingsDialog(); break;
    case 13: toggleZoom(); break;
    case 14: equalizePanes(); break;
    case 15: openWorkspaceSnapshotMenu(); break;
    case 16: openNewWindow(false); break;
    case 17: openNewWindow(true); break;
    case 18: openDiagnosticsFolder(); break;
    case 19: copyDiagnosticSummary(); break;
    case 20: exportDiagnostics(); break;
    case 21: searchHistory(); break;
    case 12:
        ShellExecuteW(hwnd_, L"open",
                      L"https://github.com/everettjf/liney-win/issues/new",
                      nullptr, nullptr, SW_SHOWNORMAL);
        break;
    default: break;
    }
}

void Window::openTabMenu(int xi, int yi) {
    const size_t idx = activeTab_;           // the right-clicked tab
    const size_t n = tabs_.size();
    POINT pt{ xi, yi };
    ClientToScreen(hwnd_, &pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"New tab\tCtrl+Shift+T");
    AppendMenuW(m, MF_STRING, 2, L"Open in Explorer");
    AppendMenuW(m, MF_STRING, 3, L"Copy path");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (tabs_[idx]->pinned() ? MF_CHECKED : 0), 9,
                L"Pin tab");
    AppendMenuW(m, MF_STRING, 10, L"Rename tab…");
    AppendMenuW(m, MF_STRING, 11, L"Duplicate tab");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 4, L"Close tab\tCtrl+Shift+W");
    // Close-multiple, each disabled when it would be a no-op.
    const UINT dR = (idx + 1 < n) ? 0u : MF_GRAYED;
    const UINT dL = (idx > 0) ? 0u : MF_GRAYED;
    const UINT dO = (n > 1) ? 0u : MF_GRAYED;
    AppendMenuW(m, MF_STRING | dR, 5, L"Close tabs to the right");
    AppendMenuW(m, MF_STRING | dL, 6, L"Close tabs to the left");
    AppendMenuW(m, MF_STRING | dO, 7, L"Close other tabs");
    AppendMenuW(m, MF_STRING, 8, L"Close all tabs");
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(m);

    TerminalSession* s = activeSession();
    const std::wstring cwd = s ? s->cwd() : L"";
    switch (cmd) {
    case 1: newTab(cwd.empty() ? homeDir() : cwd); break;
    case 2:
        if (!cwd.empty())
            ShellExecuteW(hwnd_, L"open", cwd.c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
        break;
    case 3:
        if (!cwd.empty() && OpenClipboard(hwnd_)) {
            EmptyClipboard();
            const size_t bytes = (cwd.size() + 1) * sizeof(wchar_t);
            if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
                memcpy(GlobalLock(h), cwd.c_str(), bytes);
                GlobalUnlock(h);
                SetClipboardData(CF_UNICODETEXT, h);
            }
            CloseClipboard();
        }
        break;
    case 9: togglePinActiveTab(); break;
    case 10: {
        const std::wstring title = inputBox(hwnd_, L"Rename tab", L"Tab title:",
                                            tabs_[idx]->customTitle());
        tabs_[idx]->setCustomTitle(title);
        updateTitle();
        break;
    }
    case 11:
        if (s) newTabShell(s->shellCommand(), cwd.empty() ? homeDir() : cwd);
        break;
    case 4: closeTabConfirming(idx); break;
    case 5: {  // tabs to the right
        std::vector<size_t> v;
        for (size_t i = idx + 1; i < n; ++i) v.push_back(i);
        closeTabSet(v, tabs_[idx].get());
        break;
    }
    case 6: {  // tabs to the left
        std::vector<size_t> v;
        for (size_t i = 0; i < idx; ++i) v.push_back(i);
        closeTabSet(v, tabs_[idx].get());
        break;
    }
    case 7: {  // all other tabs
        std::vector<size_t> v;
        for (size_t i = 0; i < n; ++i) if (i != idx) v.push_back(i);
        closeTabSet(v, tabs_[idx].get());
        break;
    }
    case 8: {  // every tab
        std::vector<size_t> v;
        for (size_t i = 0; i < n; ++i) v.push_back(i);
        closeTabSet(v, nullptr);
        break;
    }
    default: break;
    }
}

void Window::togglePinActiveTab() {
    if (activeTab_ >= tabs_.size()) return;
    Tab* selected = tabs_[activeTab_].get();
    selected->setPinned(!selected->pinned());
    std::stable_sort(tabs_.begin(), tabs_.end(), [](const auto& a, const auto& b) {
        return a->pinned() && !b->pinned();
    });
    for (size_t i = 0; i < tabs_.size(); ++i)
        if (tabs_[i].get() == selected) { activeTab_ = i; break; }
}

bool Window::tabHasRunningProcess(size_t idx) const {
    if (idx >= tabs_.size()) return false;
    // Collect the shell PIDs of every pane, then take ONE system process
    // snapshot and see if any process is a child of one of them. Snapshotting
    // per pane (as hasRunningChild does) would freeze the UI for a moment on a
    // heavily-split tab, which reads as a hang.
    std::vector<DWORD> shellPids;
    for (Pane* leaf : tabs_[idx]->leaves())
        if (leaf->session && leaf->session->shellPid() != 0)
            shellPids.push_back(leaf->session->shellPid());
    if (shellPids.empty()) return false;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool running = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            for (DWORD pid : shellPids)
                if (pe.th32ParentProcessID == pid) { running = true; break; }
        } while (!running && Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return running;
}

std::vector<std::wstring> Window::runningTabTitles(
    const std::vector<size_t>& idxs) const {
    // Map every candidate pane's shell PID to its tab, then take ONE system
    // process snapshot for the whole set (not one per tab) and mark any tab
    // that has a child process — i.e. is running a command.
    std::unordered_map<DWORD, size_t> pidToTab;
    for (size_t i : idxs)
        if (i < tabs_.size())
            for (Pane* leaf : tabs_[i]->leaves())
                if (leaf->session && leaf->session->shellPid() != 0)
                    pidToTab[leaf->session->shellPid()] = i;
    std::vector<std::wstring> out;
    if (pidToTab.empty()) return out;

    std::unordered_set<size_t> runningTabs;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                auto it = pidToTab.find(pe.th32ParentProcessID);
                if (it != pidToTab.end()) runningTabs.insert(it->second);
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    for (size_t i : idxs)  // preserve the caller's order
        if (runningTabs.count(i)) out.push_back(tabs_[i]->title());
    return out;
}

bool Window::confirmCloseRunning(const std::vector<std::wstring>& titles,
                                 const std::wstring& prompt) {
    if (titles.empty()) return true;  // nothing running — no prompt at all
    std::wstring msg = prompt + L"\n\nThese tabs are still running a command:\n";
    constexpr size_t kMax = 12;
    for (size_t i = 0; i < titles.size() && i < kMax; ++i)
        msg += L"    •  " + titles[i] + L"\n";
    if (titles.size() > kMax)
        msg += L"    …  and " + std::to_wstring(titles.size() - kMax) +
               L" more\n";
    msg += L"\nClose anyway?";
    return MessageBoxW(hwnd_, msg.c_str(), L"liney",
                       MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

void Window::closeTabConfirming(size_t idx) {
    if (idx >= tabs_.size()) return;
    if (!confirmCloseRunning(runningTabTitles({ idx }), L"Close this tab?"))
        return;
    closeTab(idx);
}

void Window::closeTab(size_t idx) {
    if (idx >= tabs_.size()) return;
    clearSelection();
    hoverTab_ = -1;  // indices shift; drop stale hover
    runDetached(sessionExitHook_, L"");  // hooks.sessionExit
    tabs_.erase(tabs_.begin() + idx);
    if (tabs_.empty()) { PostQuitMessage(0); return; }
    if (activeTab_ >= tabs_.size()) activeTab_ = tabs_.size() - 1;
    updateTitle();
}

void Window::closeTabSet(const std::vector<size_t>& victims, Tab* keep) {
    if (victims.empty()) return;
    // One consolidated confirmation listing the running tabs.
    if (!confirmCloseRunning(runningTabTitles(victims), L"Close these tabs?"))
        return;
    clearSelection();
    hoverTab_ = -1;
    // Erase high-index-first so the lower indices stay valid.
    std::vector<size_t> sorted = victims;
    std::sort(sorted.begin(), sorted.end(), std::greater<size_t>());
    for (size_t i : sorted) {
        if (i >= tabs_.size()) continue;
        runDetached(sessionExitHook_, L"");  // hooks.sessionExit (per tab)
        tabs_.erase(tabs_.begin() + i);
    }
    if (tabs_.empty()) { PostQuitMessage(0); return; }
    // Re-focus the anchor tab (found by identity — indices have shifted).
    size_t newActive = 0;
    if (keep)
        for (size_t k = 0; k < tabs_.size(); ++k)
            if (tabs_[k].get() == keep) { newActive = k; break; }
    activeTab_ = newActive;
    updateTitle();
}


} // namespace liney
