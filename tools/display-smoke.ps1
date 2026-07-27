param([Parameter(Mandatory = $true)] [string]$Exe)

$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Exe).Path
$saved = @{
    LINEY_HEADLESS = $env:LINEY_HEADLESS
    LINEY_AUTOCLOSE_MS = $env:LINEY_AUTOCLOSE_MS
    LINEY_FORCE_WARP = $env:LINEY_FORCE_WARP
    LINEY_SIMULATE_DEVICE_LOSS = $env:LINEY_SIMULATE_DEVICE_LOSS
    LINEY_TEST_DPI = $env:LINEY_TEST_DPI
    LINEY_CAPTURE_PNG = $env:LINEY_CAPTURE_PNG
    LINEY_TEST_TABS = $env:LINEY_TEST_TABS
    LINEY_TEST_PANES = $env:LINEY_TEST_PANES
    LINEY_TEST_FOLDER_PROJECT = $env:LINEY_TEST_FOLDER_PROJECT
    LINEY_TEST_PALETTE = $env:LINEY_TEST_PALETTE
    LINEY_TEST_WIDTH = $env:LINEY_TEST_WIDTH
    LINEY_TEST_HEIGHT = $env:LINEY_TEST_HEIGHT
    LINEY_TEST_FILES_PANEL = $env:LINEY_TEST_FILES_PANEL
    LINEY_REQUIRE_D3D_GLYPHS = $env:LINEY_REQUIRE_D3D_GLYPHS
    LINEY_TEST_RENDER_TEXT = $env:LINEY_TEST_RENDER_TEXT
    LINEY_TEST_INLINE_IMAGE = $env:LINEY_TEST_INLINE_IMAGE
    LINEY_REQUIRE_INLINE_IMAGE = $env:LINEY_REQUIRE_INLINE_IMAGE
    LINEY_TEST_LIGATURES = $env:LINEY_TEST_LIGATURES
    LINEY_REQUIRE_LIGATURES = $env:LINEY_REQUIRE_LIGATURES
}
try {
    $env:LINEY_HEADLESS = '1'
    $env:LINEY_AUTOCLOSE_MS = '350'
    $env:LINEY_CAPTURE_PNG = $null
    $env:LINEY_TEST_TABS = $null
    $env:LINEY_TEST_PANES = $null
    $env:LINEY_TEST_FOLDER_PROJECT = $null
    $env:LINEY_TEST_PALETTE = '1'
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
    # Responsive-density matrix: both panels requested at widths where they
    # are full, compact, and where the optional files panel must collapse.
    $env:LINEY_TEST_DPI = '96'
    # The WARP path is already exercised above and intentionally permits the
    # DirectWrite fallback. This matrix requires the hardware shader path.
    $env:LINEY_FORCE_WARP = $null
    $env:LINEY_SIMULATE_DEVICE_LOSS = $null
    $env:LINEY_TEST_FILES_PANEL = '1'
    $env:LINEY_REQUIRE_D3D_GLYPHS = '1'
    $env:LINEY_TEST_RENDER_TEXT = '1'
    $env:LINEY_AUTOCLOSE_MS = '1200'
    foreach ($width in 640, 800, 1000) {
        $env:LINEY_TEST_WIDTH = [string]$width
        $env:LINEY_TEST_HEIGHT = '520'
        $process = Start-Process -FilePath $resolved -PassThru
        if (-not $process.WaitForExit(10000)) {
            $process.Kill()
            throw "Responsive display smoke timed out at width $width"
        }
        if ($process.ExitCode -ne 0) {
            throw "Responsive display smoke failed at width $width with $($process.ExitCode)"
        }
    }
    $env:LINEY_REQUIRE_D3D_GLYPHS = $null
    $env:LINEY_TEST_RENDER_TEXT = $null
    $env:LINEY_TEST_INLINE_IMAGE = '1'
    $env:LINEY_REQUIRE_INLINE_IMAGE = '1'
    $env:LINEY_AUTOCLOSE_MS = '1800'
    $process = Start-Process -FilePath $resolved -PassThru
    if (-not $process.WaitForExit(10000)) {
        $process.Kill()
        throw 'OSC 1337 inline-image display smoke timed out'
    }
    if ($process.ExitCode -ne 0) {
        throw "OSC 1337 inline-image display smoke failed with $($process.ExitCode)"
    }
    $env:LINEY_TEST_INLINE_IMAGE = $null
    $env:LINEY_REQUIRE_INLINE_IMAGE = $null
    $env:LINEY_TEST_LIGATURES = '1'
    $env:LINEY_REQUIRE_LIGATURES = '1'
    $process = Start-Process -FilePath $resolved -PassThru
    if (-not $process.WaitForExit(10000)) {
        $process.Kill()
        throw 'DirectWrite ligature display smoke timed out'
    }
    if ($process.ExitCode -ne 0) {
        throw "DirectWrite ligature display smoke failed with $($process.ExitCode)"
    }
} finally {
    foreach ($entry in $saved.GetEnumerator()) {
        Set-Item -Path "Env:$($entry.Key)" -Value $entry.Value -ErrorAction SilentlyContinue
        if ($null -eq $entry.Value) { Remove-Item -Path "Env:$($entry.Key)" -ErrorAction SilentlyContinue }
    }
}
Write-Host 'GPU device-loss/WARP and 100%-300% DPI smoke tests passed.'
