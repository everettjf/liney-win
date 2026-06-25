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

param([switch]$SelfSign)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$pkgSrc = Join-Path $root 'build\msix-src'
$out = Join-Path $root 'dist\liney-win.msix'

# 1) Build Release + assets.
& (Join-Path $PSScriptRoot 'make-portable.ps1') | Out-Null  # ensures a build
& (Join-Path $PSScriptRoot 'gen-assets.ps1')

# 2) Locate the built exes.
function Find-Exe($name) {
    foreach ($p in @((Join-Path $build $name), (Join-Path $build "Release\$name"))) {
        if (Test-Path $p) { return $p }
    }
    throw "$name not found; build first."
}

# 3) Stage the package layout (manifest + exes + assets).
if (Test-Path $pkgSrc) { Remove-Item $pkgSrc -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $pkgSrc 'Assets') | Out-Null
Copy-Item (Join-Path $root 'packaging\AppxManifest.xml') $pkgSrc
Copy-Item (Join-Path $root 'packaging\Assets\*') (Join-Path $pkgSrc 'Assets')
Copy-Item (Find-Exe 'liney_win.exe') $pkgSrc
Copy-Item (Find-Exe 'liney.exe') $pkgSrc

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
    $cert = New-SelfSignedCertificate -Type Custom -Subject 'CN=liney-win' `
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
