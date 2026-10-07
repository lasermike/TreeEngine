[CmdletBinding()]
param(
    [string]$Executable,
    [string]$Manifest = "$PSScriptRoot\scenes.json",
    [string]$GoldDirectory = "$PSScriptRoot\Gold",
    [string]$OutputDirectory = "$PSScriptRoot\Artifacts",
    [string[]]$Scenes = @(),
    [ValidateSet('raster', 'raytracing')][string[]]$Modes = @(),
    [switch]$UpdateGold,
    [switch]$Build,
    [ValidateRange(1, 1800)][int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ($Build) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    if (-not $msbuild) { throw 'Visual Studio MSBuild was not found.' }
    & $msbuild (Join-Path $workspaceRoot 'TreeEngine.sln') /t:TreeClassic /p:Configuration=Debug /p:Platform=x64 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw 'Game build failed.' }
}
if (-not $Executable) { $Executable = Join-Path $workspaceRoot '..\..\Binaries\Debug\x64\TreeClassic\TreeClassic.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$config = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
if ($config.version -ne 1 -or $config.width -lt 32 -or $config.height -lt 32 -or $config.width -gt 2048 -or $config.height -gt 2048) { throw 'Invalid render-test manifest.' }
if ($config.rmsTolerance -lt 0 -or $config.rmsTolerance -gt 1 -or $config.maxErrorTolerance -lt 0 -or $config.maxErrorTolerance -gt 1) { throw 'Image tolerances must be in [0,1].' }
if (-not $Modes.Count) { $Modes = @($config.modes) }
if (-not $Modes.Count -or @($Modes | Where-Object { $_ -notin @('raster', 'raytracing') }).Count) { throw 'Invalid rendering modes.' }
$selectedScenes = @($config.scenes | Where-Object { -not $Scenes.Count -or $_.id -in $Scenes })
if (-not $selectedScenes.Count -or @($Scenes | Where-Object { $_ -notin @($config.scenes.id) }).Count) { throw 'Unknown or empty scene selection.' }
if (-not @($config.times).Count -or @($config.times | Where-Object { $_ -lt 0 -or [double]::IsNaN($_) -or [double]::IsInfinity($_) }).Count) { throw 'Capture times must be nonnegative and finite.' }
$GoldDirectory = [IO.Path]::GetFullPath($GoldDirectory)
$runDirectory = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null
if ($UpdateGold) { New-Item -ItemType Directory -Path $GoldDirectory -Force | Out-Null }
if (-not ('TreeRenderTests.ImageComparison' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'ImageComparison.cs') -ReferencedAssemblies System.Drawing
}
$utf8 = New-Object Text.UTF8Encoding($false)
$results = New-Object 'System.Collections.Generic.List[object]'
$failures = 0
foreach ($scene in $selectedScenes) {
    if ($scene.id -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Scene IDs must be safe file names.' }
    foreach ($mode in $Modes) {
        Write-Host "Capturing $($scene.id) / $mode..."
        $jobDirectory = Join-Path $runDirectory "$($scene.id)-$mode"
        New-Item -ItemType Directory -Path $jobDirectory -Force | Out-Null
        $points = @()
        for ($i = 0; $i -lt @($config.times).Count; $i++) {
            $points += [ordered]@{ time = [double]$config.times[$i]; filename = (Join-Path $jobDirectory "time-$i.png") }
        }
        $workerReportPath = Join-Path $jobDirectory 'capture-report.json'
        $request = [ordered]@{
            scene = $scene.name; mode = $mode; seed = $config.seed; width = $config.width; height = $config.height
            warmupFrames = $config.warmupFrames; postProcessing = $config.postProcessing; report = $workerReportPath; captures = $points
        }
        $requestPath = Join-Path $jobDirectory 'request.json'
        [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 10), $utf8)
        $jobError = $null
        try {
            $process = Start-Process -FilePath $Executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $jobDirectory 'stdout.log') -RedirectStandardError (Join-Path $jobDirectory 'stderr.log')
            # Retain the process handle so Windows PowerShell can read ExitCode after exit.
            [void]$process.Handle
            if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
                $process.Kill(); $process.WaitForExit(); throw "Capture timed out after $TimeoutSeconds seconds."
            }
            if ($process.ExitCode -ne 0) { throw "Capture process exited with code $($process.ExitCode). See stderr.log." }
            if (-not (Test-Path -LiteralPath $workerReportPath)) { throw 'Capture report was not written.' }
            $workerReport = Get-Content -LiteralPath $workerReportPath -Raw | ConvertFrom-Json
            if (-not $workerReport.success -or @($workerReport.captures).Count -ne $points.Count) { throw 'Capture job did not complete all requested images.' }
        } catch { $jobError = $_.Exception.Message }

        for ($i = 0; $i -lt $points.Count; $i++) {
            $actual = $points[$i].filename
            $goldName = "$($scene.id)-$mode-time-$i"
            $gold = Join-Path $GoldDirectory "$goldName.png"
            $goldMetadata = Join-Path $GoldDirectory "$goldName.json"
            $diff = Join-Path $jobDirectory "time-$i-diff.png"
            $settings = [ordered]@{ scene = $scene.name; mode = $mode; time = $points[$i].time; width = $config.width; height = $config.height; seed = $config.seed; warmupFrames = $config.warmupFrames; postProcessing = $config.postProcessing }
            $settingsText = $settings | ConvertTo-Json -Compress
            $row = [ordered]@{ scene = $scene.id; mode = $mode; time = $points[$i].time; status = 'pass'; rms = $null; maxError = $null; changedPixels = $null; actual = $actual; gold = $gold; diff = $null; error = $null }
            try {
                if ($jobError) { throw $jobError }
                if (-not (Test-Path -LiteralPath $actual)) { throw 'Captured PNG is missing.' }
                $actualImage = [Drawing.Image]::FromFile($actual)
                try { if ($actualImage.Width -ne $config.width -or $actualImage.Height -ne $config.height) { throw 'Captured dimensions differ from the request.' } } finally { $actualImage.Dispose() }
                if ($UpdateGold) {
                    Copy-Item -LiteralPath $actual -Destination $gold -Force
                    [IO.File]::WriteAllText($goldMetadata, $settingsText, $utf8)
                    $row.status = 'gold-updated'
                } else {
                    if (-not (Test-Path -LiteralPath $gold)) { throw 'Gold image is missing. Review captures and use -UpdateGold to establish a baseline.' }
                    if (-not (Test-Path -LiteralPath $goldMetadata) -or (Get-Content -LiteralPath $goldMetadata -Raw).Trim() -ne $settingsText) { throw 'Gold rendering settings differ from this run. Baselines need explicit review/update.' }
                    $comparison = [TreeRenderTests.ImageComparison]::Compare($gold, $actual, $diff)
                    if (-not $comparison.SameDimensions) { throw 'Gold and actual image dimensions differ.' }
                    $row.rms = $comparison.Rms; $row.maxError = $comparison.MaxError; $row.changedPixels = $comparison.ChangedPixels; $row.diff = $diff
                    if ($comparison.Rms -gt $config.rmsTolerance -or $comparison.MaxError -gt $config.maxErrorTolerance) { $row.status = 'fail'; $row.error = 'Image difference exceeds tolerance.' }
                }
            } catch { $row.status = 'fail'; $row.error = $_.Exception.Message }
            if ($row.status -eq 'fail') { $failures++ }
            $results.Add([pscustomobject]$row)
            Write-Host ("  t={0}: {1}, RMS={2}" -f $row.time, $row.status, $row.rms)
        }
    }
}
$report = [ordered]@{ version = 1; executable = $Executable; executableSha256 = (Get-FileHash -LiteralPath $Executable).Hash; manifest = $config; utc = [DateTime]::UtcNow.ToString('o'); os = [Environment]::OSVersion.VersionString; goldUpdate = [bool]$UpdateGold; failures = $failures; results = @($results.ToArray()) }
[IO.File]::WriteAllText((Join-Path $runDirectory 'report.json'), ($report | ConvertTo-Json -Depth 20), $utf8)
Write-Host "$($results.Count) captures; $failures failures. Report: $runDirectory\report.json"
if ($failures) { exit 1 }
exit 0
