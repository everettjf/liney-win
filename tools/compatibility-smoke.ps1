param(
    [Parameter(Mandatory = $true)] [string]$Exe,
    [string]$Output = "compatibility-report.json"
)

$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Exe).Path
$build = [int](Get-ItemPropertyValue `
    -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' `
    -Name CurrentBuildNumber)
if ($build -lt 17763) {
    throw "Liney requires Windows 10 build 17763 or newer; found $build"
}
if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'Liney requires 64-bit Windows.'
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class LineyNativeProbe {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr GetModuleHandle(string name);
  [DllImport("kernel32.dll", CharSet=CharSet.Ansi)]
  public static extern IntPtr GetProcAddress(IntPtr module, string name);
}
'@
$kernel = [LineyNativeProbe]::GetModuleHandle('kernel32.dll')
foreach ($api in 'CreatePseudoConsole','ResizePseudoConsole','ClosePseudoConsole') {
    if ([LineyNativeProbe]::GetProcAddress($kernel, $api) -eq [IntPtr]::Zero) {
        throw "Required ConPTY API is unavailable: $api"
    }
}

& (Join-Path $PSScriptRoot 'smoke-test.ps1') -Exe $resolved
& (Join-Path $PSScriptRoot 'display-smoke.ps1') -Exe $resolved

$caption = Get-CimInstance Win32_OperatingSystem
$report = [ordered]@{
    timestampUtc = [DateTime]::UtcNow.ToString('o')
    productName = $caption.Caption
    version = $caption.Version
    build = $build
    architecture = $caption.OSArchitecture
    executable = $resolved
    conpty = 'passed'
    gpuWarpAndRecovery = 'passed'
    dpi = @(96,120,144,192,288)
}
$report | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 $Output
Write-Host "Windows compatibility smoke passed; report: $Output"
