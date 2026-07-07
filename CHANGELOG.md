# Changelog

All notable changes to liney-win. Versioning follows [SemVer](https://semver.org)
(0.x: minor bumps may change behavior).

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
