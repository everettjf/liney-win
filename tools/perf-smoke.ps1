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
$previousHeadless = $env:LINEY_HEADLESS
$previousAutoClose = $env:LINEY_AUTOCLOSE_MS
$env:LINEY_HEADLESS = '1'
$env:LINEY_AUTOCLOSE_MS = '50'
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
$env:LINEY_HEADLESS = $previousHeadless
$env:LINEY_AUTOCLOSE_MS = $previousAutoClose
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
