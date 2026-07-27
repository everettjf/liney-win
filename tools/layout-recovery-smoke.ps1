param([Parameter(Mandatory = $true)] [string]$Exe)

$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Exe).Path
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("liney-layout-" + [Guid]::NewGuid())
$profile = Join-Path $scratch 'profile'
$configDir = Join-Path $profile '.liney'
$null = New-Item -ItemType Directory -Force -Path $configDir
$config = '{"rememberLayout":true,"checkForUpdatesOnStartup":false}'
$layout = @'
{
  "schemaVersion": 1,
  "projects": [],
  "tabs": [{
    "root": {"type":"leaf","cwd":"","shell":"cmd.exe"},
    "title":"Recovered",
    "pinned":false
  }],
  "activeTab": 0
}
'@
Set-Content -LiteralPath (Join-Path $configDir 'config.json') -Value $config -Encoding utf8
Set-Content -LiteralPath (Join-Path $configDir 'layout.json') -Value '{broken' -Encoding utf8
Set-Content -LiteralPath (Join-Path $configDir 'layout.json.bak') -Value $layout -Encoding utf8
$savedProfile = $env:USERPROFILE
$savedHeadless = $env:LINEY_HEADLESS
$savedClose = $env:LINEY_AUTOCLOSE_MS
try {
    $env:USERPROFILE = $profile
    $env:LINEY_HEADLESS = '1'
    $env:LINEY_AUTOCLOSE_MS = '900'
    $process = Start-Process -FilePath $resolved -PassThru
    if (-not $process.WaitForExit(10000)) {
        $process.Kill()
        throw 'Layout recovery smoke timed out'
    }
    if ($process.ExitCode -ne 0) {
        throw "Layout recovery smoke failed with $($process.ExitCode)"
    }
    $primary = Get-Content -LiteralPath (Join-Path $configDir 'layout.json') -Raw |
        ConvertFrom-Json
    if ($primary.tabs[0].title -ne 'Recovered') {
        throw 'The valid backup did not repair the corrupt primary layout.'
    }
} finally {
    $env:USERPROFILE = $savedProfile
    $env:LINEY_HEADLESS = $savedHeadless
    $env:LINEY_AUTOCLOSE_MS = $savedClose
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host 'Atomic layout backup recovery passed.'
