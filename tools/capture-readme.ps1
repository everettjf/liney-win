param(
    [string]$Exe = '.\build-store\Liney.exe',
    [string]$OutputDir = 'docs'
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$resolvedExe = (Resolve-Path -LiteralPath $Exe).Path
$outputRoot = [IO.Path]::GetFullPath((Join-Path $PWD $OutputDir))
$null = New-Item -ItemType Directory -Force -Path $outputRoot

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'liney-readme-' + [Guid]::NewGuid().ToString('N'))
$configDir = Join-Path $fixtureRoot 'config'
$null = New-Item -ItemType Directory -Force -Path $configDir
$workspaceDir = Join-Path $fixtureRoot 'workspace'
$demoRoot = Join-Path $workspaceDir 'liney-win'
$atlasRoot = Join-Path $workspaceDir 'atlas-web'
$notesRoot = Join-Path $workspaceDir 'team-notes'
$null = New-Item -ItemType Directory -Force -Path @(
    $demoRoot,
    $atlasRoot,
    $notesRoot,
    (Join-Path $demoRoot 'docs'),
    (Join-Path $demoRoot 'res'),
    (Join-Path $demoRoot 'src'),
    (Join-Path $demoRoot 'tests'),
    (Join-Path $demoRoot 'tools')
)

