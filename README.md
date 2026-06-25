# liney-win

**English** · [中文](README.zh-CN.md)

A **terminal workspace for Windows** — keep multiple repositories / worktrees,
splits, and tabs in a single window. A Windows take on macOS
[liney](https://github.com/everettjf/liney). Self-built terminal core, fully
self-drawn Win32 / Direct2D, **needs only MSVC — no runtime dependencies**.

![liney-win screenshot](docs/screenshot.png)

---

## ✨ Features

**Terminal** (built-in xterm-subset core `VTEmulator`)
- VT parsing: cursor moves / erase / scroll regions / insert-delete lines & chars,
  SGR 16/256/truecolor + bold/italic/underline/inverse, UTF-8, wide chars
- **Scrollback** + wheel / `Shift+PgUp` scrolling; long lines **reflow** on resize
- **Alternate screen** — vim / less / `git log` and other full-screen apps work
- **Selection + copy/paste** (`Ctrl+Shift+C/V`, bracketed paste)
- **IME** (CJK input) with the candidate window at the cursor
- Font zoom and a configurable **color theme**
- **Unix tools**: when Git for Windows is installed, `ls` / `cat` / `grep` / `rm`
  / `sed` / `awk` / … work in any shell (its `usr\bin` is added to PATH)

**Workspace** (liney's differentiator)
- Multiple tabs + binary **splits** (drag dividers to resize, drag tabs to
  reorder), `Alt+Arrows` to move focus
- A left sidebar of **repositories** with **project icons** (config or repo-local),
  expandable to **worktrees**; right-click to create / remove a worktree
- Click a worktree / SSH host / agent to open a tab in its directory
- A right-side **folder-tree** panel that follows the focused pane
- **Layout persistence** — tabs + split tree + per-pane cwd restore next launch

**Sessions & integrations**
- Local shells (cmd / PowerShell / pwsh / wsl…), **SSH**, **agent** sessions
  (tmux via `wsl tmux`)
- **Notifications**: a `liney notify` CLI + OSC `9` / `777` → Windows tray balloons
- **Git**: `Ctrl+Shift+L/G` opens `git log` / `git diff` in a new tab
- **Lifecycle hooks**: run a command on session start/exit and app exit
- **Auto-update**: `Ctrl+Shift+U` checks GitHub releases and installs the new build

---

## 📦 Install

**Download** (recommended): from [Releases](https://github.com/everettjf/liney-win/releases)
- `liney-win-setup.exe` — installer (per-user, no admin; Start Menu + uninstall)
- `liney-win-portable.zip` — portable, unzip and run `liney_win.exe`

**Build from source** (Windows 10 1809+/11, Visual Studio 2022 Desktop C++,
CMake ≥ 3.20; VS 2022 bundles CMake/Ninja):

```powershell
# in the "x64 Native Tools Command Prompt for VS 2022"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\liney_win.exe
```

> Optional: `-DLINEY_WITH_LIBGHOSTTY=ON` uses
> [libghostty-vt](https://github.com/ghostty-org/ghostty) as the terminal core
> (needs Zig); the default built-in `VTEmulator` needs nothing extra.

---

## ⌨️ Shortcuts

| Key | Action |
|---|---|
| `Ctrl+Shift+T` / `Ctrl+Shift+W` | New tab / close current pane |
| `Ctrl+Shift+E` / `Ctrl+Shift+O` | Split left/right · top/bottom |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Next / previous tab |
| `Alt+Arrows` | Move focus between split panes |
| `Ctrl+Shift+B` / `Ctrl+Shift+F` | Toggle the left sidebar / right files panel |
| `Ctrl+Shift+C` / `Ctrl+Shift+V` | Copy selection / paste |
| `Ctrl++` / `Ctrl+-` / `Ctrl+0` | Zoom font in / out / reset |
| `Ctrl+Shift+L` / `Ctrl+Shift+G` | `git log` / `git diff` for the current repo |
| `Ctrl+Shift+U` | Check for & install updates |
| Wheel · `Shift+PgUp/PgDn/Home/End` | Scroll through scrollback history |
| Mouse drag | Select text in a pane; drag dividers to resize; drag tabs to reorder |
| Mouse left / right | Switch tab · focus pane · expand repo · open worktree/SSH/agent / right-click to manage worktrees |

---

## ⚙️ Configuration

The first run writes a default config to `%USERPROFILE%\.liney\config.json`
(mirroring macOS liney's `~/.liney/`; full sample in
[`config.example.json`](./config.example.json)):

```json
{
  "shell": "cmd.exe",
  "fontFamily": "Cascadia Mono",
  "fontSize": 16,
  "workspaceRoot": "",
  "unixTools": true,
  "hooks": { "sessionStart": "", "sessionExit": "", "appExit": "" },
  "sshHosts": ["user@host"],
  "agents": [{ "name": "agent", "command": "claude", "cwd": "" }],
  "projectIcons": { "my-repo": "C:\\path\\to\\icon.png" },
  "theme": { "background": "#102840", "foreground": "#e8e8d0", "palette": ["#000000", "..."] }
}
```

- `shell` — shell for new tabs (`powershell.exe` / `pwsh.exe` / `wsl.exe`; `wsl tmux` for tmux)
- `workspaceRoot` — directory scanned for repos; empty = the launch directory's parent
- `unixTools` — append Git's `usr\bin` to PATH so `ls`/`cat`/`grep`/… work
- `sshHosts` / `agents` — entries in the sidebar SSH / AGENTS sections
- `projectIcons` — per-repo sidebar icons (else a repo-local `icon.png`/`logo.png`)
- `theme` — terminal fg/bg + the 16-color ANSI palette
- `hooks` — commands run on session start/exit and app exit

The window layout is saved to `%USERPROFILE%\.liney\layout.json` and restored on
the next launch.

---

## 🔔 `liney` CLI & notifications

A companion CLI `liney.exe` ships with the app; run it inside a pane to drive the
terminal over OSC (mirrors macOS liney's `liney notify`):

```
liney notify <body>            # Windows tray notification
liney notify <title> <body>
liney title  <text>           # set the tab/window title
```

Put `liney.exe` on PATH and `liney notify "done"` notifies you when a long task
finishes. The terminal also parses common OSC: `0/2` (title), `7` (cwd), `9` and
`777;notify` (notifications).

---

## 📦 Packaging

Scripts in `tools/`, manifests in `packaging/`:

```powershell
powershell -ExecutionPolicy Bypass -File tools\make-installer.ps1   # NSIS setup (winget install NSIS.NSIS)
powershell -ExecutionPolicy Bypass -File tools\make-portable.ps1    # portable zip
powershell -ExecutionPolicy Bypass -File tools\make-msix.ps1 -SelfSign  # MSIX (Windows SDK)
```

The app icon (`res/liney.ico`, generated by `tools/gen-icon.ps1` from liney's
own icon) is embedded via `res/resource.rc`. WinGet manifest templates are in
`packaging/winget/`.

---

## 🏗️ Architecture

```
keyboard/mouse → Window (workspace orchestration) → routes to the focused pane
                 ↑ composes sidebar / tab strip / split tree / files panel
TerminalSession = Terminal + ConPty + Grid
   ConPty      — Windows pseudoconsole (spawn shell, read/write, resize)
   Terminal    — built-in VTEmulator (or libghostty-vt) parses PTY bytes → Grid
   D2DRenderer — Direct2D/DirectWrite draws the Grid + chrome to the window
```

Source map: [`src/`](./src). Design / research notes: [`RESEARCH.md`](./RESEARCH.md),
[`ALT_PLAN_SELFBUILT.md`](./ALT_PLAN_SELFBUILT.md),
[`TERMINAL_LANDSCAPE.md`](./TERMINAL_LANDSCAPE.md); rendering plan:
[`RENDERING.md`](./RENDERING.md).

## 🗺️ Roadmap

Done and remaining items (with a macOS-liney comparison) are in
[`ROADMAP.md`](./ROADMAP.md). Still pending: mouse reporting (ConPTY-limited),
SFTP remote file tree, a glyph-atlas renderer, native tmux control-mode.

## License

[Apache-2.0](./LICENSE) (same as liney).
