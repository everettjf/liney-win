param(
    [Parameter(Mandatory = $true)] [string]$Exe,
    [int]$DurationSeconds = 60,
    [int]$MaxPeakWorkingSetMB = 300,
    [int]$MaxGrowthMB = 80
)

$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Exe).Path
$deadline = [DateTime]::UtcNow.AddSeconds([Math]::Max(10, $DurationSeconds))
$iterations = 0
$peak = 0L
$firstPeak = 0L
$lastPeak = 0L
while ([DateTime]::UtcNow -lt $deadline) {
    $process = [Diagnostics.Process]::Start($resolved, 'stability-self-test')
    $iterationPeak = 0L
    while (-not $process.HasExited) {
        $process.Refresh()
        $peak = [Math]::Max($peak, $process.WorkingSet64)
        $iterationPeak = [Math]::Max($iterationPeak, $process.WorkingSet64)
        if (-not $process.WaitForExit(100)) { continue }
    }
    if ($process.ExitCode -ne 0) {
        throw "Stability soak iteration $iterations failed with $($process.ExitCode)"
    }
    if ($iterations -eq 0) { $firstPeak = $iterationPeak }
    $lastPeak = $iterationPeak
    $iterations++
}
if ($iterations -lt 1) { throw 'Stability soak completed no iterations.' }
$peakMB = [Math]::Ceiling($peak / 1MB)
$growthMB = [Math]::Ceiling(($lastPeak - $firstPeak) / 1MB)
if ($peakMB -gt $MaxPeakWorkingSetMB) {
    throw "Stability peak ${peakMB}MB exceeds budget ${MaxPeakWorkingSetMB}MB"
}
if ($growthMB -gt $MaxGrowthMB) {
    throw "Stability working-set growth ${growthMB}MB exceeds budget ${MaxGrowthMB}MB"
}
Write-Host "Stability soak passed: iterations=$iterations duration=${DurationSeconds}s peak=${peakMB}MB growth=${growthMB}MB"
