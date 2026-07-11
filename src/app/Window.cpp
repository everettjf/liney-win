#include "app/Window.h"

#include <windowsx.h>  // GET_X_LPARAM / GET_WHEEL_DELTA_WPARAM
#include <commdlg.h>   // ChooseFontW (the Font… picker)

#include <cstdio>
#include <memory>
#include <string>

#include "app/SettingsDialog.h"
#include "app/WindowInternal.h"
#include "core/Config.h"
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

// Monitor DPI as a 96-relative scale. GetDpiForWindow is Win10 1607+, so load it
// dynamically and fall back to the device caps.
static float queryDpiScale(HWND hwnd) {
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
}  // namespace

Window::Window() : renderer_(std::make_unique<D2DRenderer>()) {}
Window::~Window() {
    // Wait for update-check/download workers: they capture `this` and would
    // otherwise write into a destroyed Window (bounded by the HTTP timeouts).
    for (std::thread& t : updateThreads_)
        if (t.joinable()) t.join();
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
    const int x = wa.left + (waW - w) / 2;
    const int y = wa.top + (waH - h) / 2;

    hwnd_ = CreateWindowExW(0, kClassName, title, WS_OVERLAPPEDWINDOW,
                            x, y, w, h, nullptr, nullptr, hInstance, this);
    if (!hwnd_) return false;

    g_wakeHwnd.store(hwnd_, std::memory_order_relaxed);  // PTY thread wakes us here
    dpiScale_ = queryDpiScale(hwnd_);                    // scale the font to the monitor

    if (!renderer_->initialize(hwnd_)) return false;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    renderer_->resize(rc.right - rc.left, rc.bottom - rc.top);

    // User config (shell, font, workspace root); seeds %USERPROFILE%\.liney.
    const Config cfg = loadConfig();
    shell_ = cfg.shell;
    fontFamily_ = cfg.fontFamily;
    fontSize_ = cfg.fontSize;
    defaultFontSize_ = cfg.fontSize;
    sessionStartHook_ = cfg.sessionStartHook;
    sessionExitHook_ = cfg.sessionExitHook;
    appExitHook_ = cfg.appExitHook;
    copyOnSelect_ = cfg.copyOnSelect;
    multiLinePasteWarning_ = cfg.multiLinePasteWarning;
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
        if (base.find(L"]7;") == std::wstring::npos)  // don't double-add
            SetEnvironmentVariableW(
                L"PROMPT", (L"$e]7;file://localhost/$p$e\\" + base).c_str());
    }
    sshHosts_ = cfg.sshHosts;
    agents_ = cfg.agents;
    projectIcons_ = cfg.projectIcons;
    projects_ = cfg.projects;
    workspaceRoot_ = cfg.workspaceRoot;
    theme_ = cfg.theme;
    uiTheme_ = cfg.uiTheme;
    themeName_ = cfg.themeName;
    // Gutters/margins behind panes use the chrome's workspace color; panes
    // fill with the terminal background themselves.
    renderer_->setColors(uiTheme_.workspaceBg, theme_.background);
    applyFont();

    // Workspace root: config override, else the parent of the launch directory
    // (so sibling repos show up).
    wchar_t cwd[MAX_PATH]{};
    GetCurrentDirectoryW(MAX_PATH, cwd);
    std::wstring startCwd = cwd;
    launchParent_ = parentDir(startCwd);
    rescanWorkspace();

    // Restore the saved tab/pane layout if any; otherwise open one tab.
    if (!restoreLayout()) newTab(startCwd);
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
            L"liney-win", MB_OK | MB_ICONERROR);
        return false;
    }

    initTray();  // for OSC 9/777 balloon notifications
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
                saveLayout();  // persist tabs/panes for next launch
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
        if (wParam == kKeepAwakeTimerId) {
            setKeepAwake(0);
            showBalloon(L"liney-win",
                        L"Keep awake ended — normal sleep resumed");
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
    pollUpdateResult();   // show the update-check result when it arrives
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
    renderer_->endFrame();
}


