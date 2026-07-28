# make-msix.ps1 — build a Release and package it as an MSIX.
#
# Requires the Windows SDK (makeappx.exe / signtool.exe). For local install you
# must sign with a cert whose subject matches AppxManifest's Publisher
# (CN=liney-win) and trust it. Store submission signs server-side.
#
# Usage (VS dev shell):
#   powershell -ExecutionPolicy Bypass -File tools\make-msix.ps1
#   # optional self-signed local install:
#   powershell -ExecutionPolicy Bypass -File tools\make-msix.ps1 -SelfSign

param([switch]$SelfSign, [string]$BuildDir = "build-store")
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$pkgSrc = Join-Path $build 'msix-src'
$out = Join-Path $root 'dist\liney-win.msix'

# 1) Build a dedicated Store binary + assets. Keep this in a separate build
# tree so configuring MSIX never turns the normal GitHub/portable build into a
# Store build (or vice versa).
$sharedZigCache = Join-Path $root 'build-ghostty\zig-global-cache'
$env:ZIG_GLOBAL_CACHE_DIR = if (Test-Path $sharedZigCache) {
    $sharedZigCache
} else {
    Join-Path $build 'zig-global-cache'
}
$zig = (Get-Command zig -ErrorAction SilentlyContinue).Source
if (-not $zig) {
    $zig = Get-ChildItem (Join-Path $root '.toolchain') -Recurse -Filter zig.exe `
        -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $zig) {
    throw 'Zig 0.15.2 not found on PATH or under .toolchain.'
}
$cmakeArgs = @(
    '-S', $root, '-B', $build, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    '-DLINEY_STORE_BUILD=ON',
    "-DZIG_EXECUTABLE=$zig"
)
$cachedGhostty = Join-Path $root 'build-ghostty\_deps\ghostty-src'
if (Test-Path (Join-Path $cachedGhostty 'build.zig')) {
    $cmakeArgs += "-DFETCHCONTENT_SOURCE_DIR_GHOSTTY=$cachedGhostty"
}
& cmake @cmakeArgs | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Store CMake configure failed' }
& cmake --build $build --config Release | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Store build failed' }
& (Join-Path $PSScriptRoot 'gen-assets.ps1')

# 2) Locate the built executable.
function Find-Exe($name) {
    foreach ($p in @((Join-Path $build $name), (Join-Path $build "Release\$name"))) {
        if (Test-Path $p) { return $p }
    }
    throw "$name not found; build first."
}

# 3) Stage the package layout (manifest + executable + DLLs + assets).
if (Test-Path $pkgSrc) { Remove-Item $pkgSrc -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $pkgSrc 'Assets') | Out-Null
Copy-Item (Join-Path $root 'packaging\AppxManifest.xml') $pkgSrc
Copy-Item (Join-Path $root 'packaging\Assets\*') (Join-Path $pkgSrc 'Assets')
$winExe = Find-Exe 'Liney.exe'
$binDir = Split-Path $winExe
Copy-Item $winExe $pkgSrc
Copy-Item (Join-Path $binDir 'ghostty-vt.dll') $pkgSrc
foreach ($pattern in @('msvcp140*.dll', 'vcruntime140*.dll')) {
    Get-ChildItem (Join-Path $binDir $pattern) -ErrorAction SilentlyContinue |
        Copy-Item -Destination $pkgSrc
}
if (-not (Test-Path (Join-Path $pkgSrc 'vcruntime140.dll'))) {
    throw "MSVC runtime DLLs were not staged by CMake"
}

# 4) Find makeappx.exe from the latest Windows SDK.
$sdkBin = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Directory -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName 'x64\makeappx.exe') } |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $sdkBin) { throw "makeappx.exe not found; install the Windows 10/11 SDK." }
$makeappx = Join-Path $sdkBin.FullName 'x64\makeappx.exe'

New-Item -ItemType Directory -Force -Path (Split-Path $out) | Out-Null
& $makeappx pack /d $pkgSrc /p $out /o | Out-Host
Write-Host "MSIX package: $out"

if ($SelfSign) {
    $signtool = Join-Path $sdkBin.FullName 'x64\signtool.exe'
    $manifest = [xml](Get-Content (Join-Path $root 'packaging\AppxManifest.xml') -Raw)
    $publisher = $manifest.Package.Identity.Publisher
    $cert = New-SelfSignedCertificate -Type Custom -Subject $publisher `
        -KeyUsage DigitalSignature -FriendlyName 'liney-win dev' `
        -CertStoreLocation 'Cert:\CurrentUser\My' `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
    $pfx = Join-Path $root 'dist\liney-win-dev.pfx'
    $pw = ConvertTo-SecureString -String 'liney' -Force -AsPlainText
    Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $pw | Out-Null
    & $signtool sign /fd SHA256 /a /f $pfx /p 'liney' $out | Out-Host
    Write-Host "Signed. To install locally, trust dist\liney-win-dev.pfx in the"
    Write-Host "Local Machine 'Trusted People' store, then: Add-AppxPackage $out"
}
