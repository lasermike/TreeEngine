[CmdletBinding()]
param([string]$Executable)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $Executable) {
    $Executable = Join-Path $PSScriptRoot '..\..\..\..\Binaries\Debug\x64\TreeClassic\TreeClassic.exe'
}
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not ('TreeRenderTests.ImageComparison' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'ImageComparison.cs') -ReferencedAssemblies System.Drawing
}
$directory = Join-Path $PSScriptRoot ('Artifacts\toggle-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
foreach ($postProcessing in @($true, $false)) {
  foreach ($scene in @('LoadSimpleBox', 'LoadAITree')) {
    $jobDirectory = Join-Path $directory "$scene-postprocessing-$postProcessing"
    New-Item -ItemType Directory -Path $jobDirectory -Force | Out-Null
    $first = Join-Path $jobDirectory 'dxr-before.png'
    $raster = Join-Path $jobDirectory 'raster.png'
    $last = Join-Path $jobDirectory 'dxr-after.png'
    $reportPath = Join-Path $jobDirectory 'capture-report.json'
    $request = @{
        scene = $scene; mode = 'raytracing'; seed = 12345; width = 640; height = 480
        warmupFrames = 3; postProcessing = $postProcessing; report = $reportPath
        captures = @(
            @{ time = 1; mode = 'raytracing'; filename = $first },
            @{ time = 1; mode = 'raster'; filename = $raster },
            @{ time = 1; mode = 'raytracing'; filename = $last }
        )
    }
    $requestPath = Join-Path $jobDirectory 'request.json'
    [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 8), $utf8)
    $process = Start-Process -FilePath $Executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $jobDirectory 'stderr.log')
    [void]$process.Handle
    if (-not $process.WaitForExit(120000)) {
        $process.Kill(); $process.WaitForExit(); throw 'Toggle test timed out.'
    }
    if ($process.ExitCode -ne 0) { throw "Toggle worker failed: $($process.ExitCode). See $jobDirectory." }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if (-not $report.success -or @($report.captures).Count -ne 3) { throw 'Toggle captures did not complete.' }
    $repeat = [TreeRenderTests.ImageComparison]::Compare($first, $last, (Join-Path $jobDirectory 'repeat-diff.png'))
    $changed = [TreeRenderTests.ImageComparison]::Compare($first, $raster, (Join-Path $jobDirectory 'mode-diff.png'))
    if (-not $repeat.SameDimensions -or $repeat.Rms -ne 0) { throw 'Re-enabling DXR changed the captured image.' }
    if (-not $changed.SameDimensions -or $changed.Rms -le 0.002) { throw 'Disabling DXR did not switch the main output to raster.' }
    if ($postProcessing -and $scene -eq 'LoadSimpleBox') {
        $gold = Join-Path $PSScriptRoot 'Gold\simple-box-raster-time-1.png'
        $rasterMatch = [TreeRenderTests.ImageComparison]::Compare($gold, $raster, (Join-Path $jobDirectory 'raster-diff.png'))
        if (-not $rasterMatch.SameDimensions -or $rasterMatch.Rms -gt 0.002) { throw 'Disabled DXR output differs from the raster baseline.' }
    }
    if ($scene -in @('LoadAITree', 'LoadSimpleBox')) {
        # The top strip contains only the skybox or scene clear color.
        $dxrImage = New-Object Drawing.Bitmap($first)
        $rasterImage = New-Object Drawing.Bitmap($raster)
        try {
            $squaredError = 0.0
            $minRed = 255; $maxRed = 0
            for ($y = 0; $y -lt 100; $y++) {
                for ($x = 0; $x -lt 640; $x++) {
                    $dxrPixel = $dxrImage.GetPixel($x, $y)
                    $rasterPixel = $rasterImage.GetPixel($x, $y)
                    $squaredError += [Math]::Pow($dxrPixel.R - $rasterPixel.R, 2)
                    $squaredError += [Math]::Pow($dxrPixel.G - $rasterPixel.G, 2)
                    $squaredError += [Math]::Pow($dxrPixel.B - $rasterPixel.B, 2)
                    $minRed = [Math]::Min($minRed, $dxrPixel.R)
                    $maxRed = [Math]::Max($maxRed, $dxrPixel.R)
                }
            }
            $skyRms = [Math]::Sqrt($squaredError / (640 * 100 * 3)) / 255
            if ($skyRms -gt 0.002) {
                throw "DXR background differs from raster: RMS=$skyRms."
            }
            if ($scene -eq 'LoadAITree' -and $maxRed - $minRed -lt 10) {
                throw 'DXR skybox is missing.'
            }
            if ($scene -eq 'LoadSimpleBox' -and $maxRed - $minRed -gt 1) {
                throw 'DXR clear-color background is not uniform.'
            }
            Write-Host "Background matches raster for ${scene}: RMS=$skyRms."
        } finally { $dxrImage.Dispose(); $rasterImage.Dispose() }
    }
    Write-Host "DXR on/off/on passed for $scene with postprocessing=$postProcessing."
  }
}
Write-Host "Toggle captures: $directory"
