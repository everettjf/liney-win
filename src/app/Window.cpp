#include "app/Window.h"

#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#include <imm.h>       // IME composition window positioning

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include "core/Config.h"
#include "render/D2DRenderer.h"
#include "util/InputBox.h"
#include "util/Json.h"

namespace liney {

static const wchar_t* kClassName = L"LineyWinMainWindow";

// Chrome palette.
static const Color kSidebarBg{ 24, 24, 28 };
static const Color kSidebarHdr{ 130, 130, 140 };
static const Color kText{ 205, 205, 210 };
static const Color kDim{ 140, 140, 150 };
static const Color kTabBg{ 18, 18, 22 };
static const Color kTabActiveBg{ 40, 42, 52 };
static const Color kAccent{ 120, 200, 160 };
static const Color kBorder{ 55, 55, 66 };

namespace {
std::wstring parentDir(const std::wstring& path) {
    size_t end = path.size();
    while (end > 0 && (path[end - 1] == L'\\' || path[end - 1] == L'/')) --end;
    size_t slash = path.find_last_of(L"\\/", end ? end - 1 : 0);
    if (slash == std::wstring::npos) return path;
    return path.substr(0, slash);
}
bool keyDown(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(),
                        n, nullptr, nullptr);
    return s;
}

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// Serialize a pane subtree: splits carry dir/ratio/children, leaves carry cwd.
Json paneToJson(const Pane* p) {
    Json j = Json::object();
    if (p->isSplit) {
        j.set("type", Json::str("split"));
        j.set("dir", Json::str(p->dir == SplitDir::Rows ? "rows" : "cols"));
        j.set("ratio", Json::number(p->ratio));
        j.set("a", paneToJson(p->a.get()));
        j.set("b", paneToJson(p->b.get()));
    } else {
        j.set("type", Json::str("leaf"));
        j.set("cwd", Json::str(wideToUtf8(p->session ? p->session->cwd() : L"")));
        j.set("shell",
              Json::str(wideToUtf8(p->session ? p->session->shellCommand() : L"")));
    }
    return j;
}
}  // namespace

Window::Window() : renderer_(std::make_unique<D2DRenderer>()) {}
Window::~Window() = default;

bool Window::create(HINSTANCE hInstance, const wchar_t* title, int width,
                    int height) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Window::wndProcThunk;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
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
    sshHosts_ = cfg.sshHosts;
    agents_ = cfg.agents;
    theme_ = cfg.theme;
    renderer_->setColors(theme_.background, theme_.background);
    applyFont();

    // Workspace root: config override, else the parent of the launch directory
    // (so sibling repos show up).
    wchar_t cwd[MAX_PATH]{};
    GetCurrentDirectoryW(MAX_PATH, cwd);
    std::wstring startCwd = cwd;
    workspace_.scan(cfg.workspaceRoot.empty() ? parentDir(startCwd)
                                              : cfg.workspaceRoot);

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
    case WM_LBUTTONDOWN:
        SetFocus(hwnd_);
        onMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
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
    case WM_COPY:
        copySelection();
        return 0;
    case WM_PASTE:
        paste();
        return 0;
    case WM_MOUSEWHEEL:
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

void Window::regions(Rect& sidebar, Rect& tabBar, Rect& panes) const {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const float W = static_cast<float>(rc.right - rc.left);
    const float H = static_cast<float>(rc.bottom - rc.top);
    const float sw = sidebarVisible_ ? metrics_.sidebarW() : 0.0f;
    const float tb = metrics_.tabBarH();
    sidebar = { 0, 0, sw, H };
    tabBar = { sw, 0, W - sw, tb };
    panes = { sw, tb, W - sw, H - tb };
}

void Window::renderFrame() {
    reapExitedPanes();
    if (tabs_.empty()) return;

    pollNotifications();  // OSC 9/777 across all sessions -> balloons
    updateTitle();        // reflect OSC 0/2 title changes live

    Tab* t = activeTab();
    if (t) for (Pane* leaf : t->leaves())
        if (leaf->session) leaf->session->snapshot();

    Rect sidebar, tabBar, panes;
    regions(sidebar, tabBar, panes);

    renderer_->beginFrame();
    if (sidebarVisible_) drawSidebar(sidebar);
    drawTabBar(tabBar);
    drawPanes(panes);
    renderer_->endFrame();
}

