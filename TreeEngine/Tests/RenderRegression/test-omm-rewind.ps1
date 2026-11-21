[CmdletBinding()]
param([string]$Executable, [string]$OutputDirectory)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $Executable) {
    $Executable = Join-Path $PSScriptRoot '..\..\TreeClassic\bin\x64\Debug\TreeClassic.exe'
}
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $PSScriptRoot ('Artifacts\omm-rewind-' + [Guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
if (-not ('TreeRenderTests.ImageComparison' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'ImageComparison.cs') -ReferencedAssemblies System.Drawing
}
$times = @(0, 0.1, 0, 1, 0, 5, 0, 60, 0)
foreach ($enabled in @($true, $false)) {
    $name = if ($enabled) { 'on' } else { 'off' }
    $job = Join-Path $OutputDirectory $name
    New-Item -ItemType Directory -Path $job -Force | Out-Null
    $captures = @()
    for ($i = 0; $i -lt $times.Count; ++$i) {
        $captures += @{ time = $times[$i]; filename = (Join-Path $job ("$i.png")) }
    }
    $request = @{
        scene = 'LeafAtlases'; mode = 'raytracing'; opacityMicromaps = $enabled
        seed = 12345; width = 640; height = 480; warmupFrames = 0; benchmarkFrames = 1
        postProcessing = $false; report = (Join-Path $job 'report.json'); captures = $captures
    }
    $path = Join-Path $job 'request.json'
    [IO.File]::WriteAllText($path, ($request | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
    $process = Start-Process -FilePath $Executable -ArgumentList @('--render-test', ('"' + $path + '"')) -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $job 'stderr.log')
    [void]$process.Handle
    if (-not $process.WaitForExit(120000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "Micromap rewind timed out: $name"
    }
    if ($process.ExitCode -ne 0) {
        throw "Micromap rewind failed: $name; see $job\stderr.log"
    }
    $report = Get-Content -LiteralPath (Join-Path $job 'report.json') -Raw | ConvertFrom-Json
    if (-not $report.success -or @($report.captures).Count -ne $times.Count) {
        throw "Incomplete micromap rewind captures: $name"
    }
    foreach ($i in @(2, 4, 6, 8)) {
        $result = [TreeRenderTests.ImageComparison]::Compare((Join-Path $job '0.png'), (Join-Path $job "$i.png"), (Join-Path $job "diff-$i.png"))
        if (-not $result.SameDimensions -or $result.Rms -ne 0) {
            throw "Rewound image differs: $name / capture $i (RMS $($result.Rms))"
        }
    }
    Write-Host "Repeated Leaf Atlases rewinds passed: micromaps $name"
}
Write-Host "Captures: $OutputDirectory"
