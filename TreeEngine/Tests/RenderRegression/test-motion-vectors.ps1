[CmdletBinding()]
param([string]$Executable, [string]$OutputDirectory)
# Motion vectors (Game/Shaders/MotionVectors.hlsl and the scene pass's velocity target), checked through the debug
# views, for camera moves and for growing trees:
#   reprojection - last frame's color fetched along the motion vectors is much closer to this frame's image than the
#                  history without motion: at most 0.6 of its mean error. Negated vectors would be about twice as far
#                  and doubled ones about as far. The rest is foliage edges, where the move uncovered the sky or
#                  bilinear fetches blur aliased leaves; TAA's neighborhood clamp is for those.
#   no motion    - an unmoved camera gives exactly zero motion (an all-black motion view), so the reprojection is
#                  the history unchanged
#   jitter       - a fixed one-pixel jitter shifts the image one pixel right
#   growth       - trees growing under a still camera move in the motion view, reproject well, and show exactly zero
#                  motion when nothing changed between frames (which also checks each branch finds its last slot)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $Executable) { $Executable = Join-Path $root 'TreeClassic\bin\x64\Debug\TreeClassic.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot ('Artifacts\motion-vectors-' + [Guid]::NewGuid().ToString('N')) }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path

if (-not ('TreeRenderTests.MotionImages' -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
namespace TreeRenderTests
{
    public sealed class MotionStats { public double WithinTolerance, MeanError, Maximum; public long Count; }
    public static class MotionImages
    {
        static byte[] Pixels(string path, out int width, out int height)
        {
            using (Image image = Image.FromFile(path))
            using (Bitmap bitmap = new Bitmap(image.Width, image.Height, PixelFormat.Format32bppArgb))
            {
                using (Graphics graphics = Graphics.FromImage(bitmap))
                {
                    graphics.CompositingMode = System.Drawing.Drawing2D.CompositingMode.SourceCopy;
                    graphics.DrawImage(image, new Rectangle(0, 0, image.Width, image.Height), 0, 0, image.Width, image.Height, GraphicsUnit.Pixel);
                }
                width = bitmap.Width; height = bitmap.Height;
                BitmapData data = bitmap.LockBits(new Rectangle(0, 0, width, height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                byte[] pixels = new byte[width * height * 4];
                for (int y = 0; y < height; y++)
                    Marshal.Copy(data.Scan0 + y * data.Stride, pixels, y * width * 4, width * 4);
                bitmap.UnlockBits(data);
                return pixels;
            }
        }

        // Per pixel, the largest RGB difference between a at (x, y) and b at (x + shiftX, y), in code values.
        // Columns that the shift takes outside b are skipped.
        public static MotionStats Compare(string aPath, string bPath, int shiftX, int tolerance)
        {
            return CompareMasked(aPath, bPath, null, 0, shiftX, tolerance);
        }

        // Compare over only the pixels whose brightest channel in the mask image exceeds maskThreshold.
        public static MotionStats CompareMasked(string aPath, string bPath, string maskPath, int maskThreshold, int shiftX, int tolerance)
        {
            int width, height, bWidth, bHeight;
            byte[] a = Pixels(aPath, out width, out height), b = Pixels(bPath, out bWidth, out bHeight);
            if (width != bWidth || height != bHeight) throw new ArgumentException("Image sizes differ");
            byte[] mask = maskPath == null ? null : Pixels(maskPath, out bWidth, out bHeight);
            long count = 0, within = 0; double sum = 0; int maximum = 0;
            for (int y = 0; y < height; y++)
                for (int x = Math.Max(0, -shiftX); x < Math.Min(width, width - shiftX); x++)
                {
                    int i = (y * width + x) * 4, j = (y * width + x + shiftX) * 4, error = 0;
                    if (mask != null && Math.Max(mask[i], Math.Max(mask[i + 1], mask[i + 2])) <= maskThreshold) continue;
                    for (int c = 0; c < 3; c++) error = Math.Max(error, Math.Abs(a[i + c] - b[j + c]));
                    count++; sum += error; maximum = Math.Max(maximum, error);
                    if (error <= tolerance) within++;
                }
            MotionStats stats = new MotionStats();
            stats.Count = count;
            stats.WithinTolerance = count > 0 ? (double)within / count : 0; stats.MeanError = count > 0 ? sum / count : 0;
            stats.Maximum = maximum;
            return stats;
        }

        // The brightest RGB code value in an image.
        public static int Brightest(string path)
        {
            int width, height;
            byte[] pixels = Pixels(path, out width, out height);
            int maximum = 0;
            for (int i = 0; i < pixels.Length; i += 4)
                maximum = Math.Max(maximum, Math.Max(pixels[i], Math.Max(pixels[i + 1], pixels[i + 2])));
            return maximum;
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
    if (-not $process.WaitForExit(300000)) { $process.Kill(); $process.WaitForExit(); throw "Capture timed out: $Job" }
    if ($process.ExitCode -ne 0) { throw "Capture failed; see $Job\stderr.log" }
    $report = Get-Content -LiteralPath $Request.report -Raw | ConvertFrom-Json
    if (-not $report.success -or @($report.captures).Count -ne @($Request.captures).Count) { throw "Incomplete captures: $Job" }
}

# Each scene's camera, and three moves from it: sideways, forward, and a yaw.
$scenes = @(
    @{ id = 'leaf-atlas-test'; name = 'LeafAtlasTest'; time = 5; position = @(0, 1.6, -9); rotation = @(6, 0, 0)
       moves = @(@{ name = 'sideways'; position = @(0.12, 1.6, -9); rotation = @(6, 0, 0) },
                 @{ name = 'forward'; position = @(0, 1.6, -8.7); rotation = @(6, 0, 0) },
                 @{ name = 'yaw'; position = @(0, 1.6, -9); rotation = @(6, 1.5, 0) }) },
    @{ id = 'ai-tree'; name = 'LoadAITree'; time = 5; position = @(2, 7, -77); rotation = @(1, 21.5, 1)
       moves = @(@{ name = 'sideways'; position = @(2.8, 7, -77); rotation = @(1, 21.5, 1) },
                 @{ name = 'forward'; position = @(2.6, 7, -75.5); rotation = @(1, 21.5, 1) },
                 @{ name = 'yaw'; position = @(2, 7, -77); rotation = @(1, 23, 1) }) }
)
$failures = New-Object System.Collections.Generic.List[string]
foreach ($scene in $scenes) {
    foreach ($mode in @('raster', 'raytracing')) {
        $job = Join-Path $OutputDirectory "$($scene.id)-$mode"
        $captures = New-Object System.Collections.Generic.List[object]
        # No motion: the same camera before and after.
        $captures.Add(@{ time = $scene.time; filename = (Join-Path $job 'still-motion.png'); debugView = 'motion'
            cameraPosition = $scene.position; cameraRotationDegrees = $scene.rotation
            previousCameraPosition = $scene.position; previousCameraRotationDegrees = $scene.rotation })
        $captures.Add(@{ time = $scene.time; filename = (Join-Path $job 'still.png')
            cameraPosition = $scene.position; cameraRotationDegrees = $scene.rotation })
        foreach ($move in $scene.moves) {
            foreach ($view in @('none', 'motion', 'reprojection', 'history')) {
                $captures.Add(@{ time = $scene.time; filename = (Join-Path $job "$($move.name)-$view.png"); debugView = $view
                    cameraPosition = $move.position; cameraRotationDegrees = $move.rotation
                    previousCameraPosition = $scene.position; previousCameraRotationDegrees = $scene.rotation })
            }
        }
        # Jitter: a one-pixel offset to the right.
        $captures.Add(@{ time = $scene.time; filename = (Join-Path $job 'jitter.png'); jitter = @(1, 0)
            cameraPosition = $scene.position; cameraRotationDegrees = $scene.rotation })
        Invoke-Capture $job @{ scene = $scene.name; mode = $mode; seed = 12345; width = 640; height = 480
            warmupFrames = 3; postProcessing = $false; captures = $captures.ToArray() }

        $label = "$($scene.id) $mode"
        $brightest = [TreeRenderTests.MotionImages]::Brightest((Join-Path $job 'still-motion.png'))
        Write-Host ("{0} no motion: motion view max {1}" -f $label, $brightest)
        if ($brightest -ne 0) { $failures.Add("$label`: an unmoved camera has motion ($brightest/255 in the motion view)") }

        foreach ($move in $scene.moves) {
            $current = Join-Path $job "$($move.name)-none.png"
            $warped = [TreeRenderTests.MotionImages]::Compare($current, (Join-Path $job "$($move.name)-reprojection.png"), 0, 4)
            $unwarped = [TreeRenderTests.MotionImages]::Compare($current, (Join-Path $job "$($move.name)-history.png"), 0, 4)
            $moving = [TreeRenderTests.MotionImages]::Brightest((Join-Path $job "$($move.name)-motion.png"))
            Write-Host ("{0} {1}: reprojection {2:P1} within 4/255, mean {3:N2}; without motion {4:P1}, mean {5:N2}; motion view max {6}" -f `
                $label, $move.name, $warped.WithinTolerance, $warped.MeanError, $unwarped.WithinTolerance, $unwarped.MeanError, $moving)
            if ($warped.WithinTolerance -le $unwarped.WithinTolerance) { $failures.Add("$label $($move.name): reprojection matches fewer pixels than the history without motion") }
            if ($warped.MeanError -gt 0.6 * $unwarped.MeanError) { $failures.Add("$label $($move.name): reprojection error $($warped.MeanError) is over 0.6 of $($unwarped.MeanError)") }
            if ($moving -eq 0) { $failures.Add("$label $($move.name): the motion view is black") }
        }

        # Jittered image at x + 1 equals the plain one at x, but for rounding at edges and animation between captures.
        $shifted = [TreeRenderTests.MotionImages]::Compare((Join-Path $job 'still.png'), (Join-Path $job 'jitter.png'), 1, 4)
        $unshifted = [TreeRenderTests.MotionImages]::Compare((Join-Path $job 'still.png'), (Join-Path $job 'jitter.png'), 0, 4)
        Write-Host ("{0} jitter: {1:P2} match shifted one pixel, mean {2:N3}; unshifted mean {3:N3}" -f $label, $shifted.WithinTolerance, $shifted.MeanError, $unshifted.MeanError)
        if ($shifted.WithinTolerance -lt 0.99) { $failures.Add("$label`: a one-pixel jitter is not a one-pixel shift ($($shifted.WithinTolerance))") }
        if ($shifted.MeanError -gt 0.1 * $unshifted.MeanError) { $failures.Add("$label`: the jittered image is not much closer shifted than unshifted") }
    }
}

# Object motion: trees growing under a fixed scene camera, from 0.1 s earlier, when growth changes the most pixels.
# LeafAtlasTest draws trees' own instances; Instancing draws instanced trees through their shared layouts.
# Instancing's ray-traced foliage is sparser than its raster foliage, which writes the velocity, so its DXR
# reprojection isn't judged (the zero-motion check still is).
foreach ($scene in @(@{ id = 'leaf-atlas-test'; name = 'LeafAtlasTest'; time = 0.6; judgeDxr = $true },
                     @{ id = 'instancing'; name = 'Instancing'; time = 1.0; judgeDxr = $false })) {
    foreach ($mode in @('raster', 'raytracing')) {
        $job = Join-Path $OutputDirectory "growth-$($scene.id)-$mode"
        $captures = New-Object System.Collections.Generic.List[object]
        # Same time before and after: nothing moves, so the motion must be exactly zero.
        $captures.Add(@{ time = $scene.time; previousTime = $scene.time; filename = (Join-Path $job 'still-motion.png'); debugView = 'motion' })
        foreach ($view in @('none', 'motion', 'reprojection', 'history')) {
            $captures.Add(@{ time = $scene.time; previousTime = $scene.time - 0.1; filename = (Join-Path $job "grow-$view.png"); debugView = $view })
        }
        Invoke-Capture $job @{ scene = $scene.name; mode = $mode; seed = 12345; width = 640; height = 480
            warmupFrames = 3; postProcessing = $false; captures = $captures.ToArray() }

        $label = "$($scene.id) $mode growth"
        $still = [TreeRenderTests.MotionImages]::Brightest((Join-Path $job 'still-motion.png'))
        $moving = [TreeRenderTests.MotionImages]::Brightest((Join-Path $job 'grow-motion.png'))
        # Judged where the motion view shows over half a pixel of motion (8/255; 16 pixels is full brightness). New
        # branches have no history and so no motion, and no vector could fetch them from last frame anyway.
        $current = Join-Path $job 'grow-none.png'
        $mask = Join-Path $job 'grow-motion.png'
        $warped = [TreeRenderTests.MotionImages]::CompareMasked($current, (Join-Path $job 'grow-reprojection.png'), $mask, 8, 0, 4)
        $unwarped = [TreeRenderTests.MotionImages]::CompareMasked($current, (Join-Path $job 'grow-history.png'), $mask, 8, 0, 4)
        Write-Host ("{0}: still motion max {1}; growing motion max {2}; over {3} moving pixels, reprojection mean {4:N2}, without motion {5:N2}" -f `
            $label, $still, $moving, $warped.Count, $warped.MeanError, $unwarped.MeanError)
        if ($still -ne 0) { $failures.Add("$label`: nothing moved, but the motion view shows $still/255") }
        if ($warped.Count -lt 200) { $failures.Add("$label`: only $($warped.Count) pixels moved over half a pixel") }
        elseif (($mode -eq 'raster' -or $scene.judgeDxr) -and $warped.MeanError -gt 0.6 * $unwarped.MeanError) { $failures.Add("$label`: reprojection error $($warped.MeanError) is over 0.6 of $($unwarped.MeanError) on moving pixels") }
    }
}
if ($failures.Count) { $failures | ForEach-Object { Write-Host "FAIL $_" }; throw "$($failures.Count) motion-vector checks failed. Captures: $OutputDirectory" }
Write-Host "Motion-vector checks passed. Captures: $OutputDirectory"