void Window::drawSidebar(const Rect& r) {
    sidebarRows_.clear();
    renderer_->fillRect(r.x, r.y, r.w, r.h, kSidebarBg);

    const float pad = 8.0f;
    const float rowH = metrics_.rowH();
    float y = r.y + 6.0f;

    renderer_->drawText(L"WORKSPACE", r.x + pad, y, r.w - pad, rowH, kSidebarHdr,
                        true);
    y += rowH + 2.0f;

    auto& repos = workspace_.repos();
    if (repos.empty()) {
        renderer_->drawText(L"(no git repos found)", r.x + pad, y, r.w - pad,
                            rowH, kDim, false);
        y += rowH;
    }
    for (int i = 0; i < static_cast<int>(repos.size()); ++i) {
        if (y > r.bottom()) break;
        Repo& repo = repos[i];
        const std::wstring marker = repo.expanded ? L"v " : L"> ";
        renderer_->drawText(marker + repo.name, r.x + pad, y, r.w - pad, rowH,
                            kText, true);
        sidebarRows_.push_back({ { r.x, y, r.w, rowH }, RowKind::RepoHeader, i, -1, L"" });
        y += rowH;

        if (repo.expanded) {
            for (int w = 0; w < static_cast<int>(repo.worktrees.size()); ++w) {
                renderer_->drawText(L"- " + repo.worktrees[w].label,
                                    r.x + pad + metrics_.cellW * 2.0f, y,
                                    r.w - pad, rowH, kDim, false);
                sidebarRows_.push_back({ { r.x, y, r.w, rowH }, RowKind::Worktree, i, w, L"" });
                y += rowH;
            }
        }
    }

    // ---- FILES: directory of the focused pane, navigable -------------------
    refreshFileList();
    y += rowH * 0.5f;
    std::wstring header = L"FILES";
    if (!browsePath_.empty()) {
        size_t s = browsePath_.find_last_of(L"\\/");
        header += L"  " + (s == std::wstring::npos ? browsePath_
                                                   : browsePath_.substr(s + 1));
    }
    renderer_->drawText(header, r.x + pad, y, r.w - pad, rowH, kSidebarHdr, true);
    y += rowH + 2.0f;

    if (!browsePath_.empty()) {
        renderer_->drawText(L".. ", r.x + pad, y, r.w - pad, rowH, kDim, false);
        sidebarRows_.push_back({ { r.x, y, r.w, rowH }, RowKind::FileUp, -1, -1, L"" });
        y += rowH;
    }
    for (const FileEntry& e : fileEntries_) {
        if (y > r.bottom()) break;
        const std::wstring text =
            (e.isDir ? L"> " : L"  ") + e.name + (e.isDir ? L"/" : L"");
        renderer_->drawText(text, r.x + pad, y, r.w - pad, rowH,
                            e.isDir ? kText : kDim, false);
        sidebarRows_.push_back(
            { { r.x, y, r.w, rowH },
              e.isDir ? RowKind::FileDir : RowKind::FileEntry, -1, -1, e.path });
        y += rowH;
    }

    // ---- SSH: configured hosts; click to open `ssh <host>` in a new tab -----
    if (!sshHosts_.empty()) {
        y += rowH * 0.5f;
        renderer_->drawText(L"SSH", r.x + pad, y, r.w - pad, rowH, kSidebarHdr, true);
        y += rowH + 2.0f;
        for (int i = 0; i < static_cast<int>(sshHosts_.size()); ++i) {
            if (y > r.bottom()) break;
            renderer_->drawText(L"@ " + sshHosts_[i], r.x + pad, y, r.w - pad,
                                rowH, kText, false);
            sidebarRows_.push_back({ { r.x, y, r.w, rowH }, RowKind::SshHost, i, -1, L"" });
            y += rowH;
        }
    }

    // ---- AGENTS: configured agent sessions; click to open in a new tab ------
    if (!agents_.empty()) {
        y += rowH * 0.5f;
        renderer_->drawText(L"AGENTS", r.x + pad, y, r.w - pad, rowH, kSidebarHdr, true);
        y += rowH + 2.0f;
        for (int i = 0; i < static_cast<int>(agents_.size()); ++i) {
            if (y > r.bottom()) break;
            renderer_->drawText(L"* " + agents_[i].name, r.x + pad, y, r.w - pad,
                                rowH, kText, false);
            sidebarRows_.push_back({ { r.x, y, r.w, rowH }, RowKind::Agent, i, -1, L"" });
            y += rowH;
        }
    }
}

