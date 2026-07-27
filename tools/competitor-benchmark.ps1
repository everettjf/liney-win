param(
    [Parameter(Mandatory = $true)] [string]$LineyExe,
    [int]$Iterations = 5,
    [string]$Output = 'terminal-benchmark.json'
)

$ErrorActionPreference = 'Stop'
$liney = (Resolve-Path -LiteralPath $LineyExe).Path
$scratch = Join-Path ([IO.Path]::GetTempPath()) (
    'liney-terminal-benchmark-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null

$payload = Join-Path $scratch 'payload.ps1'
@'
param([Parameter(Mandatory=$true)][string]$Marker)
1..20000 | ForEach-Object {
    [Console]::WriteLine(('line {0:D5} alpha beta gamma 0123456789' -f $_))
}
[IO.File]::WriteAllText($Marker, 'done')
'@ | Set-Content -LiteralPath $payload -Encoding utf8

function Wait-Marker([string]$Marker, [Diagnostics.Stopwatch]$Watch) {
    while ($Watch.ElapsedMilliseconds -lt 15000) {
        if (Test-Path -LiteralPath $Marker) { return }
        Start-Sleep -Milliseconds 10
    }
    throw "Benchmark payload timed out: $Marker"
}

function Measure-Terminal([string]$Name, [scriptblock]$Launch) {
    $samples = @()
    for ($i = 0; $i -lt $Iterations; $i++) {
        $marker = Join-Path $scratch "$Name-$i.done"
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $process = & $Launch $marker
        Wait-Marker $marker $watch
        $watch.Stop()
        $samples += [int]$watch.ElapsedMilliseconds
        if ($process -and -not $process.HasExited) {
            if (-not $process.WaitForExit(10000)) {
                $process.Kill()
                throw "$Name benchmark process did not exit"
            }
        }
    }
    $sorted = $samples | Sort-Object
    [ordered]@{
        samplesMs = $samples
        medianMs = $sorted[[Math]::Floor($sorted.Count / 2)]
        p95Ms = $sorted[[Math]::Min(
            $sorted.Count - 1, [Math]::Ceiling($sorted.Count * 0.95) - 1)]
    }
}

$saved = @{
    LINEY_HEADLESS = $env:LINEY_HEADLESS
    LINEY_AUTOCLOSE_MS = $env:LINEY_AUTOCLOSE_MS
    LINEY_TEST_STRESS_OUTPUT = $env:LINEY_TEST_STRESS_OUTPUT
    LINEY_STRESS_MARKER = $env:LINEY_STRESS_MARKER
}
try {
    $results = [ordered]@{}
    $results.Liney = Measure-Terminal 'liney' {
        param($marker)
        $env:LINEY_HEADLESS = '1'
        $env:LINEY_AUTOCLOSE_MS = '1500'
        $env:LINEY_TEST_STRESS_OUTPUT = '1'
        $env:LINEY_STRESS_MARKER = $marker
        Start-Process -FilePath $liney -PassThru
    }

    $commands = [ordered]@{
        WindowsTerminal = Get-Command wt.exe -ErrorAction SilentlyContinue
        WezTerm = Get-Command wezterm.exe -ErrorAction SilentlyContinue
        Alacritty = Get-Command alacritty.exe -ErrorAction SilentlyContinue
    }
    foreach ($entry in $commands.GetEnumerator()) {
        if (-not $entry.Value) {
            $results[$entry.Key] = [ordered]@{ status = 'not-installed' }
            continue
        }
        $exe = $entry.Value.Source
        if ($entry.Key -eq 'WindowsTerminal') {
            $results[$entry.Key] = Measure-Terminal 'windows-terminal' {
                param($marker)
                Start-Process -FilePath $exe -ArgumentList @(
                    '-w', 'new', 'powershell.exe', '-NoLogo', '-NoProfile',
                    '-ExecutionPolicy', 'Bypass', '-File', $payload,
                    '-Marker', $marker) -PassThru
            }
        } elseif ($entry.Key -eq 'WezTerm') {
            $results[$entry.Key] = Measure-Terminal 'wezterm' {
                param($marker)
                Start-Process -FilePath $exe -ArgumentList @(
                    'start', '--always-new-process', '--', 'powershell.exe',
                    '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                    '-File', $payload, '-Marker', $marker) -PassThru
            }
        } else {
            $results[$entry.Key] = Measure-Terminal 'alacritty' {
                param($marker)
                Start-Process -FilePath $exe -ArgumentList @(
                    '-e', 'powershell.exe', '-NoLogo', '-NoProfile',
                    '-ExecutionPolicy', 'Bypass', '-File', $payload,
                    '-Marker', $marker) -PassThru
            }
        }
    }

    $cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
    $gpu = Get-CimInstance Win32_VideoController | Select-Object -First 1
    [ordered]@{
        timestampUtc = [DateTime]::UtcNow.ToString('o')
        methodology = 'fresh window; PowerShell 7-compatible 20,000-line payload; invocation-to-marker; 10ms polling'
        iterations = $Iterations
        cpu = $cpu.Name
        gpu = $gpu.Name
        os = (Get-CimInstance Win32_OperatingSystem).Caption
        results = $results
        caveat = 'Measures startup plus producer/backpressure completion, not frame latency or dropped-frame percentiles.'
    } | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $Output -Encoding utf8
    Write-Host "Terminal benchmark complete: $Output"
} finally {
    foreach ($entry in $saved.GetEnumerator()) {
        if ($null -eq $entry.Value) {
            Remove-Item -Path "Env:$($entry.Key)" -ErrorAction SilentlyContinue
        } else {
            Set-Item -Path "Env:$($entry.Key)" -Value $entry.Value
        }
    }
    if (Test-Path -LiteralPath $scratch) {
        Remove-Item -LiteralPath $scratch -Recurse -Force
    }
}
