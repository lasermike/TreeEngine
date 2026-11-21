[CmdletBinding()]
param([string]$Executable, [string]$OutputDirectory, [switch]$SkipTaa)
# HDR scene color and tonemapping (Game/Shaders/Tonemap.hlsl):
#   equivalence - HDR with the clamp operator at EV 0 matches HDR off within float16 rounding (RMS <= 0.002) in
#                 scenes without water or glass. Terra and AI Tree are reported: their water now blends in linear
#                 light without clipping its reflection, and ray tracing combines reflection and transmission in
#                 linear light.
#   toggling    - one process capturing off, on, off: both off captures are identical, and the on capture matches a
#                 separate HDR run, so pipelines and targets switch without a reload
#   highlights  - in Terra at EV +1, ACES and AgX clip fewer pixels to white than clamp
#   exposure    - EV -1, 0, +1 with clamp: mean brightness strictly increases
#   TAA         - test-taa.ps1 -Hdr (skip with -SkipTaa)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $Executable) { $Executable = Join-Path $root 'TreeClassic\bin\x64\Debug\TreeClassic.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot ('Artifacts\hdr-' + [Guid]::NewGuid().ToString('N')) }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
if (-not ('TreeRenderTests.ImageComparison' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'ImageComparison.cs') -ReferencedAssemblies System.Drawing
}
if (-not ('TreeRenderTests.ImageStats' -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System.Drawing;
namespace TreeRenderTests
{
    public static class ImageStats
    {
        // Mean of R, G and B over the image, 0-255.
        public static double Mean(string path)
        {
            using (Bitmap image = new Bitmap(path))
            {
                double sum = 0;
                for (int y = 0; y < image.Height; y++)
                    for (int x = 0; x < image.Width; x++) { Color c = image.GetPixel(x, y); sum += c.R + c.G + c.B; }
                return sum / (3.0 * image.Width * image.Height);
            }
        }

        // Pixels with any channel at 255.
        public static int Clipped(string path)
        {
            using (Bitmap image = new Bitmap(path))
            {
                int count = 0;
                for (int y = 0; y < image.Height; y++)
                    for (int x = 0; x < image.Width; x++) { Color c = image.GetPixel(x, y); if (c.R == 255 || c.G == 255 || c.B == 255) count++; }
                return count;
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

$failures = New-Object System.Collections.Generic.List[string]
$scenes = @(
    @{ id = 'tree'; name = 'LoadTreeScene'; strict = $true },
    @{ id = 'leaf-atlas-test'; name = 'LeafAtlasTest'; strict = $true },
    @{ id = 'gltf-models'; name = 'LoadGltfDemo'; strict = $true },
    @{ id = 'instancing'; name = 'Instancing'; strict = $true },
    @{ id = 'terra'; name = 'LoadTerra'; strict = $false },
    @{ id = 'ai-tree'; name = 'LoadAITree'; strict = $false }
)
foreach ($scene in $scenes) {
    foreach ($mode in @('raster', 'raytracing')) {
        $job = Join-Path $OutputDirectory "$($scene.id)-$mode"
        # Off, on, off in one process; then on again in its own process.
        Invoke-Capture $job @{ scene = $scene.name; mode = $mode; seed = 12345; width = 640; height = 480; warmupFrames = 3
            captures = @(
                @{ time = 5; filename = (Join-Path $job 'off.png') },
                @{ time = 5; filename = (Join-Path $job 'clamp.png'); hdr = $true },
                @{ time = 5; filename = (Join-Path $job 'off-again.png') }) }
        Invoke-Capture (Join-Path $job 'separate') @{ scene = $scene.name; mode = $mode; seed = 12345; width = 640; height = 480
            warmupFrames = 3; hdr = $true; captures = @(@{ time = 5; filename = (Join-Path $job 'clamp-separate.png') }) }
        $label = "$($scene.id) $mode"
        $equivalent = [TreeRenderTests.ImageComparison]::Compare((Join-Path $job 'off.png'), (Join-Path $job 'clamp.png'), (Join-Path $job 'diff-clamp.png'))
        $offAgain = [TreeRenderTests.ImageComparison]::Compare((Join-Path $job 'off.png'), (Join-Path $job 'off-again.png'), (Join-Path $job 'diff-off-again.png'))
        $separate = [TreeRenderTests.ImageComparison]::Compare((Join-Path $job 'clamp.png'), (Join-Path $job 'clamp-separate.png'), (Join-Path $job 'diff-separate.png'))
        Write-Host ("{0}: HDR clamp against off RMS {1:N5}; off after on {2}; toggled against a separate HDR run {3}" -f $label, $equivalent.Rms, $offAgain.Rms, $separate.Rms)
        if ($scene.strict -and $equivalent.Rms -gt 0.002) { $failures.Add("$label`: HDR clamp differs from HDR off by RMS $($equivalent.Rms)") }
        if ($offAgain.Rms -ne 0) { $failures.Add("$label`: turning HDR off again changed the image (RMS $($offAgain.Rms))") }
        if ($separate.Rms -ne 0) { $failures.Add("$label`: HDR toggled on differs from a separate HDR run (RMS $($separate.Rms))") }
    }
}

# Operators and exposure, in Terra (bright sky and sunlit rock).
foreach ($mode in @('raster', 'raytracing')) {
    $job = Join-Path $OutputDirectory "terra-operators-$mode"
    Invoke-Capture $job @{ scene = 'LoadTerra'; mode = $mode; seed = 12345; width = 640; height = 480; warmupFrames = 3; hdr = $true
        captures = @(
            @{ time = 5; filename = (Join-Path $job 'clamp-ev-1.png'); exposure = -1 },
            @{ time = 5; filename = (Join-Path $job 'clamp-ev0.png'); exposure = 0 },
            @{ time = 5; filename = (Join-Path $job 'clamp-ev1.png'); exposure = 1 },
            @{ time = 5; filename = (Join-Path $job 'aces-ev1.png'); exposure = 1; tonemap = 'aces' },
            @{ time = 5; filename = (Join-Path $job 'agx-ev1.png'); exposure = 1; tonemap = 'agx' }) }
    $means = @(-1, 0, 1) | ForEach-Object { [TreeRenderTests.ImageStats]::Mean((Join-Path $job "clamp-ev$_.png")) }
    $clipped = @('clamp', 'aces', 'agx') | ForEach-Object { [TreeRenderTests.ImageStats]::Clipped((Join-Path $job "$_-ev1.png")) }
    Write-Host ("terra $mode`: mean at EV -1, 0, +1: {0:N1}, {1:N1}, {2:N1}; clipped pixels at EV +1: clamp {3}, ACES {4}, AgX {5}" -f `
        $means[0], $means[1], $means[2], $clipped[0], $clipped[1], $clipped[2])
    if (-not ($means[0] -lt $means[1] -and $means[1] -lt $means[2])) { $failures.Add("terra $mode`: brightness doesn't increase with exposure") }
    if ($clipped[1] -ge $clipped[0]) { $failures.Add("terra $mode`: ACES clips $($clipped[1]) pixels, not fewer than clamp's $($clipped[0])") }
    if ($clipped[2] -ge $clipped[0]) { $failures.Add("terra $mode`: AgX clips $($clipped[2]) pixels, not fewer than clamp's $($clipped[0])") }
}

if (-not $SkipTaa) {
    try { & (Join-Path $PSScriptRoot 'test-taa.ps1') -Executable $Executable -OutputDirectory (Join-Path $OutputDirectory 'taa') -Hdr }
    catch { $failures.Add("TAA with HDR: $_") }
}
if ($failures.Count) { $failures | ForEach-Object { Write-Host "FAIL $_" }; throw "$($failures.Count) HDR checks failed. Captures: $OutputDirectory" }
Write-Host "HDR checks passed. Captures: $OutputDirectory"
