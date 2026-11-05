# Renders the original SVG using Windows GDI+, then packs PNG frames into an ICO.
# Supported SVG subset: absolute M/L/C/Z paths, rounded rectangles and circles.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot -Parent
[xml]$svg = Get-Content -LiteralPath (Join-Path $root 'resources\icon.svg') -Raw
$sizes = @(16,20,24,32,40,48,64,96,128,256)
$frames = [System.Collections.Generic.List[byte[]]]::new()
foreach ($size in $sizes) {
    $bitmap = [System.Drawing.Bitmap]::new($size * 4, $size * 4)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = 'AntiAlias'
    $graphics.ScaleTransform($size / 64.0, $size / 64.0)
    foreach ($node in $svg.DocumentElement.ChildNodes) {
        if ($node.NodeType -ne 'Element') { continue }
        if ($size -lt 32 -and $node.GetAttribute('class') -eq 'detail') { continue }
        $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
        switch ($node.LocalName) {
            'rect' {
                $x=[float]$node.x; $y=[float]$node.y
                $w=[float]$node.width; $h=[float]$node.height; $r=[float]$node.rx * 2
                $path.AddArc($x,$y,$r,$r,180,90)
                $path.AddArc($x+$w-$r,$y,$r,$r,270,90)
                $path.AddArc($x+$w-$r,$y+$h-$r,$r,$r,0,90)
                $path.AddArc($x,$y+$h-$r,$r,$r,90,90)
                $path.CloseFigure()
            }
            'circle' {
                $r=[float]$node.r
                $path.AddEllipse([float]$node.cx-$r,[float]$node.cy-$r,$r*2,$r*2)
            }
            'path' {
                $tokens=[regex]::Matches($node.d,'[MLCZ]|-?\d+(?:\.\d+)?')
                $i=0; $x=0.0; $y=0.0
                while ($i -lt $tokens.Count) {
                    $command=$tokens[$i++].Value
                    switch ($command) {
                        'M' { $x=[float]$tokens[$i++].Value; $y=[float]$tokens[$i++].Value; $path.StartFigure() }
                        'L' {
                            $nx=[float]$tokens[$i++].Value; $ny=[float]$tokens[$i++].Value
                            $path.AddLine([float]$x,[float]$y,$nx,$ny); $x=$nx; $y=$ny
                        }
                        'C' {
                            $a=[float]$tokens[$i++].Value; $b=[float]$tokens[$i++].Value
                            $c=[float]$tokens[$i++].Value; $d=[float]$tokens[$i++].Value
                            $nx=[float]$tokens[$i++].Value; $ny=[float]$tokens[$i++].Value
                            $path.AddBezier([float]$x,[float]$y,$a,$b,$c,$d,$nx,$ny); $x=$nx; $y=$ny
                        }
                        'Z' { $path.CloseFigure() }
                        default { throw "Unsupported SVG path command: $command" }
                    }
                }
            }
            default { $path.Dispose(); continue }
        }
        $fill=$node.GetAttribute('fill')
        if ($fill -and $fill -ne 'none') {
            $brush=[System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml($fill))
            $graphics.FillPath($brush,$path); $brush.Dispose()
        }
        $stroke=$node.GetAttribute('stroke')
        if ($stroke -and $stroke -ne 'none') {
            $width=[float]$node.GetAttribute('stroke-width')
            if ($size -lt 32) { $width *= 1.25 }
            $pen=[System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml($stroke),$width)
            $pen.StartCap='Round'; $pen.EndCap='Round'; $pen.LineJoin='Round'
            $graphics.DrawPath($pen,$path); $pen.Dispose()
        }
        $path.Dispose()
    }
    $graphics.Dispose()
    $small=[System.Drawing.Bitmap]::new($size,$size)
    $g=[System.Drawing.Graphics]::FromImage($small)
    $g.InterpolationMode='HighQualityBicubic'
    $g.DrawImage($bitmap,0,0,$size,$size)
    $g.Dispose(); $bitmap.Dispose()
    if ($size -eq 256) { $small.Save((Join-Path $root 'resources\cat-preview.png'),[System.Drawing.Imaging.ImageFormat]::Png) }
    $memory=[System.IO.MemoryStream]::new()
    $small.Save($memory,[System.Drawing.Imaging.ImageFormat]::Png)
    $frames.Add($memory.ToArray()); $memory.Dispose(); $small.Dispose()
}
$file=[System.IO.File]::Create((Join-Path $root 'resources\app.ico'))
$writer=[System.IO.BinaryWriter]::new($file)
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
$offset=6+16*$sizes.Count
for ($i=0;$i -lt $sizes.Count;$i++) {
    $dimension=if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
    $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
    $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32)
    $writer.Write([uint32]$frames[$i].Length); $writer.Write([uint32]$offset)
    $offset += $frames[$i].Length
}
foreach ($frame in $frames) { $writer.Write([byte[]]$frame) }
$writer.Dispose(); $file.Dispose()
Write-Output "Generated cat icon sizes: $($sizes -join ', ')"
