param(
    [Parameter(Mandatory = $true)] [string]$Installer,
    [Parameter(Mandatory = $true)] [string]$PortableZip,
    [Parameter(Mandatory = $true)] [string]$ScratchRoot,
    [string]$PreviousInstaller = ''
)

$ErrorActionPreference = 'Stop'
$installerPath = (Resolve-Path -LiteralPath $Installer).Path
$firstInstallerPath = if ($PreviousInstaller) {
    (Resolve-Path -LiteralPath $PreviousInstaller).Path
} else {
    $installerPath
}
$zipPath = (Resolve-Path -LiteralPath $PortableZip).Path
$installDir = Join-Path $ScratchRoot 'installed'
$portableDir = Join-Path $ScratchRoot 'portable'
New-Item -ItemType Directory -Force -Path $ScratchRoot | Out-Null

$install = Start-Process -FilePath $firstInstallerPath -ArgumentList @('/S', "/D=$installDir") -PassThru -Wait
if ($install.ExitCode -ne 0) { throw "Silent install failed: $($install.ExitCode)" }
$installedExe = Join-Path $installDir 'Liney.exe'
if (-not (Test-Path -LiteralPath $installedExe)) { throw 'Installer did not produce Liney.exe' }
if ($PreviousInstaller) {
    # Old releases do not necessarily know the newest self-test verbs. Verify
    # only clean-machine GUI startup here; the upgraded binary gets the full
    # current smoke suite below.
    $oldHeadless = $env:LINEY_HEADLESS
    $oldAutoClose = $env:LINEY_AUTOCLOSE_MS
    try {
        $env:LINEY_HEADLESS = '1'
        $env:LINEY_AUTOCLOSE_MS = '700'
        $previousLaunch = Start-Process -FilePath $installedExe -PassThru
        if (-not $previousLaunch.WaitForExit(15000)) {
            $previousLaunch.Kill()
            throw 'Previous release did not complete the startup lifecycle.'
        }
        if ($previousLaunch.ExitCode -ne 0) {
            throw "Previous release startup failed: $($previousLaunch.ExitCode)"
        }
    } finally {
        $env:LINEY_HEADLESS = $oldHeadless
        $env:LINEY_AUTOCLOSE_MS = $oldAutoClose
    }
} else {
    & (Join-Path $PSScriptRoot 'smoke-test.ps1') -Exe $installedExe
}

# Exercise the in-place upgrade path (from the previous stable release when
# supplied) as well as a first install. A marker in
# the install directory represents user-owned state that an update must keep.
$upgradeMarker = Join-Path $installDir 'liney-upgrade-smoke.marker'
Set-Content -LiteralPath $upgradeMarker -Value 'preserve' -Encoding ascii
$upgrade = Start-Process -FilePath $installerPath -ArgumentList @('/S', "/D=$installDir") -PassThru -Wait
if ($upgrade.ExitCode -ne 0) { throw "In-place upgrade failed: $($upgrade.ExitCode)" }
if (-not (Test-Path -LiteralPath $upgradeMarker)) { throw 'Upgrade removed existing user state' }
& (Join-Path $PSScriptRoot 'smoke-test.ps1') -Exe $installedExe

Expand-Archive -LiteralPath $zipPath -DestinationPath $portableDir -Force
$portableExe = Get-ChildItem $portableDir -Recurse -Filter Liney.exe | Select-Object -First 1
if (-not $portableExe) { throw 'Portable archive did not contain Liney.exe' }
& (Join-Path $PSScriptRoot 'smoke-test.ps1') -Exe $portableExe.FullName

$uninstaller = Join-Path $installDir 'Uninstall.exe'
if (-not (Test-Path -LiteralPath $uninstaller)) { throw 'Uninstaller was not installed' }
$uninstall = Start-Process -FilePath $uninstaller -ArgumentList '/S' -PassThru -Wait
if ($uninstall.ExitCode -ne 0) { throw "Silent uninstall failed: $($uninstall.ExitCode)" }
if (Test-Path -LiteralPath $installedExe) { throw 'Uninstall left Liney.exe behind' }
Write-Host 'Installer, portable archive, and uninstall smoke tests passed.'
