[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Label,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$Build,
    [string]$Executable,
    [ValidateRange(1, 100)][int]$Runs = 3,
    [ValidateRange(32, 2048)][int]$Width = 1280,
    [ValidateRange(32, 2048)][int]$Height = 720,
    [ValidateSet('raster', 'raytracing')][string[]]$Modes = @('raster', 'raytracing'),
    # Test ids from scenes.json. Steady tests use their scene's id, so -Scenes still works.
    [Alias('Scenes')][string[]]$Tests = @(),
    # Overrides the start time of steady tests.
    [ValidateRange(0, 1000)][double]$StartTime = 5,
    [ValidateRange(1, 1800)][int]$TimeoutSeconds = 180,
    # The commit -Executable was built from, when it isn't this checkout's HEAD.
    [string]$SourceCommit,
    # -Executable is an older commit with this harness's capture code added, to backfill history.
    [switch]$BackportedHarness,
    # Ray trace leaves without opacity micromaps (DXR 1.2), to measure what they save.
    [switch]$DisableOpacityMicromaps,
    # Anti-aliasing for every test, overriding scenes.json: none or smaa.
    [ValidateSet('none', 'smaa')][string]$Antialiasing
)

# Runs the tests in scenes.json and writes the results to
# Tests\Performance\Artifacts\<yyyyMMdd-HHmm>Z_<machine>_<label>. See README.md for the test types and result format.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$sceneDirectory = Join-Path $workspaceRoot 'Game\Resources\Scenes'
$manifestPath = Join-Path $PSScriptRoot 'scenes.json'
$windows = @(
    [ordered]@{ name = 'frames5to10'; first = 5; end = 10 },
    [ordered]@{ name = 'frames10to20'; first = 10; end = 20 }
)
$gpuPasses = @('frame', 'shadow', 'main', 'deformation', 'blas', 'tlas', 'rays', 'antialiasing')
$metricNames = @('frameMs', 'sceneUpdateMs', 'drawUploadMs', 'blasCpuMs', 'blasSnapshotMs', 'blasHashMs', 'blasDescriptorsMs', 'blasSubmitMs', 'tlasSubmitMs', 'firstBlasBuildCpuMs', 'firstBlasBuildGpuMs') + @($gpuPasses | ForEach-Object { "gpu_$($_)Ms" })
$pacingMetricNames = @('frameP50Ms', 'frameP90Ms', 'frameP99Ms', 'frameMaxMs', 'frameMeanMs', 'spikes', 'over33Ms', 'spikeExcessMs', 'rebuiltBLASFrames', 'maxBlasCpuMs')
$utf8 = New-Object Text.UTF8Encoding($false)

if ($Build) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    if (-not $msbuild) { throw 'Visual Studio MSBuild was not found.' }
    & $msbuild (Join-Path $workspaceRoot 'TreeEngine.sln') /t:TreeClassic /p:Configuration=$Configuration /p:Platform=x64 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw 'Game build failed.' }
}
if (-not $Executable) { $Executable = Join-Path $workspaceRoot "TreeClassic\bin\x64\$Configuration\TreeClassic.exe" }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if ($Executable -match '\\Debug\\') { Write-Warning 'Measuring a Debug build; timings will not reflect Release performance.' }

# ---- Tests from scenes.json ----------------------------------------------------------------------
function Test-FiniteNumber($Value) {
    return ($Value -is [int] -or $Value -is [long] -or $Value -is [double] -or $Value -is [decimal]) -and
        -not [double]::IsNaN([double]$Value) -and -not [double]::IsInfinity([double]$Value)
}
function Get-Setting($Test, $Defaults, [string]$Name, $Fallback) {
    if ($Test.PSObject.Properties.Name -contains $Name) { return $Test.$Name }
    if ($Defaults -and $Defaults.PSObject.Properties.Name -contains $Name) { return $Defaults.$Name }
    return $Fallback
}

