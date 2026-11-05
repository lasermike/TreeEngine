[CmdletBinding()]
param([string]$Executable, [string]$OutputDirectory)
# Temporal anti-aliasing (Game/Shaders/Taa.hlsl). The reference is the same frame rendered at twice the width and
# height without anti-aliasing and box-filtered down, four samples per pixel. For each scene and mode:
#   convergence - after 40 frames on a still camera, TAA's RMS error against the reference is at most 0.7 of the
#                 unfiltered image's
#   stability   - two converged TAA frames 41 frames apart differ by a mean of at most 1/255 (no shimmer)
#   motion      - converged, then one sideways camera step: TAA at the new camera is still closer to the new
#                 reference than the unfiltered image (reprojection and history clipping, without gross ghosting)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $Executable) { $Executable = Join-Path $root 'TreeClassic\bin\x64\Debug\TreeClassic.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot ('Artifacts\taa-' + [Guid]::NewGuid().ToString('N')) }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
if (-not ('TreeRenderTests.ImageComparison' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'ImageComparison.cs') -ReferencedAssemblies System.Drawing
}
if (-not ('TreeRenderTests.Downsample' -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System.Drawing;
using System.Drawing.Imaging;
namespace TreeRenderTests
{
    public static class Downsample
    {
        // Averages each 2x2 block of display values into one pixel.
        public static void Half(string source, string destination)
        {
            using (Bitmap image = new Bitmap(source))
            using (Bitmap half = new Bitmap(image.Width / 2, image.Height / 2, PixelFormat.Format32bppArgb))
            {
                for (int y = 0; y < half.Height; y++)
                    for (int x = 0; x < half.Width; x++)
                    {
                        int r = 0, g = 0, b = 0;
                        for (int dy = 0; dy < 2; dy++)
                            for (int dx = 0; dx < 2; dx++)
                            {
                                Color c = image.GetPixel(2 * x + dx, 2 * y + dy);
                                r += c.R; g += c.G; b += c.B;
                            }
                        half.SetPixel(x, y, Color.FromArgb(255, (r + 2) / 4, (g + 2) / 4, (b + 2) / 4));
                    }
                half.Save(destination, ImageFormat.Png);
            }
        }
    }
}
'@
}

function Invoke-Capture([string]$Job, [hashtable]$Request) {
    New-Item -ItemType Directory -Path $Job -Force | Out-Null
    $Request.report = Join-Path $Job 'report.json'
    $requestPath = Join-Path $Job 'request.json'
    [IO.File]::WriteAllText($requestPath, ($Request | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
    $process = Start-Process -FilePath $Executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $Job 'stderr.log')
    [void]$process.Handle
    if (-not $process.WaitForExit(600000)) { $process.Kill(); $process.WaitForExit(); throw "Capture timed out: $Job" }
    if ($process.ExitCode -ne 0) { throw "Capture failed; see $Job\stderr.log" }
    $report = Get-Content -LiteralPath $Request.report -Raw | ConvertFrom-Json
    if (-not $report.success -or @($report.captures).Count -ne @($Request.captures).Count) { throw "Incomplete captures: $Job" }
}

# Each scene's camera and a sideways step from it (scene cameras are overridden, so AI Tree's path doesn't move).
$scenes = @(
    @{ id = 'leaf-atlas-test'; name = 'LeafAtlasTest'; position = @(0, 1.6, -9); rotation = @(6, 0, 0); moved = @(0.12, 1.6, -9) },
    @{ id = 'instancing'; name = 'Instancing'; position = @(0, 2.2, -7); rotation = @(0, 0, 0); moved = @(0.1, 2.2, -7) },
    @{ id = 'ai-tree'; name = 'LoadAITree'; position = @(2, 7, -77); rotation = @(1, 21.5, 1); moved = @(2.8, 7, -77) }
)
$failures = New-Object System.Collections.Generic.List[string]
foreach ($scene in $scenes) {
    foreach ($mode in @('raster', 'raytracing')) {
        $job = Join-Path $OutputDirectory "$($scene.id)-$mode"
        $still = @{ cameraPosition = $scene.position; cameraRotationDegrees = $scene.rotation }
        $captures = @(
            ($still + @{ time = 5; filename = (Join-Path $job 'none.png'); antialiasing = 'none' }),
            ($still + @{ time = 5; filename = (Join-Path $job 'taa.png'); antialiasing = 'taa' }),
            ($still + @{ time = 5; filename = (Join-Path $job 'taa-next.png'); antialiasing = 'taa' }),
            @{ time = 5; filename = (Join-Path $job 'none-moved.png'); antialiasing = 'none'
               cameraPosition = $scene.moved; cameraRotationDegrees = $scene.rotation },
            @{ time = 5; filename = (Join-Path $job 'taa-moved.png'); antialiasing = 'taa'
               cameraPosition = $scene.moved; cameraRotationDegrees = $scene.rotation
               previousCameraPosition = $scene.position; previousCameraRotationDegrees = $scene.rotation }
        )
        Invoke-Capture $job @{ scene = $scene.name; mode = $mode; seed = 12345; width = 640; height = 480
            warmupFrames = 40; postProcessing = $false; captures = $captures }
        $references = @(
            ($still + @{ time = 5; filename = (Join-Path $job 'reference-2x.png'); antialiasing = 'none' }),
            @{ time = 5; filename = (Join-Path $job 'reference-moved-2x.png'); antialiasing = 'none'
               cameraPosition = $scene.moved; cameraRotationDegrees = $scene.rotation }
        )
        Invoke-Capture (Join-Path $job 'reference') @{ scene = $scene.name; mode = $mode; seed = 12345; width = 1280; height = 960
            warmupFrames = 3; postProcessing = $false; captures = $references }
        [TreeRenderTests.Downsample]::Half((Join-Path $job 'reference-2x.png'), (Join-Path $job 'reference.png'))
        [TreeRenderTests.Downsample]::Half((Join-Path $job 'reference-moved-2x.png'), (Join-Path $job 'reference-moved.png'))

        $label = "$($scene.id) $mode"
        $compare = { param($a, $b) [TreeRenderTests.ImageComparison]::Compare((Join-Path $job $a), (Join-Path $job $b), (Join-Path $job ("diff-" + $b))) }
        $none = & $compare 'reference.png' 'none.png'
        $taa = & $compare 'reference.png' 'taa.png'
        $stable = & $compare 'taa.png' 'taa-next.png'
        $noneMoved = & $compare 'reference-moved.png' 'none-moved.png'
        $taaMoved = & $compare 'reference-moved.png' 'taa-moved.png'
        Write-Host ("{0}: RMS against reference none {1:N4}, TAA {2:N4} ({3:P0}); converged frames differ by mean {4:N5}; after a step none {5:N4}, TAA {6:N4}" -f `
            $label, $none.Rms, $taa.Rms, ($taa.Rms / $none.Rms), $stable.MeanAbsoluteError, $noneMoved.Rms, $taaMoved.Rms)
        if ($taa.Rms -gt 0.7 * $none.Rms) { $failures.Add("$label`: TAA error $($taa.Rms) is over 0.7 of the unfiltered $($none.Rms)") }
        if ($stable.MeanAbsoluteError -gt 1.0 / 255) { $failures.Add("$label`: converged TAA frames differ by mean $($stable.MeanAbsoluteError)") }
        if ($taaMoved.Rms -ge $noneMoved.Rms) { $failures.Add("$label`: after a camera step TAA error $($taaMoved.Rms) is not below the unfiltered $($noneMoved.Rms)") }
    }
}
if ($failures.Count) { $failures | ForEach-Object { Write-Host "FAIL $_" }; throw "$($failures.Count) TAA checks failed. Captures: $OutputDirectory" }
Write-Host "TAA checks passed. Captures: $OutputDirectory"
