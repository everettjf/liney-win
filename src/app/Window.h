#pragma once

#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "app/Layout.h"
#include "app/Tab.h"
#include "core/Config.h"
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
    void regions(Rect& leftBar, Rect& rightPanel, Rect& tabBar, Rect& panes) const;
    void renderFrame();
    void drawLeftSidebar(const Rect& r);   // WORKSPACE / SSH / AGENTS
    void drawFilesPanel(const Rect& r);    // FILES (folder tree, right side)
    std::wstring resolveRepoIcon(const Repo& repo) const;  // config or repo-local

    // Workspace management (sidebar).
    void rescanWorkspace();          // scan root + re-add explicit projects_
    void addWorkspaceFolder();       // pick a folder, add as a project, persist
    void removeProject(const Repo& repo);   // drop a project, persist
    void setProjectIcon(const Repo& repo);  // pick an icon for a project, persist
    void persistWorkspaceConfig();   // write projects_ + projectIcons_ to config
    void drawTabBar(const Rect& r);
    void drawPanes(const Rect& r);
    void reapExitedPanes();

    // Workspace / tabs.
    Tab* activeTab() const;
    TerminalSession* activeSession() const;
    void cellsForRect(const Rect& r, int& cols, int& rows) const;
    void newTab(const std::wstring& cwd);
    void newTabShell(const std::wstring& shellCmd, const std::wstring& cwd);
    void splitActive(SplitDir dir);
    void runStartHook(TerminalSession* s);  // send sessionStart hook to a shell
    void closeActivePane();
    void switchTab(int delta);
    void updateTitle();

    // Font sizing.
    void applyFont();        // push current family/size to the renderer + metrics
    void zoomFont(int step); // step font size by `step` points (0 == reset)

    // Top-right menu + quick actions.
    void toggleKeepAwake();  // prevent/allow system+display sleep (caffeine)
    void openConfigFile();   // open %USERPROFILE%\.liney\config.json in the editor
    void openMainMenu();     // native popup menu for the top-right "☰" button

    // Layout persistence (%USERPROFILE%\.liney\layout.json).
    void saveLayout() const;
    bool restoreLayout();    // returns true if at least one tab was restored
    std::unique_ptr<Pane> paneFromJson(const class Json& j, int cols, int rows);

    // Tray icon + balloon notifications (driven by OSC 9/777).
    void initTray();
    void showBalloon(const std::wstring& title, const std::wstring& body);
    void removeTray();
    void pollNotifications();  // drain OSC notifications from all sessions

    // Update check + auto-update (Sparkle analog): query GitHub releases
    // off-thread; on confirmation download the installer asset and run it.
    void checkForUpdates();
    void pollUpdateResult();
    void startDownloadAndInstall(const std::wstring& url);

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

    // IME: keep the composition/candidate window at the cursor.
    void positionIme();
    void cursorPixelPos(int& px, int& py) const;

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
    bool sidebarVisible_ = true;      // left WORKSPACE/SSH/AGENTS panel
    bool filesPanelVisible_ = false;  // right FILES (folder tree) panel (Ctrl+Shift+F)
    bool keepAwake_ = false;          // SetThreadExecutionState keep-awake state

    std::wstring shell_ = L"cmd.exe";
    std::wstring fontFamily_ = L"Cascadia Mono";
    float fontSize_ = 16.0f;
    float defaultFontSize_ = 16.0f;
    std::wstring sessionStartHook_; // command sent to each newly started shell
    std::wstring sessionExitHook_;  // command run when a pane closes
    std::wstring appExitHook_;      // command run on app quit
    std::vector<std::wstring> sshHosts_;
    std::vector<AgentDef> agents_;
    std::vector<std::pair<std::wstring, std::wstring>> projectIcons_;
    std::vector<std::wstring> projects_;   // explicit sidebar project folders
    std::wstring workspaceRoot_;           // scanned root (empty = launch parent)
    std::wstring launchParent_;            // parent of the launch dir (default root)
    Theme theme_;
    std::wstring lastTitle_;        // avoid redundant SetWindowText calls
    NOTIFYICONDATAW nid_{};
    bool trayAdded_ = false;
    std::atomic<bool> updateReady_{ false };
    std::atomic<bool> installerReady_{ false };
    std::mutex updateMutex_;
    std::wstring updateMsg_;
    std::wstring downloadUrl_;     // installer asset URL when an update is found
    std::wstring installerPath_;   // downloaded installer path
    bool pendingUpdate_ = false;   // an update is available to install
    wchar_t pendingHighSurrogate_ = 0;
    bool swallowNextChar_ = false;  // drop the WM_CHAR following a shortcut

    // Hit-test rects rebuilt each frame.
    enum class RowKind { RepoHeader, Worktree, FileUp, FileDir, FileEntry, SshHost, Agent };
    struct SidebarRow {
        Rect rect;
        RowKind kind = RowKind::RepoHeader;
        int repo = -1;
        int worktree = -1;
        std::wstring path;  // for file rows
    };
    std::vector<SidebarRow> sidebarRows_;
    std::vector<Rect> tabRects_;
    Rect workspaceAddRect_{};  // the WORKSPACE "+" (add project) button
    Rect plusRect_{};
    int tabDragIndex_ = -1;  // tab being dragged in the strip (-1 = none)

    // Top-right "☰" menu button (rebuilt each frame in drawTabBar).
    Rect menuButtonRect_{};

    // FILES panel: a navigable listing that follows the focused pane's cwd.
    void refreshFileList();   // re-list browsePath_ when it changes
    std::wstring browsePath_;
    std::wstring lastActiveCwd_;
    std::wstring listedDir_;
    struct FileEntry { std::wstring name; std::wstring path; bool isDir; };
    std::vector<FileEntry> fileEntries_;

    // Selection (over the focused pane's viewport, in that pane's cell coords).
    bool selecting_ = false;       // a text-selection drag is in progress
    Pane* dragDivider_ = nullptr;  // split node being resized by a divider drag
    bool hasSelection_ = false;
    Pane* selPane_ = nullptr;      // pane the selection belongs to
    int selAX_ = 0, selAY_ = 0;    // anchor
    int selBX_ = 0, selBY_ = 0;    // head
};

} // namespace liney
