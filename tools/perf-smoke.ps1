param(
    [Parameter(Mandatory = $true)] [string]$Exe,
    [int]$Iterations = 20,
    [int]$MaxP95Milliseconds = 2000,
    [int]$MaxPeakWorkingSetMB = 250
)

$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Exe).Path
$samples = @()
$memorySamples = @()
$saved = @{
    LINEY_HEADLESS = $env:LINEY_HEADLESS
    LINEY_AUTOCLOSE_MS = $env:LINEY_AUTOCLOSE_MS
    LINEY_CAPTURE_PNG = $env:LINEY_CAPTURE_PNG
    LINEY_TEST_TABS = $env:LINEY_TEST_TABS
    LINEY_TEST_PANES = $env:LINEY_TEST_PANES
    LINEY_TEST_FOLDER_PROJECT = $env:LINEY_TEST_FOLDER_PROJECT
    LINEY_TEST_STRESS_OUTPUT = $env:LINEY_TEST_STRESS_OUTPUT
    LINEY_STRESS_MARKER = $env:LINEY_STRESS_MARKER
    LINEY_FRAME_METRICS = $env:LINEY_FRAME_METRICS
}
try {
    $env:LINEY_HEADLESS = '1'
    $env:LINEY_AUTOCLOSE_MS = '50'
    $env:LINEY_CAPTURE_PNG = $null
    $env:LINEY_TEST_TABS = $null
    $env:LINEY_TEST_PANES = $null
    $env:LINEY_TEST_FOLDER_PROJECT = $null
    for ($i = 0; $i -lt $Iterations; $i++) {
        $start = [Diagnostics.ProcessStartInfo]::new()
        $start.FileName = $resolved
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $process = [Diagnostics.Process]::Start($start)
        $iterationPeak = 0
        while (-not $process.HasExited -and $watch.ElapsedMilliseconds -lt 5000) {
            $process.Refresh()
            $iterationPeak = [Math]::Max($iterationPeak, $process.WorkingSet64)
            Start-Sleep -Milliseconds 5
        }
        if (-not $process.HasExited) {
            $process.Kill()
            throw "Iteration $i timed out"
        }
        $watch.Stop()
        if ($process.ExitCode -ne 0) { throw "Iteration $i failed" }
        $samples += [int]$watch.ElapsedMilliseconds
        $memorySamples += [int][Math]::Ceiling($iterationPeak / 1MB)
    }

    # Sustained output gate: completion proves all 20k lines reached the PTY;
    # peak memory catches unbounded scroll/render caches. The fixed close delay
    # keeps repeated measurements comparable on the same runner.
    $marker = Join-Path ([IO.Path]::GetTempPath()) ("liney-stress-" + [guid]::NewGuid() + ".txt")
    $env:LINEY_TEST_STRESS_OUTPUT = '1'
    $env:LINEY_STRESS_MARKER = $marker
    $frameMetrics = Join-Path ([IO.Path]::GetTempPath()) ("liney-frames-" + [guid]::NewGuid() + ".json")
    $env:LINEY_FRAME_METRICS = $frameMetrics
    $env:LINEY_AUTOCLOSE_MS = '5000'
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $resolved
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $stress = [Diagnostics.Process]::Start($start)
    $stressPeak = 0
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while (-not $stress.HasExited -and $watch.ElapsedMilliseconds -lt 10000) {
        $stress.Refresh()
        $stressPeak = [Math]::Max($stressPeak, $stress.WorkingSet64)
        Start-Sleep -Milliseconds 10
    }
    if (-not $stress.HasExited) {
        $stress.Kill()
        throw 'Sustained-output scenario timed out'
    }
    if ($stress.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $marker)) {
        throw 'Sustained-output scenario did not process all 20,000 lines'
    }
    if (-not (Test-Path -LiteralPath $frameMetrics)) {
        throw 'Renderer did not publish frame-time metrics'
    }
    $frames = Get-Content -LiteralPath $frameMetrics -Raw | ConvertFrom-Json
    if ($frames.frames -lt 10 -or $frames.p95Ms -gt 25) {
        throw "Renderer frame gate failed: frames=$($frames.frames) p95=$($frames.p95Ms)ms"
    }
    Remove-Item -LiteralPath $marker -Force
    Remove-Item -LiteralPath $frameMetrics -Force
    $stressMB = [int][Math]::Ceiling($stressPeak / 1MB)
    if ($stressMB -gt $MaxPeakWorkingSetMB) {
        throw "Sustained-output peak ${stressMB}MB exceeds budget ${MaxPeakWorkingSetMB}MB"
    }
    Write-Host "20k-line sustained output: completed peak=${stressMB}MB elapsed=$($watch.ElapsedMilliseconds)ms frame-p95=$($frames.p95Ms)ms frame-p99=$($frames.p99Ms)ms"
} finally {
    foreach ($entry in $saved.GetEnumerator()) {
        Set-Item -Path "Env:$($entry.Key)" -Value $entry.Value -ErrorAction SilentlyContinue
        if ($null -eq $entry.Value) {
            Remove-Item -Path "Env:$($entry.Key)" -ErrorAction SilentlyContinue
        }
    }
}
$sorted = $samples | Sort-Object
$index = [Math]::Min($sorted.Count - 1,
                     [Math]::Ceiling($sorted.Count * 0.95) - 1)
$p95 = $sorted[$index]
$average = [Math]::Round(($samples | Measure-Object -Average).Average, 1)
$peakMemory = ($memorySamples | Measure-Object -Maximum).Maximum
if ($peakMemory -gt $MaxPeakWorkingSetMB) {
    throw "Peak working set ${peakMemory}MB exceeds budget ${MaxPeakWorkingSetMB}MB"
}
Write-Host "GUI startup/lifecycle: average=${average}ms p95=${p95}ms peak=${peakMemory}MB samples=$Iterations"
if ($p95 -gt $MaxP95Milliseconds) {
    throw "GUI startup p95 ${p95}ms exceeds budget ${MaxP95Milliseconds}ms"
}
