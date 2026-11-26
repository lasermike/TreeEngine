[CmdletBinding()]
param(
    [string]$Executable,
    [string]$OutputDirectory,
    [string[]]$Modes = @('raster', 'raytracing')
)
# Plant growth runs on its own clock (RenderData::growthTime), apart from scene time (camera paths, tides, the rest).
# A capture's "growthTime" sets the growth clock; without it the clock follows the scene's (growth.startTime +
# time x growth.speed, 0 and 1 by default). Checks, in each mode:
# - LoadTestTree at time 5: the default capture and growthTime 5 match exactly (the default clock is scene time);
# - growth doesn't follow scene time: growthTime 5 at time 0 and at time 5 match exactly (nothing else in the scene
#   moves with time);
# - growth shows: growthTime 0.5 at time 5 differs from growthTime 5;
# - scene time still moves the rest: AI Tree's camera path at time 0 and time 5 with growthTime 5 differ.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $Executable) { $Executable = Join-Path $root 'TreeClassic\bin\x64\Debug\TreeClassic.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot ('Artifacts\growth-clock-' + [Guid]::NewGuid().ToString('N')) }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
if (-not ('TreeRenderTests.ImageComparison' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'ImageComparison.cs') -ReferencedAssemblies System.Drawing
}
$utf8 = New-Object Text.UTF8Encoding($false)

function Invoke-Captures([string]$Scene, [string]$Mode, [string]$Job, [object[]]$Captures) {
    New-Item -ItemType Directory -Path $Job -Force | Out-Null
    $captures = @($Captures | ForEach-Object { $c = @{} + $_; $c.filename = Join-Path $Job $_.filename; $c })
    $reportPath = Join-Path $Job 'capture-report.json'
    $request = @{
        scene = $Scene; mode = $Mode; seed = 12345; width = 640; height = 480
        warmupFrames = 3; benchmarkFrames = 1; postProcessing = $false
        report = $reportPath; captures = $captures
    }
    $requestPath = Join-Path $Job 'request.json'
    [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 8), $utf8)
    $process = Start-Process -FilePath $Executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) `
        -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $Job 'stderr.log')
    [void]$process.Handle
    if (-not $process.WaitForExit(300000)) { $process.Kill(); $process.WaitForExit(); throw "Growth clock capture timed out: $Job" }
    if ($process.ExitCode -ne 0) { throw "Growth clock worker failed ($($process.ExitCode)): $Job" }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if (-not $report.success -or @($report.captures).Count -ne $captures.Count) { throw "Growth clock captures did not complete: $Job" }
}

function Compare-Images([string]$Job, [string]$A, [string]$B) {
    $match = [TreeRenderTests.ImageComparison]::Compare((Join-Path $Job $A), (Join-Path $Job $B),
        (Join-Path $Job ("diff-" + [IO.Path]::GetFileNameWithoutExtension($A) + "-" + $B)))
    if (-not $match.SameDimensions) { throw "Capture sizes differ: $Job $A $B" }
    return $match.Rms
}

foreach ($mode in $Modes) {
    $job = Join-Path $OutputDirectory "test-tree-$mode"
    Invoke-Captures 'LoadTestTree' $mode $job @(
        @{ time = 5; filename = 'default.png' },
        @{ time = 5; growthTime = 5; filename = 'growth5-time5.png' },
        @{ time = 0; growthTime = 5; filename = 'growth5-time0.png' },
        @{ time = 5; growthTime = 0.5; filename = 'growth05-time5.png' })
    $same = Compare-Images $job 'default.png' 'growth5-time5.png'
    if ($same -ne 0) { throw "LoadTestTree / ${mode}: growthTime 5 differs from the default clock at time 5 (RMS $same)" }
    $independent = Compare-Images $job 'growth5-time5.png' 'growth5-time0.png'
    if ($independent -ne 0) { throw "LoadTestTree / ${mode}: growth followed scene time (RMS $independent)" }
    $grows = Compare-Images $job 'growth5-time5.png' 'growth05-time5.png'
    if ($grows -lt 0.001) { throw "LoadTestTree / ${mode}: growthTime 0.5 looks like 5 (RMS $grows)" }
    Write-Host "LoadTestTree / ${mode}: default = growth 5 (RMS 0), scene time doesn't grow plants (RMS 0), growth 0.5 differs (RMS $grows)"

    $job = Join-Path $OutputDirectory "ai-tree-$mode"
    Invoke-Captures 'LoadAITree' $mode $job @(
        @{ time = 0; growthTime = 5; filename = 'time0.png' },
        @{ time = 5; growthTime = 5; filename = 'time5.png' })
    $moves = Compare-Images $job 'time0.png' 'time5.png'
    if ($moves -lt 0.001) { throw "LoadAITree / ${mode}: the camera path didn't move with scene time (RMS $moves)" }
    Write-Host "LoadAITree / ${mode}: scene time moves the camera path with growth held (RMS $moves)"
}
Write-Host "Growth clock checks passed. Captures: $OutputDirectory"