$sceneFiles = @{}
foreach ($file in Get-ChildItem -LiteralPath $sceneDirectory -Filter '*.scene.json') {
    $definition = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
    if ($definition.PSObject.Properties.Name -contains 'id') {
        $sceneFiles[$definition.id] = [pscustomobject]@{ id = $definition.id; name = $definition.name; file = $file.Name }
    }
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.version -ne 1) { throw "$manifestPath`: unsupported version." }
$defaults = $(if ($manifest.PSObject.Properties.Name -contains 'defaults') { $manifest.defaults } else { $null })
$allTests = @()
foreach ($entry in @($manifest.tests)) {
    $where = "$manifestPath`: test '$($entry.id)'"
    if ($entry.id -notmatch '^[A-Za-z0-9-]+$') { throw "$where`: ids use letters, digits and hyphens." }
    if ($entry.id -in @($allTests | ForEach-Object { $_.id })) { throw "$where`: duplicate id." }
    if (-not $sceneFiles.ContainsKey([string]$entry.scene)) { throw "$where`: no scene file has id '$($entry.scene)'." }
    if ($entry.type -notin @('steady', 'pacing')) { throw "$where`: type must be steady or pacing." }
    $testModes = @(Get-Setting $entry $defaults 'modes' @('raster', 'raytracing'))
    if (-not $testModes.Count -or @($testModes | Where-Object { $_ -notin @('raster', 'raytracing') }).Count) { throw "$where`: invalid modes." }
    $step = Get-Setting $entry $defaults 'frameTimeStep' (1.0 / 60.0)
    if (-not (Test-FiniteNumber $step) -or $step -le 0) { throw "$where`: frameTimeStep must be positive." }
    $testAntialiasing = $(if ($Antialiasing) { $Antialiasing } else { [string](Get-Setting $entry $defaults 'antialiasing' 'none') })
    if ($testAntialiasing -notin @('none', 'smaa')) { throw "$where`: antialiasing must be none or smaa." }
    $test = [ordered]@{ id = $entry.id; scene = $entry.scene; sceneName = $sceneFiles[[string]$entry.scene].name; type = $entry.type
        modes = $testModes; frameTimeStep = [double]$step; antialiasing = $testAntialiasing }
    if ($entry.type -eq 'steady') {
        $test.startTime = [double]$(if ($PSBoundParameters.ContainsKey('StartTime')) { $StartTime } else { Get-Setting $entry $defaults 'startTime' 5 })
        $test.frames = [int](Get-Setting $entry $defaults 'frames' 20)
        if ($test.frames -lt $windows[-1].end) { throw "$where`: steady tests need at least $($windows[-1].end) frames." }
        $test.settleFrames = 0
    } else {
        $test.startTime = [double](Get-Setting $entry $null 'startTime' 0)
        $test.duration = Get-Setting $entry $null 'duration' 0
        if (-not (Test-FiniteNumber $test.duration) -or $test.duration -le 0) { throw "$where`: duration must be positive." }
        $test.frames = [int][math]::Round($test.duration / $test.frameTimeStep)
        $test.settleFrames = [int](Get-Setting $entry $null 'settleFrames' 30)
        if ($test.settleFrames -lt 1) { throw "$where`: pacing tests need at least one settle frame." }
        $keys = @($entry.camera)
        if ($keys.Count -lt 2 -or $keys[0].time -ne 0 -or $keys[-1].time -ne $test.duration) { throw "$where`: the camera path needs keys from time 0 to the duration." }
        for ($i = 0; $i -lt $keys.Count; $i++) {
            $numbers = @($keys[$i].time) + @($keys[$i].position) + @($keys[$i].rotationDegrees)
            if (@($keys[$i].position).Count -ne 3 -or @($keys[$i].rotationDegrees).Count -ne 3 -or
                @($numbers | Where-Object { -not (Test-FiniteNumber $_) }).Count) { throw "$where`: camera key $i needs a time, position[3] and rotationDegrees[3]." }
            if ($i -gt 0 -and $keys[$i].time -le $keys[$i - 1].time) { throw "$where`: camera key times must increase." }
        }
        $test.camera = $keys
    }
    if (-not (Test-FiniteNumber $test.startTime) -or $test.startTime -lt 0) { throw "$where`: startTime must be nonnegative." }
    $allTests += [pscustomobject]$test
}
$selected = $allTests
if ($Tests.Count) {
    $unknown = @($Tests | Where-Object { $_ -notin @($allTests | ForEach-Object { $_.id }) })
    if ($unknown.Count) { throw "Not in $manifestPath`: $($unknown -join ', ')" }
    $selected = @($allTests | Where-Object { $_.id -in $Tests })
}
if (-not $selected.Count) { throw "No tests selected from $manifestPath." }

