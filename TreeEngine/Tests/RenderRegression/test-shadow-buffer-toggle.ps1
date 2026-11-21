[CmdletBinding()]
param([string]$Executable)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $Executable) {
    $Executable = Join-Path $PSScriptRoot '..\..\TreeClassic\bin\x64\Debug\TreeClassic.exe'
}
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not ('TreeRenderTests.ImageComparison' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'ImageComparison.cs') -ReferencedAssemblies System.Drawing
}
$directory = Join-Path $PSScriptRoot ('Artifacts\shadow-buffer-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)

foreach ($mode in @('raster', 'raytracing')) {
    $job = Join-Path $directory $mode
    New-Item -ItemType Directory -Path $job -Force | Out-Null
    $before = Join-Path $job 'off-before.png'
    $preview = Join-Path $job 'on.png'
    $after = Join-Path $job 'off-after.png'
    $reportPath = Join-Path $job 'capture-report.json'
    $requestPath = Join-Path $job 'request.json'
    $request = @{
        scene = 'builtin:LoadSimpleBox'; mode = $mode; seed = 12345
        width = 640; height = 480; warmupFrames = 3
        postProcessing = $true; report = $reportPath
        captures = @(
            @{ time = 1; filename = $before; showShadowBuffer = $false },
            @{ time = 1; filename = $preview; showShadowBuffer = $true },
            @{ time = 1; filename = $after; showShadowBuffer = $false }
        )
    }
    [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 8), $utf8)
    $process = Start-Process -FilePath $Executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $job 'stderr.log')
    [void]$process.Handle
    if (-not $process.WaitForExit(120000)) {
        $process.Kill(); $process.WaitForExit(); throw "Shadow-buffer worker timed out: $job"
    }
    if ($process.ExitCode -ne 0) { throw "Shadow-buffer worker failed: $($process.ExitCode). See $job." }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if (-not $report.success -or @($report.captures).Count -ne 3) { throw "Shadow-buffer captures did not complete: $job" }
    foreach ($capture in $report.captures) {
        if (-not $capture.success -or $capture.mode -ne $mode) { throw "Shadow-buffer capture failed: $job" }
    }
    $repeat = [TreeRenderTests.ImageComparison]::Compare($before, $after, (Join-Path $job 'repeat-diff.png'))
    if (-not $repeat.SameDimensions -or $repeat.Rms -ne 0) {
        throw "Disabling the preview did not restore the image: $job, RMS=$($repeat.Rms)"
    }
    $difference = [TreeRenderTests.ImageComparison]::Compare($before, $preview, (Join-Path $job 'preview-diff.png'))
    if (-not $difference.SameDimensions -or $difference.Rms -eq 0) {
        throw "Enabling the preview did not alter the image: $job"
    }
    Write-Host "Shadow-buffer toggle passed for $mode."
}
Write-Host "Shadow-buffer captures: $directory"
