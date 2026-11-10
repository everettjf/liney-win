param([string]$Shell = 'powershell.exe')
$ErrorActionPreference = 'Stop'
# Execute the generated integration script in an isolated, profile-free shell.
$source = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../src/core/ShellProfiles.cpp') -Raw
$script = [regex]::Match($source, '(?s)R"PS1\((.*?)\)PS1"').Groups[1].Value
if (-not $script) { throw 'Integration script missing' }
& $Shell -NoLogo -NoProfile -NonInteractive -Command {
    param($integration)
    $ProgressPreference = 'SilentlyContinue'
    $ErrorActionPreference = 'Stop'
    function global:git { $global:GitArguments = @($args); $global:LASTEXITCODE = 17 }
    & ([scriptblock]::Create($integration))
    foreach ($item in @(@('GS','status'), @('ga','add'), @('GC','commit'),
                       @('gl','log'), @('gd','diff'), @('gb','branch'))) {
        & $item[0] '--' 'file with spaces.txt'
        if (($global:GitArguments -join '|') -ne ($item[1] + '|--|file with spaces.txt') -or
            $global:LASTEXITCODE -ne 17) { throw "Shortcut failed: $($item[0])" }
    }
    Remove-Item Env:LINEY_SHELL_INTEGRATION_ACTIVE
    Set-Alias -Name ga -Value Write-Output -Scope Global
    Remove-Item Alias:gb
    function global:gb { 'custom-branch' }
    & ([scriptblock]::Create($integration))
    if ((ga 'custom-add') -ne 'custom-add' -or (gb) -ne 'custom-branch') {
        throw 'Custom command overwritten'
    }
    Write-Output 'Git shortcut smoke passed: arguments, case, exit codes, custom commands.'
} -args $script
if ($LASTEXITCODE -ne 0) { throw 'Git shortcut smoke failed' }
