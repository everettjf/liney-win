# make-portable.ps1 — build a Release and produce a portable .zip distributable.
#
# Output: dist\liney-portable.zip containing Liney.exe, ghostty-vt.dll,
# README.md and LICENSE. No install needed — unzip and run Liney.exe.
#
# Usage (from the repo root, in a shell where CMake/MSVC are available, e.g. the
# "x64 Native Tools Command Prompt for VS 2022"):
#   powershell -ExecutionPolicy Bypass -File tools\make-portable.ps1

param([string]$BuildDir = "build-ghostty")
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$dist = Join-Path $root 'dist'

# Locate CMake (PATH, else the VS-bundled copy).
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}
if (-not (Test-Path $cmake)) { throw "cmake not found; run from a VS dev shell." }

Write-Host "Configuring + building Release..."
# Zig (libghostty-vt core) needs its cache on the build drive (see tools\build.ps1).
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $build 'zig-global-cache'
if (-not (Test-Path $build)) {
    & $cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release | Out-Host
}
& $cmake --build $build --config Release | Out-Host

# Find the built exes (Ninja puts them in build\, VS generators in build\Release\).
function Find-Exe($name) {
    $p1 = Join-Path $build $name
    $p2 = Join-Path $build "Release\$name"
    if (Test-Path $p1) { return $p1 }
    if (Test-Path $p2) { return $p2 }
    throw "$name not found under $build"
}
$winExe = Find-Exe 'Liney.exe'

# Stage and zip.
$stage = Join-Path $dist 'liney'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item $winExe $stage
# Bundle the terminal-core DLL (copied next to the exe by the build).
$dll = Join-Path (Split-Path $winExe) 'ghostty-vt.dll'
if (-not (Test-Path $dll)) { throw "ghostty-vt.dll not found next to Liney.exe" }
Copy-Item $dll $stage
Copy-Item (Join-Path $root 'README.md') $stage
Copy-Item (Join-Path $root 'LICENSE') $stage

$zip = Join-Path $dist 'liney-portable.zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Write-Host "Portable package: $zip"
