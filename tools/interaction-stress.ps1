param(
    [Parameter(Mandatory = $true)] [string]$Exe,
    [int]$Iterations = 25
)

$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Exe).Path
$saved = @{
    LINEY_HEADLESS = $env:LINEY_HEADLESS
    LINEY_AUTOCLOSE_MS = $env:LINEY_AUTOCLOSE_MS
    LINEY_TEST_TABS = $env:LINEY_TEST_TABS
    LINEY_TEST_PANES = $env:LINEY_TEST_PANES
    LINEY_TEST_STRESS_OUTPUT = $env:LINEY_TEST_STRESS_OUTPUT
    LINEY_TEST_WIDTH = $env:LINEY_TEST_WIDTH
    LINEY_TEST_HEIGHT = $env:LINEY_TEST_HEIGHT
}
try {
    $env:LINEY_HEADLESS = '1'
    $env:LINEY_AUTOCLOSE_MS = '1800'
    $env:LINEY_TEST_STRESS_OUTPUT = '1'
    foreach ($i in 1..$Iterations) {
        $env:LINEY_TEST_TABS = [string](1 + ($i % 12))
        $env:LINEY_TEST_PANES = [string](1 + ($i % 8))
        $env:LINEY_TEST_WIDTH = [string](640 + (($i % 5) * 160))
        $env:LINEY_TEST_HEIGHT = [string](480 + (($i % 3) * 120))
        $process = Start-Process -FilePath $resolved -PassThru
        if (-not $process.WaitForExit(15000)) {
            $process.Kill()
            throw "Interaction stress iteration $i hung"
        }
        if ($process.ExitCode -ne 0) {
            throw "Interaction stress iteration $i failed with $($process.ExitCode)"
        }
    }

    # The built-in lifecycle fixture exits shells, cancels a long-running
    # reader and proves a replacement ConPTY can start in the same host.
    $process = Start-Process -FilePath $resolved `
        -ArgumentList 'stability-self-test' -PassThru
    if (-not $process.WaitForExit(45000)) {
        $process.Kill()
        throw 'Shell-crash isolation fixture timed out'
    }
    if ($process.ExitCode -ne 0) {
        throw "Shell-crash isolation failed with exit $($process.ExitCode)"
    }
} finally {
    foreach ($entry in $saved.GetEnumerator()) {
        if ($null -eq $entry.Value) {
            Remove-Item -Path "Env:$($entry.Key)" -ErrorAction SilentlyContinue
        } else {
            Set-Item -Path "Env:$($entry.Key)" -Value $entry.Value
        }
    }
}
Write-Host "Interaction stress passed: $Iterations tab/pane/resize cycles and shell crash/reconnect isolation."
