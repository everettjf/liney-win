# Terminal quality gates

This document is the acceptance matrix for claiming that Liney is a mature
Windows terminal workspace. A feature is not complete because it exists; it is
complete when the evidence named here passes on the release commit.

Status meanings:

- **Gate** — automated and required in CI.
- **Client gate** — automated, but requires the real Windows 10/11 runners.
- **Planned** — implementation or authoritative evidence is still missing.

## Compatibility

| Area | Required evidence | Current status |
|---|---|---|
| VT state and Unicode | Golden cases for cursor/erase/scroll/alt-screen, malformed streams, UTF-8, graphemes, CJK and emoji | Gate: `smoke-test.ps1`; fuzz smoke in `liney_tests` |
| Keyboard input | Table-driven normal/application cursor keys and xterm Shift/Alt/Ctrl combinations; IME/surrogate smoke | Gate: `KeyEncoder` unit tests; IME client automation planned |
| Mouse input | SGR and legacy press/release/drag/wheel fixtures; Shift override | SGR press/release gate; legacy/drag/wheel planned |
| Selection and reflow | Selection remains anchored across output, scrollback and width changes | Resize anchor gate; streaming/scrollback matrix planned |
| Font shaping | Fallback, emoji, combining sequences, ligatures on/off, missing-glyph behavior | Fallback/emoji gate; forced opt-in operator-shaping display gate |
| Display lifecycle | Hardware/WARP, device loss, 96–288 DPI, suspend/resume | Gate; multi-monitor restore is a client gate |
| Accessibility | Root and toolbar UIA identity, keyboard paths, Invoke; terminal text exposed through TextPattern | TextPattern live-text, toolbar and keyboard gate |
| ConPTY lifecycle | Start/resize/input/output/exit, 50-cycle soak, large output, cancellation, WSL/SSH reconnect | Gate |
| Shell integration | PowerShell/pwsh/cmd/WSL/Git Bash detection, OSC 7/133, idempotence and conflicting prompt hooks | PowerShell/idempotence gate; cross-shell conflict matrix planned |
| TUI applications | vim, less, tmux, htop/btop, fzf, mc and a curses fixture on supported shells | Nightly ConPTY gate for vim/less/fzf; interactive mouse fixtures remain planned |

## Rendering

| Area | Required evidence | Current status |
|---|---|---|
| Device/presentation | D3D11 device, flip-model DXGI swap chain, WARP fallback and device-loss rebuild | Gate |
| Terminal cells | Shader-rendered batched quads sampling a GPU glyph atlas; D2D fallback for color glyphs and WARP limitations | Hardware shader-path and WARP-fallback display gate |
| Shaping | DirectWrite analyzer/fallback with configurable ligatures and stable cell advance | Forced DirectWrite typography-run display gate |
| Images | At least one documented terminal image protocol with bounded decoding, memory and placement | OSC 1337 inline PNG/JPEG/GIF parser and end-to-end display gate |
| Performance | Frame-time percentiles for steady output, full-screen scroll, resize and mixed Unicode; no dropped-input gate | Startup/memory, 20k-line output and renderer frame-p95 gate; nightly resource-growth sampling; mixed-Unicode scenario planned |
| Competitors | Same machine, shell, font, grid, warm-up, payload and measurement harness for Windows Terminal, WezTerm and Alacritty | Reproducible 20k-line harness; Liney/Windows Terminal evidence recorded, WezTerm/Alacritty unavailable on the test host |

## Workspace UI

| Area | Required evidence | Current status |
|---|---|---|
| Responsive density | Golden captures at minimum width, 100–300% DPI, long names, 1/4/16/48 panes and tab overflow | Width/DPI layout and committed golden comparison gate |
| Object semantics | Repository, ordinary folder, worktree, SSH host and Agent task have distinct icon/state/secondary text | Gate for distinct icons and explicit dirty/ahead/behind plus Agent activity labels |
| Side panels | Terminal retains a usable minimum grid; panels collapse predictably and preserve user intent | Pure layout and 640/800/1000px display gate |
| File tree scope | UI and copy describe navigation/context, not IDE editing | Planned |
| Visual regression | Pixel/tolerance comparison of deterministic headless Direct2D captures | Gate: isolated profile, WARP captures, committed baselines and 1% tolerance |

## Reliability schedules

- Every pull request runs the fast lifecycle, layout-backup recovery,
  interaction stress, resource sampling and visual comparison gates.
- `.github/workflows/nightly-reliability.yml` runs the same release binary for
  two hours by default, records working set plus handle/thread/GDI/USER growth,
  and retains JSON evidence.
- The nightly job installs and exercises vim, less and fzf through Liney's
  ConPTY host. A non-zero exit, empty terminal buffer or timeout fails the run.

## Release evidence

Every release candidate must publish or retain:

1. Unit and protocol test results.
2. Windows 2022/2025 integration reports.
3. Windows 10/11 client certification reports.
4. Startup, memory, frame-time and soak results.
5. Deterministic visual-regression captures.
6. The competitor benchmark environment and raw measurements.

Code signing is tracked separately in `TODO.md` and is intentionally not part
of this engineering matrix.
