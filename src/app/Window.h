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

    // Font sizing.
    void applyFont();        // push current family/size to the renderer + metrics
    void zoomFont(int step); // step font size by `step` points (0 == reset)

    // Layout persistence (%USERPROFILE%\.liney\layout.json).
    void saveLayout() const;
    bool restoreLayout();    // returns true if at least one tab was restored
    std::unique_ptr<Pane> paneFromJson(const class Json& j, int cols, int rows);

    // Input.
    void onChar(wchar_t unit);
    bool onKeyDown(WPARAM vk);
    void onMouseDown(int x, int y);
    void onMouseDownRight(int x, int y);   // sidebar worktree create/remove
    void onMouseMove(int x, int y);
    void onMouseUp(int x, int y);
    void onWheel(int delta);
    void scrollActive(int lines);
    int activePaneRows() const;
    void sendToActive(const char* data, size_t len);
    void sendUtf16(const wchar_t* s, size_t len);

    // Selection / clipboard.
    bool paneCellAt(const Pane* p, int px, int py, int& cx, int& cy) const;
    void applySelectionToGrid();   // push current selection onto the active grid
    void clearSelection();         // drop selection (e.g. before freeing panes)
    std::wstring selectionText() const;
    void copySelection();
    void paste();

    HWND hwnd_ = nullptr;
    std::unique_ptr<IRenderer> renderer_;
    Metrics metrics_;
    Workspace workspace_;

    std::vector<std::unique_ptr<Tab>> tabs_;
    size_t activeTab_ = 0;
    bool sidebarVisible_ = true;

    std::wstring shell_ = L"cmd.exe";
    std::wstring fontFamily_ = L"Cascadia Mono";
    float fontSize_ = 16.0f;
    float defaultFontSize_ = 16.0f;
    wchar_t pendingHighSurrogate_ = 0;
    bool swallowNextChar_ = false;  // drop the WM_CHAR following a shortcut

    // Hit-test rects rebuilt each frame.
    struct SidebarRow { Rect rect; int repo; int worktree; };  // worktree<0 => header
    std::vector<SidebarRow> sidebarRows_;
    std::vector<Rect> tabRects_;
    Rect plusRect_{};

    // Selection (over the focused pane's viewport, in that pane's cell coords).
    bool selecting_ = false;       // a text-selection drag is in progress
    Pane* dragDivider_ = nullptr;  // split node being resized by a divider drag
    bool hasSelection_ = false;
    Pane* selPane_ = nullptr;      // pane the selection belongs to
    int selAX_ = 0, selAY_ = 0;    // anchor
    int selBX_ = 0, selBY_ = 0;    // head
};

} // namespace liney
