Add-Type -AssemblyName System.Drawing

$source = Join-Path $PSScriptRoot 'app_icon.png'
$output = Join-Path $PSScriptRoot 'app_icon.ico'
$src = [System.Drawing.Image]::FromFile($source)
try {
    $sizes = @(16, 24, 32, 48, 64, 128, 256)
    $entries = @()
    foreach ($size in $sizes) {
        $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $bmp.SetResolution(96, 96)
            $graphics = [System.Drawing.Graphics]::FromImage($bmp)
            try {
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $padding = 0
                $drawSize = $size - $padding
                $rect = New-Object System.Drawing.Rectangle $padding, $padding, $drawSize, $drawSize
                $graphics.DrawImage($src, $rect)
            } finally {
                $graphics.Dispose()
            }
            $stream = New-Object System.IO.MemoryStream
            $bmp.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            $entries += [pscustomobject]@{
                Width = $size
                Height = $size
                Data = $stream.ToArray()
            }
            $stream.Dispose()
        } finally {
            $bmp.Dispose()
        }
    }

    $ms = New-Object System.IO.MemoryStream
    $writer = New-Object System.IO.BinaryWriter($ms)
    $writer.Write([UInt16]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]$entries.Count)
    $offset = 6 + 16 * $entries.Count
    foreach ($entry in $entries) {
        $sizeByte = if ($entry.Width -ge 256) { 0 } else { $entry.Width }
        $writer.Write([Byte]$sizeByte)
        $writer.Write([Byte]$sizeByte)
        $writer.Write([Byte]0)
        $writer.Write([Byte]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]32)
        $writer.Write([UInt32]$entry.Data.Length)
        $writer.Write([UInt32]$offset)
        $offset += $entry.Data.Length
    }
    foreach ($entry in $entries) {
        $writer.Write($entry.Data)
    }
    $writer.Flush()
    [System.IO.File]::WriteAllBytes($output, $ms.ToArray())
    $writer.Dispose()
    $ms.Dispose()
} finally {
    $src.Dispose()
}