void Window::reapExitedPanes() {
    // Scan every tab, not just the active one — a shell that exits in a
    // background tab should be reaped too, not linger until it's focused.
    for (size_t ti = 0; ti < tabs_.size(); ++ti) {
        Tab* t = tabs_[ti].get();
        for (Pane* leaf : t->leaves()) {
            if (!leaf->session || !leaf->session->exited()) continue;
            Tab* prevActive =
                (activeTab_ < tabs_.size()) ? tabs_[activeTab_].get() : nullptr;
            activeTab_ = ti;
            t->setActive(leaf);
            closeActivePane();  // may erase the tab and shift indices
            // Put focus back on the tab the user was on (found by identity —
            // the erase above may have shifted indices).
            if (prevActive && prevActive != t) {
                for (size_t k = 0; k < tabs_.size(); ++k) {
                    if (tabs_[k].get() == prevActive) {
                        activeTab_ = k;
                        break;
                    }
                }
                updateTitle();
            }
            return;  // tree changed; revisit next frame
        }
    }
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

void Window::newTabShell(const std::wstring& shellCmd, const std::wstring& cwd) {
    clearSelection();
    Rect leftBar, rightPanel, tabBar, panes;
    regions(leftBar, rightPanel, tabBar, panes);
    int cols = 80, rows = 24;
    cellsForRect(panes, cols, rows);

    auto session = std::make_unique<TerminalSession>();
    if (!session->start(shellCmd, cwd, cols, rows, scrollback_)) return;
    session->setTheme(theme_);
    runStartHook(session.get());
    tabs_.push_back(std::make_unique<Tab>(std::move(session)));
    activeTab_ = tabs_.size() - 1;
    updateTitle();
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
    const std::wstring cwd = a->session->cwd();

    auto session = std::make_unique<TerminalSession>();
    if (!session->start(shell_, cwd, cols, rows, scrollback_)) return;
    session->setTheme(theme_);
    runStartHook(session.get());
    t->splitActive(dir, std::move(session));
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
    std::wstring title = L"liney-win";
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
        showBalloon(L"liney-win",
                    L"Keep awake: on for " + std::to_wstring(hours) +
                        (hours == 1 ? L" hour" : L" hours"));
    } else if (hours < 0) {
        showBalloon(L"liney-win", L"Keep awake: on until turned off");
    } else {
        showBalloon(L"liney-win", L"Keep awake: off");
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

void Window::applyTheme(const std::wstring& presetName, const Color& accent) {
    const auto presets = builtinThemePresets();
    if (const ThemePreset* p = findThemePreset(presets, presetName)) {
        themeName_ = p->name;
        theme_ = p->terminal;
        uiTheme_ = p->ui;
    }
    uiTheme_.accent = accent;  // override on top of the preset
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
    v.workspaceRoot = workspaceRoot_;
    if (!showSettingsDialog(hwnd_, v)) return;

    // Apply live. Shell/scrollback affect sessions started from now on.
    shell_ = v.shell;
    scrollback_ = v.scrollback;
    copyOnSelect_ = v.copyOnSelect;
    multiLinePasteWarning_ = v.multiLinePasteWarning;
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
    const bool themeChanged =
        v.themeName != themeName_ || !(v.accent.r == uiTheme_.accent.r &&
                                       v.accent.g == uiTheme_.accent.g &&
                                       v.accent.b == uiTheme_.accent.b);
    if (themeChanged) applyTheme(v.themeName, v.accent);

    // Persist, preserving keys the dialog doesn't edit (hooks, sshHosts, …).
    updateConfigJson([&](Json& j) {
        j.set("shell", Json::str(wideToUtf8(shell_)));
        j.set("fontFamily", Json::str(wideToUtf8(fontFamily_)));
        j.set("fontSize", Json::number(fontSize_));
        j.set("theme", Json::str(wideToUtf8(themeName_)));
        j.set("accentColor", Json::str(colorToHex(uiTheme_.accent)));
        j.set("scrollback", Json::number(scrollback_));
        j.set("copyOnSelect", Json::boolean(copyOnSelect_));
        j.set("multiLinePasteWarning", Json::boolean(multiLinePasteWarning_));
        j.set("unixTools", Json::boolean(unixToolsEnabled_));
        j.set("workspaceRoot", Json::str(wideToUtf8(workspaceRoot_)));
    });
}

void Window::openConfigFile() {
    const std::wstring dir = configDir();
    if (dir.empty()) return;
    ShellExecuteW(hwnd_, L"open", (dir + L"\\config.json").c_str(), nullptr,
                  nullptr, SW_SHOWNORMAL);
}

void Window::openMainMenu() {
    POINT pt{ static_cast<int>(menuButtonRect_.right()),
              static_cast<int>(menuButtonRect_.bottom()) };
    ClientToScreen(hwnd_, &pt);
    HMENU m = CreatePopupMenu();
    auto item = [&](UINT id, const wchar_t* text, bool checked = false) {
        AppendMenuW(m, MF_STRING | (checked ? MF_CHECKED : 0), id, text);
    };

    // Keep awake ▸ duration presets (Amphetamine / PowerToys Awake pattern).
    // Command ids 20..26; the active preset carries a radio check.
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
    std::wstring awakeLabel = L"Keep awake";
    const std::wstring status = keepAwakeStatus();
    if (!status.empty()) awakeLabel += L" (" + status + L")";
    awakeLabel += L"\tCtrl+Shift+K";
    AppendMenuW(m, MF_POPUP | (keepAwake_ ? MF_CHECKED : 0),
                reinterpret_cast<UINT_PTR>(awake), awakeLabel.c_str());

    item(2, L"Show sidebar\tCtrl+Shift+B", sidebarVisible_);
    item(3, L"Show files panel\tCtrl+Shift+F", filesPanelVisible_);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    item(4, L"New tab\tCtrl+Shift+T");
    item(5, L"Split side by side\tAlt+D");
    item(6, L"Split stacked\tShift+Alt+D");
    item(9, L"Find on screen…\tCtrl+F");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    item(11, L"Settings…");
    item(10, L"Font…");
    item(7, L"Open config file (config.json)");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    item(12, L"Report an issue…");
    item(8, L"Check for updates\tCtrl+Shift+U");

    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN, pt.x, pt.y,
                                   0, hwnd_, nullptr);
    DestroyMenu(m);  // also frees the submenu
    switch (cmd) {
    case 2: sidebarVisible_ = !sidebarVisible_; break;
    case 3: filesPanelVisible_ = !filesPanelVisible_; break;
    case 4: newTab(activeSession() ? activeSession()->cwd() : workspace_.root()); break;
    case 5: splitActive(SplitDir::Cols); break;
    case 6: splitActive(SplitDir::Rows); break;
    case 7: openConfigFile(); break;
    case 8: checkForUpdates(); break;
    case 9: openFind(); break;
    case 10: chooseFontDialog(); break;
    case 11: openSettingsDialog(); break;
    case 12:
        ShellExecuteW(hwnd_, L"open",
                      L"https://github.com/everettjf/liney-win/issues/new",
                      nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case 20: case 21: case 22: case 23: case 24: case 25: case 26:
        for (const AwakeOpt& o : kAwake)
            if (o.id == static_cast<UINT>(cmd)) { setKeepAwake(o.hours); break; }
        break;
    default: break;
    }
}

void Window::openTabMenu(int xi, int yi) {
    POINT pt{ xi, yi };
    ClientToScreen(hwnd_, &pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"New tab\tCtrl+Shift+T");
    AppendMenuW(m, MF_STRING, 2, L"Open in Explorer");
    AppendMenuW(m, MF_STRING, 3, L"Copy path");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 4, L"Close tab\tCtrl+Shift+W");
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(m);

    TerminalSession* s = activeSession();
    const std::wstring cwd = s ? s->cwd() : L"";
    switch (cmd) {
    case 1: newTab(cwd.empty() ? workspace_.root() : cwd); break;
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
    case 4: closeTab(activeTab_); break;
    default: break;
    }
}

void Window::closeTab(size_t idx) {
    if (idx >= tabs_.size()) return;
    clearSelection();
    runDetached(sessionExitHook_, L"");  // hooks.sessionExit
    tabs_.erase(tabs_.begin() + idx);
    if (tabs_.empty()) { PostQuitMessage(0); return; }
    if (activeTab_ >= tabs_.size()) activeTab_ = tabs_.size() - 1;
    updateTitle();
}


} // namespace liney
