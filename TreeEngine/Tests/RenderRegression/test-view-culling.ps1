[CmdletBinding()]
param(
    [string]$Executable,
    [string]$OutputDirectory,
    [string[]]$Scenes = @('LoadAITree', 'Instancing', 'LeafAtlases', 'LoadTerra', 'LoadTrees'),
    [string[]]$Modes = @('raster', 'raytracing')
)
# View culling must not change any image: each view is rendered with culling on and off and must match exactly.
# Turning the camera around each scene's start position makes some views cull objects, which the profile confirms.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $Executable) { $Executable = Join-Path $root 'TreeClassic\bin\x64\Debug\TreeClassic.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot ('Artifacts\view-culling-' + [Guid]::NewGuid().ToString('N')) }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
if (-not ('TreeRenderTests.ImageComparison' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'ImageComparison.cs') -ReferencedAssemblies System.Drawing
}
$utf8 = New-Object Text.UTF8Encoding($false)

# Each scene's start position (its scene file's camera), from which the views turn.
$positions = @{
    LoadAITree = @(2, 7, -77); Instancing = @(0, 2.2, -7); LeafAtlases = @(0, 2.4, -14); LoadTerra = @(15, 13.5, -79)
    LoadTrees = @(-4, 1.5, -4)
}
$yaws = @(0, 90, 180, 270)
# Scenes whose views must cull something. Terra is one terrain model (always in view) and the sky.
$expectCulling = @('LoadAITree', 'Instancing', 'LeafAtlases', 'LoadTrees')

function Invoke-Views([string]$Scene, [string]$Mode, [bool]$Culling, [string]$Job) {
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
        warmupFrames = 3; benchmarkFrames = 1; postProcessing = $false; viewCulling = $Culling
        report = $reportPath; captures = $captures
    }
    $requestPath = Join-Path $Job 'request.json'
    [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 8), $utf8)
    $process = Start-Process -FilePath $Executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) `
        -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $Job 'stderr.log')
    [void]$process.Handle
    if (-not $process.WaitForExit(300000)) { $process.Kill(); $process.WaitForExit(); throw "View culling capture timed out: $Job" }
    if ($process.ExitCode -ne 0) { throw "View culling worker failed ($($process.ExitCode)): $Job" }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if (-not $report.success -or @($report.captures).Count -ne $captures.Count) { throw "View culling captures did not complete: $Job" }
    return $report
}

foreach ($scene in $Scenes) {
    if (-not $positions.ContainsKey($scene)) { throw "No start position for $scene" }
    foreach ($mode in $Modes) {
        $culledJob = Join-Path $OutputDirectory "$scene-$mode-culled"
        $fullJob = Join-Path $OutputDirectory "$scene-$mode-unculled"
        $culled = Invoke-Views $scene $mode $true $culledJob
        $full = Invoke-Views $scene $mode $false $fullJob
        $names = @('default') + ($yaws | ForEach-Object { "yaw$_" })
        $culledTotal = 0
        for ($i = 0; $i -lt $names.Count; ++$i) {
            $name = $names[$i]
            $match = [TreeRenderTests.ImageComparison]::Compare((Join-Path $culledJob "$name.png"), (Join-Path $fullJob "$name.png"),
                (Join-Path $culledJob "$name-diff.png"))
            if (-not $match.SameDimensions -or $match.Rms -ne 0) {
                throw "View culling changed the image: $scene / $mode / $name, RMS=$($match.Rms). See $culledJob"
            }
            $culledSample = $culled.captures[$i].samples[0]
            if ($full.captures[$i].samples[0].culledObjects -ne 0) { throw "Objects were culled with culling off: $scene / $mode / $name" }
            $culledTotal += $culledSample.culledObjects
            Write-Host ("{0} / {1} / {2}: identical; {3} drawn, {4} culled" -f $scene, $mode, $name, $culledSample.drawnObjects, $culledSample.culledObjects)
        }
        if ($culledTotal -eq 0 -and $scene -in $expectCulling) { throw "No view culled anything: $scene / $mode" }
    }
}
Write-Host "View culling checks passed. Captures: $OutputDirectory"
