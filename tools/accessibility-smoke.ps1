param([Parameter(Mandatory = $true)] [string]$Exe)

$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Exe).Path
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName Microsoft.VisualBasic
$oldClose = $env:LINEY_AUTOCLOSE_MS
$oldConfig = $env:LINEY_CONFIG_DIR
$testConfig = Join-Path $env:TEMP ('liney-accessibility-' + [Guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $testConfig | Out-Null
    '{"checkForUpdatesOnStartup":false}' |
        Set-Content -Encoding utf8 (Join-Path $testConfig 'config.json')
    $env:LINEY_CONFIG_DIR = $testConfig
    $env:LINEY_AUTOCLOSE_MS = '5000'
    $process = Start-Process -FilePath $resolved -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 50
        $process.Refresh()
        $window = $process.MainWindowHandle
    } while ($window -eq 0 -and -not $process.HasExited -and
             [DateTime]::UtcNow -lt $deadline)
    if ($process.HasExited -or $window -eq 0) {
        throw 'Liney did not expose a main window for UI Automation.'
    }
    $element = [System.Windows.Automation.AutomationElement]::FromHandle(
        $window)
    if ($element.Current.Name -ne 'Liney terminal workspace') {
        throw "Unexpected accessible name: $($element.Current.Name)"
    }
    if ($element.Current.AutomationId -ne 'Liney.MainWindow') {
        throw "Unexpected automation id: $($element.Current.AutomationId)"
    }
    if (-not $element.Current.IsKeyboardFocusable) {
        throw 'The terminal workspace is not keyboard focusable.'
    }
    if ($element.Current.AcceleratorKey -ne 'Ctrl+Shift+P') {
        throw "Command-palette accelerator is not exposed to UIA."
    }
    [void][Microsoft.VisualBasic.Interaction]::AppActivate($process.Id)
    [System.Windows.Forms.SendKeys]::SendWait('^+p')
    Start-Sleep -Milliseconds 150
    if ($process.HasExited) { throw 'Keyboard command caused the app to exit.' }
} finally {
    if ($process -and -not $process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if (-not $process.WaitForExit(3000)) { $process.Kill() }
    }
    $env:LINEY_AUTOCLOSE_MS = $oldClose
    $env:LINEY_CONFIG_DIR = $oldConfig
    Remove-Item -LiteralPath (Join-Path $testConfig 'config.json') -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $testConfig 'config.json.bak') -Force -ErrorAction SilentlyContinue
}
Write-Host 'UI Automation identity and keyboard command-palette smoke passed.'
