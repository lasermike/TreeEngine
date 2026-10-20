[CmdletBinding()]
param(
    [string]$Executable,
    [string[]]$Scenes = @('LoadSimpleBox', 'LoadSeaScene', 'LoadTrees', 'LoadGraph', 'LoadGltfDemo'),
    [string[]]$Modes = @('raster', 'raytracing')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $Executable) {
    $Executable = Join-Path $PSScriptRoot '..\..\TreeClassic\bin\x64\Debug\TreeClassic.exe'
}
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not ('TreeRenderTests.ImageComparison' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'ImageComparison.cs') -ReferencedAssemblies System.Drawing
}
$directory = Join-Path $PSScriptRoot ('Artifacts\scene-persistence-' + [Guid]::NewGuid().ToString('N'))
$runtime = Join-Path $directory 'runtime with spaces'
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
Copy-Item -Path (Join-Path (Split-Path $Executable) '*') -Destination $runtime -Recurse -Exclude '*.pdb', 'UserScenes', 'imgui.ini'
$isolatedExecutable = Join-Path $runtime 'TreeClassic.exe'
$utf8 = New-Object Text.UTF8Encoding($false)

function Invoke-Capture([string]$Scene, [string]$Mode, [string]$Job, [bool]$Validate) {
    New-Item -ItemType Directory -Path $Job -Force | Out-Null
    $report = Join-Path $Job 'report.json'
    $request = @{
        scene = $Scene; mode = $Mode; seed = 12345; width = 640; height = 480
        warmupFrames = 3; postProcessing = $true; validateScenePersistence = $Validate
        report = $report
        captures = @(@{ time = 1; filename = (Join-Path $Job 'capture.png') })
    }
    $requestFile = Join-Path $Job 'request.json'
    [IO.File]::WriteAllText($requestFile, ($request | ConvertTo-Json -Depth 8), $utf8)
    # Deliberately use a working directory other than the executable directory.
    $process = Start-Process -FilePath $isolatedExecutable -ArgumentList @('--render-test', ('"' + $requestFile + '"')) `
        -WorkingDirectory $Job -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $Job 'stderr.log')
    [void]$process.Handle
    if (-not $process.WaitForExit(120000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "Scene persistence worker timed out: $Job"
    }
    if ($process.ExitCode -ne 0) { throw "Scene persistence worker failed ($($process.ExitCode)): $Job" }
    $result = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    if (-not $result.success -or ($Validate -and -not $result.scenePersistenceValidated)) {
        throw "Scene persistence checks did not complete: $Job"
    }
    return $report
}

foreach ($scene in $Scenes) {
    foreach ($mode in $Modes) {
        if ($mode -notin @('raster', 'raytracing')) { throw "Unsupported mode: $mode" }
        $job = Join-Path $directory "$scene-$mode"
        $report = Invoke-Capture $scene $mode $job $true
        $match = [TreeRenderTests.ImageComparison]::Compare("$report.before.png", "$report.after.png", "$report.diff.png")
        if (-not $match.SameDimensions -or $match.Rms -gt 0.002) {
            throw "Save/reload changed appearance: $scene / $mode, RMS=$($match.Rms). See $job"
        }
        $userDirectory = Join-Path $runtime 'UserScenes'
        New-Item -ItemType Directory -Path $userDirectory -Force | Out-Null
        $restartedScene = Join-Path $userDirectory 'restart.scene.json'
        Copy-Item -LiteralPath "$report.scene.json" -Destination $restartedScene
        try {
            $saved = Get-Content -LiteralPath $restartedScene -Raw | ConvertFrom-Json
            $restartJob = Join-Path $job 'restart'
            [void](Invoke-Capture ("user:" + $saved.id) $mode $restartJob $false)
            $restartMatch = [TreeRenderTests.ImageComparison]::Compare("$report.after.png", (Join-Path $restartJob 'capture.png'), "$report.restart-diff.png")
            if (-not $restartMatch.SameDimensions -or $restartMatch.Rms -gt 0.002) {
                throw "Restart changed saved scene appearance: $scene / $mode, RMS=$($restartMatch.Rms)"
            }
        } finally {
            Remove-Item -LiteralPath $restartedScene
        }
        Write-Host "Scene edit/save/reload/restart passed: $scene / $mode; RMS=$($match.Rms)"
    }
}
Write-Host "Scene persistence artifacts: $directory"
