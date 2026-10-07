$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
if (-not ('TreeRenderTests.ImageComparison' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'ImageComparison.cs') -ReferencedAssemblies System.Drawing
}
$directory = Join-Path $PSScriptRoot ('Artifacts\comparator-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$gold = Join-Path $directory 'gold.png'
$actual = Join-Path $directory 'actual.png'
$diff = Join-Path $directory 'diff.png'
$image = New-Object Drawing.Bitmap(2, 2)
try {
    for ($y = 0; $y -lt 2; $y++) { for ($x = 0; $x -lt 2; $x++) { $image.SetPixel($x, $y, [Drawing.Color]::Black) } }
    $image.Save($gold, [Drawing.Imaging.ImageFormat]::Png)
    $image.Save($actual, [Drawing.Imaging.ImageFormat]::Png)
    $equal = [TreeRenderTests.ImageComparison]::Compare($gold, $actual, $diff)
    if (-not $equal.SameDimensions -or $equal.Rms -ne 0 -or $equal.ChangedPixels -ne 0) { throw 'Equal-image comparison failed.' }
    $image.SetPixel(0, 0, [Drawing.Color]::FromArgb(255, 255, 0, 0))
    $image.Save($actual, [Drawing.Imaging.ImageFormat]::Png)
    $changed = [TreeRenderTests.ImageComparison]::Compare($gold, $actual, $diff)
    $expected = [Math]::Sqrt(1.0 / 12.0)
    if ([Math]::Abs($changed.Rms - $expected) -gt 1e-12 -or $changed.MaxError -ne 1 -or $changed.ChangedPixels -ne 1) { throw 'Known RGB difference comparison failed.' }
    if ($changed.Rms -le 0.002) { throw 'Changed image would incorrectly pass the default RMS threshold.' }
    $difference = [Drawing.Image]::FromFile($diff)
    try { if ($difference.Width -ne 2 -or $difference.Height -ne 2) { throw 'Difference image was not written correctly.' } } finally { $difference.Dispose() }
} finally { $image.Dispose() }
$differentSize = New-Object Drawing.Bitmap(3, 2)
try { $differentSize.Save($actual, [Drawing.Imaging.ImageFormat]::Png) } finally { $differentSize.Dispose() }
$sizeMismatch = [TreeRenderTests.ImageComparison]::Compare($gold, $actual, $diff)
if ($sizeMismatch.SameDimensions) { throw 'Dimension mismatch was accepted.' }
Write-Host 'Image comparison checks passed: equal images, exact RMS, maximum error, changed pixels, diff output, threshold rejection, dimension mismatch.'
