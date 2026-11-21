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
$directory = Join-Path $PSScriptRoot ('Artifacts\resize-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)

foreach ($mode in @('raster', 'raytracing')) {
    foreach ($postProcessing in @($true, $false)) {
        $job = Join-Path $directory "$mode-postprocessing-$postProcessing"
        New-Item -ItemType Directory -Path $job -Force | Out-Null
        $smallBefore = Join-Path $job 'small-before.png'
        $large = Join-Path $job 'large.png'
        $smallAfter = Join-Path $job 'small-after.png'
        $reportPath = Join-Path $job 'capture-report.json'
        $requestPath = Join-Path $job 'request.json'
        $request = @{
            scene = 'builtin:LoadSimpleBox'; mode = $mode; seed = 12345
            width = 640; height = 480; warmupFrames = 3
            postProcessing = $postProcessing; report = $reportPath
            captures = @(
                @{ time = 1; filename = $smallBefore },
                @{ time = 1; width = 1024; height = 768; filename = $large },
                @{ time = 1; width = 640; height = 480; filename = $smallAfter }
            )
        }
        [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 8), $utf8)
        $process = Start-Process -FilePath $Executable -ArgumentList @('--render-test', ('"' + $requestPath + '"')) -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $job 'stderr.log')
        [void]$process.Handle
        if (-not $process.WaitForExit(120000)) {
            $process.Kill(); $process.WaitForExit(); throw "Resize worker timed out: $job"
        }
        if ($process.ExitCode -ne 0) { throw "Resize worker failed: $($process.ExitCode). See $job." }
        $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
        if (-not $report.success -or @($report.captures).Count -ne 3) { throw "Resize captures did not complete: $job" }
        $expected = @(@(640, 480), @(1024, 768), @(640, 480))
        $images = @($smallBefore, $large, $smallAfter)
        for ($i = 0; $i -lt 3; $i++) {
            $capture = $report.captures[$i]
            if (-not $capture.success -or $capture.mode -ne $mode -or
                $capture.width -ne $expected[$i][0] -or $capture.height -ne $expected[$i][1]) {
                throw "Incorrect capture dimensions in report: $job, capture $i"
            }
            $bitmap = New-Object Drawing.Bitmap($images[$i])
            try {
                if ($bitmap.Width -ne $expected[$i][0] -or $bitmap.Height -ne $expected[$i][1]) {
                    throw "Incorrect PNG dimensions: $job, capture $i"
                }
            } finally { $bitmap.Dispose() }
        }
        $repeat = [TreeRenderTests.ImageComparison]::Compare($smallBefore, $smallAfter, (Join-Path $job 'return-diff.png'))
        if (-not $repeat.SameDimensions -or $repeat.Rms -ne 0) {
            throw "Returning to the original size changed the image: $job, RMS=$($repeat.Rms)"
        }
        Write-Host "Window resize passed for $mode with postprocessing=$postProcessing."
    }
}
Write-Host "Resize captures: $directory"
