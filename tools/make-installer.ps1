# make-installer.ps1 — build Release and produce dist\liney-win-Setup.exe (NSIS).
#
# Requires NSIS (makensis.exe). Install: winget install NSIS.NSIS
# Usage (VS dev shell): powershell -ExecutionPolicy Bypass -File tools\make-installer.ps1

param([string]$BuildDir = "build")
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$dist = Join-Path $root 'dist'
$nsi = Join-Path $root 'packaging\liney-win.nsi'
$ico = Join-Path $root 'res\liney.ico'

# Build a Release first (reuses make-portable's build step).
& (Join-Path $PSScriptRoot 'make-portable.ps1') -BuildDir $BuildDir | Out-Null

function Find-Exe($name) {
    foreach ($p in @((Join-Path $build $name), (Join-Path $build "Release\$name"))) {
        if (Test-Path $p) { return $p }
    }
    throw "$name not found; build first."
}
$winExe = Find-Exe 'liney_win.exe'
$cliExe = Find-Exe 'liney.exe'
$dll = Join-Path (Split-Path $winExe) 'ghostty-vt.dll'
if (-not (Test-Path $dll)) { throw "ghostty-vt.dll not found next to liney_win.exe" }

# Read the version from res\resource.rc (FileVersion x,y,z,0).
$ver = '0.1.0'
$rc = Get-Content (Join-Path $root 'res\resource.rc') -Raw
if ($rc -match 'FILEVERSION\s+(\d+),(\d+),(\d+)') { $ver = "$($matches[1]).$($matches[2]).$($matches[3])" }

# Locate makensis (PATH or common install dirs).
$makensis = (Get-Command makensis -ErrorAction SilentlyContinue).Source
if (-not $makensis) {
    foreach ($p in @("$env:ProgramFiles\NSIS\makensis.exe", "${env:ProgramFiles(x86)}\NSIS\makensis.exe")) {
        if (Test-Path $p) { $makensis = $p; break }
    }
}
if (-not $makensis) { throw "makensis not found. Install NSIS: winget install NSIS.NSIS" }

New-Item -ItemType Directory -Force -Path $dist | Out-Null
$out = Join-Path $dist 'liney-win-setup.exe'

& $makensis `
    "/DAPPVERSION=$ver" `
    "/DWINEXE=$winExe" `
    "/DCLIEXE=$cliExe" `
    "/DGHOSTTYDLL=$dll" `
    "/DICONFILE=$ico" `
    "/DOUTFILE=$out" `
    $nsi | Out-Host
if ($LASTEXITCODE -ne 0) { throw "makensis failed ($LASTEXITCODE)" }
Write-Host "Installer: $out"
