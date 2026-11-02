[CmdletBinding()]
param(
    # A run's results.json to add or replace. Without it, the index is rebuilt from every run directory.
    [string]$Results
)

# Writes Artifacts\index.json: one line per performance run, oldest first, so a dashboard can list runs
# without reading every results.json. Run it with no arguments after merging runs from other machines.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$artifactRoot = Join-Path $PSScriptRoot 'Artifacts'
$indexPath = Join-Path $artifactRoot 'index.json'
$utf8 = New-Object Text.UTF8Encoding($false)

function Get-IndexEntry([string]$Path) {
    $run = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    $jobs = @($run.jobs)
    return [pscustomobject][ordered]@{
        directory = Split-Path (Split-Path $Path) -Leaf
        startedUtc = $run.startedUtc
        machine = $run.machine
        label = $run.label
        commit = $run.commit
        commitDate = $run.commitDate
        commitSubject = $run.commitSubject
        branch = $run.branch
        dirty = $run.dirty
        backportedHarness = [bool]($run.PSObject.Properties.Name -contains 'backportedHarness' -and $run.backportedHarness)
        opacityMicromapsOff = [bool]($run.PSObject.Properties.Name -contains 'opacityMicromaps' -and -not $run.opacityMicromaps)
        configuration = $run.configuration
        adapter = $run.adapter
        scenes = @($run.scenes | ForEach-Object { $_.id })
        modes = @($run.modes)
        runs = $run.runs
        jobs = $jobs.Count
        failures = @($jobs | Where-Object { -not $_.success }).Count
    }
}

if ($Results) {
    $entries = @()
    if (Test-Path -LiteralPath $indexPath) { $entries = @((Get-Content -LiteralPath $indexPath -Raw | ConvertFrom-Json).runs) }
    $entry = Get-IndexEntry (Resolve-Path -LiteralPath $Results).Path
    $entries = @($entries | Where-Object { $_.directory -ne $entry.directory }) + $entry
} else {
    $entries = @(Get-ChildItem -LiteralPath $artifactRoot -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'results.json') } |
        ForEach-Object { Get-IndexEntry (Join-Path $_.FullName 'results.json') })
}

# One run per line keeps diffs and merges readable.
$lines = @($entries | Sort-Object startedUtc, directory | ForEach-Object { '    ' + ($_ | ConvertTo-Json -Depth 4 -Compress) })
$text = "{`n  `"schemaVersion`": 1,`n  `"runs`": [`n" + ($lines -join ",`n") + "`n  ]`n}`n"
[IO.File]::WriteAllText($indexPath, $text, $utf8)
Write-Host "Index: $($lines.Count) runs in $indexPath"
