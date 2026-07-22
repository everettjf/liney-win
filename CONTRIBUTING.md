# Contributing to liney-win

Thanks for your interest! liney-win is a Windows-only C++ terminal workspace
(Win32 + Direct2D, terminal core = Ghostty's libghostty-vt).

## Prerequisites

You need **all three** on PATH:

| Tool | Version | Notes |
|---|---|---|
| **Visual Studio 2022** (Desktop C++) | any recent | Provides MSVC `cl`, CMake ≥ 3.20, Ninja. Build from the **"x64 Native Tools Command Prompt for VS 2022"** (so `cl` is on PATH). |
| **Zig** | **exactly 0.15.2** | The terminal core (libghostty-vt) is built from Ghostty via Zig. See the warning below. |
| **Git** | any | CMake fetches Ghostty over Git. |

### ⚠️ Zig must be 0.15.2 (not 0.16.x)

Ghostty `main` pins its Zig version in `build.zig.zon`
(`minimum_zig_version = "0.15.2"`), and its check requires the **same major.minor
with patch ≥ 2**. So:

- **Zig 0.16.0 is rejected** — the build fails the version gate.
- You need **Zig 0.15.2** (i.e. Zig `0.15.x` with patch ≥ 2; today that's 0.15.2).

`winget` installs the *latest* Zig (0.16.x), which won't work. Get 0.15.2 directly:

1. Download `zig-x86_64-windows-0.15.2.zip` from
   <https://ziglang.org/download/0.15.2/> and unzip it.
2. Put that folder on PATH (and ahead of any other Zig). Verify:
   ```powershell
   zig version   # must print 0.15.2
   ```
   You don't have to uninstall a newer Zig — just make sure the `zig` resolved on
   PATH during the build is 0.15.2. (A version manager like `zvm` works too.)

> The required version tracks Ghostty `main`; if a future Ghostty bumps it, follow
> `build.zig.zon`'s `minimum_zig_version`.

## Build

```powershell
# in the "x64 Native Tools Command Prompt for VS 2022", with zig 0.15.2 on PATH
powershell -ExecutionPolicy Bypass -File tools\build.ps1
.\build\liney_win.exe
```

`tools\build.ps1` configures + builds Release and points Zig's cache at the build
drive. The first build fetches Ghostty and compiles `libghostty-vt`, so it takes a
while; `ghostty-vt.dll` is copied next to the exe automatically.

### ⚠️ Cross-drive Zig cache gotcha

Zig 0.15.2's build runner panics
(`assert(!std.fs.path.isAbsolute(...))` in `std/Build/Step/Run.zig`) when the
**build tree and Zig's global cache are on different drives** on Windows
(`std.fs.path.relative()` can't relativise across drives, so it returns an absolute
path). `tools\build.ps1` avoids this by setting `ZIG_GLOBAL_CACHE_DIR` to a folder
under the build directory (same drive). If you invoke CMake directly, set it
yourself:

```powershell
$env:ZIG_GLOBAL_CACHE_DIR = "$PWD\build\zig-global-cache"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Project layout

```
src/app/      Window + workspace shell (Window*.cpp split by concern), Tab, Layout
src/vt/       Terminal — wraps libghostty-vt (the terminal core)
src/render/   D2DRenderer (Direct2D/DirectWrite) behind IRenderer
src/pty/      ConPty (Windows pseudoconsole)
src/core/     TerminalSession, Config
src/workspace/ repo + worktree model
src/util/     Json, Http, Process, Dialogs, InputBox
src/cli/      liney.exe companion CLI
tools/        build.ps1, packaging (make-portable / make-installer / make-msix), gen-icon
```

## Packaging

```powershell
powershell -ExecutionPolicy Bypass -File tools\make-portable.ps1    # portable zip (+ ghostty-vt.dll)
powershell -ExecutionPolicy Bypass -File tools\make-installer.ps1   # NSIS setup (needs makensis)
```

Both accept `-BuildDir <dir>` to package from an existing build.

## Releasing

The release workflow optionally accepts an Authenticode code-signing certificate
with its private key in PFX/P12 format. Configure it once from an authenticated
GitHub CLI session; the script validates the private key, Code Signing EKU, and
validity period before writing encrypted repository secrets directly from
memory (`C:\secure\...` below is an example path, not a bundled certificate):

```powershell
powershell -ExecutionPolicy Bypass -File tools\configure-signing.ps1 -PfxPath C:\secure\liney-signing.pfx
```

Do not commit the certificate. When both secrets are configured, the workflow
timestamps and verifies `Liney.exe`, `ghostty-vt.dll`, and the installer. When
neither secret is configured, it publishes an explicitly labelled unsigned
release with a prominent SmartScreen warning. Every release still publishes
SHA-256 checksums and an SBOM. A partially configured credential pair fails.

For qualifying open-source projects, SignPath Foundation is a free alternative
that keeps the private key in an HSM and signs artifacts through GitHub Actions.
Enrollment and project approval must be completed with SignPath before its
organization, project, policy, and API-token values can be wired into CI.

1. Bump the version in `res\resource.rc` (FILEVERSION/PRODUCTVERSION + the two
   string values), `src\app\WindowInternal.h` (`kAppVersion`),
   `packaging\AppxManifest.xml`, `packaging\liney-win.nsi` (default), and the
   `packaging\winget\*.yaml` manifests — all should agree.
2. Add a section to `CHANGELOG.md`.
3. Merge, then tag: `git tag v0.x.0 && git push origin v0.x.0`.
4. The `Release` workflow builds `liney-setup.exe` + `liney-portable.zip`, runs
   upgrade/rollback/package smoke tests, signs the artifacts when credentials
   are available, and publishes the GitHub release (it fails if the tag doesn't
   match `resource.rc`).

## Style & PRs

- Plain **C++20 + Win32 + Direct2D**, no third-party runtime deps beyond
  libghostty-vt. Match the surrounding style; keep files small and cohesive.
- Build and smoke-test before opening a PR.
- Known gaps worth picking up: desktop notifications (OSC 9/777) need re-wiring
  through Ghostty's OSC parser for the libghostty core; SFTP file tree and
  tmux control-mode are open. See [`ROADMAP.md`](ROADMAP.md).
