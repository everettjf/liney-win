# Changelog

All notable changes to liney-win. Versioning follows [SemVer](https://semver.org)
(0.x: minor bumps may change behavior).

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
