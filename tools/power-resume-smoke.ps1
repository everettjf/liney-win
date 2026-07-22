param([Parameter(Mandatory = $true)] [string]$Exe)

$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Exe).Path
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class LineyPowerProbe {
  [DllImport("user32.dll", SetLastError=true)]
  public static extern IntPtr SendMessageTimeout(
    IntPtr hWnd, uint msg, UIntPtr wParam, IntPtr lParam,
    uint flags, uint timeout, out UIntPtr result);
}
'@

$oldClose = $env:LINEY_AUTOCLOSE_MS
$oldConfig = $env:LINEY_CONFIG_DIR
$oldHeadless = $env:LINEY_HEADLESS
$config = Join-Path $env:TEMP ("liney-power-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $config | Out-Null
('{"checkForUpdatesOnStartup":false}') |
    Set-Content -Encoding utf8 (Join-Path $config 'config.json')
try {
    $env:LINEY_AUTOCLOSE_MS = '7000'
    Remove-Item Env:LINEY_HEADLESS -ErrorAction SilentlyContinue
    $env:LINEY_CONFIG_DIR = $config
    $process = Start-Process -FilePath $resolved -PassThru
    $null = $process.WaitForInputIdle(5000)
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ($process.MainWindowHandle -eq [IntPtr]::Zero -and
           [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 50
        $process.Refresh()
    }
    if ($process.MainWindowHandle -eq [IntPtr]::Zero) {
        if ($process.HasExited) {
            throw "Liney exited before creating a window: $($process.ExitCode)"
        }
        throw 'Liney did not create a window for power-resume smoke testing.'
    }
    $result = [UIntPtr]::Zero
    foreach ($event in 4,18,7) { # suspend, automatic resume, user-visible resume
        $sent = [LineyPowerProbe]::SendMessageTimeout(
            $process.MainWindowHandle, 0x0218,
            [UIntPtr]::new([uint64]$event),
            [IntPtr]::Zero, 2, 3000, [ref]$result)
        if ($sent -eq [IntPtr]::Zero) { throw "Power event $event timed out." }
        if ($process.HasExited) { throw "Liney exited after power event $event." }
    }
    if (-not $process.WaitForExit(10000)) {
        $process.Kill()
        throw 'Liney did not close after power-resume smoke testing.'
    }
    if ($process.ExitCode -ne 0) { throw "Liney exited with $($process.ExitCode)." }
    Write-Host 'Suspend/resume display recovery smoke passed.'
} finally {
    $env:LINEY_AUTOCLOSE_MS = $oldClose
    $env:LINEY_CONFIG_DIR = $oldConfig
    $env:LINEY_HEADLESS = $oldHeadless
}
