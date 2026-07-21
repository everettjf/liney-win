param(
    [Parameter(Mandatory = $true)]
    [string]$Exe
)

$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Exe).Path

# The GUI binary also exposes a non-interactive CLI. This verifies the EXE can
# load all app-local DLLs on a clean machine without leaving a window behind.
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $resolved
$start.Arguments = 'title liney-smoke'
$start.UseShellExecute = $false
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$process = [Diagnostics.Process]::Start($start)
if (-not $process.WaitForExit(10000)) {
    $process.Kill()
    throw 'Liney smoke command did not exit within 10 seconds.'
}
$output = $process.StandardOutput.ReadToEnd()
$errors = $process.StandardError.ReadToEnd()
if ($process.ExitCode -ne 0) {
    throw "Liney smoke command failed with exit code $($process.ExitCode)`n$errors"
}
if ($output -notmatch [regex]::Escape("$([char]27)]2;liney-smoke$([char]7)")) {
    throw 'Liney smoke command did not emit the expected OSC title sequence.'
}

$required = @('Liney.exe', 'ghostty-vt.dll', 'msvcp140.dll', 'vcruntime140.dll')
$dir = Split-Path -Parent $resolved
foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $dir $name))) {
        throw "Missing app-local runtime dependency: $name"
    }
}

Write-Host "Smoke test passed: $resolved"

$selfTest = [Diagnostics.Process]::Start($resolved, 'self-test')
if (-not $selfTest.WaitForExit(15000)) {
    $selfTest.Kill()
    throw 'ConPTY/VT self-test timed out.'
}
if ($selfTest.ExitCode -ne 0) {
    throw "ConPTY/VT self-test failed with exit code $($selfTest.ExitCode)."
}
Write-Host 'ConPTY/VT self-test passed.'

$configTest = [Diagnostics.Process]::Start($resolved, 'config-self-test')
if (-not $configTest.WaitForExit(15000)) {
    $configTest.Kill()
    throw 'Configuration recovery self-test timed out.'
}
if ($configTest.ExitCode -ne 0) {
    throw "Configuration recovery self-test failed with exit code $($configTest.ExitCode)."
}
Write-Host 'Configuration recovery self-test passed.'

$processTest = [Diagnostics.Process]::Start($resolved, 'process-self-test')
if (-not $processTest.WaitForExit(10000)) {
    $processTest.Kill()
    throw 'Bounded child-process self-test timed out.'
}
if ($processTest.ExitCode -ne 0) {
    throw "Bounded child-process self-test failed with exit code $($processTest.ExitCode)."
}
Write-Host 'Bounded child-process capture/timeout self-test passed.'

$semanticTest = [Diagnostics.Process]::Start($resolved, 'semantic-self-test')
if (-not $semanticTest.WaitForExit(15000)) {
    $semanticTest.Kill()
    throw 'OSC 133 semantic command self-test timed out.'
}
if ($semanticTest.ExitCode -ne 0) {
    throw "OSC 133 semantic command self-test failed with exit code $($semanticTest.ExitCode)."
}
Write-Host 'OSC 133 semantic command self-test passed.'

$shellIntegration = [Diagnostics.Process]::Start($resolved, 'shell-integration-self-test')
# Cold Windows PowerShell startup and ConPTY cleanup can exceed 15 seconds on
# contended GitHub-hosted Windows runners even after the assertions complete.
if (-not $shellIntegration.WaitForExit(30000)) {
    $shellIntegration.Kill()
    throw 'PowerShell integration self-test timed out.'
}
if ($shellIntegration.ExitCode -ne 0) {
    throw "PowerShell integration self-test failed with exit code $($shellIntegration.ExitCode)."
}
Write-Host 'PowerShell profile integration self-test passed.'

$vtTest = [Diagnostics.Process]::Start($resolved, 'vt-regression-self-test')
if (-not $vtTest.WaitForExit(15000)) {
    $vtTest.Kill()
    throw 'VT regression self-test timed out.'
}
if ($vtTest.ExitCode -ne 0) {
    throw "VT regression self-test failed with exit code $($vtTest.ExitCode)."
}
Write-Host 'VT regression self-test passed.'

$font = [Diagnostics.Process]::Start($resolved, 'font-self-test')
if (-not $font.WaitForExit(10000)) {
    $font.Kill()
    throw 'DirectWrite font fallback self-test timed out.'
}
if ($font.ExitCode -ne 0) {
    throw "DirectWrite font fallback self-test failed with exit code $($font.ExitCode)."
}
Write-Host 'DirectWrite font fallback/emoji shaping self-test passed.'

$agentStart = [Diagnostics.ProcessStartInfo]::new()
$agentStart.FileName = $resolved
$agentStart.Arguments = 'agent-status waiting'
$agentStart.UseShellExecute = $false
$agentStart.RedirectStandardOutput = $true
$agent = [Diagnostics.Process]::Start($agentStart)
$agent.WaitForExit(5000) | Out-Null
$agentOutput = $agent.StandardOutput.ReadToEnd()
if ($agent.ExitCode -ne 0 -or
    $agentOutput -notmatch [regex]::Escape("$([char]27)]777;agent-status;waiting$([char]7)")) {
    throw 'Agent status companion protocol self-test failed.'
}
Write-Host 'Agent status protocol self-test passed.'
