# gen-icon.ps1 — build res\liney.ico from res\liney-icon.png (liney's app icon).
#
# Resizes the 1024px source to standard sizes (16..256) with alpha-preserving
# resampling and assembles a .ico using 32-bit BMP/DIB entries (the format
# Windows shells / taskbar / .NET decode reliably at every size). Run once and
# commit res\liney.ico (the build's .rc references it).
#
# Usage: powershell -ExecutionPolicy Bypass -File tools\gen-icon.ps1

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
$res = Join-Path $root 'res'
$src = Join-Path $res 'liney-icon.png'
$icoPath = Join-Path $res 'liney.ico'
if (-not (Test-Path $src)) { throw "source icon not found: $src" }

$source = [System.Drawing.Image]::FromFile($src)
$sizes = @(16, 32, 48, 64, 128, 256)
$entries = New-Object System.Collections.ArrayList   # of byte[] (DIB per size)

foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = 'HighQualityBicubic'; $g.PixelOffsetMode = 'HighQuality'
    $g.SmoothingMode = 'HighQuality'; $g.CompositingQuality = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($source, (New-Object System.Drawing.Rectangle 0, 0, $s, $s))
    $g.Dispose()

    $rect = New-Object System.Drawing.Rectangle 0, 0, $s, $s
    $bd = $bmp.LockBits($rect, 'ReadOnly', 'Format32bppArgb')
    $stride = $bd.Stride
    $pix = New-Object byte[] ($stride * $s)
    [System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0, $pix, 0, $pix.Length)
    $bmp.UnlockBits($bd); $bmp.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter $ms
    # BITMAPINFOHEADER (height doubled: XOR colour + AND mask)
    $bw.Write([uint32]40); $bw.Write([int32]$s); $bw.Write([int32]($s * 2))
    $bw.Write([uint16]1); $bw.Write([uint16]32); $bw.Write([uint32]0)
    $bw.Write([uint32]0); $bw.Write([int32]0); $bw.Write([int32]0)
    $bw.Write([uint32]0); $bw.Write([uint32]0)
    for ($y = $s - 1; $y -ge 0; $y--) { $bw.Write($pix, $y * $stride, $s * 4) }  # XOR, bottom-up BGRA
    $maskRow = [int]([Math]::Ceiling([Math]::Ceiling($s / 8.0) / 4.0) * 4)
    $bw.Write((New-Object byte[] ($maskRow * $s)))                                # AND mask, all-zero
    $bw.Flush()
    [void]$entries.Add($ms.ToArray())
    $bw.Dispose(); $ms.Dispose()
}
$source.Dispose()

$out = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $out
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
$offset = 6 + (16 * $sizes.Count)
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; [byte[]]$data = $entries[$i]
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $bw.Write([byte]0); $bw.Write([byte]0); $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$data.Length); $bw.Write([uint32]$offset)
    $offset += $data.Length
}
for ($i = 0; $i -lt $sizes.Count; $i++) { [byte[]]$data = $entries[$i]; $bw.Write($data, 0, $data.Length) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($icoPath, $out.ToArray())
$bw.Dispose(); $out.Dispose()
Write-Host "Wrote $icoPath ($([System.IO.File]::ReadAllBytes($icoPath).Length) bytes) from liney's icon"
