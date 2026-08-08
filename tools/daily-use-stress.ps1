param(
    [Parameter(Mandatory = $true)] [string]$Exe,
    [int]$Tabs = 8,
    [int]$Switches = 400,
    [int]$MaxPeakWorkingSetMB = 300,
    [double]$MaxFrameP95Milliseconds = 25,
    [string]$Output = 'artifacts/daily-use-stress.json'
)

$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Exe).Path
$scratch = Join-Path ([IO.Path]::GetTempPath()) `
    ('liney-daily-use-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $scratch
$markerPrefix = Join-Path $scratch 'workload'
$frameMetrics = Join-Path $scratch 'frames.json'
$saved = @{}
$names = @(
    'LINEY_HEADLESS', 'LINEY_CONFIG_DIR', 'LINEY_AUTOCLOSE_MS',
    'LINEY_TEST_TABS', 'LINEY_TEST_TAB_SWITCHES',
    'LINEY_TEST_DAILY_MARKER_PREFIX', 'LINEY_FRAME_METRICS'
)
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }

try {
    $env:LINEY_HEADLESS = '1'
    $env:LINEY_CONFIG_DIR = Join-Path $scratch 'profile'
    $env:LINEY_AUTOCLOSE_MS = '15000'
    $env:LINEY_TEST_TABS = [string][Math]::Max(4, $Tabs)
    $env:LINEY_TEST_TAB_SWITCHES = [string][Math]::Max(1, $Switches)
    $env:LINEY_TEST_DAILY_MARKER_PREFIX = $markerPrefix
    $env:LINEY_FRAME_METRICS = $frameMetrics

    $watch = [Diagnostics.Stopwatch]::StartNew()
    $process = [Diagnostics.Process]::Start($resolved)
    $peak = 0L
    while (-not $process.HasExited -and $watch.ElapsedMilliseconds -lt 25000) {
        $process.Refresh()
        $peak = [Math]::Max($peak, $process.WorkingSet64)
        Start-Sleep -Milliseconds 20
    }
    if (-not $process.HasExited) {
        $process.Kill()
        throw 'Daily-use workload timed out.'
    }
    $watch.Stop()
    if ($process.ExitCode -ne 0) {
        throw "Daily-use workload failed with exit $($process.ExitCode)."
    }

    $requiredMarkers = @("$markerPrefix-output.done")
    1..3 | ForEach-Object { $requiredMarkers += "$markerPrefix-cpu$_.done" }
    $missing = @($requiredMarkers | Where-Object { -not (Test-Path -LiteralPath $_) })
    if ($missing.Count -ne 0) {
        throw "Daily-use workload did not finish: $($missing -join ', ')"
    }
    if (-not (Test-Path -LiteralPath $frameMetrics)) {
        throw 'Daily-use workload did not publish renderer frame metrics.'
    }
    $frames = Get-Content -LiteralPath $frameMetrics -Raw | ConvertFrom-Json
    if ($frames.frames -lt 50 -or $frames.p95Ms -gt $MaxFrameP95Milliseconds) {
        throw "Daily-use frame gate failed: frames=$($frames.frames) p95=$($frames.p95Ms)ms"
    }
    $peakMB = [int][Math]::Ceiling($peak / 1MB)
    if ($peakMB -gt $MaxPeakWorkingSetMB) {
        throw "Daily-use peak ${peakMB}MB exceeds ${MaxPeakWorkingSetMB}MB."
    }

    $report = [ordered]@{
        timestampUtc = [DateTime]::UtcNow.ToString('o')
        executable = $resolved
        tabs = [Math]::Max(4, $Tabs)
        completedTabSwitches = [Math]::Max(1, $Switches)
        outputLines = 100000
        cpuIntensiveTabs = 3
        elapsedMilliseconds = $watch.ElapsedMilliseconds
        peakWorkingSetMB = $peakMB
        frames = $frames.frames
        frameP95Ms = $frames.p95Ms
        frameP99Ms = $frames.p99Ms
    }
    $outputPath = [IO.Path]::GetFullPath($Output)
    $outputParent = Split-Path -Parent $outputPath
    if ($outputParent) { $null = New-Item -ItemType Directory -Force -Path $outputParent }
    $report | ConvertTo-Json | Set-Content -LiteralPath $outputPath -Encoding utf8
    Write-Host ('Daily-use stress passed: ' + ($report | ConvertTo-Json -Compress))
} finally {
    foreach ($name in $names) {
        if ($null -eq $saved[$name]) {
            Remove-Item "Env:$name" -ErrorAction SilentlyContinue
        } else {
            Set-Item "Env:$name" $saved[$name]
        }
    }
    if (Test-Path -LiteralPath $scratch) {
        Remove-Item -LiteralPath $scratch -Recurse -Force
    }
}