# Run directory: <yyyyMMdd-HHmm>Z_<machine>_<label> in UTC, so directories sort by time. Underscores only
# separate the parts. A numeric suffix is added if the directory already exists.
$started = Get-Date
function Get-SafeName([string]$Value) { return ($Value.Trim() -replace '[^A-Za-z0-9.-]+', '-').Trim('-', '.') }
$safeLabel = Get-SafeName $Label
if (-not $safeLabel) { throw 'The label must contain at least one letter or digit.' }
$artifactRoot = Join-Path $PSScriptRoot 'Artifacts'
$baseName = '{0}Z_{1}_{2}' -f $started.ToUniversalTime().ToString('yyyyMMdd-HHmm'), (Get-SafeName $env:COMPUTERNAME), $safeLabel
$runDirectory = Join-Path $artifactRoot $baseName
for ($suffix = 2; Test-Path -LiteralPath $runDirectory; $suffix++) { $runDirectory = Join-Path $artifactRoot "$baseName-$suffix" }
New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null

function Get-GitValue([string[]]$Arguments) {
    $value = & git -C $workspaceRoot @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    return ($value -join "`n").Trim()
}
$commitRef = 'HEAD'
$status = Get-GitValue @('status', '--porcelain')
if ($SourceCommit) {
    $commitRef = Get-GitValue @('rev-parse', '--verify', "$SourceCommit^{commit}")
    if (-not $commitRef) { throw "Unknown commit: $SourceCommit" }
    $status = $null   # The build came from that commit, not from this checkout.
}
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$os = Get-CimInstance Win32_OperatingSystem

function Get-Statistics([double[]]$Values) {
    $measure = $Values | Measure-Object -Average -Minimum -Maximum
    return [ordered]@{ mean = $measure.Average; min = $measure.Minimum; max = $measure.Maximum }
}

# Mean/min/max of each timing over the frames [first, end).
function Get-WindowSummary($Samples, $Window) {
    $inWindow = @($Samples | Where-Object { $_.frame -ge $Window.first -and $_.frame -lt $Window.end })
    if ($inWindow.Count -ne ($Window.end - $Window.first)) { return $null }
    $summary = [ordered]@{
        frameMs = Get-Statistics @($inWindow | ForEach-Object { $_.frameMs })
        sceneUpdateMs = Get-Statistics @($inWindow | ForEach-Object { $_.sceneUpdateMs })
        drawUploadMs = Get-Statistics @($inWindow | ForEach-Object { $_.drawUploadMs })
    }
    foreach ($metric in @('blasCpuMs', 'blasSnapshotMs', 'blasHashMs', 'blasDescriptorsMs', 'blasSubmitMs', 'tlasSubmitMs')) {
        $summary[$metric] = $null
        if (@($inWindow | Where-Object { $_.PSObject.Properties.Name -notcontains $metric }).Count -eq 0) {
            $summary[$metric] = Get-Statistics @($inWindow | ForEach-Object { $_.$metric })
        }
    }
    # Initial creation is usually frame 0, outside the steady-state windows. Use the first actual rebuild.
    $firstBuild = @($Samples | Where-Object { $_.PSObject.Properties.Name -contains 'rebuiltBLAS' -and $_.rebuiltBLAS } | Select-Object -First 1)
    $summary['firstBlasBuildCpuMs'] = $null
    $summary['firstBlasBuildGpuMs'] = $null
    if ($firstBuild.Count -gt 0) {
        $first = $firstBuild[0]
        if ($first.PSObject.Properties.Name -contains 'blasCpuMs') {
            $summary['firstBlasBuildCpuMs'] = Get-Statistics @([double]$first.blasCpuMs)
        }
        if ($first.PSObject.Properties.Name -contains 'gpuMs' -and @($first.gpuMs).Count -gt 4) {
            $summary['firstBlasBuildGpuMs'] = Get-Statistics @([double]$first.gpuMs[4])
        }
    }
    for ($i = 0; $i -lt $gpuPasses.Count; $i++) {
        $summary["gpu_$($gpuPasses[$i])Ms"] = Get-Statistics @($inWindow | ForEach-Object { $_.gpuMs[$i] })
    }
    return $summary
}

