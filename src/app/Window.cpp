#include "app/Window.h"

#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

#include <string>

#include "render/D2DRenderer.h"

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

    unsigned cw = 0, ch = 0;
    renderer_->cellSize(cw, ch);
    metrics_.cellW = cw ? static_cast<float>(cw) : 8.0f;
    metrics_.cellH = ch ? static_cast<float>(ch) : 16.0f;

    // Workspace root = parent of the launch directory, so sibling repos show up.
    wchar_t cwd[MAX_PATH]{};
    GetCurrentDirectoryW(MAX_PATH, cwd);
    std::wstring startCwd = cwd;
    workspace_.scan(parentDir(startCwd));

    newTab(startCwd);
    if (tabs_.empty()) return false;  // shell failed to launch
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
            if (msg.message == WM_QUIT) return static_cast<int>(msg.wParam);
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
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (onKeyDown(wParam)) return 0;
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_LBUTTONDOWN:
        SetFocus(hwnd_);
        onMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_DESTROY:
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
        return;
    }

    for (int i = 0; i < static_cast<int>(repos.size()); ++i) {
        Repo& repo = repos[i];
        const std::wstring marker = repo.expanded ? L"v " : L"> ";
        renderer_->drawText(marker + repo.name, r.x + pad, y, r.w - pad, rowH,
                            kText, true);
        sidebarRows_.push_back({ { r.x, y, r.w, rowH }, i, -1 });
        y += rowH;

        if (repo.expanded) {
            for (int w = 0; w < static_cast<int>(repo.worktrees.size()); ++w) {
                renderer_->drawText(L"- " + repo.worktrees[w].label,
                                    r.x + pad + metrics_.cellW * 2.0f, y,
                                    r.w - pad, rowH, kDim, false);
                sidebarRows_.push_back({ { r.x, y, r.w, rowH }, i, w });
                y += rowH;
            }
        }
        if (y > r.bottom()) break;
    }
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

void Window::newTab(const std::wstring& cwd) {
    Rect sidebar, tabBar, panes;
    regions(sidebar, tabBar, panes);
    int cols = 80, rows = 24;
    cellsForRect(panes, cols, rows);

    auto session = std::make_unique<TerminalSession>();
    if (!session->start(shell_, cwd, cols, rows)) return;
    tabs_.push_back(std::make_unique<Tab>(std::move(session)));
    activeTab_ = tabs_.size() - 1;
    updateTitle();
}

void Window::splitActive(SplitDir dir) {
    Tab* t = activeTab();
    if (!t || !t->active() || !t->active()->session) return;

    const Pane* a = t->active();
    int cols = 80, rows = 24;
    cellsForRect(a->rect, cols, rows);
    const std::wstring cwd = a->session->cwd();

    auto session = std::make_unique<TerminalSession>();
    if (!session->start(shell_, cwd, cols, rows)) return;
    t->splitActive(dir, std::move(session));
}

void Window::closeActivePane() {
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
    if (tabs_.empty()) return;
    const int n = static_cast<int>(tabs_.size());
    activeTab_ = static_cast<size_t>(((static_cast<int>(activeTab_) + delta) % n + n) % n);
    updateTitle();
}

void Window::updateTitle() {
    Tab* t = activeTab();
    std::wstring title = L"liney-win";
    if (t) title += L" — " + t->title();
    SetWindowTextW(hwnd_, title.c_str());
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void Window::sendToActive(const char* data, size_t len) {
    if (auto* s = activeSession()) s->sendBytes(data, len);
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
        if (shift) {
            switch (vk) {
            case 'T': newTab(activeSession() ? activeSession()->cwd() : workspace_.root()); swallowNextChar_ = true; return true;
            case 'W': closeActivePane(); swallowNextChar_ = true; return true;
            case 'E': splitActive(SplitDir::Cols); swallowNextChar_ = true; return true;  // side by side
            case 'O': splitActive(SplitDir::Rows); swallowNextChar_ = true; return true;  // stacked
            case 'B': sidebarVisible_ = !sidebarVisible_; swallowNextChar_ = true; return true;
            default: break;
            }
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
            auto& repos = workspace_.repos();
            if (row.repo < 0 || row.repo >= static_cast<int>(repos.size())) return;
            Repo& repo = repos[row.repo];
            if (row.worktree < 0) {  // repo header: toggle, load lazily
                repo.expanded = !repo.expanded;
                if (repo.expanded) workspace_.loadWorktrees(repo);
            } else if (row.worktree < static_cast<int>(repo.worktrees.size())) {
                newTab(repo.worktrees[row.worktree].path);  // open terminal there
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
            if (tabRects_[i].contains(x, y)) { activeTab_ = i; updateTitle(); return; }
        }
        return;
    }

    if (panes.contains(x, y)) {
        Tab* t = activeTab();
        if (t) {
            if (Pane* leaf = t->hitTest(x, y)) t->setActive(leaf);
        }
    }
}

} // namespace liney
