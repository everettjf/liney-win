param(
    [Parameter(Mandatory = $true)] [string]$Exe,
    [int]$DurationSeconds = 60,
    [int]$MaxPeakWorkingSetMB = 300,
    [int]$MaxGrowthMB = 80,
    [int]$MaxHandleGrowth = 40,
    [int]$MaxThreadGrowth = 4,
    [int]$MaxGdiGrowth = 8,
    [int]$MaxUserGrowth = 8,
    [string]$Output = ''
)

$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Exe).Path
$native = @'
using System;
using System.Runtime.InteropServices;
public static class LineyGuiResources {
    [DllImport("user32.dll")]
    public static extern int GetGuiResources(IntPtr process, int flags);
}
'@
Add-Type -TypeDefinition $native
$deadline = [DateTime]::UtcNow.AddSeconds([Math]::Max(10, $DurationSeconds))
$iterations = 0
$crashes = 0
$peak = 0L
$firstPeak = 0L
$lastPeak = 0L
$firstHandles = $firstThreads = $firstGdi = $firstUser = $null
$lastHandles = $lastThreads = $lastGdi = $lastUser = 0
while ([DateTime]::UtcNow -lt $deadline) {
    $process = [Diagnostics.Process]::Start($resolved, 'stability-self-test')
    $iterationPeak = 0L
    $iterationHandles = $iterationThreads = $iterationGdi = $iterationUser = 0
    while (-not $process.HasExited) {
        $process.Refresh()
        $peak = [Math]::Max($peak, $process.WorkingSet64)
        $iterationPeak = [Math]::Max($iterationPeak, $process.WorkingSet64)
        $iterationHandles = [Math]::Max($iterationHandles, $process.HandleCount)
        $iterationThreads = [Math]::Max($iterationThreads, $process.Threads.Count)
        $iterationGdi = [Math]::Max($iterationGdi,
            [LineyGuiResources]::GetGuiResources($process.Handle, 0))
        $iterationUser = [Math]::Max($iterationUser,
            [LineyGuiResources]::GetGuiResources($process.Handle, 1))
        if (-not $process.WaitForExit(100)) { continue }
    }
    if ($process.ExitCode -ne 0) {
        ++$crashes
        throw "Stability soak iteration $iterations failed with $($process.ExitCode)"
    }
    if ($iterations -eq 0) {
        $firstPeak = $iterationPeak
        $firstHandles = $iterationHandles
        $firstThreads = $iterationThreads
        $firstGdi = $iterationGdi
        $firstUser = $iterationUser
    }
    $lastPeak = $iterationPeak
    $lastHandles = $iterationHandles
    $lastThreads = $iterationThreads
    $lastGdi = $iterationGdi
    $lastUser = $iterationUser
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
$handleGrowth = $lastHandles - $firstHandles
$threadGrowth = $lastThreads - $firstThreads
$gdiGrowth = $lastGdi - $firstGdi
$userGrowth = $lastUser - $firstUser
if ($handleGrowth -gt $MaxHandleGrowth) {
    throw "Handle growth $handleGrowth exceeds budget $MaxHandleGrowth"
}
if ($threadGrowth -gt $MaxThreadGrowth) {
    throw "Thread growth $threadGrowth exceeds budget $MaxThreadGrowth"
}
if ($gdiGrowth -gt $MaxGdiGrowth) {
    throw "GDI object growth $gdiGrowth exceeds budget $MaxGdiGrowth"
}
if ($userGrowth -gt $MaxUserGrowth) {
    throw "USER object growth $userGrowth exceeds budget $MaxUserGrowth"
}
if ($crashes -ne 0) { throw "Crash-rate gate failed: $crashes crashes." }
$crashRate = if ($iterations) { $crashes / $iterations } else { 0 }
$report = [ordered]@{
    timestampUtc = [DateTime]::UtcNow.ToString('o')
    executable = $resolved
    iterations = $iterations
    durationSeconds = $DurationSeconds
    peakWorkingSetMB = $peakMB
    workingSetGrowthMB = $growthMB
    handleGrowth = $handleGrowth
    threadGrowth = $threadGrowth
    gdiGrowth = $gdiGrowth
    userGrowth = $userGrowth
    crashes = $crashes
    crashRate = $crashRate
}
if ($Output) {
    $outputParent = Split-Path -Parent ([IO.Path]::GetFullPath($Output))
    if ($outputParent) {
        $null = New-Item -ItemType Directory -Force -Path $outputParent
    }
    $report | ConvertTo-Json | Set-Content -LiteralPath $Output -Encoding utf8
}
Write-Host ("Stability soak passed: " + ($report | ConvertTo-Json -Compress))