# Frame-time distribution of a pacing job's measured (non-settle) frames. Percentiles use the nearest rank.
function Get-PacingSummary($Samples, [int]$Frames) {
    $measured = @($Samples | Where-Object { -not $_.settle })
    if ($measured.Count -ne $Frames) { return $null }
    $sorted = [double[]]@($measured | ForEach-Object { $_.frameMs } | Sort-Object)
    $rank = { param($p) $sorted[[math]::Max(0, [int][math]::Ceiling($p * $sorted.Count) - 1)] }
    $median = & $rank 0.5
    # A spike is a visible hitch: over twice the median and longer than a 60 fps frame.
    $spikeThreshold = [math]::Max(2 * $median, 1000.0 / 60.0)
    $spikes = @($measured | Where-Object { $_.frameMs -gt $spikeThreshold })
    return [ordered]@{
        frameP50Ms = $median
        frameP90Ms = & $rank 0.9
        frameP99Ms = & $rank 0.99
        frameMaxMs = $sorted[-1]
        frameMeanMs = ($sorted | Measure-Object -Average).Average
        spikes = $spikes.Count
        spikeThresholdMs = $spikeThreshold
        over33Ms = @($sorted | Where-Object { $_ -gt 1000.0 / 30.0 }).Count
        spikeExcessMs = [double](($spikes | ForEach-Object { $_.frameMs - $median } | Measure-Object -Sum).Sum)
        rebuiltBLASFrames = @($measured | Where-Object { $_.rebuiltBLAS }).Count
        maxBlasCpuMs = [double](($measured | ForEach-Object { $_.blasCpuMs } | Measure-Object -Maximum).Maximum)
    }
}

