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
    [string[]]$Scenes = @(),
    [ValidateRange(0, 1000)][double]$StartTime = 5,
    [ValidateRange(1, 1800)][int]$TimeoutSeconds = 180,
    # The commit -Executable was built from, when it isn't this checkout's HEAD.
    [string]$SourceCommit,
    # -Executable is an older commit with this harness's capture code added, to backfill history.
    [switch]$BackportedHarness,
    # Ray trace leaves without opacity micromaps (DXR 1.2), to measure what they save.
    [switch]$DisableOpacityMicromaps
)

# Measures every scene whose definition has "testPass": "performance" and writes the results to
# Tests\Performance\Artifacts\<yyyyMMdd-HHmm>Z_<machine>_<label>. See README.md for the result format.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$sceneDirectory = Join-Path $workspaceRoot 'Game\Resources\Scenes'
$frames = 20
$frameTimeStep = 1.0 / 60.0
$windows = @(
    [ordered]@{ name = 'frames5to10'; first = 5; end = 10 },
    [ordered]@{ name = 'frames10to20'; first = 10; end = 20 }
)
$gpuPasses = @('frame', 'shadow', 'main', 'deformation', 'blas', 'tlas', 'rays')
$metricNames = @('frameMs', 'sceneUpdateMs', 'drawUploadMs', 'blasHashMs', 'blasDescriptorsMs', 'blasSubmitMs', 'tlasSubmitMs') + @($gpuPasses | ForEach-Object { "gpu_$($_)Ms" })
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

# Scenes tagged for the performance pass, in file-name order.
$selected = @()
foreach ($file in Get-ChildItem -LiteralPath $sceneDirectory -Filter '*.scene.json' | Sort-Object Name) {
    $definition = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
    if (-not ($definition.PSObject.Properties.Name -contains 'testPass')) { continue }
    if (@($definition.testPass) -notcontains 'performance') { continue }
    if (-not ($definition.PSObject.Properties.Name -contains 'id')) { throw "$($file.Name) is in the performance pass but has no id." }
    $selected += [pscustomobject]@{ id = $definition.id; name = $definition.name; file = $file.Name }
}
if ($Scenes.Count) {
    $unknown = @($Scenes | Where-Object { $_ -notin @($selected.id) })
    if ($unknown.Count) { throw "Not in the performance pass: $($unknown -join ', ')" }
    $selected = @($selected | Where-Object { $_.id -in $Scenes })
}
if (-not $selected.Count) { throw "No scenes in $sceneDirectory have `"testPass`": `"performance`"." }

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
    foreach ($metric in @('blasHashMs', 'blasDescriptorsMs', 'blasSubmitMs', 'tlasSubmitMs')) {
        $summary[$metric] = $null
        if (@($inWindow | Where-Object { $_.PSObject.Properties.Name -notcontains $metric }).Count -eq 0) {
            $summary[$metric] = Get-Statistics @($inWindow | ForEach-Object { $_.$metric })
        }
    }
    for ($i = 0; $i -lt $gpuPasses.Count; $i++) {
        $summary["gpu_$($gpuPasses[$i])Ms"] = Get-Statistics @($inWindow | ForEach-Object { $_.gpuMs[$i] })
    }
    return $summary
}

$jobs = New-Object 'System.Collections.Generic.List[object]'
$adapter = $null
$workerThreads = $null
$failures = 0
# Runs are the outer loop so background or thermal noise spreads across scenes instead of landing on one.
for ($run = 1; $run -le $Runs; $run++) {
    foreach ($scene in $selected) {
        foreach ($mode in $Modes) {
            Write-Host "Run $run/$Runs`: $($scene.id) / $mode..."
            $jobDirectory = Join-Path $runDirectory "jobs\$($scene.id)-$mode-run$run"
            New-Item -ItemType Directory -Path $jobDirectory -Force | Out-Null
            $reportPath = Join-Path $jobDirectory 'report.json'
            $request = [ordered]@{
                scene = $scene.id; mode = $mode; seed = 12345; width = $Width; height = $Height; postProcessing = $true
                opacityMicromaps = (-not $DisableOpacityMicromaps)
                report = $reportPath; performance = [ordered]@{ frames = $frames; frameTimeStep = $frameTimeStep; startTime = $StartTime }
            }
            $requestPath = Join-Path $jobDirectory 'request.json'
            [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 5), $utf8)
            $job = [ordered]@{ scene = $scene.id; sceneName = $scene.name; mode = $mode; run = $run; success = $false; error = $null
                processMs = $null; sceneLoad = $null; opacityMicromaps = $null; windows = $null; samples = @() }
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
                $job.windows = [ordered]@{}
                foreach ($window in $windows) {
                    $summary = Get-WindowSummary $job.samples $window
                    if (-not $summary) { throw "Frames $($window.first)-$($window.end - 1) were not all reported." }
                    $job.windows[$window.name] = $summary
                }
                $adapter = $report.adapter
                $workerThreads = $report.workerThreads
                $job.success = $true
                Write-Host ("  load {0:N0} ms, frame {1:N2} ms, GPU {2:N2} ms" -f $report.sceneLoad.totalMs,
                    $job.windows.frames10to20.frameMs.mean, $job.windows.frames10to20.gpu_frameMs.mean)
            } catch {
                $job.error = $_.Exception.Message
                $failures++
                Write-Host "  FAILED: $($job.error)"
            }
            $jobs.Add([pscustomobject]$job)
        }
    }
}

$results = [ordered]@{
    schemaVersion = 2
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
    configuration = $(if ($Executable -match '\\Debug\\') { 'Debug' } else { $Configuration })
    executable = $Executable
    cpu = $cpu.Name.Trim()
    logicalProcessors = $cpu.NumberOfLogicalProcessors
    os = "$($os.Caption) $($os.Version)"
    adapter = $adapter
    workerThreads = $workerThreads
    width = $Width
    height = $Height
    frames = $frames
    startTime = $StartTime
    frameTimeStep = $frameTimeStep
    runs = $Runs
    modes = $Modes
    scenes = $selected
    gpuPasses = $gpuPasses
    windows = $windows
    jobs = $jobs
}
# Compact JSON: indentation made the file four times larger.
$resultsPath = Join-Path $runDirectory 'results.json'
[IO.File]::WriteAllText($resultsPath, ($results | ConvertTo-Json -Depth 12 -Compress), $utf8)

# One row per scene, mode, run, and window with load times and window means.
$rows = foreach ($job in $jobs) {
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
$rows | Export-Csv -LiteralPath (Join-Path $runDirectory 'summary.csv') -NoTypeInformation -Encoding UTF8
& (Join-Path $PSScriptRoot 'update-performance-index.ps1') -Results $resultsPath

Write-Host "$($jobs.Count) jobs; $failures failed. Results: $runDirectory"
Write-Host 'Run show-dashboard.ps1 to see the results over time.'
if ($failures) { exit 1 }
