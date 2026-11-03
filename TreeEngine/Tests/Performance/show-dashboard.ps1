[CmdletBinding()]
param(
    # Write dashboard-data.js without opening the dashboard.
    [switch]$NoOpen
)

# Gathers every run in Artifacts\index.json and its summary.csv into dashboard-data.js, then opens
# dashboard.html. The data file is generated and not committed; run this again after new runs.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$artifactRoot = Join-Path $PSScriptRoot 'Artifacts'
$indexPath = Join-Path $artifactRoot 'index.json'
$sceneDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\Game\Resources\Scenes'))
$utf8 = New-Object Text.UTF8Encoding($false)

if (-not (Test-Path -LiteralPath $indexPath)) { throw "No runs yet: $indexPath does not exist. Run run-performance-tests.ps1 first." }
$index = Get-Content -LiteralPath $indexPath -Raw | ConvertFrom-Json

# Display names for scene ids, from the current scene files.
$sceneNames = [ordered]@{}
foreach ($file in Get-ChildItem -LiteralPath $sceneDirectory -Filter '*.scene.json') {
    $definition = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
    if ($definition.PSObject.Properties.Name -contains 'id') { $sceneNames[$definition.id] = $definition.name }
}

# summary.csv columns copied into each run, as numbers. Other columns identify the row.
$valueColumns = @('loadTotalMs', 'loadDefinitionMs', 'loadGenerationMs', 'loadGraphicsMs', 'processMs',
    'frameMs', 'sceneUpdateMs', 'drawUploadMs', 'blasCpuMs', 'blasSnapshotMs', 'blasHashMs', 'blasDescriptorsMs', 'blasSubmitMs', 'tlasSubmitMs', 'firstBlasBuildCpuMs', 'firstBlasBuildGpuMs', 'gpu_frameMs', 'gpu_shadowMs', 'gpu_mainMs',
    'gpu_deformationMs', 'gpu_blasMs', 'gpu_tlasMs', 'gpu_raysMs', 'gpu_antialiasingMs')
$columns = @('scene', 'mode', 'run', 'window') + $valueColumns

function ConvertTo-Number([string]$Value) {
    if ([string]::IsNullOrEmpty($Value)) { return $null }
    return [double]::Parse($Value, [Globalization.CultureInfo]::InvariantCulture)
}

# Pacing jobs (results.json schema 3): each repeat's summary, frame times, and the frames that rebuilt the BLAS.
function Get-PacingJobs([string]$ResultsPath) {
    if (-not (Test-Path -LiteralPath $ResultsPath)) { return }
    $results = Get-Content -LiteralPath $ResultsPath -Raw | ConvertFrom-Json
    if ($results.schemaVersion -lt 3) { return }
    foreach ($job in @($results.jobs | Where-Object { $_.type -eq 'pacing' -and $_.success })) {
        $measured = @($job.samples | Where-Object { -not $_.settle })
        [ordered]@{
            test = $job.test; scene = $job.scene; mode = $job.mode; run = $job.run; summary = $job.pacing
            frameTimeStep = @($results.tests | Where-Object { $_.id -eq $job.test })[0].frameTimeStep
            frameMs = @($measured | ForEach-Object { [math]::Round([double]$_.frameMs, 3) })
            rebuilt = @($measured | Where-Object { $_.rebuiltBLAS } | ForEach-Object { [int]$_.frame })
        }
    }
}

$runs = foreach ($entry in @($index.runs)) {
    $summaryPath = Join-Path (Join-Path $artifactRoot $entry.directory) 'summary.csv'
    $rows = New-Object 'System.Collections.Generic.List[object]'
    $hasSummary = Test-Path -LiteralPath $summaryPath
    if (-not $hasSummary -and -not (Test-Path -LiteralPath (Join-Path (Join-Path $artifactRoot $entry.directory) 'pacing.csv'))) {
        Write-Warning "Skipping $($entry.directory): no summary.csv"; continue
    }
    foreach ($row in $(if ($hasSummary) { Import-Csv -LiteralPath $summaryPath } else { @() })) {
        if ($row.success -ne 'True') { continue }
        $values = @($row.scene, $row.mode, [int]$row.run, $row.window)
        foreach ($column in $valueColumns) {
            $values += $(if ($row.PSObject.Properties.Name -contains $column) { ConvertTo-Number $row.$column } else { $null })
        }
        $rows.Add($values)
    }
    $run = [ordered]@{}
    foreach ($property in $entry.PSObject.Properties) { $run[$property.Name] = $property.Value }
    $run.rows = $rows.ToArray()
    $run.pacing = @(Get-PacingJobs (Join-Path (Join-Path $artifactRoot $entry.directory) 'results.json'))
    [pscustomobject]$run
}

$data = [ordered]@{
    generatedUtc = (Get-Date).ToUniversalTime().ToString('o')
    sceneNames = $sceneNames
    columns = $columns
    runs = @($runs)
}
$dataPath = Join-Path $PSScriptRoot 'dashboard-data.js'
$json = $data | ConvertTo-Json -Depth 10 -Compress
[IO.File]::WriteAllText($dataPath, "window.PERF_DATA = $json;`n", $utf8)
Write-Host "Dashboard data: $(@($runs).Count) runs in $dataPath"
if (-not $NoOpen) { Start-Process (Join-Path $PSScriptRoot 'dashboard.html') }
