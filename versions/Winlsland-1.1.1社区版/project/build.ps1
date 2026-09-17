$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$project = $PSScriptRoot
$artifacts = Join-Path $project 'artifacts'
New-Item -ItemType Directory -Path $artifacts -Force | Out-Null

function New-RoundPath([single]$x, [single]$y, [single]$width, [single]$height, [single]$radius) {
    $path = [Drawing.Drawing2D.GraphicsPath]::new()
    $d = $radius * 2
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc(($x + $width - $d), $y, $d, $d, 270, 90)
    $path.AddArc(($x + $width - $d), ($y + $height - $d), $d, $d, 0, 90)
    $path.AddArc($x, ($y + $height - $d), $d, $d, 90, 90)
    $path.CloseFigure()
    return ,$path
}

# Embedded, multi-resolution icon. Draw at high resolution for smooth tray edges.
$images = @()
foreach ($size in @(16, 20, 24, 32, 48, 64, 128, 256)) {
    $large = [Drawing.Bitmap]::new(256, 256, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($large)
    $g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([Drawing.Color]::Transparent)
    $outline = New-RoundPath 12 63 232 130 65
    $g.FillPath([Drawing.Brushes]::White, $outline)
    $inside = New-RoundPath 24 75 208 106 53
    $g.FillPath([Drawing.Brushes]::Black, $inside)
    $accent = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(137, 173, 255))
    $g.FillEllipse($accent, 60, 110, 36, 36)
    $pen = [Drawing.Pen]::new([Drawing.Color]::White, 12)
    $pen.StartCap = [Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [Drawing.Drawing2D.LineCap]::Round
    $g.DrawLine($pen, 117, 116, 192, 116)
    $g.DrawLine($pen, 117, 142, 167, 142)
    $small = [Drawing.Bitmap]::new($size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $sg = [Drawing.Graphics]::FromImage($small)
    $sg.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $sg.DrawImage($large, 0, 0, $size, $size)
    $memory = [IO.MemoryStream]::new()
    $small.Save($memory, [Drawing.Imaging.ImageFormat]::Png)
    $images += ,@{ Size = $size; Data = $memory.ToArray() }
    $memory.Dispose(); $sg.Dispose(); $small.Dispose(); $pen.Dispose(); $accent.Dispose()
    $inside.Dispose(); $outline.Dispose(); $g.Dispose(); $large.Dispose()
}
$iconPath = Join-Path $project 'WinIsland.ico'
$file = [IO.File]::Create($iconPath)
$writer = [IO.BinaryWriter]::new($file)
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($entry in $images) {
    $dimension = if ($entry.Size -eq 256) { 0 } else { $entry.Size }
    $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
    $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32)
    $writer.Write([uint32]$entry.Data.Length); $writer.Write([uint32]$offset)
    $offset += $entry.Data.Length
}
foreach ($entry in $images) { $writer.Write([byte[]]$entry.Data) }
$writer.Dispose(); $file.Dispose()

$framework = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$compiler = Join-Path $framework 'csc.exe'
$references = @('System.dll', 'System.Core.dll', 'System.Drawing.dll', 'System.Windows.Forms.dll', 'System.Xaml.dll', 'System.Xml.dll', 'System.Xml.Linq.dll')
$compilerArgs = @('/nologo', '/target:winexe', '/platform:x64', '/optimize+', '/warn:4', '/utf8output', '/codepage:65001',
    "/out:$artifacts\WinIsland-1.1.exe", "/win32manifest:$project\app.manifest", "/win32icon:$iconPath", "/resource:$iconPath,WinIsland.ico")
foreach ($reference in $references) { $compilerArgs += "/reference:$framework\$reference" }
foreach ($reference in @('PresentationCore.dll', 'PresentationFramework.dll', 'WindowsBase.dll')) { $compilerArgs += "/reference:$framework\WPF\$reference" }
$compilerArgs += Get-ChildItem -LiteralPath $project -Filter '*.cs' | ForEach-Object { $_.FullName }
& $compiler @compilerArgs
if ($LASTEXITCODE -ne 0) { throw "C# compilation failed ($LASTEXITCODE)" }
Get-Item -LiteralPath (Join-Path $artifacts 'WinIsland-1.1.exe') | Select-Object FullName, Length, LastWriteTime
