#pragma once

#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/Layout.h"
#include "app/Tab.h"
#include "core/Config.h"
#include "core/Ai.h"
#include "core/ShellProfiles.h"
#include "render/IRenderer.h"
#include "workspace/Workspace.h"

struct IRawElementProviderSimple;

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
    TerminalSession* newTabShell(const std::wstring& shellCmd, const std::wstring& cwd);
    void splitActive(SplitDir dir);
    void toggleZoom();     // Ctrl+Shift+Z: maximize/restore the active pane
    void equalizePanes();  // reset all split ratios evenly
    void closeOtherPanes();  // collapse the tab to just the active pane
    void runStartHook(TerminalSession* s);  // send sessionStart hook to a shell
    // The "default" starting directory: the first workspace repo when the
    // sidebar has one, else the user's home. Used for the split-in-workspace
    // option.
    std::wstring defaultStartDir();
    void closeActivePane();
    void closeActivePaneConfirming();  // Ctrl+Shift+W: prompt if pane is busy
    void switchTab(int delta);
    void updateTitle();

    // Font sizing.
    void applyFont();        // push current family/size to the renderer + metrics
    void zoomFont(int step); // step font size by `step` points (0 == reset)
    void chooseFontDialog(); // native font picker (fixed-pitch); persists config

    // Top-right menu + quick actions.
    // Keep awake (caffeine): block system+display sleep for a fixed duration
    // or until turned off. hours: 0 = off, -1 = until turned off (forever).
    // Mirrors the duration-preset pattern of PowerToys Awake / Amphetamine.
    void setKeepAwake(int hours);
    void toggleKeepAwake();  // Ctrl+Shift+K: forever <-> off
    std::wstring keepAwakeStatus() const;  // e.g. "on — 1h 12m left" for menus
    void openConfigFile();   // open %USERPROFILE%\.liney\config.json in the editor
    void openSettingsDialog();  // GUI settings; applies + persists on OK
    // Switch to a theme preset (by name) with an accent override, live across
    // all panes. Used by the Settings dialog.
    void applyTheme(const std::wstring& presetName, const Color& accent);
    void applyHighContrastIfEnabled();
    void openMainMenu();              // overflow menu (top-right three dots)
    void openDirectoryMenu();         // app picker for the active pane cwd
    void openCurrentDirectory(UINT);  // 30 Explorer, 31 PowerShell, 32.. editors
    void openNewWindow(bool elevated);
    void openDiagnosticsFolder();
    void copyDiagnosticSummary();
    void exportDiagnostics();
    void searchHistory();
    void restartSession(TerminalSession* session);
    void openKeepAwakeMenu();          // duration picker beside the coffee button
    void openTabMenu(int x, int y);  // right-click a tab: close / open in explorer…
    void closeTab(size_t idx);       // close an entire tab (all its panes)
    // Close a tab, but confirm first if any of its shells is running a command
    // (a child process is alive). Used by the tab × button and the tab menu.
    void closeTabConfirming(size_t idx);
    // Close a set of tabs at once (right-click menu: right / left / others /
    // all). `keep` is the tab to re-focus afterward, or nullptr to allow
    // closing everything. Confirms once if any of them is running a command.
    void closeTabSet(const std::vector<size_t>& victims, Tab* keep);
    bool tabHasRunningProcess(size_t idx) const;
    // Titles of the given tabs that are running a command, and a single
    // consolidated Yes/No dialog listing them (returns true to proceed / when
    // nothing is running). Used by every multi-tab and app-quit close path so
    // there's one dialog with a list instead of one dialog per tab.
    std::vector<std::wstring> runningTabTitles(const std::vector<size_t>& idxs) const;
    bool confirmCloseRunning(const std::vector<std::wstring>& titles,
                             const std::wstring& prompt);

    // Layout persistence (%USERPROFILE%\.liney\layout.json).
    void saveLayout() const;
    bool saveWorkspaceSnapshot(const std::wstring& name) const;
    void openWorkspaceSnapshotMenu();
    bool restoreLayout();    // returns true if at least one tab was restored
    bool restoreLayoutFrom(const std::wstring& path);
    bool writeLayoutTo(const std::wstring& path) const;
    std::unique_ptr<Pane> paneFromJson(const class Json& j, int cols, int rows);

    // Tray icon + balloon notifications (driven by OSC 9/777).
    void initTray();
    void showBalloon(const std::wstring& title, const std::wstring& body);
    void removeTray();
    void pollNotifications();  // drain OSC notifications from all sessions
    void pollClipboardRequests();

    // Update check + auto-update (Sparkle analog): query GitHub releases
    // off-thread; on confirmation download the installer asset and run it.
    void checkForUpdates(bool quiet = false);
    void pollUpdateResult();
    void startDownloadAndInstall(const std::wstring& url,
                                 const std::string& sha256);
    void requestAiForLastCommand(TerminalSession* session);
    void pollAiResult();

    // Input.
    void onChar(wchar_t unit);
    bool onKeyDown(WPARAM vk);
    void onMouseDown(int x, int y);
    void onMouseDownRight(int x, int y);   // sidebar worktree create/remove
    void onMouseDoubleClick(int x, int y); // word (double) / line (triple) select
    void onMouseMove(int x, int y);
    void onMouseUp(int x, int y);
    void onWheel(int delta, int x, int y);
    bool updateCursor();      // WM_SETCURSOR: I-beam over text, resize over dividers
    void openPaneMenu(int x, int y);  // right-click in a pane: copy/paste/find…
    void scrollActive(int lines);
    int activePaneRows() const;
    void sendToActive(const char* data, size_t len);
    void sendUtf16(const wchar_t* s, size_t len);

    // IME: keep the composition/candidate window at the cursor.
    void positionIme();
    void cursorPixelPos(int& px, int& py) const;

    // Selection / clipboard. The selection itself lives in the terminal core
    // (buffer-anchored, so it stays on its text through scrolling and reflow);
    // the Window tracks which pane owns it and the drag gesture.
    bool paneCellAt(const Pane* p, int px, int py, int& cx, int& cy) const;
    void clearSelection();         // drop selection (e.g. before freeing panes)
    bool paneHasSelection() const; // does selPane_'s terminal hold a selection?
    std::wstring selectionText() const;
    void copySelection();
    void setClipboardText(const std::wstring& text);
    void paste();
    void selectWordAt(Pane* p, int cx, int cy);   // double-click word selection
    void selectLineAt(Pane* p, int cy);           // triple-click line selection
    void selectAllActive();                       // Ctrl+Shift+A
    void maybeCopyOnSelect();                      // copy if copyOnSelect_ is set

    // Mouse reporting: forward one event to the pane under the pointer when
    // its app enabled tracking (holding Shift bypasses, keeping local
    // selection available). Returns true when the event was consumed.
    // action: 0 press, 1 release, 2 motion; button: 0 none, 1 left, 2 right,
    // 3 middle, 4 wheel-up, 5 wheel-down.
    bool forwardMouse(int action, int button, int xi, int yi);

    // Find-on-screen (Ctrl+F): highlights query matches in the focused pane's
    // viewport; navigate with Enter/F3 (and Shift to reverse), Esc closes. See
    // WindowFind.cpp.
    void openFind();
    void closeFind();
    void onFindChar(wchar_t c);    // edit the query (printable / Enter / Backspace)
    void findBackspace();
    void findNext(bool newer);     // Enter/F3 = older (up); +Shift = newer (down)
    void findJumpGlobal(bool up);  // search the whole scrollback, jump viewport
    void stampFindMatches();       // recompute matches for the active pane
    void drawFindBar(const Rect& paneRect);
    std::wstring rowText(const Grid& g, int y, std::vector<int>* colOfPos) const;

    // Searchable action surface (Ctrl+Shift+P).
    void openCommandPalette();
    void closeCommandPalette();
    void drawCommandPalette();
    void onPaletteChar(wchar_t c);
    bool onPaletteKey(WPARAM key);
    void executePaletteAction(int id);
    std::vector<int> filteredPaletteActions() const;
    bool executeConfiguredBinding(int virtualKey, bool ctrl, bool shift, bool alt);

    HWND hwnd_ = nullptr;
    ::IRawElementProviderSimple* accessibilityProvider_ = nullptr;
    std::unique_ptr<IRenderer> renderer_;
    Metrics metrics_;
    Workspace workspace_;

    std::vector<std::unique_ptr<Tab>> tabs_;
    size_t activeTab_ = 0;
    bool sidebarVisible_ = true;      // left WORKSPACE/SSH/AGENTS panel
    bool filesPanelVisible_ = false;  // right FILES (folder tree) panel (Ctrl+Shift+F)
    bool keepAwake_ = false;          // SetThreadExecutionState keep-awake state
    int keepAwakeHours_ = 0;          // active preset (-1 forever, 0 off, else hours)
    ULONGLONG keepAwakeUntil_ = 0;    // GetTickCount64 deadline (0 = no deadline)
    bool pendingMaximize_ = false;    // restore a maximized window on first show

    std::wstring shell_ = L"cmd.exe";
    std::wstring fontFamily_ = L"Cascadia Mono";
    float fontSize_ = 16.0f;          // logical (DPI-independent) point size
    float defaultFontSize_ = 16.0f;
    float dpiScale_ = 1.0f;           // device px per logical px (monitor DPI / 96)
    int scrollback_ = 10000;          // history lines retained per session
    std::wstring sessionStartHook_; // command sent to each newly started shell
    std::wstring sessionExitHook_;  // command run when a pane closes
    std::wstring appExitHook_;      // command run on app quit
    std::vector<SshProfile> sshHosts_;
    std::vector<ShellProfile> shellProfiles_;
    std::vector<KeyBinding> keybindings_;
    std::vector<AgentDef> agents_;
    std::vector<std::pair<std::wstring, std::wstring>> projectIcons_;
    std::vector<std::wstring> projects_;   // explicit sidebar project folders
    std::wstring workspaceRoot_;           // scanned root (empty = explicit only)
    Theme theme_;                  // terminal palette
    UiTheme uiTheme_;              // chrome palette (sidebar/tabs/accent/border)
    std::wstring themeName_;       // active preset name (persisted)
    std::wstring lastTitle_;        // avoid redundant SetWindowText calls
    NOTIFYICONDATAW nid_{};
    bool trayAdded_ = false;
    std::atomic<bool> updateReady_{ false };
    std::atomic<bool> installerReady_{ false };
    std::mutex updateMutex_;
    // Update-check / download workers. Joined in ~Window — a detached thread
    // capturing `this` would write into a freed Window if the app exits while
    // a check is in flight (the HTTP layer carries timeouts, so the join is
    // bounded).
    std::vector<std::thread> updateThreads_;
    std::wstring updateMsg_;
    std::wstring downloadUrl_;     // installer asset URL when an update is found
    std::string downloadSha256_;   // GitHub-provided digest for that exact asset
    std::wstring installerPath_;   // downloaded installer path
    bool pendingUpdate_ = false;   // an update is available to install
    bool checkForUpdatesOnStartup_ = true;
    std::wstring aiProvider_ = L"off";
    std::wstring aiModel_ = L"gpt-5.6-luna";
    std::wstring aiEndpoint_;
    bool aiIncludeCwd_ = false;
    std::atomic<bool> aiBusy_{ false };
    std::atomic<bool> aiReady_{ false };
    std::mutex aiMutex_;
    AiAnswer aiAnswer_;
    std::wstring aiRequestedCwd_;
    wchar_t pendingHighSurrogate_ = 0;
    bool swallowNextChar_ = false;  // drop the WM_CHAR following a shortcut

    // Hit-test rects rebuilt each frame.
    enum class RowKind { RepoHeader, Worktree, FileUp, FileDir, FileEntry,
                         SshHost, Agent };
    struct SidebarRow {
        Rect rect;
        RowKind kind = RowKind::RepoHeader;
        int repo = -1;
        int worktree = -1;
        std::wstring path;  // for file rows
    };
    std::vector<SidebarRow> sidebarRows_;
    std::vector<Rect> tabRects_;
    std::vector<Rect> tabCloseRects_;  // per-tab × button hit rects
    int hoverTab_ = -1;        // tab under the pointer (-1 = none); shows its ×
    int lastMouseX_ = -1, lastMouseY_ = -1;  // client-space pointer, for hover
    Rect workspaceAddRect_{};  // the WORKSPACE "+" (add project) button
    Rect sidebarToggleRect_{}; // tab-strip button; remains visible when collapsed
    Rect plusRect_{};
    int tabDragIndex_ = -1;  // tab being dragged in the strip (-1 = none)

    // Top-right icon menu buttons (rebuilt each frame in drawTabBar).
    Rect openButtonRect_{};
    Rect awakeButtonRect_{};
    Rect menuButtonRect_{};

    // FILES panel: a navigable listing that follows the focused pane's cwd.
    void refreshFileList();   // re-list browsePath_ when it changes
    std::wstring browsePath_;
    std::wstring lastActiveCwd_;
    std::wstring listedDir_;
    struct FileEntry { std::wstring name; std::wstring path; bool isDir; };
    std::vector<FileEntry> fileEntries_;

    // Selection gesture state (the selection itself is terminal-owned).
    bool selecting_ = false;       // a text-selection drag is in progress
    Pane* dragDivider_ = nullptr;  // split node being resized by a divider drag
    Pane* selPane_ = nullptr;      // pane whose terminal owns the selection
    int selDragCX_ = -1, selDragCY_ = -1;  // press cell: drags start on leaving it
    bool selDragged_ = false;      // the drag left its press cell at least once
    bool copyOnSelect_ = false;    // copy to clipboard as soon as a selection ends
    bool multiLinePasteWarning_ = true;  // confirm before pasting multiple lines
    bool rememberLayout_ = false;  // restore tabs/panes on launch (opt-in)
    bool splitUseWorkspaceDir_ = false;  // splits open in workspace/home dir vs inherit
    Osc52Policy osc52Clipboard_ = Osc52Policy::Ask;
    bool unixToolsEnabled_ = true; // Git's usr/bin appended to shells' PATH
    int mouseButtonsDown_ = 0;     // forwarded-to-app buttons, bitmask by number

    // Double / triple-click tracking (for word / line selection).
    DWORD lastClickTick_ = 0;
    int lastClickCY_ = -1;
    int clickStreak_ = 0;          // 1 = single, 2 = double (word), 3 = triple (line)

    // Find state. Matches are in the active pane's viewport coords; when a
    // jump into scrollback is pending, findSeekRow_ holds the absolute row to
    // re-seed the active match on after the viewport lands there.
    bool findActive_ = false;
    std::wstring findQuery_;
    std::vector<Grid::FindSpan> findMatches_;
    int findIndex_ = -1;           // active match index into findMatches_ (-1 none)
    long long findSeekRow_ = -1;   // absolute row of a pending scrollback jump
    Rect findBarRect_{};           // find bar hit rect (rebuilt each frame)
    bool paletteActive_ = false;
    std::wstring paletteQuery_;
    size_t paletteSelected_ = 0;
};

} // namespace liney
