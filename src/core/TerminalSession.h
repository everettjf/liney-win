#pragma once

#include <cstdint>
#include <chrono>
#include <string>
#include <vector>
#include <utility>

#include "pty/ConPty.h"
#include "render/Cell.h"
#include "vt/Notification.h"
#include "vt/Terminal.h"

namespace liney {

enum class CommandState { Running, Succeeded, Failed };

struct CommandBlock {
    uint64_t id = 0;
    std::wstring command;
    std::wstring cwd;
    CommandState state = CommandState::Running;
    int exitCode = 0;
    std::chrono::steady_clock::time_point startedAt;
    std::chrono::milliseconds duration{0};
    uint64_t startRow = 0;
    uint64_t endRow = 0;
    bool bookmarked = false;
};

enum class SessionRole { Shell, Ssh, Agent };
enum class AgentActivity { Idle, Running, Waiting, NeedsInput, Done, Failed };

struct SessionContext {
    SessionRole role = SessionRole::Shell;
    std::wstring projectPath;
    std::wstring worktreePath;
    std::wstring taskName;
    std::wstring agentName;
    std::wstring testCommand;
};

// One terminal: a shell (ConPTY) feeding a terminal core (Terminal) whose
// screen is snapshotted into a Grid for rendering. This is the unit a pane
// hosts; tabs/splits compose many of these.
//
// Not movable/copyable (owns a reader thread and a mutex); always heap-owned
// via unique_ptr. terminal_ is declared before pty_ so the reader thread, which
// calls terminal_.write, is joined before terminal_ is destroyed.
class TerminalSession {
public:
    TerminalSession() = default;

    TerminalSession(const TerminalSession&) = delete;
    TerminalSession& operator=(const TerminalSession&) = delete;

    // Start the shell in `cwd` (empty = inherit) at the given cell size.
    // `scrollback` is the max number of history lines to retain.
    bool start(const std::wstring& shell, const std::wstring& cwd, int cols,
               int rows, int scrollback = 10000);

    void sendBytes(const char* data, size_t len);
    void resize(int cols, int rows, int cellWidthPx, int cellHeightPx);
    void setTheme(const Theme& theme) { terminal_.setTheme(theme); }

    // Refresh the renderable snapshot into the session's grid.
    void snapshot();
    const Grid& grid() const { return grid_; }
    Grid& grid() { return grid_; }  // UI stamps selection onto it before drawing

    // Scroll the viewport over scrollback history.
    void scrollViewport(int deltaLines) { terminal_.scrollViewport(deltaLines); }
    void scrollToBottom() { terminal_.scrollToBottom(); }

    // Terminal modes the UI keys off (queried from the core).
    bool bracketedPaste() { return terminal_.bracketedPaste(); }
    bool applicationCursorKeys() { return terminal_.applicationCursorKeys(); }
    bool altScreenActive() { return terminal_.altScreenActive(); }
    std::wstring hyperlinkAt(int vx, int vy) { return terminal_.hyperlinkAt(vx, vy); }

    // Selection (terminal-owned; viewport cell coordinates).
    void selectionBegin(int vx, int vy) { terminal_.selectionBegin(vx, vy); }
    bool selectionDragTo(int vx, int vy) { return terminal_.selectionDragTo(vx, vy); }
    void selectionWord(int vx, int vy) { terminal_.selectionWord(vx, vy); }
    void selectionLine(int vx, int vy) { terminal_.selectionLine(vx, vy); }
    void selectionAll() { terminal_.selectionAll(); }
    void selectionClear() { terminal_.selectionClear(); }
    bool hasSelection() { return terminal_.hasSelection(); }
    std::string selectionUtf8() { return terminal_.selectionUtf8(); }

    // Scrollback-wide find support.
    bool dumpBufferUtf8(std::string& out) { return terminal_.dumpBufferUtf8(out); }
    uint64_t viewportRow() { return terminal_.viewportRow(); }
    void scrollToRow(uint64_t row) { terminal_.scrollToRow(row); }

    // Mouse reporting (see Terminal::encodeMouse for the parameters).
    bool mouseTracking() { return terminal_.mouseTracking(); }
    std::string encodeMouse(int action, int button, float px, float py,
                            bool shift, bool ctrl, bool alt, bool anyButtonDown,
                            unsigned cellW, unsigned cellH, unsigned screenW,
                            unsigned screenH) {
        return terminal_.encodeMouse(action, button, px, py, shift, ctrl, alt,
                                     anyButtonDown, cellW, cellH, screenW,
                                     screenH);
    }

    bool exited() const { return pty_.hasExited(); }
    // True while the shell is running a command (has a child process), so the
    // UI can warn before closing a tab/pane that's doing work.
    bool hasRunningChild() const { return pty_.hasRunningChild(); }
    unsigned long shellPid() const { return pty_.processId(); }
    AgentActivity agentActivity() const;
    const std::wstring& cwd() const { return cwd_; }
    const std::wstring& title() const { return title_; }
    const std::wstring& shellCommand() const { return shell_; }
    const SessionContext& context() const { return context_; }
    void setContext(SessionContext context) { context_ = std::move(context); }
    const std::vector<CommandBlock>& commandBlocks() const { return commandBlocks_; }
    bool hasPendingClipboardRequest() const { return !clipboardRequest_.empty(); }
    std::string takeClipboardRequest();
    std::string commandOutputUtf8(size_t index);
    bool jumpPreviousCommand();
    bool jumpNextCommand();
    void toggleBookmarkLastCommand();

    // Pull OSC-driven updates: refresh title/cwd and append any notifications.
    // Called every frame for every session (so background panes notify too).
    void poll(std::vector<Notification>& notes) {
        std::wstring c;
        if (terminal_.takeCwd(c) && !c.empty()) cwd_ = c;
        std::wstring t = terminal_.oscTitle();
        if (!t.empty()) title_ = prettifyTitle(t);
        terminal_.drainNotifications(notes);
        processSemanticEvents();
    }

private:
    // Shells report their own exe path as the console title
    // ("C:\WINDOWS\SYSTEM32\cmd.exe", or "…cmd.exe - <command>" while a
    // command runs). Turn that into something a human wants on a tab: the
    // running command, or the current directory's name.
    std::wstring prettifyTitle(const std::wstring& t) const;
    void processSemanticEvents();
    void capturePromptInput(const char* data, size_t len);

    Terminal terminal_;
    ConPty pty_;
    Grid grid_;
    std::wstring cwd_;
    std::wstring title_;
    std::wstring shell_;
    int cols_ = 0, rows_ = 0;
    int cellW_ = 0, cellH_ = 0;
    bool active_ = false;
    bool atPrompt_ = false;
    uint64_t nextCommandId_ = 1;
    std::string promptInputUtf8_;
    bool promptEscape_ = false;
    std::chrono::steady_clock::time_point pendingCommandStartedAt_{};
    uint64_t pendingCommandRow_ = 0;
    std::vector<CommandBlock> commandBlocks_;
    size_t commandNavigation_ = 0;
    std::string clipboardRequest_;
    SessionContext context_;
    AgentActivity reportedAgentActivity_ = AgentActivity::Idle;
};

} // namespace liney
