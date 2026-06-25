# gen-icon.ps1 — generate res\liney.ico (multi-size, PNG-compressed entries).
#
# A dark-blue "L" mark matching the app theme. Sizes: 16/32/48/64/128/256.
# Run once and commit the .ico (the build's .rc references it).
#
# Usage: powershell -ExecutionPolicy Bypass -File tools\gen-icon.ps1

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
$res = Join-Path $root 'res'
New-Item -ItemType Directory -Force -Path $res | Out-Null
$icoPath = Join-Path $res 'liney.ico'

$bg = [System.Drawing.Color]::FromArgb(16, 40, 64)     # #102840
$fg = [System.Drawing.Color]::FromArgb(120, 200, 160)  # accent #78c8a0
$sizes = @(16, 32, 48, 64, 128, 256)

# Render each size to an in-memory PNG.
$pngs = @()
foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    # Rounded-ish background.
    $g.Clear($bg)
    $fontSize = [int]($s * 0.62)
    $font = New-Object System.Drawing.Font 'Segoe UI', $fontSize, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    $brush = New-Object System.Drawing.SolidBrush $fg
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
    $rect = New-Object System.Drawing.RectangleF 0, 0, $s, $s
    $g.DrawString('L', $font, $brush, $rect, $fmt)
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $pngs += , $ms.ToArray()
}

# Assemble the ICO container: ICONDIR + ICONDIRENTRY[] + PNG blobs.
$out = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $out
$bw.Write([uint16]0)            # reserved
$bw.Write([uint16]1)            # type = icon
$bw.Write([uint16]$sizes.Count) # image count
$offset = 6 + (16 * $sizes.Count)
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $data = $pngs[$i]
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))  # width  (0 == 256)
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))  # height
    $bw.Write([byte]0)             # color count
    $bw.Write([byte]0)             # reserved
    $bw.Write([uint16]1)           # planes
    $bw.Write([uint16]32)          # bit count
    $bw.Write([uint32]$data.Length)
    $bw.Write([uint32]$offset)
    $offset += $data.Length
}
foreach ($data in $pngs) { $bw.Write($data) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($icoPath, $out.ToArray())
$bw.Dispose(); $out.Dispose()
Write-Host "Wrote $icoPath ($([System.IO.File]::ReadAllBytes($icoPath).Length) bytes)"
