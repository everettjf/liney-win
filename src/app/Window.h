#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "app/Layout.h"
#include "app/Tab.h"
#include "render/IRenderer.h"
#include "workspace/Workspace.h"

namespace liney {

// Top-level Win32 window and workspace shell. Composes a self-drawn UI:
//   [ sidebar (repos/worktrees) ] [ tab strip            ]
//                                 [ pane tree of terminals ]
// Keyboard goes to the focused pane's shell; app shortcuts manage tabs/splits/
// focus/sidebar; mouse switches tabs, focuses panes, and opens worktrees.
class Window {
public:
    Window();
    ~Window();

    bool create(HINSTANCE hInstance, const wchar_t* title, int width, int height);
    void show(int nCmdShow);
    int runMessageLoop();

private:
    static LRESULT CALLBACK wndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndProc(UINT msg, WPARAM wParam, LPARAM lParam);

    // Layout / rendering.
    void regions(Rect& sidebar, Rect& tabBar, Rect& panes) const;
    void renderFrame();
    void drawSidebar(const Rect& r);
    void drawTabBar(const Rect& r);
    void drawPanes(const Rect& r);
    void reapExitedPanes();

    // Workspace / tabs.
    Tab* activeTab() const;
    TerminalSession* activeSession() const;
    void cellsForRect(const Rect& r, int& cols, int& rows) const;
    void newTab(const std::wstring& cwd);
    void splitActive(SplitDir dir);
    void closeActivePane();
    void switchTab(int delta);
    void updateTitle();

    // Input.
    void onChar(wchar_t unit);
    bool onKeyDown(WPARAM vk);
    void onMouseDown(int x, int y);
    void sendToActive(const char* data, size_t len);
    void sendUtf16(const wchar_t* s, size_t len);

    HWND hwnd_ = nullptr;
    std::unique_ptr<IRenderer> renderer_;
    Metrics metrics_;
    Workspace workspace_;

    std::vector<std::unique_ptr<Tab>> tabs_;
    size_t activeTab_ = 0;
    bool sidebarVisible_ = true;

    std::wstring shell_ = L"cmd.exe";
    wchar_t pendingHighSurrogate_ = 0;
    bool swallowNextChar_ = false;  // drop the WM_CHAR following a shortcut

    // Hit-test rects rebuilt each frame.
    struct SidebarRow { Rect rect; int repo; int worktree; };  // worktree<0 => header
    std::vector<SidebarRow> sidebarRows_;
    std::vector<Rect> tabRects_;
    Rect plusRect_{};
};

} // namespace liney
