param(
    [Parameter(Mandatory = $true)] [string]$Exe,
    [int]$DurationSeconds = 60
)

$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Exe).Path
$deadline = [DateTime]::UtcNow.AddSeconds([Math]::Max(10, $DurationSeconds))
$iterations = 0
$peak = 0L
while ([DateTime]::UtcNow -lt $deadline) {
    $process = [Diagnostics.Process]::Start($resolved, 'stability-self-test')
    while (-not $process.HasExited) {
        $process.Refresh()
        $peak = [Math]::Max($peak, $process.WorkingSet64)
        if (-not $process.WaitForExit(100)) { continue }
    }
    if ($process.ExitCode -ne 0) {
        throw "Stability soak iteration $iterations failed with $($process.ExitCode)"
    }
    $iterations++
}
if ($iterations -lt 1) { throw 'Stability soak completed no iterations.' }
Write-Host "Stability soak passed: iterations=$iterations duration=${DurationSeconds}s peak=$([Math]::Ceiling($peak / 1MB))MB"
