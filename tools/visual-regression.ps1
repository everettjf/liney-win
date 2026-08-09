param(
    [Parameter(Mandatory = $true)] [string]$Exe,
    [string]$BaselineDir = 'tests/visual-baselines',
    [string]$OutputDir = 'artifacts/visual-regression',
    [switch]$UpdateBaselines,
    [double]$MaxChangedPixelRatio = 0.01,
    [int]$ChannelTolerance = 12,
    [int]$ScenarioTimeoutMilliseconds = 15000
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$resolved = (Resolve-Path -LiteralPath $Exe).Path
$baselineRoot = [IO.Path]::GetFullPath((Join-Path $PWD $BaselineDir))
$outputRoot = [IO.Path]::GetFullPath((Join-Path $PWD $OutputDir))
$null = New-Item -ItemType Directory -Force -Path $baselineRoot, $outputRoot
$saved = @{}
$keys = @('LINEY_HEADLESS','LINEY_AUTOCLOSE_MS','LINEY_CAPTURE_DELAY_MS',
          'LINEY_CAPTURE_PNG','LINEY_FORCE_WARP','LINEY_TEST_DPI',
          'LINEY_TEST_WIDTH','LINEY_TEST_HEIGHT','LINEY_TEST_TABS',
          'LINEY_TEST_PANES','LINEY_TEST_FILES_PANEL','LINEY_TEST_PALETTE',
          'USERPROFILE')
foreach ($key in $keys) { $saved[$key] = [Environment]::GetEnvironmentVariable($key) }
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("liney-visual-" + [Guid]::NewGuid())
$fixtureProfile = Join-Path $fixtureRoot 'profile'
$fixtureConfig = Join-Path $fixtureProfile '.liney'
$null = New-Item -ItemType Directory -Force -Path $fixtureConfig
Set-Content -LiteralPath (Join-Path $fixtureConfig 'config.json') `
    -Value '{"checkForUpdatesOnStartup":false,"projects":[],"workspaceRoot":"","shell":"cmd.exe /d /q /k \"cls & prompt LINEY$G\""}' `
    -Encoding utf8

function Compare-Png([string]$actualPath, [string]$baselinePath) {
    $actual = [Drawing.Bitmap]::FromFile($actualPath)
    $baseline = [Drawing.Bitmap]::FromFile($baselinePath)
    try {
        if ($actual.Width -ne $baseline.Width -or $actual.Height -ne $baseline.Height) {
            throw "Dimensions differ: actual=$($actual.Width)x$($actual.Height), baseline=$($baseline.Width)x$($baseline.Height)"
        }
        $changed = 0L
        $total = [long]$actual.Width * $actual.Height
        for ($y = 0; $y -lt $actual.Height; $y++) {
            for ($x = 0; $x -lt $actual.Width; $x++) {
                $a = $actual.GetPixel($x, $y)
                $b = $baseline.GetPixel($x, $y)
                if ([Math]::Abs([int]$a.R - [int]$b.R) -gt $ChannelTolerance -or
                    [Math]::Abs([int]$a.G - [int]$b.G) -gt $ChannelTolerance -or
                    [Math]::Abs([int]$a.B - [int]$b.B) -gt $ChannelTolerance) {
                    $changed++
                }
            }
        }
        return $changed / [double]$total
    } finally {
        $actual.Dispose()
        $baseline.Dispose()
    }
}

$scenarios = @(
    @{ Name='welcome-100'; Width='1000'; Height='640'; Dpi='96'; Tabs='1'; Panes='1'; Files=$null },
    @{ Name='compact-150'; Width='800'; Height='600'; Dpi='144'; Tabs='4'; Panes='4'; Files='1' },
    # Keep the requested client height below the smallest hosted-runner work
    # area. Windows Server 2022/2025 reserve different title/taskbar heights;
    # 700px plus the native frame is clamped and produces a variable capture.
    @{ Name='dense-200'; Width='1000'; Height='600'; Dpi='192'; Tabs='16'; Panes='8'; Files='1' }
)
try {
    $env:LINEY_HEADLESS = '1'
    $env:USERPROFILE = $fixtureProfile
    $env:LINEY_AUTOCLOSE_MS = '1800'
    $env:LINEY_CAPTURE_DELAY_MS = '900'
    $env:LINEY_FORCE_WARP = '1'
    $env:LINEY_TEST_PALETTE = $null
    foreach ($scenario in $scenarios) {
        $actual = Join-Path $outputRoot "$($scenario.Name).png"
        $baseline = Join-Path $baselineRoot "$($scenario.Name).png"
        Remove-Item -LiteralPath $actual -ErrorAction SilentlyContinue
        $env:LINEY_CAPTURE_PNG = $actual
        $env:LINEY_TEST_WIDTH = $scenario.Width
        $env:LINEY_TEST_HEIGHT = $scenario.Height
        $env:LINEY_TEST_DPI = $scenario.Dpi
        $env:LINEY_TEST_TABS = $scenario.Tabs
        $env:LINEY_TEST_PANES = $scenario.Panes
        $env:LINEY_TEST_FILES_PANEL = $scenario.Files
        $process = Start-Process -FilePath $resolved -PassThru
        if (-not $process.WaitForExit($ScenarioTimeoutMilliseconds)) {
            $process.Kill()
            throw "Visual scenario $($scenario.Name) timed out"
        }
        if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $actual)) {
            throw "Visual scenario $($scenario.Name) did not produce a valid capture"
        }
        if ($UpdateBaselines) {
            Copy-Item -LiteralPath $actual -Destination $baseline -Force
            continue
        }
        if (-not (Test-Path -LiteralPath $baseline)) {
            throw "Missing visual baseline: $baseline (run with -UpdateBaselines intentionally)"
        }
        $ratio = Compare-Png $actual $baseline
        if ($ratio -gt $MaxChangedPixelRatio) {
            throw ("Visual regression in {0}: {1:P3} pixels changed, budget {2:P3}" -f
                   $scenario.Name, $ratio, $MaxChangedPixelRatio)
        }
        Write-Host ("Visual $($scenario.Name) passed: {0:P3} pixels changed" -f $ratio)
    }
} finally {
    foreach ($key in $keys) {
        [Environment]::SetEnvironmentVariable($key, $saved[$key])
    }
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host 'Visual regression gate passed.'
