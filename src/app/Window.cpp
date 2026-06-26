#include "app/Window.h"

#include <windowsx.h>  // GET_X_LPARAM / GET_WHEEL_DELTA_WPARAM

#include <memory>
#include <string>

#include "app/WindowInternal.h"
#include "core/Config.h"
#include "render/D2DRenderer.h"
#include "util/Process.h"  // runDetached (lifecycle hooks)

namespace liney {

static const wchar_t* kClassName = L"LineyWinMainWindow";

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
Window::~Window() = default;

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

    hwnd_ = CreateWindowExW(0, kClassName, title, WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr,
                            nullptr, hInstance, this);
    if (!hwnd_) return false;

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
    renderer_->setColors(theme_.background, theme_.background);
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
    if (tabs_.empty()) return false;  // shell failed to launch

    initTray();  // for OSC 9/777 balloon notifications
    return true;
}

void Window::show(int nCmdShow) {
    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
}

int Window::runMessageLoop() {
    MSG msg{};
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                saveLayout();  // persist tabs/panes for next launch
                runDetached(appExitHook_, L"");  // hooks.appExit
                return static_cast<int>(msg.wParam);
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        renderFrame();  // vsync-throttled Present, so this does not spin
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
        if (LOWORD(lParam) && HIWORD(lParam))
            renderer_->resize(LOWORD(lParam), HIWORD(lParam));
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
        // Ctrl+Wheel zooms the font; a plain wheel scrolls scrollback.
        if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL)
            zoomFont(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? +1 : -1);
        else
            onWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
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
    Tab* t = activeTab();
    if (!t) return;
    for (Pane* leaf : t->leaves()) {
        if (leaf->session && leaf->session->exited()) {
            t->setActive(leaf);
            closeActivePane();
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
    cols = static_cast<int>(r.w / metrics_.cellW);
    rows = static_cast<int>(r.h / metrics_.cellH);
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
    if (!session->start(shellCmd, cwd, cols, rows)) return;
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
    if (!session->start(shell_, cwd, cols, rows)) return;
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
    renderer_->setFont(fontFamily_, fontSize_);
    unsigned cw = 0, ch = 0;
    renderer_->cellSize(cw, ch);
    metrics_.cellW = cw ? static_cast<float>(cw) : 8.0f;
    metrics_.cellH = ch ? static_cast<float>(ch) : 16.0f;
    // Panes are re-laid out (and sessions resized) on the next frame.
}

void Window::zoomFont(int step) {
    fontSize_ = (step == 0) ? defaultFontSize_ : fontSize_ + step;
    if (fontSize_ < 6.0f) fontSize_ = 6.0f;
    if (fontSize_ > 72.0f) fontSize_ = 72.0f;
    applyFont();
    saveFontSize(fontSize_);  // remember the zoom level across launches
}

void Window::toggleKeepAwake() {
    keepAwake_ = !keepAwake_;
    // ES_CONTINUOUS persists the request; SYSTEM/DISPLAY keep both awake.
    SetThreadExecutionState(keepAwake_
        ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)
        : ES_CONTINUOUS);
    showBalloon(L"liney-win",
                keepAwake_ ? L"Keep awake: on — sleep is blocked"
                           : L"Keep awake: off");
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
    item(1, L"Keep awake\tCtrl+Shift+K", keepAwake_);
    item(2, L"Show sidebar\tCtrl+Shift+B", sidebarVisible_);
    item(3, L"Show files panel\tCtrl+Shift+F", filesPanelVisible_);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    item(4, L"New tab\tCtrl+Shift+T");
    item(5, L"Split side by side\tAlt+D");
    item(6, L"Split stacked\tShift+Alt+D");
    item(9, L"Find on screen…\tCtrl+F");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    item(7, L"Settings (config.json)…");
    item(8, L"Check for updates\tCtrl+Shift+U");

    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN, pt.x, pt.y,
                                   0, hwnd_, nullptr);
    DestroyMenu(m);
    switch (cmd) {
    case 1: toggleKeepAwake(); break;
    case 2: sidebarVisible_ = !sidebarVisible_; break;
    case 3: filesPanelVisible_ = !filesPanelVisible_; break;
    case 4: newTab(activeSession() ? activeSession()->cwd() : workspace_.root()); break;
    case 5: splitActive(SplitDir::Cols); break;
    case 6: splitActive(SplitDir::Rows); break;
    case 7: openConfigFile(); break;
    case 8: checkForUpdates(); break;
    case 9: openFind(); break;
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