void Window::refreshFileList() {
    // Follow the focused pane's cwd unless the user navigated manually.
    if (TerminalSession* s = activeSession()) {
        if (s->cwd() != lastActiveCwd_) {
            lastActiveCwd_ = s->cwd();
            browsePath_ = s->cwd();
        }
    }
    if (browsePath_ == listedDir_) return;  // already listed
    listedDir_ = browsePath_;
    fileEntries_.clear();
    if (browsePath_.empty()) return;

    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((browsePath_ + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const bool dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        fileEntries_.push_back({ name, browsePath_ + L"\\" + name, dir });
        if (fileEntries_.size() >= 500) break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    std::sort(fileEntries_.begin(), fileEntries_.end(),
              [](const FileEntry& a, const FileEntry& b) {
                  if (a.isDir != b.isDir) return a.isDir;  // dirs first
                  return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
              });
}

void Window::drawTabBar(const Rect& r) {
    tabRects_.clear();
    renderer_->fillRect(r.x, r.y, r.w, r.h, kTabBg);

    const float pad = metrics_.cellW;
    float x = r.x;
    for (size_t i = 0; i < tabs_.size(); ++i) {
        std::wstring title = tabs_[i]->title();
        if (title.size() > 18) title = title.substr(0, 17) + L"…";
        const float tw = (static_cast<float>(title.size()) + 3.0f) * metrics_.cellW;
        const bool active = (i == activeTab_);
        if (active) renderer_->fillRect(x, r.y, tw, r.h, kTabActiveBg);
        renderer_->drawText(title, x + pad, r.y + 5.0f, tw - pad, metrics_.cellH,
                            active ? kAccent : kDim, active);
        if (active)
            renderer_->fillRect(x, r.bottom() - 2.0f, tw, 2.0f, kAccent);
        tabRects_.push_back({ x, r.y, tw, r.h });
        x += tw;
    }

    // "+" new-tab button.
    const float plusW = metrics_.cellW * 3.0f;
    plusRect_ = { x, r.y, plusW, r.h };
    renderer_->drawText(L"+", x + metrics_.cellW, r.y + 5.0f, plusW,
                        metrics_.cellH, kDim, true);
}

void Window::drawPanes(const Rect& r) {
    Tab* t = activeTab();
    if (!t) return;
    t->layout(r, metrics_);

    // Refresh selection highlight on the owning pane; clear it elsewhere.
    for (Pane* leaf : t->leaves())
        if (leaf->session) leaf->session->grid().hasSelection = false;
    applySelectionToGrid();

    for (Pane* leaf : t->leaves()) {
        if (!leaf->session) continue;
        renderer_->drawGrid(leaf->session->grid(), leaf->rect.x, leaf->rect.y);
        const bool focused = (leaf == t->active());
        renderer_->strokeRect(leaf->rect.x, leaf->rect.y, leaf->rect.w,
                              leaf->rect.h, focused ? kAccent : kBorder,
                              focused ? 1.5f : 1.0f);
    }
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
    Rect sidebar, tabBar, panes;
    regions(sidebar, tabBar, panes);
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

void Window::initTray() {
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_TIP;
    nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(nid_.szTip, L"liney-win", _TRUNCATE);
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
}

void Window::showBalloon(const std::wstring& title, const std::wstring& body) {
    if (!trayAdded_) return;
    nid_.uFlags = NIF_INFO;
    nid_.dwInfoFlags = NIIF_INFO;
    wcsncpy_s(nid_.szInfoTitle, title.empty() ? L"liney-win" : title.c_str(),
              _TRUNCATE);
    wcsncpy_s(nid_.szInfo, body.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void Window::removeTray() {
    if (trayAdded_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        trayAdded_ = false;
    }
}

void Window::pollNotifications() {
    std::vector<Notification> notes;
    for (auto& tab : tabs_)
        for (Pane* leaf : tab->leaves())
            if (leaf->session) leaf->session->poll(notes);
    for (const Notification& n : notes) showBalloon(n.title, n.body);
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
}

void Window::saveLayout() const {
    const std::wstring dir = configDir();
    if (dir.empty() || tabs_.empty()) return;
    Json root = Json::object();
    Json tabs = Json::array();
    for (const auto& tab : tabs_) {
        Json t = Json::object();
        t.set("root", paneToJson(tab->root()));
        tabs.push(std::move(t));
    }
    root.set("tabs", std::move(tabs));
    root.set("activeTab", Json::number(static_cast<double>(activeTab_)));

    std::ofstream f((dir + L"\\layout.json").c_str(), std::ios::binary);
    if (f) {
        const std::string s = root.dump(2);
        f.write(s.data(), static_cast<std::streamsize>(s.size()));
    }
}

std::unique_ptr<Pane> Window::paneFromJson(const Json& j, int cols, int rows) {
    if (!j.isObject()) return nullptr;
    if (j["type"].asString() == "split") {
        auto p = std::make_unique<Pane>();
        p->isSplit = true;
        p->dir = (j["dir"].asString() == "rows") ? SplitDir::Rows : SplitDir::Cols;
        p->ratio = static_cast<float>(j["ratio"].asNumber(0.5));
        if (p->ratio < 0.05f) p->ratio = 0.05f;
        if (p->ratio > 0.95f) p->ratio = 0.95f;
        auto a = paneFromJson(j["a"], cols, rows);
        auto b = paneFromJson(j["b"], cols, rows);
        if (a && b) { p->a = std::move(a); p->b = std::move(b); return p; }
        // A child failed (e.g. its cwd is gone): collapse to the survivor.
        if (a) return a;
        if (b) return b;
        return nullptr;
    }
    // Leaf: start a session in the saved cwd with its saved shell command.
    const std::wstring cwd = utf8ToWide(j["cwd"].asString());
    std::wstring shell = utf8ToWide(j["shell"].asString());
    if (shell.empty()) shell = shell_;
    auto s = std::make_unique<TerminalSession>();
    if (!s->start(shell, cwd, cols, rows)) return nullptr;
    s->setTheme(theme_);
    auto p = std::make_unique<Pane>();
    p->session = std::move(s);
    return p;
}

bool Window::restoreLayout() {
    const std::wstring dir = configDir();
    if (dir.empty()) return false;
    std::ifstream f((dir + L"\\layout.json").c_str(), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    if (text.empty()) return false;

    bool ok = false;
    Json root = Json::parse(text, &ok);
    if (!ok || !root.isObject()) return false;
    const Json& tabsJ = root["tabs"];
    if (!tabsJ.isArray() || tabsJ.size() == 0) return false;

    Rect sidebar, tabBar, panes;
    regions(sidebar, tabBar, panes);
    int cols = 80, rows = 24;
    cellsForRect(panes, cols, rows);

    for (const Json& t : tabsJ.items()) {
        auto pane = paneFromJson(t["root"], cols, rows);
        if (pane) tabs_.push_back(std::make_unique<Tab>(std::move(pane)));
    }
    if (tabs_.empty()) return false;

    const int at = static_cast<int>(root["activeTab"].asNumber(0));
    activeTab_ = (at >= 0 && at < static_cast<int>(tabs_.size()))
                     ? static_cast<size_t>(at)
                     : 0;
    updateTitle();
    return true;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void Window::sendToActive(const char* data, size_t len) {
    if (auto* s = activeSession()) {
        s->scrollToBottom();  // typing snaps the viewport back to live output
        s->sendBytes(data, len);
    }
}

void Window::scrollActive(int lines) {
    if (auto* s = activeSession()) s->scrollViewport(lines);
}

int Window::activePaneRows() const {
    Tab* t = activeTab();
    if (!t || !t->active()) return 24;
    int r = static_cast<int>(t->active()->rect.h / metrics_.cellH);
    return r < 1 ? 1 : r;
}

void Window::onWheel(int delta) {
    // One notch (WHEEL_DELTA) scrolls 3 lines into history (+) or toward live.
    scrollActive((delta / WHEEL_DELTA) * 3);
}

void Window::sendUtf16(const wchar_t* s, size_t len) {
    if (len == 0) return;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(len),
                                    nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return;
    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(len), utf8.data(), bytes,
                        nullptr, nullptr);
    sendToActive(utf8.data(), utf8.size());
}

void Window::cursorPixelPos(int& px, int& py) const {
    Tab* t = activeTab();
    if (t && t->active() && t->active()->session) {
        const Grid& g = t->active()->session->grid();
        const Rect r = t->active()->rect;
        px = static_cast<int>(r.x + g.cursorX * metrics_.cellW);
        py = static_cast<int>(r.y + g.cursorY * metrics_.cellH);
    } else {
        px = 0;
        py = 0;
    }
}

void Window::positionIme() {
    int px = 0, py = 0;
    cursorPixelPos(px, py);
    HIMC himc = ImmGetContext(hwnd_);
    if (!himc) return;
    COMPOSITIONFORM cf{};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos = { px, py };
    ImmSetCompositionWindow(himc, &cf);
    CANDIDATEFORM caf{};
    caf.dwStyle = CFS_CANDIDATEPOS;
    caf.ptCurrentPos = { px, py + static_cast<int>(metrics_.cellH) };
    ImmSetCandidateWindow(himc, &caf);
    ImmReleaseContext(hwnd_, himc);
}

void Window::onChar(wchar_t unit) {
    if (swallowNextChar_) { swallowNextChar_ = false; return; }
    if (unit >= 0xD800 && unit <= 0xDBFF) { pendingHighSurrogate_ = unit; return; }
    if (unit >= 0xDC00 && unit <= 0xDFFF) {
        if (pendingHighSurrogate_) {
            wchar_t pair[2] = { pendingHighSurrogate_, unit };
            sendUtf16(pair, 2);
            pendingHighSurrogate_ = 0;
        }
        return;
    }
    pendingHighSurrogate_ = 0;
    sendUtf16(&unit, 1);
}

bool Window::onKeyDown(WPARAM vk) {
    const bool ctrl = keyDown(VK_CONTROL);
    const bool shift = keyDown(VK_SHIFT);
    const bool alt = keyDown(VK_MENU);

    // Alt + arrows: move pane focus.
    if (alt && !ctrl) {
        Tab* t = activeTab();
        switch (vk) {
        case VK_LEFT:  if (t) t->focusDir(SplitDir::Cols, false); swallowNextChar_ = true; return true;
        case VK_RIGHT: if (t) t->focusDir(SplitDir::Cols, true);  swallowNextChar_ = true; return true;
        case VK_UP:    if (t) t->focusDir(SplitDir::Rows, false); swallowNextChar_ = true; return true;
        case VK_DOWN:  if (t) t->focusDir(SplitDir::Rows, true);  swallowNextChar_ = true; return true;
        default: break;
        }
    }

    // Ctrl(+Shift) app shortcuts.
    if (ctrl) {
        if (vk == VK_TAB) { switchTab(shift ? -1 : 1); swallowNextChar_ = true; return true; }
        if (!shift) {
            switch (vk) {
            case VK_OEM_PLUS:
            case VK_ADD: zoomFont(+1); swallowNextChar_ = true; return true;
            case VK_OEM_MINUS:
            case VK_SUBTRACT: zoomFont(-1); swallowNextChar_ = true; return true;
            case '0':
            case VK_NUMPAD0: zoomFont(0); swallowNextChar_ = true; return true;
            default: break;
            }
        }
        if (shift) {
            switch (vk) {
            case 'T': newTab(activeSession() ? activeSession()->cwd() : workspace_.root()); swallowNextChar_ = true; return true;
            case 'W': closeActivePane(); swallowNextChar_ = true; return true;
            case 'E': splitActive(SplitDir::Cols); swallowNextChar_ = true; return true;  // side by side
            case 'O': splitActive(SplitDir::Rows); swallowNextChar_ = true; return true;  // stacked
            case 'B': sidebarVisible_ = !sidebarVisible_; swallowNextChar_ = true; return true;
            case 'C': copySelection(); swallowNextChar_ = true; return true;
            case 'V': paste(); swallowNextChar_ = true; return true;
            case 'L':  // git history for the active pane's repo (pager view)
                if (auto* s = activeSession()) {
                    const std::wstring cwd = s->cwd();
                    if (!cwd.empty())
                        newTabShell(L"git -C \"" + cwd +
                                        L"\" log --oneline --graph --decorate -300",
                                    cwd);
                }
                swallowNextChar_ = true; return true;
            case 'G':  // git diff for the active pane's repo (pager view)
                if (auto* s = activeSession()) {
                    const std::wstring cwd = s->cwd();
                    if (!cwd.empty())
                        newTabShell(L"git -C \"" + cwd + L"\" diff", cwd);
                }
                swallowNextChar_ = true; return true;
            default: break;
            }
        }
    }

    // Shift + navigation keys scroll the viewport over scrollback history.
    if (shift && !ctrl && !alt) {
        const int page = activePaneRows() - 1;
        switch (vk) {
        case VK_PRIOR: scrollActive(page > 0 ? page : 1); return true;   // PgUp
        case VK_NEXT:  scrollActive(-(page > 0 ? page : 1)); return true; // PgDn
        case VK_HOME:  scrollActive(1000000); return true;               // to oldest
        case VK_END:   if (auto* s = activeSession()) s->scrollToBottom(); return true;
        default: break;
        }
    }

    // Keys that produce no WM_CHAR: forward as xterm escape sequences.
    const char* seq = nullptr;
    switch (vk) {
    case VK_UP:     seq = "\x1b[A"; break;
    case VK_DOWN:   seq = "\x1b[B"; break;
    case VK_RIGHT:  seq = "\x1b[C"; break;
    case VK_LEFT:   seq = "\x1b[D"; break;
    case VK_HOME:   seq = "\x1b[H"; break;
    case VK_END:    seq = "\x1b[F"; break;
    case VK_PRIOR:  seq = "\x1b[5~"; break;
    case VK_NEXT:   seq = "\x1b[6~"; break;
    case VK_INSERT: seq = "\x1b[2~"; break;
    case VK_DELETE: seq = "\x1b[3~"; break;
    case VK_F1:  seq = "\x1bOP"; break;
    case VK_F2:  seq = "\x1bOQ"; break;
    case VK_F3:  seq = "\x1bOR"; break;
    case VK_F4:  seq = "\x1bOS"; break;
    case VK_F5:  seq = "\x1b[15~"; break;
    case VK_F6:  seq = "\x1b[17~"; break;
    case VK_F7:  seq = "\x1b[18~"; break;
    case VK_F8:  seq = "\x1b[19~"; break;
    case VK_F9:  seq = "\x1b[20~"; break;
    case VK_F10: seq = "\x1b[21~"; break;
    case VK_F11: seq = "\x1b[23~"; break;
    case VK_F12: seq = "\x1b[24~"; break;
    default: return false;  // let WM_CHAR handle character keys
    }
    sendToActive(seq, std::char_traits<char>::length(seq));
    return true;
}

void Window::onMouseDown(int xi, int yi) {
    const float x = static_cast<float>(xi), y = static_cast<float>(yi);
    Rect sidebar, tabBar, panes;
    regions(sidebar, tabBar, panes);

    if (sidebarVisible_ && sidebar.contains(x, y)) {
        for (const SidebarRow& row : sidebarRows_) {
            if (!row.rect.contains(x, y)) continue;
            switch (row.kind) {
            case RowKind::RepoHeader: {
                auto& repos = workspace_.repos();
                if (row.repo < 0 || row.repo >= static_cast<int>(repos.size())) return;
                Repo& repo = repos[row.repo];
                repo.expanded = !repo.expanded;
                if (repo.expanded) workspace_.loadWorktrees(repo);
                break;
            }
            case RowKind::Worktree: {
                auto& repos = workspace_.repos();
                if (row.repo < 0 || row.repo >= static_cast<int>(repos.size())) return;
                Repo& repo = repos[row.repo];
                if (row.worktree < static_cast<int>(repo.worktrees.size()))
                    newTab(repo.worktrees[row.worktree].path);
                break;
            }
            case RowKind::FileUp: {
                size_t s = browsePath_.find_last_of(L"\\/");
                if (s != std::wstring::npos) browsePath_ = browsePath_.substr(0, s);
                break;
            }
            case RowKind::FileDir:
                browsePath_ = row.path;  // navigate the panel into the directory
                break;
            case RowKind::FileEntry: {
                // Insert the filename into the focused pane (quote if needed).
                std::wstring name = row.path;
                size_t s = name.find_last_of(L"\\/");
                if (s != std::wstring::npos) name = name.substr(s + 1);
                std::wstring ins = name.find(L' ') != std::wstring::npos
                                       ? L"\"" + name + L"\" "
                                       : name + L" ";
                sendUtf16(ins.c_str(), ins.size());
                break;
            }
            case RowKind::SshHost:
                if (row.repo >= 0 && row.repo < static_cast<int>(sshHosts_.size()))
                    newTabShell(L"ssh " + sshHosts_[row.repo], L"");
                break;
            case RowKind::Agent:
                if (row.repo >= 0 && row.repo < static_cast<int>(agents_.size()))
                    newTabShell(agents_[row.repo].command, agents_[row.repo].cwd);
                break;
            }
            return;
        }
        return;
    }

    if (tabBar.contains(x, y)) {
        if (plusRect_.contains(x, y)) {
            newTab(activeSession() ? activeSession()->cwd() : workspace_.root());
            return;
        }
        for (size_t i = 0; i < tabRects_.size(); ++i) {
            if (tabRects_[i].contains(x, y)) {
                clearSelection();
                activeTab_ = i;
                tabDragIndex_ = static_cast<int>(i);  // start a potential reorder
                SetCapture(hwnd_);
                updateTitle();
                return;
            }
        }
        return;
    }

    if (panes.contains(x, y)) {
        Tab* t = activeTab();
        if (!t) return;
        // A click near a split divider starts a resize drag instead of a select.
        if (Pane* divider = t->splitDividerAt(x, y, 4.0f)) {
            dragDivider_ = divider;
            SetCapture(hwnd_);
            return;
        }
        Pane* leaf = t->hitTest(x, y);
        if (!leaf) return;
        t->setActive(leaf);
        // Begin a selection drag from this cell (a plain click selects nothing
        // until the mouse moves).
        int cx = 0, cy = 0;
        if (paneCellAt(leaf, xi, yi, cx, cy)) {
            selecting_ = true;
            selPane_ = leaf;
            selAX_ = selBX_ = cx;
            selAY_ = selBY_ = cy;
            hasSelection_ = false;
            SetCapture(hwnd_);
        }
    }
}

void Window::onMouseDownRight(int xi, int yi) {
    const float x = static_cast<float>(xi), y = static_cast<float>(yi);
    Rect sidebar, tabBar, panes;
    regions(sidebar, tabBar, panes);
    if (!sidebarVisible_ || !sidebar.contains(x, y)) return;

    for (const SidebarRow& row : sidebarRows_) {
        if (!row.rect.contains(x, y)) continue;
        auto& repos = workspace_.repos();
        if (row.repo < 0 || row.repo >= static_cast<int>(repos.size())) return;
        Repo& repo = repos[row.repo];

        if (row.worktree < 0) {
            // Repo header: create a new worktree (prompt for a branch name).
            std::wstring name = inputBox(hwnd_, L"New worktree",
                                         L"New branch / worktree name:", L"");
            if (name.empty()) return;
            std::wstring path = workspace_.addWorktree(repo, name);
            if (!path.empty()) newTab(path);  // open a terminal in it
            else MessageBoxW(hwnd_, L"git worktree add failed.", L"liney-win",
                             MB_OK | MB_ICONERROR);
        } else if (row.worktree < static_cast<int>(repo.worktrees.size())) {
            // Worktree row: confirm and remove.
            const Worktree& wt = repo.worktrees[row.worktree];
            std::wstring msg = L"Remove worktree?\n\n" + wt.path;
            if (MessageBoxW(hwnd_, msg.c_str(), L"Remove worktree",
                            MB_YESNO | MB_ICONWARNING) == IDYES) {
                if (!workspace_.removeWorktree(repo, wt.path))
                    MessageBoxW(hwnd_,
                                L"git worktree remove failed (the main worktree "
                                L"can't be removed).",
                                L"liney-win", MB_OK | MB_ICONERROR);
            }
        }
        return;
    }
}

void Window::onMouseMove(int xi, int yi) {
    if (tabDragIndex_ >= 0) {
        const float x = static_cast<float>(xi), y = static_cast<float>(yi);
        for (size_t i = 0; i < tabRects_.size(); ++i) {
            if (static_cast<int>(i) == tabDragIndex_) continue;
            if (!tabRects_[i].contains(x, y)) continue;
            // Move the dragged tab to position i, keeping it active.
            auto moved = std::move(tabs_[tabDragIndex_]);
            tabs_.erase(tabs_.begin() + tabDragIndex_);
            tabs_.insert(tabs_.begin() + i, std::move(moved));
            tabDragIndex_ = static_cast<int>(i);
            activeTab_ = i;
            break;
        }
        return;
    }
    if (dragDivider_) {
        Pane* s = dragDivider_;
        const float x = static_cast<float>(xi), y = static_cast<float>(yi);
        float r = s->ratio;
        if (s->dir == SplitDir::Cols && s->rect.w > 1.0f)
            r = (x - s->rect.x) / s->rect.w;
        else if (s->dir == SplitDir::Rows && s->rect.h > 1.0f)
            r = (y - s->rect.y) / s->rect.h;
        s->ratio = r < 0.05f ? 0.05f : (r > 0.95f ? 0.95f : r);
        return;
    }
    if (!selecting_ || !selPane_) return;
    int cx = 0, cy = 0;
    if (!paneCellAt(selPane_, xi, yi, cx, cy)) return;
    selBX_ = cx;
    selBY_ = cy;
    if (selBX_ != selAX_ || selBY_ != selAY_) hasSelection_ = true;
}

void Window::onMouseUp(int /*xi*/, int /*yi*/) {
    if (tabDragIndex_ >= 0) {
        tabDragIndex_ = -1;
        ReleaseCapture();
        return;
    }
    if (dragDivider_) {
        dragDivider_ = nullptr;
        ReleaseCapture();
        return;
    }
    if (selecting_) {
        selecting_ = false;
        ReleaseCapture();
    }
}

bool Window::paneCellAt(const Pane* p, int px, int py, int& cx, int& cy) const {
    if (!p || !p->session) return false;
    const Grid& g = p->session->grid();
    if (g.cols < 1 || g.rows < 1) return false;
    int x = static_cast<int>((px - p->rect.x) / metrics_.cellW);
    int y = static_cast<int>((py - p->rect.y) / metrics_.cellH);
    cx = x < 0 ? 0 : (x >= g.cols ? g.cols - 1 : x);
    cy = y < 0 ? 0 : (y >= g.rows ? g.rows - 1 : y);
    return true;
}

void Window::clearSelection() {
    selecting_ = false;
    hasSelection_ = false;
    selPane_ = nullptr;
    dragDivider_ = nullptr;
    tabDragIndex_ = -1;
}

void Window::applySelectionToGrid() {
    if (!hasSelection_ || !selPane_ || !selPane_->session) return;
    int sx = selAX_, sy = selAY_, ex = selBX_, ey = selBY_;
    if (sy > ey || (sy == ey && sx > ex)) { std::swap(sx, ex); std::swap(sy, ey); }
    Grid& g = selPane_->session->grid();
    g.hasSelection = true;
    g.selStartX = sx; g.selStartY = sy;
    g.selEndX = ex; g.selEndY = ey;
}

std::wstring Window::selectionText() const {
    if (!hasSelection_ || !selPane_ || !selPane_->session) return L"";
    const Grid& g = selPane_->session->grid();
    int sx = selAX_, sy = selAY_, ex = selBX_, ey = selBY_;
    if (sy > ey || (sy == ey && sx > ex)) { std::swap(sx, ex); std::swap(sy, ey); }

    std::wstring out;
    for (int y = sy; y <= ey && y < g.rows; ++y) {
        const int x0 = (y == sy) ? sx : 0;
        const int x1 = (y == ey) ? ex : g.cols - 1;
        std::wstring line;
        for (int x = x0; x <= x1 && x < g.cols; ++x) {
            const Cell& c = g.at(x, y);
            line += c.ch.empty() ? L" " : c.ch;
        }
        size_t last = line.find_last_not_of(L' ');
        if (last == std::wstring::npos) line.clear();
        else line.erase(last + 1);
        out += line;
        if (y < ey) out += L"\r\n";
    }
    return out;
}

void Window::copySelection() {
    const std::wstring text = selectionText();
    if (text.empty() || !OpenClipboard(hwnd_)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        } else {
            GlobalFree(h);
        }
    }
    CloseClipboard();
}

void Window::paste() {
    auto* s = activeSession();
    if (!s || !OpenClipboard(hwnd_)) return;
    std::wstring text;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            text = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    if (text.empty()) return;

    // Normalize CRLF / LF to CR (what shells expect from "Enter").
    std::wstring norm;
    norm.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (c == L'\r') {
            norm.push_back(L'\r');
            if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
        } else if (c == L'\n') {
            norm.push_back(L'\r');
        } else {
            norm.push_back(c);
        }
    }

    int bytes = WideCharToMultiByte(CP_UTF8, 0, norm.data(),
                                    static_cast<int>(norm.size()), nullptr, 0,
                                    nullptr, nullptr);
    if (bytes <= 0) return;
    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, norm.data(), static_cast<int>(norm.size()),
                        utf8.data(), bytes, nullptr, nullptr);

    s->scrollToBottom();
    if (s->bracketedPaste()) {
        const std::string out = "\x1b[200~" + utf8 + "\x1b[201~";
        s->sendBytes(out.data(), out.size());
    } else {
        s->sendBytes(utf8.data(), utf8.size());
    }
}

} // namespace liney
