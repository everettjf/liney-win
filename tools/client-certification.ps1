param(
    [Parameter(Mandatory = $true)] [ValidateSet('Windows10','Windows11')]
    [string]$Target,
    [Parameter(Mandatory = $true)] [string]$Exe,
    [Parameter(Mandatory = $true)] [string]$Installer,
    [Parameter(Mandatory = $true)] [string]$PortableZip,
    [Parameter(Mandatory = $true)] [string]$PreviousInstaller,
    [string]$Output = 'client-certification.json'
)

$ErrorActionPreference = 'Stop'
$build = [int](Get-ItemPropertyValue `
    'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' CurrentBuildNumber)
if ($Target -eq 'Windows10' -and ($build -lt 19045 -or $build -ge 22000)) {
    throw "Windows 10 22H2 certification requires build 19045; found $build."
}
if ($Target -eq 'Windows11' -and $build -lt 22000) {
    throw "Windows 11 certification requires build 22000 or newer; found $build."
}

Add-Type @'
using System.Runtime.InteropServices;
public static class LineyDisplayCount {
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
}
'@
$monitors = [LineyDisplayCount]::GetSystemMetrics(80) # SM_CMONITORS
if ($Target -eq 'Windows11' -and $monitors -lt 2) {
    throw 'Windows 11 certification requires at least two active monitors.'
}

$resolvedExe = (Resolve-Path -LiteralPath $Exe).Path
$scratch = Join-Path $env:TEMP ('liney-client-cert-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
& (Join-Path $PSScriptRoot 'compatibility-smoke.ps1') `
    -Exe $resolvedExe -Output (Join-Path $scratch 'compatibility.json')
& (Join-Path $PSScriptRoot 'power-resume-smoke.ps1') -Exe $resolvedExe
$remote = [Diagnostics.Process]::Start($resolvedExe, 'remote-self-test require-wsl')
if (-not $remote.WaitForExit(30000)) {
    $remote.Kill()
    throw 'Required WSL/SSH client certification timed out.'
}
if ($remote.ExitCode -ne 0) {
    throw "Required WSL/SSH client certification failed: $($remote.ExitCode)."
}
& (Join-Path $PSScriptRoot 'installer-smoke.ps1') `
    -Installer $Installer -PortableZip $PortableZip `
    -PreviousInstaller $PreviousInstaller `
    -ScratchRoot (Join-Path $scratch 'packages')

$os = Get-CimInstance Win32_OperatingSystem
[ordered]@{
    timestampUtc = [DateTime]::UtcNow.ToString('o')
    target = $Target
    productName = $os.Caption
    version = $os.Version
    build = $build
    architecture = $os.OSArchitecture
    monitors = $monitors
    wslRequired = $true
    sshDisconnectRestart = 'passed'
    installUpgradePortableUninstall = 'passed'
    gpuDpiDeviceLoss = 'passed'
    suspendResumeBroadcast = 'passed'
} | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 $Output
Write-Host "$Target client certification passed; report: $Output"
