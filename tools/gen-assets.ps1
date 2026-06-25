# gen-assets.ps1 — generate placeholder MSIX tile/logo PNGs.
#
# Produces solid-color "L" tiles at the sizes AppxManifest.xml references. Swap
# these for real branding before publishing. Output: packaging\Assets\*.png
#
# Usage: powershell -ExecutionPolicy Bypass -File tools\gen-assets.ps1

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
$assets = Join-Path $root 'packaging\Assets'
New-Item -ItemType Directory -Force -Path $assets | Out-Null

$bg = [System.Drawing.Color]::FromArgb(16, 40, 64)     # #102840
$fg = [System.Drawing.Color]::FromArgb(120, 200, 160)  # accent

function New-Tile($w, $h, $file) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear($bg)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $size = [Math]::Max(8, [int]([Math]::Min($w, $h) * 0.55))
    $font = New-Object System.Drawing.Font 'Segoe UI', $size, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    $brush = New-Object System.Drawing.SolidBrush $fg
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
    $rect = New-Object System.Drawing.RectangleF 0, 0, $w, $h
    $g.DrawString('L', $font, $brush, $rect, $fmt)
    $g.Dispose()
    $bmp.Save((Join-Path $assets $file), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

New-Tile 44 44 'Square44x44Logo.png'
New-Tile 150 150 'Square150x150Logo.png'
New-Tile 310 150 'Wide310x150Logo.png'
New-Tile 50 50 'StoreLogo.png'
Write-Host "Wrote placeholder assets to $assets"
