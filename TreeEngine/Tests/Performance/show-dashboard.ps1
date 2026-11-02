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
    'gpu_deformationMs', 'gpu_blasMs', 'gpu_tlasMs', 'gpu_raysMs')
$columns = @('scene', 'mode', 'run', 'window') + $valueColumns

function ConvertTo-Number([string]$Value) {
    if ([string]::IsNullOrEmpty($Value)) { return $null }
    return [double]::Parse($Value, [Globalization.CultureInfo]::InvariantCulture)
}

$runs = foreach ($entry in @($index.runs)) {
    $summaryPath = Join-Path (Join-Path $artifactRoot $entry.directory) 'summary.csv'
    if (-not (Test-Path -LiteralPath $summaryPath)) { Write-Warning "Skipping $($entry.directory): no summary.csv"; continue }
    $rows = New-Object 'System.Collections.Generic.List[object]'
    foreach ($row in Import-Csv -LiteralPath $summaryPath) {
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
    [pscustomobject]$run
}

$data = [ordered]@{
    generatedUtc = (Get-Date).ToUniversalTime().ToString('o')
    sceneNames = $sceneNames
    columns = $columns
    runs = @($runs)
}
$dataPath = Join-Path $PSScriptRoot 'dashboard-data.js'
$json = $data | ConvertTo-Json -Depth 8 -Compress
[IO.File]::WriteAllText($dataPath, "window.PERF_DATA = $json;`n", $utf8)
Write-Host "Dashboard data: $(@($runs).Count) runs in $dataPath"
if (-not $NoOpen) { Start-Process (Join-Path $PSScriptRoot 'dashboard.html') }