$jobs = New-Object 'System.Collections.Generic.List[object]'
$adapter = $null
$workerThreads = $null
$failures = 0
# Runs are the outer loop so background or thermal noise spreads across tests instead of landing on one.
for ($run = 1; $run -le $Runs; $run++) {
    foreach ($test in $selected) {
        foreach ($mode in @($Modes | Where-Object { $_ -in $test.modes })) {
            Write-Host "Run $run/$Runs`: $($test.id) / $mode..."
            $jobDirectory = Join-Path $runDirectory "jobs\$($test.id)-$mode-run$run"
            New-Item -ItemType Directory -Path $jobDirectory -Force | Out-Null
            $reportPath = Join-Path $jobDirectory 'report.json'
            $performance = [ordered]@{ frames = $test.frames; frameTimeStep = $test.frameTimeStep; startTime = $test.startTime }
            if ($test.type -eq 'pacing') {
                $performance.settleFrames = $test.settleFrames
                $performance.camera = $test.camera
            }
            $request = [ordered]@{
                scene = $test.scene; mode = $mode; seed = 12345; width = $Width; height = $Height; postProcessing = $true
                opacityMicromaps = (-not $DisableOpacityMicromaps); antialiasing = $test.antialiasing
                report = $reportPath; performance = $performance
            }
            $requestPath = Join-Path $jobDirectory 'request.json'
            [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 6), $utf8)
            # Steady jobs keep "scene" = the test id (the same as the scene id), so older runs line up.
            $job = [ordered]@{ test = $test.id; type = $test.type; scene = $(if ($test.type -eq 'steady') { $test.id } else { $test.scene })
                sceneName = $test.sceneName; mode = $mode; run = $run; success = $false; error = $null
                processMs = $null; sceneLoad = $null; opacityMicromaps = $null; windows = $null; pacing = $null; samples = @() }
            try {
                $stopwatch = [Diagnostics.Stopwatch]::StartNew()
                $process = Start-Process -FilePath $Executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) `
                    -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru `
                    -RedirectStandardOutput (Join-Path $jobDirectory 'stdout.log') -RedirectStandardError (Join-Path $jobDirectory 'stderr.log')
                # Retain the process handle so Windows PowerShell can read ExitCode after exit.
                [void]$process.Handle
                if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
                    $process.Kill(); $process.WaitForExit(); throw "Timed out after $TimeoutSeconds seconds."
                }
                $stopwatch.Stop()
                $job.processMs = $stopwatch.Elapsed.TotalMilliseconds
                if (-not (Test-Path -LiteralPath $reportPath)) { throw "No report was written (exit code $($process.ExitCode)). See stderr.log." }
                $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
                if (-not $report.success) { throw "Capture failed: $($report.error)" }
                if ($process.ExitCode -ne 0) { throw "Process exited with code $($process.ExitCode)." }
                $job.sceneLoad = $report.sceneLoad
                if ($report.PSObject.Properties.Name -contains 'opacityMicromaps') { $job.opacityMicromaps = $report.opacityMicromaps }
                $job.samples = @($report.samples)
                if ($test.type -eq 'steady') {
                    $job.windows = [ordered]@{}
                    foreach ($window in $windows) {
                        $summary = Get-WindowSummary $job.samples $window
                        if (-not $summary) { throw "Frames $($window.first)-$($window.end - 1) were not all reported." }
                        $job.windows[$window.name] = $summary
                    }
                    Write-Host ("  load {0:N0} ms, frame {1:N2} ms, GPU {2:N2} ms" -f $report.sceneLoad.totalMs,
                        $job.windows.frames10to20.frameMs.mean, $job.windows.frames10to20.gpu_frameMs.mean)
                } else {
                    if (@($job.samples | Where-Object { $_.PSObject.Properties.Name -notcontains 'settle' }).Count) {
                        throw 'The executable does not support camera paths (samples have no "settle").'
                    }
                    $job.pacing = Get-PacingSummary $job.samples $test.frames
                    if (-not $job.pacing) { throw "Not all $($test.frames) measured frames were reported." }
                    Write-Host ("  frame p50 {0:N2} ms, p99 {1:N2} ms, max {2:N2} ms, {3} spikes, {4} BLAS rebuilds" -f $job.pacing.frameP50Ms,
                        $job.pacing.frameP99Ms, $job.pacing.frameMaxMs, $job.pacing.spikes, $job.pacing.rebuiltBLASFrames)
                }
                $adapter = $report.adapter
                $workerThreads = $report.workerThreads
                $job.success = $true
            } catch {
                $job.error = $_.Exception.Message
                $failures++
                Write-Host "  FAILED: $($job.error)"
            }
            $jobs.Add([pscustomobject]$job)
        }
    }
}

