# Windows compatibility

Liney supports 64-bit Windows 10 and Windows 11. The minimum supported build is
Windows 10 version 1809 (build 17763), where ConPTY became available.

## Release matrix

Every release candidate must pass the following matrix before publication:

| Target | Required checks |
|---|---|
| Windows 10 22H2 x64 | Clean install, ConPTY/VT smoke, PowerShell/CMD/WSL, upgrade, portable, uninstall, WARP and 100%-200% DPI |
| Windows 11 current x64 | The Windows 10 checks plus device-loss recovery, 100%-300% DPI, multi-monitor restore and SSH disconnect/restart |
| GitHub Windows 2022/2025 | Full build, unit tests, 50-cycle ConPTY soak, display matrix, startup/memory budget and high-output stability soak |

`tools/compatibility-smoke.ps1` verifies the runtime build, required ConPTY
exports, terminal behavior, WARP fallback, simulated device loss and DPI matrix,
then writes a machine-readable report. Client-OS reports should be retained as
release evidence.

`tools/power-resume-smoke.ps1` injects the same suspend and resume broadcasts
used by Windows and verifies that the live UI recreates display-dependent state
without hanging or exiting. The stability soak enforces a zero-crash release
gate in addition to its memory-growth budget.

The standard smoke suite exercises two bounded OpenSSH network failures and
starts a replacement session. On Windows 10/11 certification machines, run
`Liney.exe remote-self-test require-wsl` to require an installed WSL distro and
verify that a fresh WSL session attaches after the previous one exits.

The application manifest declares PerMonitorV2 awareness, long-path support and
the Windows 10/11 compatibility GUID. Packaging remains per-user and does not
require elevation.

## Client certification runners

The `Windows Client Certification` workflow targets two self-hosted x64 runner
labels: `liney-windows-10` (Windows 10 22H2/build 19045) and
`liney-windows-11` (current Windows 11 with at least two active monitors). Both
machines must have an operational WSL distro. The workflow retains JSON evidence
and fails rather than silently skipping a missing OS, WSL, or display requirement.
Provisioning, isolation and evidence-retention instructions are in
[`docs/CLIENT_CERTIFICATION.md`](docs/CLIENT_CERTIFICATION.md).
