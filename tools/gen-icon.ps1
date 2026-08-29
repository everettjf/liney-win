# gen-icon.ps1 — build res\liney.ico from macOS Liney's native-size icons.
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
$icoPath = Join-Path $res 'liney.ico'
$sizes = @(16, 32, 48, 64, 128, 256)
$sourceSizes = @{ 16 = 16; 32 = 32; 48 = 64; 64 = 64; 128 = 128; 256 = 256 }
$entries = New-Object System.Collections.ArrayList   # of byte[] (DIB per size)

foreach ($s in $sizes) {
    $sourceSize = $sourceSizes[$s]
    $src = Join-Path $res "liney-icon-$sourceSize.png"
    if (-not (Test-Path $src)) { throw "source icon not found: $src" }
    $source = [System.Drawing.Bitmap]::FromFile($src)
    $bmp = New-Object System.Drawing.Bitmap $s, $s
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = 'HighQualityBicubic'; $g.PixelOffsetMode = 'HighQuality'
    $g.SmoothingMode = 'HighQuality'; $g.CompositingQuality = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)
    # macOS reserves roughly 9% on each side. Windows taskbar icons look
    # undersized with that much inset, so trim 8% while preserving the icon's
    # own antialiased rounded edge. Keep the native 16px artwork untouched.
    $inset = if ($s -eq 16) { 0 } else { [Math]::Round($sourceSize * 0.08) }
    $sourceRect = New-Object System.Drawing.Rectangle $inset, $inset,
        ($source.Width - 2 * $inset), ($source.Height - 2 * $inset)
    $g.DrawImage($source, (New-Object System.Drawing.Rectangle 0, 0, $s, $s),
                 $sourceRect, [System.Drawing.GraphicsUnit]::Pixel)
    $g.Dispose()
    $source.Dispose()

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
Write-Host "Wrote $icoPath ($([System.IO.File]::ReadAllBytes($icoPath).Length) bytes) from macOS native-size icons"
