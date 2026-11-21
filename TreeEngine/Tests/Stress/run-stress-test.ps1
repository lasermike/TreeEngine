[CmdletBinding()]
param(
    # Sequence seed; random (and printed) when omitted. Iterations use Seed, Seed+1, ...
    [int]$Seed = -1,
    [ValidateRange(1, 1000)][int]$Steps = 40,
    [ValidateRange(1, 10000)][int]$Iterations = 1,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$Executable,
    # Scene names to draw from; defaults to the render-regression scenes.
    [string[]]$Scenes = @(),
    # Update objects on every core, like the interactive app, instead of one ordered thread.
    [switch]$AllCores,
    # Only detect crashes, device removal and step errors; skip comparing images with fresh renders.
    [switch]$NoImageCheck,
    # Keep the folders of passing runs too (normally only failures are kept).
    [switch]$KeepPassing,
    # Rerun the sequence saved in a failure folder (its sequence.json).
    [string]$Replay,
    [ValidateRange(10, 7200)][int]$TimeoutSeconds = 900
)

# Cycles scenes inside one TreeClassic process, as the app does, changing mode, size, anti-aliasing and
# post-processing between steps. Detects crashes (with a minidump), device removal (with a DRED report),
# step errors, and images that differ from a fresh render of the same step. See README.md.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$regressionDirectory = Join-Path $workspaceRoot 'Tests\RenderRegression'
$manifest = Get-Content -LiteralPath (Join-Path $regressionDirectory 'scenes.json') -Raw | ConvertFrom-Json
if (-not $Executable) { $Executable = Join-Path $workspaceRoot "TreeClassic\bin\x64\$Configuration\TreeClassic.exe" }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not ('TreeRenderTests.ImageComparison' -as [type])) {
    Add-Type -Path (Join-Path $regressionDirectory 'ImageComparison.cs') -ReferencedAssemblies System.Drawing
}
$utf8 = New-Object Text.UTF8Encoding($false)
$artifactRoot = Join-Path $PSScriptRoot 'Artifacts'
New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null

if (-not $Scenes.Count) { $Scenes = @($manifest.scenes | ForEach-Object { $_.name } | Select-Object -Unique) }
$sizes = @(@(640, 480), @(800, 600), @(1024, 576))
$times = @(0, 1, 5)

# References are cached per build, so a rebuilt executable gets fresh ones.
$platformDll = Join-Path (Split-Path $Executable) 'RenderPlatform12.dll'
$buildStamp = '{0:x}-{1:x}' -f (Get-Item -LiteralPath $Executable).LastWriteTimeUtc.Ticks,
    $(if (Test-Path -LiteralPath $platformDll) { (Get-Item -LiteralPath $platformDll).LastWriteTimeUtc.Ticks } else { 0 })
$referenceDirectory = Join-Path $artifactRoot "references\$Configuration-$buildStamp$(if ($AllCores) { '-allcores' })"
New-Item -ItemType Directory -Path $referenceDirectory -Force | Out-Null

function New-Sequence([int]$SequenceSeed) {
    $random = New-Object System.Random($SequenceSeed)
    $recent = New-Object 'System.Collections.Generic.List[string]'
    $sequence = @()
    for ($i = 0; $i -lt $Steps; $i++) {
        # Mostly new scenes; sometimes return to one of the last three, which reloads it after others.
        $scene = $(if ($recent.Count -gt 1 -and $random.NextDouble() -lt 0.3) { $recent[$recent.Count - 1 - $random.Next([Math]::Min(3, $recent.Count))] }
            else { $Scenes[$random.Next($Scenes.Count)] })
        $recent.Add($scene)
        $size = $sizes[$random.Next($sizes.Count)]
        $sequence += [ordered]@{
            scene = $scene
            mode = @('raster', 'raytracing')[$random.Next(2)]
            width = $size[0]; height = $size[1]
            antialiasing = @('none', 'smaa')[$random.Next(2)]
            postProcessing = [bool]$random.Next(2)
            time = $times[$random.Next($times.Count)]
        }
    }
    return $sequence
}

