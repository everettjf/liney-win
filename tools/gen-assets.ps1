# gen-assets.ps1 — generate MSIX tile/logo PNGs from the primary Liney icon.
#
# Output: packaging\Assets\*.png
# Usage: powershell -ExecutionPolicy Bypass -File tools\gen-assets.ps1

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
$sourcePath = Join-Path $root 'res\liney-icon.png'
$taskbarSourcePath = Join-Path $root 'res\liney-icon-64.png'
$assets = Join-Path $root 'packaging\Assets'
New-Item -ItemType Directory -Force -Path $assets | Out-Null

if (-not (Test-Path $sourcePath)) {
    throw "Primary application icon not found: $sourcePath"
}
if (-not (Test-Path $taskbarSourcePath)) {
    throw "Taskbar icon not found: $taskbarSourcePath"
}

$source = [System.Drawing.Bitmap]::FromFile($sourcePath)
$alphaBounds = [System.Drawing.Rectangle]::Empty
for ($y = 0; $y -lt $source.Height; $y++) {
    for ($x = 0; $x -lt $source.Width; $x++) {
        if ($source.GetPixel($x, $y).A -eq 0) { continue }
        $point = New-Object System.Drawing.Rectangle $x, $y, 1, 1
        $alphaBounds = if ($alphaBounds.IsEmpty) {
            $point
        } else {
            [System.Drawing.Rectangle]::Union($alphaBounds, $point)
        }
    }
}
if ($alphaBounds.IsEmpty) { throw 'source icon has no visible pixels' }

function New-Asset($width, $height, $file, $image = $source,
                   $sourceInset = 0) {
    $bitmap = New-Object System.Drawing.Bitmap $width, $height,
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.CompositingQuality = 'HighQuality'
    $graphics.InterpolationMode = 'HighQualityBicubic'
    $graphics.PixelOffsetMode = 'HighQuality'
    $graphics.SmoothingMode = 'HighQuality'

    $side = [Math]::Min($width, $height)
    $x = [int](($width - $side) / 2)
    $y = [int](($height - $side) / 2)
    $dest = New-Object System.Drawing.Rectangle $x, $y, $side, $side
    $sourceRect = New-Object System.Drawing.Rectangle $sourceInset, $sourceInset,
        ($image.Width - 2 * $sourceInset), ($image.Height - 2 * $sourceInset)
    $graphics.DrawImage($image, $dest, $sourceRect,
                        [System.Drawing.GraphicsUnit]::Pixel)

    $graphics.Dispose()
    $bitmap.Save((Join-Path $assets $file),
        [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}

try {
    $taskbarSource = [System.Drawing.Bitmap]::FromFile($taskbarSourcePath)
    New-Asset 44 44 'Square44x44Logo.png' $taskbarSource 5
    New-Asset 150 150 'Square150x150Logo.png'
    New-Asset 310 150 'Wide310x150Logo.png'
    New-Asset 50 50 'StoreLogo.png'
} finally {
    if ($null -ne $taskbarSource) { $taskbarSource.Dispose() }
    $source.Dispose()
}

Write-Host "Wrote branded MSIX assets to $assets with the macOS icon's safe area"
