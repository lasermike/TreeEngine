[CmdletBinding()]
param([string]$Executable, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $Executable) { $Executable = Join-Path $root 'TreeClassic\bin\x64\Debug\TreeClassic.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot ('Artifacts\water-motion-' + [Guid]::NewGuid().ToString('N')) }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
if (-not ('TreeRenderTests.ImageComparison' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'ImageComparison.cs') -ReferencedAssemblies System.Drawing
}
foreach ($case in @(@{ name = 'raster'; mode = 'raster'; rays = 0 }, @{ name = 'raytracing'; mode = 'raytracing'; rays = 0 }, @{ name = 'raytracing-reflections'; mode = 'raytracing'; rays = 16 })) {
    $mode = $case.mode
    $job = Join-Path $OutputDirectory $case.name
    New-Item -ItemType Directory -Path $job -Force | Out-Null
    $paths = @((Join-Path $job 't0.png'), (Join-Path $job 't20.png'), (Join-Path $job 't20-repeat.png'))
    $request = @{
        scene = 'LoadTerra'; mode = $mode; reflectionRays = $case.rays; seed = 12345
        width = 1152; height = 648; warmupFrames = 3; postProcessing = $false
        report = (Join-Path $job 'report.json')
        captures = @(
            @{ time = 0; filename = $paths[0] },
            @{ time = 20; filename = $paths[1] },
            @{ time = 20; filename = $paths[2] }
        )
    }
    $requestPath = Join-Path $job 'request.json'
    [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
    $process = Start-Process -FilePath $Executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $job 'stderr.log')
    [void]$process.Handle
    if (-not $process.WaitForExit(120000)) { $process.Kill(); $process.WaitForExit(); throw "Capture timed out: $mode" }
    if ($process.ExitCode -ne 0) { throw "Capture failed: $mode; see $job\stderr.log" }
    $report = Get-Content -LiteralPath (Join-Path $job 'report.json') -Raw | ConvertFrom-Json
    if (-not $report.success -or @($report.captures).Count -ne 3) { throw "Incomplete captures: $mode" }
    $motion = [TreeRenderTests.ImageComparison]::Compare($paths[0], $paths[1], (Join-Path $job 'motion-diff.png'))
    $repeat = [TreeRenderTests.ImageComparison]::Compare($paths[1], $paths[2], (Join-Path $job 'repeat-diff.png'))
    if (-not $motion.SameDimensions -or $motion.Rms -lt 0.00001) { throw "Water did not visibly animate: $mode" }
    if (-not $repeat.SameDimensions -or $repeat.Rms -ne 0) { throw "Captures at the same time differ: $mode" }
    Write-Host "$($case.name) water motion RMS $($motion.Rms); repeat RMS $($repeat.Rms)"
}
Write-Host "Water motion checks passed. Captures: $OutputDirectory"

# Check the speed control: zero freezes, and 2x reaches the 1x two-second pose in one second.
foreach ($setting in @(@{ name = 'frozen'; speed = 0; endTime = 2 }, @{ name = 'normal'; speed = 1; endTime = 2 }, @{ name = 'double'; speed = 2; endTime = 1 })) {
    $job = Join-Path $OutputDirectory ('speed-' + $setting.name)
    New-Item -ItemType Directory -Path $job -Force | Out-Null
    $request = @{
        scene = 'LoadTerra'; mode = 'raytracing'; reflectionRays = 16; waterMotionSpeed = $setting.speed
        seed = 12345; width = 1152; height = 648; warmupFrames = 3; postProcessing = $false
        report = (Join-Path $job 'report.json')
        captures = @(
            @{ time = 0; filename = (Join-Path $job 'start.png') },
            @{ time = $setting.endTime; filename = (Join-Path $job 'end.png') }
        )
    }
    $requestPath = Join-Path $job 'request.json'
    [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
    $process = Start-Process -FilePath $Executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $job 'stderr.log')
    [void]$process.Handle
    if (-not $process.WaitForExit(120000)) { $process.Kill(); $process.WaitForExit(); throw "Speed capture timed out: $($setting.name)" }
    if ($process.ExitCode -ne 0) { throw "Speed capture failed: $($setting.name); see $job\stderr.log" }
}
$frozen = [TreeRenderTests.ImageComparison]::Compare((Join-Path $OutputDirectory 'speed-frozen\start.png'), (Join-Path $OutputDirectory 'speed-frozen\end.png'), (Join-Path $OutputDirectory 'speed-frozen-diff.png'))
$moving = [TreeRenderTests.ImageComparison]::Compare((Join-Path $OutputDirectory 'speed-normal\start.png'), (Join-Path $OutputDirectory 'speed-normal\end.png'), (Join-Path $OutputDirectory 'speed-normal-diff.png'))
$double = [TreeRenderTests.ImageComparison]::Compare((Join-Path $OutputDirectory 'speed-normal\end.png'), (Join-Path $OutputDirectory 'speed-double\end.png'), (Join-Path $OutputDirectory 'speed-double-diff.png'))
if (-not $frozen.SameDimensions -or $frozen.Rms -ne 0) { throw 'Zero speed did not freeze the water' }
if (-not $moving.SameDimensions -or $moving.Rms -lt 0.00001) { throw 'Normal speed did not animate the water' }
if (-not $double.SameDimensions -or $double.Rms -ne 0) { throw 'Double speed did not reach the expected pose' }
Write-Host "Water speed checks passed: frozen RMS $($frozen.Rms); moving RMS $($moving.Rms); double-speed match RMS $($double.Rms)"
