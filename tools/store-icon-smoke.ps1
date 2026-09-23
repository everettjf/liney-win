param([Parameter(Mandatory = $true)][string]$Package)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.Drawing
$sdk = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'x64\makepri.exe') } |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $sdk) { throw 'MakePRI is required to verify icon resource selection' }
$root = Split-Path -Parent $PSScriptRoot
$report = Join-Path $root ('artifacts\store\icon-check-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $report -Force | Out-Null
$pri = Join-Path $report 'resources.pri'
$dump = Join-Path $report 'resources.xml'
$zip = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Package).Path)
try {
    $entry = $zip.GetEntry('resources.pri')
    if (-not $entry) { throw 'Package has no resources.pri' }
    [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $pri)
    & (Join-Path $sdk.FullName 'x64\makepri.exe') dump /if $pri /of $dump /dt detailed /o | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect packaged PRI' }
    $index = [xml](Get-Content -LiteralPath $dump -Raw)
    $resource = $index.SelectSingleNode("//NamedResource[@name='Square44x44Logo.png']")
    if (-not $resource) { throw 'Taskbar resource missing from PRI' }
    foreach ($size in @(16, 20, 24, 30, 32, 36, 40, 44, 48, 60, 64, 72, 80, 96, 256)) {
        foreach ($form in @('', 'unplated', 'lightunplated')) {
            $suffix = if ($form) { "_altform-$form" } else { '' }
            $name = "Assets/Square44x44Logo.targetsize-${size}${suffix}.png"
            $candidate = @($resource.Candidate | Where-Object { $_.Value.Replace('\', '/') -eq $name })
            if ($candidate.Count -ne 1) { throw "PRI candidate missing or duplicated: $name" }
            $qualifiers = $candidate[0].QualifierSet.Qualifier
            $target = $qualifiers | Where-Object name -EQ 'TargetSize'
            $alternate = $qualifiers | Where-Object name -EQ 'AlternateForm'
            if ($target.value -ne "$size" -or ($form -and $alternate.value -ne $form)) {
                throw "Incorrect resource qualifiers: $name"
            }
            $entry = $zip.GetEntry($name)
            if (-not $entry) { throw "Packaged icon missing: $name" }
            $stream = $entry.Open()
            $bitmap = [Drawing.Bitmap]::new($stream)
            try {
                if ($bitmap.Width -ne $size -or $bitmap.Height -ne $size) { throw "Wrong dimensions: $name" }
                if ($bitmap.GetPixel(0, 0).A -ne 0 -or
                    $bitmap.GetPixel($size - 1, [int]($size / 2)).A -ne 0) {
                    throw "Opaque backing: $name"
                }
            } finally { $bitmap.Dispose(); $stream.Dispose() }
        }
    }
} finally { $zip.Dispose() }
Write-Host 'Store icons verified: 45 indexed variants with transparent backgrounds.'