$steadyTests = @($selected | Where-Object { $_.type -eq 'steady' })
$results = [ordered]@{
    schemaVersion = 3
    label = $Label
    machine = $env:COMPUTERNAME
    startedUtc = $started.ToUniversalTime().ToString('o')
    finishedUtc = (Get-Date).ToUniversalTime().ToString('o')
    commit = Get-GitValue @('rev-parse', $commitRef)
    # The date and subject still identify the commit if a rebase changes its hash.
    commitDate = Get-GitValue @('show', '-s', '--format=%cI', $commitRef)
    commitSubject = Get-GitValue @('show', '-s', '--format=%s', $commitRef)
    branch = Get-GitValue @('rev-parse', '--abbrev-ref', 'HEAD')
    dirty = [bool]$status
    backportedHarness = [bool]$BackportedHarness
    opacityMicromaps = (-not $DisableOpacityMicromaps)
    # -Antialiasing for the whole run, or null when each test used its scenes.json setting.
    antialiasing = $(if ($Antialiasing) { $Antialiasing } else { $null })
    configuration = $(if ($Executable -match '\\Debug\\') { 'Debug' } else { $Configuration })
    executable = $Executable
    cpu = $cpu.Name.Trim()
    logicalProcessors = $cpu.NumberOfLogicalProcessors
    os = "$($os.Caption) $($os.Version)"
    adapter = $adapter
    workerThreads = $workerThreads
    width = $Width
    height = $Height
    # Steady-test settings, as in schema 2; every test's own settings are in "tests".
    frames = $(if ($steadyTests.Count) { $steadyTests[0].frames } else { $null })
    startTime = $(if ($steadyTests.Count) { $steadyTests[0].startTime } else { $null })
    frameTimeStep = $(if ($steadyTests.Count) { $steadyTests[0].frameTimeStep } else { $null })
    runs = $Runs
    modes = $Modes
    scenes = @($steadyTests | ForEach-Object { [ordered]@{ id = $_.id; name = $_.sceneName; file = $sceneFiles[[string]$_.scene].file } })
    tests = $selected
    gpuPasses = $gpuPasses
    windows = $windows
    jobs = $jobs
}
# Compact JSON: indentation made the file four times larger.
$resultsPath = Join-Path $runDirectory 'results.json'
[IO.File]::WriteAllText($resultsPath, ($results | ConvertTo-Json -Depth 12 -Compress), $utf8)

# summary.csv: one row per steady job and window with load times and window means.
$rows = foreach ($job in @($jobs | Where-Object { $_.type -eq 'steady' })) {
    foreach ($window in $windows) {
        $row = [ordered]@{ scene = $job.scene; mode = $job.mode; run = $job.run; window = $window.name; success = $job.success
            processMs = $job.processMs; loadTotalMs = $null; loadDefinitionMs = $null; loadGenerationMs = $null; loadGraphicsMs = $null }
        foreach ($metric in $metricNames) { $row[$metric] = $null }
        if ($job.success) {
            $row.loadTotalMs = $job.sceneLoad.totalMs; $row.loadDefinitionMs = $job.sceneLoad.definitionMs
            $row.loadGenerationMs = $job.sceneLoad.generationMs; $row.loadGraphicsMs = $job.sceneLoad.graphicsMs
            foreach ($metric in $metricNames) {
                $statistics = $job.windows[$window.name][$metric]
                if ($null -ne $statistics) { $row[$metric] = $statistics.mean }
            }
        }
        [pscustomobject]$row
    }
}
if (@($rows).Count) { $rows | Export-Csv -LiteralPath (Join-Path $runDirectory 'summary.csv') -NoTypeInformation -Encoding UTF8 }

# pacing.csv: one row per pacing job.
$pacingRows = foreach ($job in @($jobs | Where-Object { $_.type -eq 'pacing' })) {
    $row = [ordered]@{ test = $job.test; scene = $job.scene; mode = $job.mode; run = $job.run; success = $job.success; processMs = $job.processMs }
    foreach ($metric in $pacingMetricNames) { $row[$metric] = $(if ($job.success) { $job.pacing[$metric] } else { $null }) }
    [pscustomobject]$row
}
if (@($pacingRows).Count) { $pacingRows | Export-Csv -LiteralPath (Join-Path $runDirectory 'pacing.csv') -NoTypeInformation -Encoding UTF8 }
& (Join-Path $PSScriptRoot 'update-performance-index.ps1') -Results $resultsPath

Write-Host "$($jobs.Count) jobs; $failures failed. Results: $runDirectory"
Write-Host 'Run show-dashboard.ps1 to see the results over time.'
if ($failures) { exit 1 }
