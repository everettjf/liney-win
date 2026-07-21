param(
    [Parameter(Mandatory = $true)] [string]$Exe,
    [int]$Iterations = 50
)

$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Exe).Path
for ($i = 0; $i -lt $Iterations; $i++) {
    $process = [Diagnostics.Process]::Start($resolved, 'self-test')
    if (-not $process.WaitForExit(15000)) {
        $process.Kill()
        throw "ConPTY/VT soak iteration $i timed out"
    }
    if ($process.ExitCode -ne 0) {
        throw "ConPTY/VT soak iteration $i failed with $($process.ExitCode)"
    }
}
Write-Host "ConPTY/VT lifecycle soak passed: $Iterations iterations"