function Invoke-Capture([string]$Directory, $StepList, [switch]$Diagnostics) {
    $captures = @()
    for ($i = 0; $i -lt @($StepList).Count; $i++) {
        $step = @($StepList)[$i]
        $captures += [ordered]@{ scene = $step.scene; mode = $step.mode; width = $step.width; height = $step.height
            antialiasing = $step.antialiasing; postProcessing = $step.postProcessing; time = $step.time
            filename = (Join-Path $Directory ('step-{0:d3}.png' -f $i)) }
    }
    $first = @($StepList)[0]
    $request = [ordered]@{ scene = $first.scene; mode = $first.mode; seed = 12345; width = $first.width; height = $first.height
        warmupFrames = 3; postProcessing = $first.postProcessing; antialiasing = $first.antialiasing; allCores = [bool]$AllCores
        report = (Join-Path $Directory 'report.json'); captures = $captures }
    if ($Diagnostics) {
        $request.dred = $true
        $request.dredReport = Join-Path $Directory 'dred.txt'
        $request.dumpFile = Join-Path $Directory 'crash.dmp'
    }
    $requestPath = Join-Path $Directory 'request.json'
    [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 6), $utf8)
    $process = Start-Process -FilePath $Executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) `
        -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $Directory 'stdout.log') -RedirectStandardError (Join-Path $Directory 'stderr.log')
    [void]$process.Handle
    $timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
    if ($timedOut) { $process.Kill(); $process.WaitForExit() }
    return [pscustomobject]@{ exitCode = $process.ExitCode; timedOut = $timedOut; report = (Join-Path $Directory 'report.json') }
}

# A fresh render of one step in its own process, cached by its settings.
function Get-Reference($Step) {
    $key = '{0}_{1}_{2}x{3}_{4}_post{5}_t{6}' -f $Step.scene, $Step.mode, $Step.width, $Step.height, $Step.antialiasing,
        [int]$Step.postProcessing, $Step.time
    $image = Join-Path $referenceDirectory "$key.png"
    if (Test-Path -LiteralPath $image) { return $image }
    $directory = Join-Path $referenceDirectory "$key.job"
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $result = Invoke-Capture $directory @($Step)
    $captured = Join-Path $directory 'step-000.png'
    if ($result.exitCode -ne 0 -or -not (Test-Path -LiteralPath $captured)) { throw "Reference render failed for $key (exit $($result.exitCode)); see $directory." }
    Move-Item -LiteralPath $captured -Destination $image
    Remove-Item -LiteralPath $directory -Recurse -Force
    return $image
}

function Get-LastStep([string]$Directory) {
    $log = Join-Path $Directory 'stderr.log'
    if (-not (Test-Path -LiteralPath $log)) { return $null }
    $line = @(Get-Content -LiteralPath $log | Where-Object { $_ -match '^step (\d+):' }) | Select-Object -Last 1
    if ($line -and $line -match '^step (\d+):') { return [int]$Matches[1] }
    return $null
}

function Invoke-Run([int]$RunSeed, $Sequence, [string]$Label) {
    $directory = Join-Path $artifactRoot ('{0}_{1}' -f [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'), $Label)
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $sequenceInfo = [ordered]@{ seed = $RunSeed; steps = @($Sequence).Count; configuration = $Configuration; executable = $Executable
        allCores = [bool]$AllCores; scenes = $Scenes; sequence = $Sequence }
    [IO.File]::WriteAllText((Join-Path $directory 'sequence.json'), ($sequenceInfo | ConvertTo-Json -Depth 6), $utf8)
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    $result = Invoke-Capture $directory $Sequence -Diagnostics
    $stopwatch.Stop()

    $failure = $null
    $lastStep = Get-LastStep $directory
    $report = $(if (Test-Path -LiteralPath $result.report) { Get-Content -LiteralPath $result.report -Raw | ConvertFrom-Json } else { $null })
    if ($result.timedOut) { $failure = [ordered]@{ kind = 'timeout'; step = $lastStep; detail = "No exit after $TimeoutSeconds s" } }
    elseif (Test-Path -LiteralPath (Join-Path $directory 'crash.dmp')) { $failure = [ordered]@{ kind = 'crash'; step = $lastStep; detail = "Exit code $($result.exitCode); minidump crash.dmp" } }
    elseif (Test-Path -LiteralPath (Join-Path $directory 'dred.txt')) { $failure = [ordered]@{ kind = 'device-removed'; step = $lastStep; detail = (Get-Content -LiteralPath (Join-Path $directory 'dred.txt') -TotalCount 1) } }
    elseif ($result.exitCode -ne 0 -or -not $report -or -not $report.success) {
        $failure = [ordered]@{ kind = 'step-error'; step = $lastStep; detail = $(if ($report -and $report.PSObject.Properties.Name -contains 'error') { $report.error } else { "Exit code $($result.exitCode)" }) }
    }
    elseif (-not $NoImageCheck) {
        for ($i = 0; $i -lt @($Sequence).Count; $i++) {
            $reference = Get-Reference @($Sequence)[$i]
            $actual = Join-Path $directory ('step-{0:d3}.png' -f $i)
            $comparison = [TreeRenderTests.ImageComparison]::Compare($reference, $actual, (Join-Path $directory ('step-{0:d3}-diff.png' -f $i)))
            if (-not $comparison.SameDimensions -or $comparison.Rms -gt $manifest.rmsTolerance) {
                Copy-Item -LiteralPath $reference -Destination (Join-Path $directory ('step-{0:d3}-fresh.png' -f $i))
                $failure = [ordered]@{ kind = 'image-mismatch'; step = $i
                    detail = 'RMS {0:G4} against a fresh render (tolerance {1}); {2} pixels changed' -f $comparison.Rms, $manifest.rmsTolerance, $comparison.ChangedPixels }
                break
            }
        }
    }

    $summary = [ordered]@{ seed = $RunSeed; directory = $directory; seconds = [Math]::Round($stopwatch.Elapsed.TotalSeconds, 1)
        passed = ($null -eq $failure); failure = $failure
        failingStep = $(if ($failure -and $null -ne $failure.step) { @($Sequence)[$failure.step] } else { $null }) }
    [IO.File]::WriteAllText((Join-Path $directory 'summary.json'), ($summary | ConvertTo-Json -Depth 6), $utf8)
    if (-not $failure) {
        if (-not $KeepPassing) { Remove-Item -LiteralPath $directory -Recurse -Force }   # Keep only failures.
        Write-Host ("  seed {0}: passed {1} steps in {2:N0} s" -f $RunSeed, @($Sequence).Count, $summary.seconds)
    } else {
        $step = $summary.failingStep
        Write-Host ("  seed {0}: FAILED ({1}) at step {2}: {3}" -f $RunSeed, $failure.kind, $failure.step, $failure.detail) -ForegroundColor Red
        if ($step) { Write-Host ("    step: {0} / {1} {2}x{3} aa={4} post={5} t={6}" -f $step.scene, $step.mode, $step.width, $step.height, $step.antialiasing, $step.postProcessing, $step.time) }
        Write-Host "    evidence: $directory"
        Write-Host "    replay:   .\run-stress-test.ps1 -Replay `"$directory`" -Configuration $Configuration"
    }
    return $summary
}

if ($Replay) {
    $saved = Get-Content -LiteralPath (Join-Path $Replay 'sequence.json') -Raw | ConvertFrom-Json
    if ($saved.allCores -and -not $AllCores) { $AllCores = [switch]$true }
    $Scenes = @($saved.scenes)
    Write-Host "Replaying seed $($saved.seed), $($saved.steps) steps ($Configuration)"
    $summary = Invoke-Run $saved.seed @($saved.sequence) "replay-seed$($saved.seed)"
    if (-not $summary.passed) { exit 1 }
    exit 0
}

if ($Seed -lt 0) { $Seed = (New-Object System.Random).Next(1000000) }
Write-Host "Stress: $Iterations x $Steps steps from seed $Seed ($Configuration$(if ($AllCores) { ', all cores' }); scenes: $($Scenes -join ', '))"
$failed = 0
for ($iteration = 0; $iteration -lt $Iterations; $iteration++) {
    $runSeed = $Seed + $iteration
    $summary = Invoke-Run $runSeed (New-Sequence $runSeed) "seed$runSeed"
    if (-not $summary.passed) { $failed++; break }
}
Write-Host "$(if ($failed) { 'Stopped at the first failure.' } else { "All $Iterations runs passed." })"
if ($failed) { exit 1 }
