# Changelog

All notable changes to liney-win. Versioning follows [SemVer](https://semver.org)
(0.x: minor bumps may change behavior).

## [0.9.0] - 2026-07-22

### Added
- The command palette now uses fuzzy matching across built-in actions, local
  shell profiles, workspaces, worktrees, recent projects, SSH hosts and agents.
- Recent projects persist locally and can be reopened directly from the command
  palette.
- Tabs can be pinned, renamed and duplicated. Pinned tabs stay grouped first,
  and their metadata is preserved in layouts and workspace snapshots.
- Split panes can be swapped or moved into a new tab from the pane menu or the
  command palette.
- First launch now explains the main workflow and offers to open Settings.
- Headless display tests cover WARP fallback, simulated GPU device loss and
  100%-300% DPI scaling. Stability soak tests enforce memory peak and growth
  budgets and exercise remote-session disconnect/reconnect behavior.

### Changed
- Custom shortcuts detect duplicate chords, keep the first safe binding and
  report the conflict. New tab and pane actions are also bindable.
- The UI Automation root exposes a stable name, automation ID, keyboard focus,
  help text and the command-palette accelerator for assistive tools.

## [0.8.1] - 2026-07-22

### Changed
- Stable releases can be published without Authenticode credentials while code
  signing enrollment is pending. Unsigned releases carry a prominent Windows
  SmartScreen warning; SHA-256 checksums, SPDX SBOM generation, clean install,
  upgrade, portable and uninstall smoke tests remain mandatory.
- Update trust is monotonic: an unsigned installation may consume an official
  repository update verified by its GitHub SHA-256 digest, but a signed running
  installation rejects unsigned installers and installers from another signer.

### Fixed
- Release metadata, MSIX/NSIS manifests and WinGet templates now agree on the
  v0.8.1 version and current portable executable layout.

## [0.8.0] - 2026-07-22

### Added
- Per-process crash markers and continuously refreshed recovery layouts restore
  window geometry, tabs, split panes, shells and working directories after an
  abnormal exit, independently of the normal remember-layout preference.
- One-click privacy-conscious diagnostic ZIP export containing system summary,
  and bounded logs, never minidumps, terminal contents, configuration, command
  history or environment variables. Local minidumps remain available for
  explicit, sensitivity-aware sharing.
- Exited and disconnected shells retain their scrollback and command blocks;
  users can explicitly restart them with the same shell, role and directory.
- A bounded, local-only, secret-redacted command history with search-and-insert
  UI. Selecting a result inserts it for review and never executes it.
- Dedicated large-output, long-running cleanup, unavailable network path,
  recovery marker and diagnostic archive smoke tests.

### Changed
- Failed commands expose a dedicated AI diagnosis action while retaining the
  existing bounded context, secret redaction and two-stage execution consent.
- Installer updates are transactional: the prior executable and terminal core
  are retained until the new build passes an actual ConPTY/VT startup check;
  failed, blocked or timed-out checks restore the known-good pair automatically.
- A recovery snapshot is deleted only after a successful restore or an explicit
  dismissal; failed restores keep it for another attempt.

### Security
- Release artifacts support optional Authenticode signing. When credentials are
  configured, application, terminal-core and installer signatures are
  timestamped and verified before release assets can be published.

## [0.7.0] - 2026-07-21

### Added
- Opt-in AI explanations for the latest semantic command block, available from
  the terminal context menu. Only that command and its bounded output are sent;
  the current directory requires a separate privacy opt-in.
- OpenAI Responses API, Codex CLI, and custom OpenAI-compatible providers.
  Credentials are read only from `OPENAI_API_KEY` or `LINEY_AI_API_KEY` and are
  never written to configuration or diagnostics.
- Deterministic secret redaction, command risk classification, structured
  response validation, and two-stage user confirmation before any suggested
  command can run. Changing terminals or directories cancels execution.

### Changed
- Captured helper processes now have bounded output and hard timeouts, so a
  stalled Git/Codex/provider helper cannot hang Liney during shutdown.

### Security
- AI is disabled by default. Common API keys, bearer tokens, passwords and
  secrets are redacted before provider calls; full terminal history is never
  included. Suggested commands are never executed silently.

## [0.6.0] - 2026-07-21

### Added
- Liney now checks quietly for stable GitHub releases at startup. The behavior
  is enabled by default and can be disabled in Settings; manual checks remain
  available from the menu and `Ctrl+Shift+U`.
- The update/version and trusted-download policy is covered by standalone unit
  tests on every CI run.

### Security
- Installer downloads are accepted only from this repository's canonical
  GitHub Release path. Foreign hosts, repositories, path traversal, missing
  SHA-256 digests, truncated downloads, and publisher changes are rejected.

## [0.5.10] - 2026-07-21

### Added
- A privacy-conscious diagnostic summary can be copied directly from the main
  menu for issue reports. It includes Liney/Windows/architecture information
  and dump metadata, never terminal contents or command history.

### Changed
- Crash dumps are retained newest-first with a five-dump limit, preventing the
  diagnostics directory from growing without bound.
- Startup logs now identify the running Liney version.

## [0.5.9] - 2026-07-21

### Added
- Reliable OSC 133 command blocks with jump, copy, rerun, status, duration,
  output extraction, and bookmarks; OSC 8 links and policy-controlled OSC 52.
- Automatic PowerShell, pwsh, cmd, WSL, and Git Bash profiles, a searchable
  command palette, configurable key bindings, multiple windows, and a separate
  administrator-window flow.
- Named workspace snapshots, Git worktree status, isolated Agent tasks,
  provider-neutral Agent states, project verification, review, and safe cleanup.
- Secure SSH profiles and connection diagnostics, UI Automation baseline,
  high-contrast support, crash dumps, rotating logs, and a diagnostics folder.

### Changed
- Ghostty is pinned to a verified commit and Windows 10/11 compatibility is
  declared in the embedded application manifest.
- Configuration writes are atomic, versioned, backed up, and automatically
  recover from malformed files without discarding the invalid source.
- Release artifacts support Authenticode signing, same-publisher update checks,
  SHA-256 checksums, an SPDX SBOM, and installer/portable/upgrade smoke tests.

### Fixed
- OSC markers now retain their exact terminal row even when several markers and
  command output arrive in one PTY chunk, preventing empty copied output.
- PowerShell integration works under restrictive machine execution policies.

## [0.5.8] — 2026-07-19

### Fixed
- **Clean Windows 10/11 compatibility** — the installer, portable zip, and
  MSIX staging now include the required MSVC Runtime DLLs, so Liney starts on
  machines without a separately installed Visual C++ Redistributable.
- **MSIX launch and payload** — the manifest now launches the renamed
  `Liney.exe`, uses the public product name, and packages Ghostty plus its
  runtime dependencies.

### Changed
- The documented support baseline is now explicit: 64-bit Windows 10 version
  1809 or newer, and Windows 11.

## [0.5.7] — 2026-07-19

### Added
- **Contextual toolbar menus** — the tab strip now exposes dedicated folder,
  Keep Awake, and overflow icons. The folder menu opens the active pane's
  directory in Explorer, PowerShell, VS Code, Sublime Text, or Warp; unavailable
  applications are disabled automatically.
- **Visible Sidebar disclosure** — a persistent button at the left of the tab
  strip collapses and restores the entire Workspace sidebar.

### Changed
- **Simpler overflow menu** — removed the internal `liney-win` version header
  and redundant Font/config entries; settings now have one canonical entry.
- **Explicit workspace discovery** — an empty `workspaceRoot` no longer scans
  the launch directory's parent. Only manually added projects are shown unless
  a scan root is explicitly configured.

### Fixed
- **Verified auto-updates** — downloaded installers now require GitHub's
  SHA-256 digest and pass status, length, and hash checks before execution;
  partial or unverified downloads are deleted.
- **ConPTY startup cleanup** — failed shell/session startup now releases every
  pipe and pseudoconsole handle instead of leaking resources.

## [0.5.6] — 2026-07-12

### Changed
- **Readable tab & window titles** — shells report their own exe path as the
  console title, so tabs used to read `C:\WINDOWS\SYSTEM32\cmd.exe`. Now an
  idle shell shows the **current directory's name**, and while a command runs
  the tab shows **the command itself** (e.g. `ping -t 127.0.0.1`). Titles set
  by apps (vim, ssh…) still show as-is.

## [0.5.5] — 2026-07-12

### Changed
- **Renamed to "Liney"** — the executable is now `Liney.exe`, the window title
  and shortcuts read **Liney** (capital L). The companion CLI is merged into
  the same binary, so `liney notify …` / `liney title …` still work (they run
  `Liney.exe` and write the OSC to the pane); no separate `liney.exe` ships.
- **Split-pane starting directory is configurable** — by default a new split
  inherits the current pane's directory; a new Settings option makes splits
  open in the workspace directory instead (the first sidebar project, or your
  home directory when there's no workspace).

## [0.5.4] — 2026-07-12

### Changed
- **New terminals open in your home directory by default** (`%USERPROFILE%`,
  e.g. `C:\Users\name`) instead of wherever the app was launched from. New
  tabs still follow the focused tab's directory when there is one; opening a
  worktree / SSH / agent from the sidebar still uses that target's directory.

## [0.5.3] — 2026-07-12

### Fixed
- **Closing a pane/tab could hang and leave a zombie** — the ConPTY teardown
  could deadlock (window closed but the process never exited), so after closing
  a few panes further closes stopped working. The shutdown now force-unblocks
  its worker threads (`CancelIoEx`) before closing the pseudoconsole, so a close
  always completes. Verified: 5 panes each running a command → close all →
  quit → clean process exit.

### Changed
- **"Restore tabs & panes on launch" is now off by default** — a fresh window
  each launch. Re-enable it in **Settings** (the old always-restore behavior).

### Added
- **Pane right-click menu** — Split right / down, Zoom pane, Equalize panes,
  Close pane, and **Close other panes** (collapse the tab back to one pane).

## [0.5.2] — 2026-07-12

### Changed
- **One consolidated "still running" dialog when closing** — closing the app
  (or several tabs at once) now shows a single confirmation that **lists** the
  tabs still running a command, instead of one dialog per tab. Quitting the
  window (title-bar ✕ / Alt+F4) with a busy tab now warns first (it didn't
  before). Nothing running → no prompt. The whole check uses one process
  snapshot instead of one per tab.

## [0.5.1] — 2026-07-12

### Added
- **Pane zoom** (`Ctrl+Shift+Z`, or the ☰ menu) — temporarily maximize the
  focused pane to fill the whole tab, then toggle back. The standard way to
  work in a deeply-split layout (tmux/iTerm2); a **ZOOM** badge shows while
  active. Moving focus, splitting, or closing a pane restores the layout.
- **Equalize panes** (`Ctrl+Shift+E`, or the ☰ menu) — reset every split to
  50/50 so panes are evenly distributed.
- **Close multiple tabs** — right-click a tab for **Close tabs to the right /
  to the left / other tabs / all tabs** (one confirmation if any is running a
  command).

### Changed
- **Splitting refuses to make unreadable panes** — a split that would leave a
  half narrower than ~24 columns or shorter than ~6 rows is declined with a
  hint to zoom or equalize instead, so repeated splitting can't shrink panes
  into illegibility.
- **Installer / shortcuts are named "liney"** — the desktop and Start-Menu
  shortcuts, the install folder (`%LOCALAPPDATA%\Programs\liney`), and the
  Add/Remove-Programs entry drop the `-win` suffix; the release assets are now
  `liney-setup.exe` and `liney-portable.zip`. Upgrading cleans up the old
  `liney-win` shortcuts.

### Fixed
- **Closing a tab no longer risks freezing the UI** — reordered the ConPTY
  teardown so `ClosePseudoConsole` runs while the reader is still draining
  (a latent deadlock), and the running-command check now takes one process
  snapshot for the whole tab instead of one per pane (which could stall a
  heavily-split tab on close).

## [0.5.0] — 2026-07-12

A polish pass on the Settings dialog and the ☰ menu.

### Changed
- **Redesigned Settings dialog** — the flat, cramped list is now three labelled
  sections (**Appearance** / **Terminal** / **Workspace**) drawn in **Segoe UI**
  and **scaled to the monitor's DPI** (crisp on HiDPI instead of the old bitmap
  font). The accent picker shows a real **color chip** next to its hex value,
  and controls are aligned in a clean two-column grid.
- **☰ menu** gains an app/version header and clearer grouping.

### Added
- **`Ctrl+,`** opens Settings (the usual shortcut; also shown in the menu).

## [0.4.1] — 2026-07-11

### Added
- **Tab close button** — each tab shows an **×** (on the active tab and on
  hover); clicking it closes that tab. Closing a tab (or a pane via
  `Ctrl+Shift+W`) whose shell is **running a command** now asks for
  confirmation first, so a long-running job isn't killed by a stray click.

## [0.4.0] — 2026-07-11

Customization + polish, aligning further with macOS liney.

### Added
- **Theme presets** — 7 built-in coordinated looks (Emerald / Azure / Violet
  Night, Amber Dark, Rose Dark, Slate Frost, Paper Light), each pairing a
  terminal palette with matching chrome. Pick one in Settings and it applies
  live across every open pane.
- **Custom accent color** — the active-pane divider / active tab / icon color
  is now user-configurable via a color picker in Settings (or `accentColor`
  in config.json). Overrides the preset's accent when set.
- **Font in Settings** — choose any installed monospace family and size from
  the Settings dialog (the ☰ Font… picker still works too).

### Changed
- **Chrome is fully themeable** — sidebar, tab strip, borders, accent and
  gutters are runtime values driven by the active theme, not hardcoded.
- **Roomier sidebar** — taller rows, real section gaps, larger inset, and text
  vertically centered in each row, so the WORKSPACE / SSH / AGENTS list reads
  less cramped. The files panel matches.
- `config.json` `theme` now accepts a **preset name** (e.g. `"Azure Night"`)
  in addition to the legacy `{ background, foreground, palette }` object.

### Fixed
- Switching theme presets brings that preset's own accent instead of pinning
  the previous one; opening Settings on a legacy overrides-object theme no
  longer force-rewrites it to a preset.
- The default window is clamped to the monitor work area (no off-screen title
  bar on small screens); page-scroll and find account for pane padding;
  numeric Settings fields can't overflow; keep-awake expiry shows one balloon.

## [0.3.0] — 2026-07-11

Hardening pass from a full-codebase review + on-Windows verification: crash
and data-loss fixes, terminal correctness, and rendering resilience. Plus a
feature-alignment pass with macOS liney: GUI settings, keep-awake durations,
and UI polish.

### Added
- **Settings… dialog** — a click-to-configure window (shell for new tabs with
  a detected-shells dropdown, scrollback, workspace root with Browse…,
  copy-on-select, multi-line paste warning, Unix tools), applied live and
  persisted to config.json without touching keys it doesn't edit. Editing
  config.json by hand still works ("Open config file" in the menu).
- **Keep awake durations** — the ☰ menu entry is now a submenu: 1 / 2 / 3 /
  6 / 24 hours or "Until turned off" (the PowerToys-Awake/Amphetamine
  pattern), with the remaining time shown in the menu, a tray balloon when
  the timer ends, and `Ctrl+Shift+K` still toggling on/off.
- **Report an issue…** menu item opening the GitHub issue tracker.

### Changed
- **Terminal panes have inner padding** — the grid no longer presses against
  the pane border (matches Windows Terminal / Ghostty defaults; scales with
  font size and DPI). Mouse hit-testing, IME placement, and mouse reporting
  account for it.
- **The default window is sized to the screen** — first launch opens at ~70%
  of the work area, centered, instead of a fixed 1000×640 physical pixels
  (postage-stamp small on high-DPI monitors). A saved layout still restores
  its exact geometry.

### Fixed
- **Stack buffer overflow on long grapheme clusters** — a cell whose cluster
  holds more than 16 codepoints (Zalgo-style combining marks in program
  output) overflowed a fixed stack buffer in the render snapshot; the buffer
  now sizes to the real cluster length.
- **Terminal query responses are now answered** — the core's `WRITE_PTY`
  callback was never installed, so DSR/CPR (`CSI 6n`), DA1/DA2, DECRQM and
  XTWINOPS queries were silently dropped and probing TUI apps could stall.
- **config.json can no longer be wiped** — a hand-edit typo used to make the
  next font-size save rewrite the whole file as a near-empty object. Saves now
  refuse to clobber an unparseable config, load warns once instead of
  silently using defaults, and all config/layout writes are atomic
  (temp file + rename).
- **JSON parser hardening** — a nesting-depth cap (corrupted config/layout
  files crashed the app at startup via stack overflow), and lone/mismatched
  `\u` surrogates now decode to U+FFFD instead of invalid UTF-8.
- **Device-lost recovery + WARP fallback** — a GPU driver update / TDR reset
  used to freeze the window permanently (`EndDraw`/`Present` results were
  ignored); the renderer now rebuilds the device, and machines with no usable
  GPU fall back to software rendering instead of failing to launch.
- **Mouse hit-testing matched to the drawn grid** — the renderer drew at a
  fractional cell pitch while hit-testing used a rounded size, drifting up to
  several columns on wide windows; the cell is now pixel-snapped so clicks,
  selection, and the IME caret land exactly.
- **Large pastes no longer freeze the window** — PTY input writes moved off
  the UI thread onto a queued writer.
- **Color emoji** — 🚀✅ and friends render in color (the atlas tinted them as
  monochrome silhouettes before).
- **Exited shells are reaped everywhere** — a shell that dies in a background
  tab now closes its pane immediately (previously it lingered until the tab
  was focused), and child exit wakes the UI loop.
- **OSC 7 cwd is percent-decoded** — directories with spaces or CJK in the
  path now track correctly in the files panel and new-tab cwd.
- **Window restore is clamped to a live monitor** — quitting on a
  since-disconnected display no longer restores the window fully off-screen.
- The update-check/download threads are joined at exit (previously a
  use-after-free race), WinHTTP calls carry timeouts, atlas overflow no
  longer corrupts glyphs mid-frame, the window repaints during interactive
  resize and while menus/dialogs are open, worktree names are validated and
  git's real error text is shown on failure, `liney notify` payloads are
  sanitized (control bytes/`;`) and stdout is binary, tab titles no longer
  split surrogate pairs, and Ctrl+zoom honors the full configured font-size
  range.

## [0.2.0] — 2026-07-07

The "daily-driver terminal" release: everything the VT core already parsed is
now actually rendered and interactive, and selection / find / mouse input moved
into the terminal core (libghostty-vt) so they behave like a real terminal.

### Added
- **Cursor shapes** — DECSCUSR block / bar / underline (vim switches shape per
  mode), blinking per terminal modes, hollow block when the pane or window is
  unfocused, OSC 12 cursor color.
- **Mouse reporting** — apps that enable tracking (vim `:set mouse=a`, htop,
  mc) receive clicks, drags, motion and wheel (SGR + legacy protocols, encoded
  by the core). Hold **Shift** to select text locally instead. Requires a
  ConPTY that passes mouse-mode requests through (Windows 11 / recent
  Windows 10); stays off gracefully otherwise.
- **Find across the whole scrollback** — `Ctrl+F`, then `Enter`/`F3` jump
  match-to-match up through history (the viewport follows); `Shift+Enter`
  walks back down.
- **Font… picker** in the ☰ menu (native dialog, monospace-only); font family
  and size persist to `config.json`.
- **`Ctrl+V` pastes** (Windows Terminal convention) alongside the existing
  `Ctrl+Shift+V` / `Shift+Insert`.
- **Multi-line paste confirmation** (`multiLinePasteWarning`, default on) so a
  stray copy can't run commands.
- **Mouse wheel in alternate-screen apps** — vim / less / `git log` scroll
  under the wheel (arrow keys are synthesized when no mouse tracking is on).
- `CHANGELOG.md` and a tag-triggered release workflow that builds and publishes
  the installer + portable zip.

### Fixed
- **SGR text attributes render** — bold / italic / faint / inverse / invisible
  / strikethrough / underline were parsed but never drawn (the snapshot didn't
  read style state). `ls --color`, git diff, prompts and vim statuslines now
  look right.
- **Wide (CJK) glyphs are no longer cut in half** — two-pass grid drawing gives
  double-width glyphs both columns; copied text and find no longer see phantom
  spaces between CJK characters; double-click word selection is CJK-aware.
- **Selection stays anchored to its text** — moved to the core's
  buffer-anchored selection, so the highlight no longer drifts when output
  streams in or you scroll; soft-wrapped lines copy as one logical line;
  select-all covers the scrollback.
- **The `theme.palette` config is applied** — the 16 ANSI colors (plus the
  256-color cube/grayscale) now reach the terminal core; it was documented but
  unwired.
- **Arrow keys honor DECCKM** (application cursor keys) and bracketed paste is
  queried from terminal state instead of scanning the output stream.

### Changed
- **Glyph atlas rendering** — each unique glyph is rasterized once and drawn
  as a tinted opacity mask, instead of re-shaping every cell every frame.
- Find semantics: `Enter`/`F3` = older (upward), `Shift+Enter` = newer, and the
  search covers history instead of paging blindly.
- `Ctrl+Shift+A` selects the entire buffer (was: visible screen only).

## [0.1.0]

Initial release: libghostty-vt terminal core (scrollback, reflow, alt-screen,
IME), tabs + binary splits, repository/worktree sidebar with per-project icons,
files panel following the shell cwd, SSH/agent sessions, layout persistence,
theme + config in `%USERPROFILE%\.liney`, notifications (`liney` CLI, OSC
9/777), lifecycle hooks, keep-awake, auto-update, NSIS installer + portable
zip + MSIX/WinGet scaffolding.

[0.2.0]: https://github.com/everettjf/liney-win/releases/tag/v0.2.0
[0.1.0]: https://github.com/everettjf/liney-win/releases/tag/v0.1.0
