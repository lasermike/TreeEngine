[CmdletBinding()]
param([string]$Executable, [switch]$Capacity)

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
foreach ($scene in @('LoadTestTree', 'LoadTrees')) {
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
        Write-Host "Skinning growth, mature-time reuse, LOD movement, and rewind passed: $scene / $mode"
    }
}
if ($Capacity) {
    $runtime = Join-Path $directory 'capacity-runtime'
    New-Item -ItemType Directory -Path $runtime -Force | Out-Null
    Copy-Item -Path (Join-Path (Split-Path $Executable) '*') -Destination $runtime -Recurse
    $rules = @()
    foreach ($pair in @(@('A', 'B'), @('B', 'C'), @('C', 'D'), @('D', 'F'))) {
        $rules += @{ input = $pair[0]; output = (('[' + $pair[1] + ']') * 10) }
    }
    foreach ($count in @(10000, 10001)) {
        $axiom = if ($count -eq 10000) { 'A' } else { 'A[F]' }
        $scene = @{
            version = 1; name = 'Skinning capacity'; camera = @{ position = @(0, 1.5, -6) }
            objects = @(@{
                id = 'capacity-tree'; type = 'tree'; transform = @{ position = @(0, 0.5, 0) }
                animationSpeed = 15; depthLOD = 20; trunkShape = 'skinnedCylinder'
                generator = @{ axiom = $axiom; iterations = 1; segmentLength = 0.1; thickness = 0.01; rules = $rules }
            })
        }
        [IO.File]::WriteAllText((Join-Path $runtime 'Scenes\test-tree.scene.json'), ($scene | ConvertTo-Json -Depth 9), $utf8)
        $reportPath = Join-Path $runtime "$count-report.json"
        $request = @{
            scene = 'LoadTestTree'; mode = 'raytracing'; seed = 12345; width = 32; height = 32
            warmupFrames = 0; benchmarkFrames = 1; postProcessing = $false; report = $reportPath
            captures = @(@{ time = 5; filename = (Join-Path $runtime "$count.png") })
        }
        $requestPath = Join-Path $runtime "$count-request.json"
        [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 8), $utf8)
        $process = Start-Process -FilePath (Join-Path $runtime 'TreeClassic.exe') -ArgumentList @('--render-test', ('"' + $requestPath + '"')) -WorkingDirectory $runtime -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $runtime "$count-stderr.log")
        [void]$process.Handle
        if (-not $process.WaitForExit(120000)) {
            $process.Kill(); $process.WaitForExit(); throw "Capacity test timed out: $count branches"
        }
        if (-not (Test-Path -LiteralPath $reportPath)) { throw "Capacity worker crashed: $count branches" }
        $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
        if ($count -eq 10000) {
            if ($process.ExitCode -ne 0 -or -not $report.success -or $report.captures[0].samples[0].worldVertices -ne 600000) {
                throw 'Exact 600,000-vertex capacity case failed.'
            }
        } elseif ($process.ExitCode -ne 3 -or $report.success -or $report.error -notmatch '2147942522') {
            throw 'Capacity overflow did not report ERROR_INSUFFICIENT_BUFFER.'
        }
    }
    Write-Host 'Exact 600,000-vertex capacity and explicit overflow rejection passed.'
}
Write-Host "Skinning captures: $directory"
