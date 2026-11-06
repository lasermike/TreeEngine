[CmdletBinding()]
param([string]$Executable, [string]$OutputDirectory, [switch]$Hdr)
# Temporal upscaling (Game/Upscaler.h, TaauResolveCS in Game/Shaders/Taa.hlsl): the scene renders at a fraction of
# the window and TAA reconstructs the window-size image. The reference is the frame rendered at twice the width and
# height without anti-aliasing and box-filtered down, as in test-taa.ps1. For each scene and mode, at Quality (0.667)
# and Performance (0.5):
#   render size - the report gives the scaled render size, and the capture is still the window's size
#   convergence - after 40 still frames the error against the reference is within a bound of native TAA's
#   stability   - two converged frames 41 frames apart differ by a small mean (no shimmer from the longer jitter cycle)
#   motion      - converged, then one sideways camera step: the error stays within a bound of native TAA's
# A last job switches scale and window size between captures in one process, which must keep working.
# -Hdr runs everything with the HDR scene target (clamp tonemap); the reference is then averaged in linear light.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $Executable) { $Executable = Join-Path $root 'TreeClassic\bin\x64\Debug\TreeClassic.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot ('Artifacts\upscaling-' + [Guid]::NewGuid().ToString('N')) }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
if (-not ('TreeRenderTests.ImageComparison' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'ImageComparison.cs') -ReferencedAssemblies System.Drawing
}
if (-not ('TreeRenderTests.UpscaleDownsample' -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System.Drawing;
using System.Drawing.Imaging;
namespace TreeRenderTests
{
    public static class UpscaleDownsample
    {
        static double Decode(int c) { double v = c / 255.0; return v <= 0.04045 ? v / 12.92 : System.Math.Pow((v + 0.055) / 1.055, 2.4); }
        static int Encode(double v) { v = System.Math.Max(0, System.Math.Min(1, v)); return (int)System.Math.Round(255 * (v <= 0.0031308 ? v * 12.92 : 1.055 * System.Math.Pow(v, 1 / 2.4) - 0.055)); }

        // Averages each 2x2 block into one pixel: the display values, or with linear the light they encode (sRGB).
        public static void Half(string source, string destination, bool linear)
        {
            using (Bitmap image = new Bitmap(source))
            using (Bitmap half = new Bitmap(image.Width / 2, image.Height / 2, PixelFormat.Format32bppArgb))
            {
                for (int y = 0; y < half.Height; y++)
                    for (int x = 0; x < half.Width; x++)
                    {
                        int r = 0, g = 0, b = 0;
                        double lr = 0, lg = 0, lb = 0;
                        for (int dy = 0; dy < 2; dy++)
                            for (int dx = 0; dx < 2; dx++)
                            {
                                Color c = image.GetPixel(2 * x + dx, 2 * y + dy);
                                r += c.R; g += c.G; b += c.B;
                                lr += Decode(c.R); lg += Decode(c.G); lb += Decode(c.B);
                            }
                        half.SetPixel(x, y, linear ? Color.FromArgb(255, Encode(lr / 4), Encode(lg / 4), Encode(lb / 4))
                            : Color.FromArgb(255, (r + 2) / 4, (g + 2) / 4, (b + 2) / 4));
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
    if (-not $process.WaitForExit(900000)) { $process.Kill(); $process.WaitForExit(); throw "Capture timed out: $Job" }
    if ($process.ExitCode -ne 0) { throw "Capture failed; see $Job\stderr.log" }
    $report = Get-Content -LiteralPath $Request.report -Raw | ConvertFrom-Json
    if (-not $report.success -or @($report.captures).Count -ne @($Request.captures).Count) { throw "Incomplete captures: $Job" }
    return $report
}

$width = 640; $height = 480
# Bounds on the error against the reference, as multiples of native TAA's. Measured when written, worst case
# (ray-traced leaves, where native TAA sees the most samples): converged 1.14x at Quality and 1.41x at Performance,
# after the step 1.45x and 1.52x; AI Tree stays within 1.37x and 1.30x.
$scales = @(
    @{ name = 'quality'; scale = 0.6666667; convergence = 1.3; motion = 1.6 },
    @{ name = 'performance'; scale = 0.5; convergence = 1.6; motion = 1.65 }
)
# Each scene's camera and a sideways step from it (as in test-taa.ps1).
$scenes = @(
    @{ id = 'leaf-atlas-test'; name = 'LeafAtlasTest'; position = @(0, 1.6, -9); rotation = @(6, 0, 0); moved = @(0.12, 1.6, -9) },
    @{ id = 'ai-tree'; name = 'LoadAITree'; position = @(2, 7, -77); rotation = @(1, 21.5, 1); moved = @(2.8, 7, -77) }
)
$failures = New-Object System.Collections.Generic.List[string]
foreach ($scene in $scenes) {
    foreach ($mode in @('raster', 'raytracing')) {
        $job = Join-Path $OutputDirectory "$($scene.id)-$mode"
        $still = @{ cameraPosition = $scene.position; cameraRotationDegrees = $scene.rotation }
        $step = @{ cameraPosition = $scene.moved; cameraRotationDegrees = $scene.rotation
            previousCameraPosition = $scene.position; previousCameraRotationDegrees = $scene.rotation }
        $captures = @(
            ($still + @{ time = 5; filename = (Join-Path $job 'taa.png'); antialiasing = 'taa'; renderScale = 1.0 }),
            ($step + @{ time = 5; filename = (Join-Path $job 'taa-moved.png'); antialiasing = 'taa'; renderScale = 1.0 })
        )
        foreach ($s in $scales) {
            $captures += ($still + @{ time = 5; filename = (Join-Path $job "$($s.name).png"); antialiasing = 'taa'; renderScale = $s.scale })
            $captures += ($still + @{ time = 5; filename = (Join-Path $job "$($s.name)-next.png"); antialiasing = 'taa'; renderScale = $s.scale })
            $captures += ($step + @{ time = 5; filename = (Join-Path $job "$($s.name)-moved.png"); antialiasing = 'taa'; renderScale = $s.scale })
        }
        $report = Invoke-Capture $job @{ scene = $scene.name; mode = $mode; seed = 12345; width = $width; height = $height
            warmupFrames = 40; postProcessing = $false; hdr = [bool]$Hdr; captures = $captures }
        $references = @(
            ($still + @{ time = 5; filename = (Join-Path $job 'reference-2x.png'); antialiasing = 'none' }),
            @{ time = 5; filename = (Join-Path $job 'reference-moved-2x.png'); antialiasing = 'none'
               cameraPosition = $scene.moved; cameraRotationDegrees = $scene.rotation }
        )
        [void](Invoke-Capture (Join-Path $job 'reference') @{ scene = $scene.name; mode = $mode; seed = 12345; width = 2 * $width
            height = 2 * $height; warmupFrames = 3; postProcessing = $false; hdr = [bool]$Hdr; captures = $references })
        [TreeRenderTests.UpscaleDownsample]::Half((Join-Path $job 'reference-2x.png'), (Join-Path $job 'reference.png'), [bool]$Hdr)
        [TreeRenderTests.UpscaleDownsample]::Half((Join-Path $job 'reference-moved-2x.png'), (Join-Path $job 'reference-moved.png'), [bool]$Hdr)

        $label = "$($scene.id) $mode"
        $compare = { param($a, $b) [TreeRenderTests.ImageComparison]::Compare((Join-Path $job $a), (Join-Path $job $b), (Join-Path $job ("diff-" + $b))) }
        $taa = & $compare 'reference.png' 'taa.png'
        $taaMoved = & $compare 'reference-moved.png' 'taa-moved.png'
        Write-Host ("{0}: native TAA RMS {1:N4}, after a step {2:N4}" -f $label, $taa.Rms, $taaMoved.Rms)
        foreach ($s in $scales) {
            $capture = @($report.captures | Where-Object { $_.renderScale -gt $s.scale - 1e-4 -and $_.renderScale -lt $s.scale + 1e-4 })[0]
            $expectedWidth = [Math]::Round($width * $s.scale); $expectedHeight = [Math]::Round($height * $s.scale)
            if ($capture.renderWidth -ne $expectedWidth -or $capture.renderHeight -ne $expectedHeight -or $capture.width -ne $width) {
                $failures.Add("$label $($s.name): rendered $($capture.renderWidth)x$($capture.renderHeight) for $($capture.width), expected $expectedWidth x $expectedHeight")
            }
            $converged = & $compare 'reference.png' "$($s.name).png"
            $stable = & $compare "$($s.name).png" "$($s.name)-next.png"
            $moved = & $compare 'reference-moved.png' "$($s.name)-moved.png"
            Write-Host ("  {0} ({1}x{2}): RMS {3:N4} ({4:N2}x native TAA); converged frames differ by mean {5:N5}; after a step {6:N4} ({7:N2}x)" -f `
                $s.name, $capture.renderWidth, $capture.renderHeight, $converged.Rms, ($converged.Rms / $taa.Rms),
                $stable.MeanAbsoluteError, $moved.Rms, ($moved.Rms / $taaMoved.Rms))
            if ($converged.Rms -gt $s.convergence * $taa.Rms) { $failures.Add("$label $($s.name): error $($converged.Rms) is over $($s.convergence)x native TAA's $($taa.Rms)") }
            if ($stable.MeanAbsoluteError -gt 1.5 / 255) { $failures.Add("$label $($s.name): converged frames differ by mean $($stable.MeanAbsoluteError)") }
            if ($moved.Rms -gt $s.motion * $taaMoved.Rms) { $failures.Add("$label $($s.name): after a step error $($moved.Rms) is over $($s.motion)x native TAA's $($taaMoved.Rms)") }
        }
    }
}

# Scale and window changes in one process: native, Performance, a resize while upscaled, Quality, native again.
$job = Join-Path $OutputDirectory 'switching'
$sequence = @(
    @{ scale = 1.0; width = 640; height = 480 }, @{ scale = 0.5; width = 640; height = 480 },
    @{ scale = 0.5; width = 800; height = 600 }, @{ scale = 0.6666667; width = 800; height = 600 },
    @{ scale = 1.0; width = 640; height = 480 })
$captures = @()
for ($i = 0; $i -lt $sequence.Count; ++$i) {
    $captures += @{ time = 5; filename = (Join-Path $job "step-$i.png"); antialiasing = 'taa'; renderScale = $sequence[$i].scale
        width = $sequence[$i].width; height = $sequence[$i].height }
}
$report = Invoke-Capture $job @{ scene = 'LeafAtlasTest'; mode = 'raytracing'; seed = 12345; width = 640; height = 480
    warmupFrames = 8; postProcessing = $true; hdr = [bool]$Hdr; captures = $captures }
for ($i = 0; $i -lt $sequence.Count; ++$i) {
    $capture = @($report.captures)[$i]
    $expectedWidth = [Math]::Round($sequence[$i].width * $sequence[$i].scale)
    if ($capture.renderWidth -ne $expectedWidth -or $capture.width -ne $sequence[$i].width) {
        $failures.Add("switching step $i`: rendered $($capture.renderWidth) wide for $($capture.width), expected $expectedWidth")
    }
}
$first = & { [TreeRenderTests.ImageComparison]::Compare((Join-Path $job 'step-0.png'), (Join-Path $job 'step-4.png'), (Join-Path $job 'diff-step-4.png')) }
Write-Host ("switching: {0} captures; native before and after differ by RMS {1:N4}" -f $sequence.Count, $first.Rms)
if ($first.Rms -gt 0.01) { $failures.Add("switching: native after switching differs from native before by RMS $($first.Rms)") }

if ($failures.Count) { $failures | ForEach-Object { Write-Host "FAIL $_" }; throw "$($failures.Count) upscaling checks failed. Captures: $OutputDirectory" }
Write-Host "Upscaling checks passed. Captures: $OutputDirectory"
