param([Parameter(Mandatory = $true)] [string]$Exe)

$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Exe).Path
$probe = Start-Process -FilePath $resolved -ArgumentList 'tui-self-test' -PassThru
if (-not $probe.WaitForExit(45000)) {
    $probe.Kill()
    throw 'TUI compatibility fixture timed out'
}
if ($probe.ExitCode -ne 0) {
    throw "TUI compatibility fixture failed with $($probe.ExitCode)"
}
Write-Host 'TUI smoke passed for vim, less, and fzf through ConPTY.'
