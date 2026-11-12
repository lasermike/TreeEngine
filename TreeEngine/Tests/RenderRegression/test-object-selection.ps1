[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$executable = Join-Path $workspace 'TreeClassic\bin\x64\Debug\TreeClassic.exe'
$artifacts = Join-Path $PSScriptRoot 'Artifacts\Selection'
[void][IO.Directory]::CreateDirectory($artifacts)
$scene = Join-Path $PSScriptRoot 'selection.scene.json'
foreach ($mode in @('raster', 'raytracing')) {
    $captures = @()
    $steps = @(
        @{ id = 'nearest'; clicks = @(@{u=0.35;v=0.5}); expected = @('front') },
        @{ id = 'multiple'; clicks = @(@{u=0.65;v=0.5;ctrl=$true}); expected = @('front','glass-instance') },
        @{ id = 'toggle'; clicks = @(@{u=0.35;v=0.5;ctrl=$true}); expected = @('glass-instance') },
        @{ id = 'geometry-gap'; clicks = @(@{u=0.29;v=0.37}); expected = @('back') },
        @{ id = 'occluded'; clicks = @(); expected = @('back'); through = $false },
        @{ id = 'ctrl-empty'; clicks = @(@{u=0.95;v=0.05;ctrl=$true}); expected = @('back') },
        @{ id = 'empty'; clicks = @(@{u=0.95;v=0.05}); expected = @() },
        @{ id = 'rapid'; clicks = @(@{u=0.35;v=0.5},@{u=0.65;v=0.5;ctrl=$true},@{u=0.35;v=0.5;ctrl=$true}); expected = @('glass-instance') },
        @{ id = 'upscaled'; clicks = @(@{u=0.35;v=0.5}); expected = @('front'); aa='taa'; scale=0.5 },
        @{ id = 'resized'; clicks = @(@{u=0.35;v=0.5}); expected = @('front'); width=800; height=600 },
        @{ id = 'no-post'; clicks = @(@{u=0.65;v=0.5}); expected = @('glass-instance'); post=$false },
        @{ id = 'leaf-surface'; clicks = @(@{u=0.5;v=0.2}); expected = @('leaf-cutout') },
        @{ id = 'leaf-hole'; clicks = @(@{u=0.432;v=0.2}); expected = @() },
        @{ id = 'tree'; scene = 'LoadTreeScene'; clicks = @(@{u=0.75;v=0.7}); expected = @('tree') },
        @{ id = 'tree-gap'; clicks = @(@{u=0.6;v=0.25}); expected = @() },
        @{ id = 'tree-instance'; scene = 'Instancing'; clicks = @(@{u=0.744;v=0.695}); expected = @('tree-right') },
        @{ id = 'model'; clicks = @(@{u=0.48;v=0.8}); expected = @('model') }
    )
    foreach ($step in $steps) {
        $capture = @{
            time=5; filename=(Join-Path $artifacts "$mode-$($step.id).png")
            selectionClicks=@($step.clicks); expectedSelection=@($step.expected)
        }
        if ($step.ContainsKey('through')) { $capture.highlightsThroughGeometry = $step.through }
        if ($step.ContainsKey('aa')) { $capture.antialiasing = $step.aa; $capture.renderScale = $step.scale }
        if ($step.ContainsKey('width')) { $capture.width = $step.width; $capture.height = $step.height }
        if ($step.ContainsKey('post')) { $capture.postProcessing = $step.post }
        if ($step.ContainsKey('scene')) { $capture.scene = $step.scene }
        $captures += $capture
    }
    $report = Join-Path $artifacts "$mode-report.json"
    $request = @{ scene=$scene; mode=$mode; width=640; height=480; warmupFrames=6; report=$report; captures=$captures }
    $requestPath = Join-Path $artifacts "$mode-request.json"
    [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 10))
    $process = Start-Process -FilePath $executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) -WorkingDirectory (Split-Path $executable) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $artifacts "$mode-stderr.log")
    [void]$process.Handle
    if (!$process.WaitForExit(60000)) { $process.Kill(); throw "$mode selection test timed out" }
    if ($process.ExitCode -ne 0) {
        $details = if (Test-Path $report) { (Get-Content -Raw $report | ConvertFrom-Json).error } else { 'See stderr log.' }
        throw "$mode selection test failed: $details"
    }
    Write-Host "$mode selection: $($steps.Count) scenarios passed"
}
