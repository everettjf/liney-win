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

The application manifest declares PerMonitorV2 awareness, long-path support and
the Windows 10/11 compatibility GUID. Packaging remains per-user and does not
require elevation.
