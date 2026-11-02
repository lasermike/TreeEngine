[CmdletBinding()]
param([string]$Executable, [string]$CapacityExecutable, [switch]$Capacity)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $Executable) {
    $Executable = Join-Path $PSScriptRoot '..\..\TreeClassic\bin\x64\Debug\TreeClassic.exe'
}
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not ('TreeRenderTests.ImageComparison' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'ImageComparison.cs') -ReferencedAssemblies System.Drawing
}
$directory = Join-Path $PSScriptRoot ('Artifacts\skinning-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
$states = @(
    @{ time = 0; camera = @(0, 1.5, -6) },
    @{ time = 1; camera = @(0, 1.5, -6) },
    @{ time = 5; camera = @(0, 1.5, -6) },
    @{ time = 60; camera = @(0, 1.5, -6); allowReuse = $true },
    @{ time = 60.25; camera = @(0, 1.5, -6); reuse = $true },
    @{ time = 60.5; camera = @(0, 1.5, -60); reuse = $true },
    @{ time = 60.75; camera = @(0, 1.5, -6); reuse = $true },
    @{ time = 1; camera = @(0, 1.5, -6) },
    @{ time = 0; camera = @(0, 1.5, -60) }
)
$instancedPoseWork = $null
foreach ($scene in @('LoadTestTree', 'LoadTrees', 'Instancing')) {
    foreach ($mode in @('raster', 'raytracing')) {
        $job = Join-Path $directory "$scene-$mode"
        New-Item -ItemType Directory -Path $job -Force | Out-Null
        $captures = @()
        for ($i = 0; $i -lt $states.Count; ++$i) {
            foreach ($cached in @($true, $false)) {
                $captures += @{
                    time = $states[$i].time; poseCache = $cached; cameraPosition = $states[$i].camera
                    filename = (Join-Path $job "$i-$cached.png")
                }
            }
        }
        $reportPath = Join-Path $job 'capture-report.json'
        $request = @{
            scene = $scene; mode = $mode; seed = 12345; width = 640; height = 480
            warmupFrames = 0; benchmarkFrames = 1; postProcessing = $true
            report = $reportPath; captures = $captures
        }
        $requestPath = Join-Path $job 'request.json'
        [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 8), $utf8)
        $process = Start-Process -FilePath $Executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $job 'stderr.log')
        [void]$process.Handle
        if (-not $process.WaitForExit(120000)) {
            $process.Kill(); $process.WaitForExit(); throw "Skinning test timed out: $job"
        }
        if ($process.ExitCode -ne 0) { throw "Skinning worker failed: $($process.ExitCode). See $job." }
        $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
        if (-not $report.success -or @($report.captures).Count -ne $captures.Count) {
            throw "Skinning captures did not complete: $job"
        }
        for ($i = 0; $i -lt $states.Count; ++$i) {
            $match = [TreeRenderTests.ImageComparison]::Compare(
                (Join-Path $job "$i-True.png"), (Join-Path $job "$i-False.png"),
                (Join-Path $job "$i-diff.png"))
            if (-not $match.SameDimensions -or $match.Rms -ne 0) {
                throw "Cached pose differs from recomputed pose: $scene / $mode / state $i, RMS=$($match.Rms)"
            }
            $sample = $report.captures[$i * 2].samples[0]
            $reference = $report.captures[$i * 2 + 1].samples[0]
            if ($mode -eq 'raytracing') {
                if (-not $reference.reusedBLAS) {
                    throw "Identical geometry did not reuse the BLAS: $scene / state $i"
                }
                if ($i -in @(1, 2, 7, 8) -and $sample.reusedBLAS) {
                    throw "Changed tree geometry reused a stale BLAS: $scene / state $i"
                }
            }
            if ($reference.rebuiltPoseBranches -le 0 -or $reference.reusedPoseBranches -ne 0) {
                throw "Reference pose was not recomputed: $scene / $mode / state $i"
            }
            if ($states[$i].ContainsKey('reuse')) {
                if ($sample.rebuiltPoseBranches -ne 0 -or $sample.reusedPoseBranches -le 0) {
                    throw "Mature pose was not reused across time/LOD changes: $scene / $mode / state $i"
                }
            } elseif (-not $states[$i].ContainsKey('allowReuse') -and $sample.rebuiltPoseBranches -le 0) {
                throw "Growth/rewind did not invalidate the pose: $scene / $mode / state $i"
            }
        }
        if ($scene -eq 'Instancing' -and $mode -eq 'raster') {
            # State 2 (time 5) recomputed without the pose cache.
            $instancedPoseWork = $report.captures[5].samples[0].rebuiltPoseBranches
        }
        Write-Host "Skinning growth, mature-time reuse, LOD movement, and rewind passed: $scene / $mode"
    }
}
if ($null -eq $instancedPoseWork) { throw 'Instancing pose work was not measured.' }
# Instances must not add pose work: the same scene with its instances removed rebuilds exactly as many branches.
$runtime = Join-Path $directory 'instancing-runtime'
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
Copy-Item -Path (Join-Path (Split-Path $Executable) '*') -Destination $runtime -Recurse -Exclude '*.pdb', 'UserScenes', 'imgui.ini'
$scenePath = Join-Path $runtime 'Scenes\instancing.scene.json'
$sourceOnly = Get-Content -LiteralPath $scenePath -Raw | ConvertFrom-Json
$sourceOnly.objects = @($sourceOnly.objects | Where-Object { -not $_.PSObject.Properties['instanceOf'] })
[IO.File]::WriteAllText($scenePath, ($sourceOnly | ConvertTo-Json -Depth 20), $utf8)
$reportPath = Join-Path $runtime 'source-only-report.json'
$request = @{
    scene = 'Instancing'; mode = 'raster'; seed = 12345; width = 640; height = 480
    warmupFrames = 0; benchmarkFrames = 1; postProcessing = $true; report = $reportPath
    captures = @(@{ time = 5; poseCache = $false; cameraPosition = @(0, 1.5, -6); filename = (Join-Path $runtime 'source-only.png') })
}
$requestPath = Join-Path $runtime 'source-only-request.json'
[IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 8), $utf8)
$process = Start-Process -FilePath (Join-Path $runtime 'TreeClassic.exe') -ArgumentList @('--render-test', ('"' + $requestPath + '"')) -WorkingDirectory $runtime -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $runtime 'source-only-stderr.log')
[void]$process.Handle
if (-not $process.WaitForExit(120000)) {
    $process.Kill(); $process.WaitForExit(); throw 'Source-only instancing capture timed out.'
}
if ($process.ExitCode -ne 0) { throw "Source-only instancing capture failed: $($process.ExitCode). See $runtime." }
$sourceOnlyWork = (Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json).captures[0].samples[0].rebuiltPoseBranches
if ($sourceOnlyWork -le 0 -or $instancedPoseWork -ne $sourceOnlyWork) {
    throw "Instances added pose work: $instancedPoseWork branches rebuilt with instances, $sourceOnlyWork without."
}
Write-Host "Instances add no pose work: $instancedPoseWork branches rebuilt with or without instances."
if ($Capacity) {
    # The expected overflow is a normal error in Release; Debug's HRR macro opens an assertion dialog.
    if (-not $CapacityExecutable) {
        $CapacityExecutable = Join-Path $PSScriptRoot '..\..\TreeClassic\bin\x64\Release\TreeClassic.exe'
    }
    $CapacityExecutable = (Resolve-Path -LiteralPath $CapacityExecutable).Path
    $runtime = Join-Path $directory 'capacity-runtime'
    New-Item -ItemType Directory -Path $runtime -Force | Out-Null
    Copy-Item -Path (Join-Path (Split-Path $CapacityExecutable) '*') -Destination $runtime -Recurse
    $modelDirectory = Join-Path $runtime 'Models\Gltf'
    New-Item -ItemType Directory -Path $modelDirectory -Force | Out-Null
    $vertexCapacity = 3000000 # maxWorldVertices in RenderPlatform12.cpp
    foreach ($count in @($vertexCapacity, ($vertexCapacity + 1))) {
        # One indexed triangle and unused positions exercise the world-vertex boundary with one draw record.
        $positionBytes = $count * 12
        $bufferPath = Join-Path $modelDirectory 'capacity.bin'
        $stream = [IO.File]::Open($bufferPath, [IO.FileMode]::Create, [IO.FileAccess]::Write)
        $writer = New-Object IO.BinaryWriter($stream)
        try {
            $stream.SetLength($positionBytes + 6)
            foreach ($coordinate in @(-1.0, -1.0, 0.0, 1.0, -1.0, 0.0, 0.0, 1.0, 0.0)) {
                $writer.Write([single]$coordinate)
            }
            $stream.Position = $positionBytes
            foreach ($index in @(0, 1, 2)) { $writer.Write([uint16]$index) }
        } finally { $writer.Dispose() }
        $model = @{
            asset = @{ version = '2.0' }
            buffers = @(@{ uri = 'capacity.bin'; byteLength = ($positionBytes + 6) })
            bufferViews = @(
                @{ buffer = 0; byteOffset = 0; byteLength = $positionBytes },
                @{ buffer = 0; byteOffset = $positionBytes; byteLength = 6 })
            accessors = @(
                @{ bufferView = 0; componentType = 5126; count = $count; type = 'VEC3'; min = @(-1, -1, 0); max = @(1, 1, 0) },
                @{ bufferView = 1; componentType = 5123; count = 3; type = 'SCALAR' })
            meshes = @(@{ primitives = @(@{ attributes = @{ POSITION = 0 }; indices = 1; mode = 4 }) })
            nodes = @(@{ mesh = 0 })
            scenes = @(@{ nodes = @(0) })
            scene = 0
        }
        [IO.File]::WriteAllText((Join-Path $modelDirectory 'capacity.gltf'), ($model | ConvertTo-Json -Depth 12), $utf8)
        $scene = @{
            version = 1; id = 'LoadTestTree'; name = 'DXR vertex capacity'
            camera = @{ position = @(0, 0, -6) }
            objects = @(@{ id = 'capacity-model'; type = 'model'; source = 'Models/Gltf/capacity.gltf' })
        }
        [IO.File]::WriteAllText((Join-Path $runtime 'Scenes\test-tree.scene.json'), ($scene | ConvertTo-Json -Depth 9), $utf8)
        $reportPath = Join-Path $runtime "$count-report.json"
        $request = @{
            scene = 'LoadTestTree'; mode = 'raytracing'; seed = 12345; width = 640; height = 480
            warmupFrames = 0; benchmarkFrames = 1; postProcessing = $false; report = $reportPath
            captures = @(@{ time = 5; filename = (Join-Path $runtime "$count.png") })
        }
        $requestPath = Join-Path $runtime "$count-request.json"
        [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 8), $utf8)
        $process = Start-Process -FilePath (Join-Path $runtime 'TreeClassic.exe') -ArgumentList @('--render-test', ('"' + $requestPath + '"')) -WorkingDirectory $runtime -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $runtime "$count-stderr.log")
        [void]$process.Handle
        if (-not $process.WaitForExit(120000)) {
            $process.Kill(); $process.WaitForExit(); throw "Capacity test timed out: $count vertices"
        }
        if (-not (Test-Path -LiteralPath $reportPath)) { throw "Capacity worker crashed: $count vertices" }
        $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
        if ($count -eq $vertexCapacity) {
            if ($process.ExitCode -ne 0 -or -not $report.success -or $report.captures[0].samples[0].worldVertices -ne $vertexCapacity) {
                throw 'Exact 3,000,000-vertex capacity case failed.'
            }
        } elseif ($process.ExitCode -ne 3 -or $report.success -or $report.error -notmatch '2147942522') {
            throw 'Capacity overflow did not report ERROR_INSUFFICIENT_BUFFER.'
        }
    }
    Write-Host 'Exact 3,000,000-vertex capacity and explicit overflow rejection passed.'
}
Write-Host "Skinning captures: $directory"
