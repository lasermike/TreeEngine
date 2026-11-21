[CmdletBinding()]
param(
    [string]$Executable,
    [string]$OutputDirectory,
    [string[]]$Scenes = @('Instancing', 'Forest'),
    [string[]]$Modes = @('raster', 'raytracing'),
    # Batching reorders draws within a render unit, so coincident surfaces at equal depth can resolve differently.
    [double]$MaxRms = 0.001
)
# Batched instance draws (one draw per shared instance range, RenderManager::Render) must render what drawing each
# instanced object on its own does: each view is rendered with batching on and off. The profile must show batched
# draws when on and none when off. Turning the camera makes some views cull instances, which batches leave out.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $Executable) { $Executable = Join-Path $root 'TreeClassic\bin\x64\Debug\TreeClassic.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot ('Artifacts\instance-batching-' + [Guid]::NewGuid().ToString('N')) }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
if (-not ('TreeRenderTests.ImageComparison' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'ImageComparison.cs') -ReferencedAssemblies System.Drawing
}
$utf8 = New-Object Text.UTF8Encoding($false)

# Each scene's start position (its scene file's camera), from which the views turn.
$positions = @{ Instancing = @(0, 2.2, -7); Forest = @(0, 6, -157) }
$yaws = @(0, 90, 180)

function Invoke-Views([string]$Scene, [string]$Mode, [bool]$Batch, [string]$Job) {
    New-Item -ItemType Directory -Path $Job -Force | Out-Null
    $captures = @(@{ time = 5; filename = (Join-Path $Job 'default.png') })
    foreach ($yaw in $yaws) {
        $captures += @{
            time = 5; cameraPosition = $positions[$Scene]; cameraRotationDegrees = @(0, $yaw, 0)
            filename = (Join-Path $Job "yaw$yaw.png")
        }
    }
    $reportPath = Join-Path $Job 'capture-report.json'
    $request = @{
        scene = $Scene; mode = $Mode; seed = 12345; width = 640; height = 480
        warmupFrames = 3; benchmarkFrames = 1; postProcessing = $false; batchInstances = $Batch
        report = $reportPath; captures = $captures
    }
    $requestPath = Join-Path $Job 'request.json'
    [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 8), $utf8)
    $process = Start-Process -FilePath $Executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) `
        -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $Job 'stderr.log')
    [void]$process.Handle
    if (-not $process.WaitForExit(600000)) { $process.Kill(); $process.WaitForExit(); throw "Instance batching capture timed out: $Job" }
    if ($process.ExitCode -ne 0) { throw "Instance batching worker failed ($($process.ExitCode)): $Job" }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if (-not $report.success -or @($report.captures).Count -ne $captures.Count) { throw "Instance batching captures did not complete: $Job" }
    return $report
}

foreach ($scene in $Scenes) {
    if (-not $positions.ContainsKey($scene)) { throw "No start position for $scene" }
    foreach ($mode in $Modes) {
        $batchedJob = Join-Path $OutputDirectory "$scene-$mode-batched"
        $singleJob = Join-Path $OutputDirectory "$scene-$mode-single"
        $batched = Invoke-Views $scene $mode $true $batchedJob
        $single = Invoke-Views $scene $mode $false $singleJob
        $names = @('default') + ($yaws | ForEach-Object { "yaw$_" })
        $batchedTotal = 0
        for ($i = 0; $i -lt $names.Count; ++$i) {
            $name = $names[$i]
            $match = [TreeRenderTests.ImageComparison]::Compare((Join-Path $batchedJob "$name.png"), (Join-Path $singleJob "$name.png"),
                (Join-Path $batchedJob "$name-diff.png"))
            if (-not $match.SameDimensions -or $match.Rms -gt $MaxRms) {
                throw "Instance batching changed the image: $scene / $mode / $name, RMS=$($match.Rms). See $batchedJob"
            }
            $sample = $batched.captures[$i].samples[0]
            if ($single.captures[$i].samples[0].batchedDraws -ne 0) { throw "Draws were batched with batching off: $scene / $mode / $name" }
            $batchedTotal += $sample.batchedObjects
            Write-Host ("{0} / {1} / {2}: RMS {3:G3}; {4} objects in {5} batched draws" -f $scene, $mode, $name, $match.Rms,
                $sample.batchedObjects, $sample.batchedDraws)
        }
        if ($batchedTotal -eq 0) { throw "Nothing was batched: $scene / $mode" }
    }
}
Write-Host "Instance batching checks passed. Captures: $OutputDirectory"