# Use a compact disposable repository so the file panel and Git output stay
# representative without exposing or depending on the developer's worktree.
foreach ($file in @(
    'CHANGELOG.md',
    'CMakeLists.txt',
    'CONTRIBUTING.md',
    'LICENSE',
    'README.md',
    'README.zh-CN.md',
    'ROADMAP.md'
)) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $file) `
        -Destination (Join-Path $demoRoot $file)
}

function Invoke-DemoGit([string[]]$gitArguments) {
    & git @gitArguments | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "README fixture git command failed: git $($gitArguments -join ' ')"
    }
}

Invoke-DemoGit @('-C', $demoRoot, 'init', '-q', '-b', 'main')
Invoke-DemoGit @('-C', $demoRoot, 'config', 'core.autocrlf', 'false')
Invoke-DemoGit @('-C', $demoRoot, 'config', 'user.name', 'Liney')
Invoke-DemoGit @('-C', $demoRoot, 'config', 'user.email', 'liney@example.com')
Invoke-DemoGit @('-C', $demoRoot, 'add', '.')
Invoke-DemoGit @('-C', $demoRoot, 'commit', '-q', '-m',
    'Add command palette and workspace snapshots')
Invoke-DemoGit @('-C', $demoRoot, 'commit', '-q', '--allow-empty', '-m',
    'Prepare Microsoft Store package')
Invoke-DemoGit @('-C', $demoRoot, 'commit', '-q', '--allow-empty', '-m',
    'Release 0.10.6')
Invoke-DemoGit @('-C', $demoRoot, 'commit', '-q', '--allow-empty', '-m',
    'Align small button glyphs')
Invoke-DemoGit @('-C', $demoRoot, 'remote', 'add', 'origin',
    'https://github.com/everettjf/liney-win.git')
Invoke-DemoGit @('-C', $demoRoot, 'update-ref', 'refs/remotes/origin/main',
    'HEAD')
Invoke-DemoGit @('-C', $demoRoot, 'branch', '--set-upstream-to',
    'origin/main', 'main')

$savedEnvironment = @{}
$environmentKeys = @(
    'LINEY_HEADLESS', 'LINEY_CONFIG_DIR', 'LINEY_AUTOCLOSE_MS',
    'LINEY_CAPTURE_DELAY_MS', 'LINEY_CAPTURE_PNG', 'LINEY_FORCE_WARP',
    'LINEY_TEST_WIDTH', 'LINEY_TEST_HEIGHT', 'LINEY_TEST_DPI',
    'LINEY_TEST_TABS', 'LINEY_TEST_PANES', 'LINEY_TEST_FILES_PANEL',
    'LINEY_TEST_PALETTE', 'USERPROFILE'
)
foreach ($key in $environmentKeys) {
    $savedEnvironment[$key] = [Environment]::GetEnvironmentVariable($key)
}

function Write-DemoConfig([string]$shell) {
    $config = [ordered]@{
        schemaVersion = 1
        shell = $shell
        fontFamily = 'Cascadia Mono'
        fontSize = 16
        scrollback = 10000
        workspaceRoot = ''
        projects = @($demoRoot, $atlasRoot, $notesRoot)
        projectIcons = @{
            'liney-win' = 'builtin:terminal'
            'atlas-web' = 'builtin:globe'
            'team-notes' = 'builtin:book'
        }
        sshHosts = @('dev@build.example.com', 'deploy@staging.example.com')
        agents = @(
            @{ name = 'Codex'; command = 'codex'; cwd = $demoRoot },
            @{ name = 'Claude'; command = 'claude'; cwd = $demoRoot }
        )
        rememberLayout = $false
        checkForUpdatesOnStartup = $false
        theme = 'Emerald Night'
    }
    $config |
        ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath (Join-Path $configDir 'config.json') -Encoding utf8
}

function Capture-ReadmeImage(
    [string]$name,
    [int]$tabs,
    [int]$panes,
    [bool]$files,
    [bool]$palette,
    [string]$shell
) {
    Write-DemoConfig $shell
    $capture = Join-Path $outputRoot ($name + '.png')

    $env:LINEY_HEADLESS = '1'
    $env:LINEY_CONFIG_DIR = $configDir
    $env:USERPROFILE = $demoRoot
    $env:LINEY_AUTOCLOSE_MS = '2500'
    $env:LINEY_CAPTURE_DELAY_MS = '1500'
    $env:LINEY_CAPTURE_PNG = $capture
    $env:LINEY_FORCE_WARP = '1'
    $env:LINEY_TEST_WIDTH = '1280'
    $env:LINEY_TEST_HEIGHT = '720'
    $env:LINEY_TEST_DPI = '96'
    $env:LINEY_TEST_TABS = [string]$tabs
    $env:LINEY_TEST_PANES = [string]$panes
    $env:LINEY_TEST_FILES_PANEL = if ($files) { '1' } else { $null }
    $env:LINEY_TEST_PALETTE = if ($palette) { '1' } else { $null }

    $process = Start-Process -FilePath $resolvedExe -PassThru
    if (-not $process.WaitForExit(15000)) {
        $process.Kill()
        throw "README capture '$name' timed out"
    }
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $capture)) {
        throw "README capture '$name' failed"
    }
    Write-Host "Captured $capture"
}

$heroShell = 'cmd.exe /d /q /k "cls & title liney-win & ' +
    'git -c color.ui=always status --short --branch & echo. & ' +
    'git -c color.ui=always log --oneline --decorate -5 & echo. & ' +
    'prompt $E[38;2;120;200;160mliney$E[0m C:\dev\liney-win$G"'
$splitShell = 'cmd.exe /d /q /k "cls & title liney-win & ' +
    'echo Workspace ready & echo Branch: & git branch --show-current & echo. & ' +
    'prompt $E[38;2;120;200;160mliney$E[0m C:\dev\liney-win$G"'
$paletteShell = 'cmd.exe /d /q /k "cls & title liney-win & prompt $S"'

try {
    Capture-ReadmeImage 'screenshot' 3 1 $true $false $heroShell
    Capture-ReadmeImage 'screenshot-splits' 3 3 $true $false $splitShell
    Capture-ReadmeImage 'screenshot-palette' 3 1 $true $true $paletteShell
} finally {
    foreach ($key in $environmentKeys) {
        [Environment]::SetEnvironmentVariable($key, $savedEnvironment[$key])
    }

    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
    $safeFixture = $resolvedFixture.StartsWith(
        $tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedFixture).StartsWith('liney-readme-')
    if ($safeFixture) {
        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force `
            -ErrorAction SilentlyContinue
    }
}
