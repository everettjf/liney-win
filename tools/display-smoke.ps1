param([Parameter(Mandatory = $true)] [string]$Exe)

$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Exe).Path
$saved = @{
    LINEY_HEADLESS = $env:LINEY_HEADLESS
    LINEY_AUTOCLOSE_MS = $env:LINEY_AUTOCLOSE_MS
    LINEY_FORCE_WARP = $env:LINEY_FORCE_WARP
    LINEY_SIMULATE_DEVICE_LOSS = $env:LINEY_SIMULATE_DEVICE_LOSS
    LINEY_TEST_DPI = $env:LINEY_TEST_DPI
}
try {
    $env:LINEY_HEADLESS = '1'
    $env:LINEY_AUTOCLOSE_MS = '350'
    foreach ($dpi in 96, 120, 144, 192, 288) {
        $env:LINEY_TEST_DPI = [string]$dpi
        foreach ($mode in 'hardware-recovery', 'warp') {
            $env:LINEY_FORCE_WARP = if ($mode -eq 'warp') { '1' } else { $null }
            $env:LINEY_SIMULATE_DEVICE_LOSS = if ($mode -eq 'hardware-recovery') { '1' } else { $null }
            $process = Start-Process -FilePath $resolved -PassThru
            if (-not $process.WaitForExit(10000)) {
                $process.Kill()
                throw "Display smoke timed out at ${dpi} DPI in $mode mode"
            }
            if ($process.ExitCode -ne 0) {
                throw "Display smoke failed at ${dpi} DPI in $mode mode with $($process.ExitCode)"
            }
        }
    }
} finally {
    foreach ($entry in $saved.GetEnumerator()) {
        Set-Item -Path "Env:$($entry.Key)" -Value $entry.Value -ErrorAction SilentlyContinue
        if ($null -eq $entry.Value) { Remove-Item -Path "Env:$($entry.Key)" -ErrorAction SilentlyContinue }
    }
}
Write-Host 'GPU device-loss/WARP and 100%-300% DPI smoke tests passed.'
