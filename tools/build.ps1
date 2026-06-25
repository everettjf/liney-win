# build.ps1 — configure + build liney-win.
#
# The terminal core is libghostty-vt, fetched from Ghostty and built via Zig, so
# you need BOTH on PATH:
#   * Zig 0.15.2            https://ziglang.org/download/
#   * MSVC (run this from the "x64 Native Tools Command Prompt for VS 2022",
#     or any shell where cl.exe is on PATH)
#
# Usage:  powershell -ExecutionPolicy Bypass -File tools\build.ps1 [-BuildDir build] [-Config Release]

param(
    [string]$BuildDir = "build",
    [string]$Config = "Release"
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }

# Force Zig's global cache onto the same drive as the build. Zig 0.15.2's build
# runner trips an assertion (std.fs.path.relative returns an absolute path) when
# the build tree and the Zig cache are on different drives on Windows — keeping
# them on one drive avoids it without patching Zig.
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $build "zig-global-cache"

cmake -S $root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
cmake --build $build
if ($LASTEXITCODE -ne 0) { throw "build failed" }

Write-Host "`nBuilt $build\liney_win.exe"
